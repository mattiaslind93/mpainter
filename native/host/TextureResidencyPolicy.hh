#pragma once
#include "ResourceBudget.hh"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>
namespace bts {
// A software fallback allocation policy. Regions may be images or independent
// UDIM pages. This only plans storage; it does not claim hardware sparse support.
inline bool coherent_vram_report(int total_kb,int free_kb){return total_kb>0&&free_kb>=0&&free_kb<=total_kb;}
inline std::uint64_t automatic_residency_budget(int total_kb,int free_kb,std::uint64_t available_ram=0,std::uint64_t resident=0){
 // KiB counters must agree with each other. A device's capacity is not a
 // validity test: both small devices and large coherent reports are usable.
 if(coherent_vram_report(total_kb,free_kb))return resource_sum(resident,resource_product(std::uint64_t(free_kb),512));
 return available_ram/4;
}
// A refresh is a new observation of the same memory, not an additional cache
// allowance. Keep a stable high-water budget while memory is ample; reclaim
// cold entries under real pressure. Already-owned storage is protected on first
// observation so adopting the policy does not flush an otherwise healthy cache.
struct AuthoringCacheBudget {
 std::uint64_t generation=~std::uint64_t(0),limit=0,observed_owned=0,observed_free=0;
 bool report_valid=false;
 std::uint64_t update(std::uint64_t serial,std::uint64_t owned,std::uint64_t free,bool valid,std::uint64_t ram){
  if(serial!=generation){
   const bool first=generation==~std::uint64_t(0);generation=serial;observed_owned=owned;observed_free=free;report_valid=valid;
   if(!valid)limit=ram/16;
   else {
    const auto target=owned/4+free/4+(owned%4+free%4)/4;
    if(!free)limit=owned-owned/4;
    else if(first)limit=std::max(owned,target);
    else if(free<owned/2)limit=std::max(target,owned-owned/4);
    else limit=std::max(limit,target);
   }
  }
  return limit;
 }
 // A mandatory dependency set may exceed the soft cache share. Admit only
 // against unspent bytes from this observation, retaining a driver margin.
 bool admit_active(std::uint64_t required,std::uint64_t owned){
  if(required<=limit)return true;if(!report_valid||required<owned)return false;
  const auto spent=owned>observed_owned?owned-observed_owned:0;
  const auto available=observed_free>spent?observed_free-spent:0;
  const auto additional=required-owned,reserve=observed_free/4;
  if(additional>available||available-additional<reserve)return false;
  limit=required;return true;
 }
};
// GPU_texture_height is the layer count for 1D arrays, not a spatial side.
// Spatial mip dimensions shrink independently; array layers never shrink.
inline std::uint64_t native_texture_storage_bytes(int dimensions,int width,int height,int depth,int layers,int mips,unsigned bytes_per_texel){
 if(dimensions<1||dimensions>3||width<1||(dimensions>=2&&height<1)||(dimensions==3&&depth<1)||layers<1||mips<1||mips>32||bytes_per_texel<1)throw std::invalid_argument("Invalid native texture storage layout");
 std::uint64_t bytes=0;
 for(int mip=0;mip<mips;++mip){const auto w=std::uint64_t(std::max(1,width>>mip)),h=std::uint64_t(dimensions>=2?std::max(1,height>>mip):1),d=std::uint64_t(dimensions==3?std::max(1,depth>>mip):1);bytes=resource_sum(bytes,resource_product(resource_product(resource_product(w,h),d),resource_product(layers,bytes_per_texel)));}
 return bytes;
}
struct ResidencyRegion {
 std::uint64_t id=0;
 std::vector<std::pair<int,int>> tiles;
 unsigned bytes_per_texel=8;
 int requested_mip=0;
 double priority=1;
};
struct ResidencyAllocation {std::uint64_t id=0,bytes=0;int mip=0;};
inline std::uint64_t residency_budget_override(std::uint64_t mebibytes,std::uint64_t available,bool known){
 const auto requested=resource_product(mebibytes,1048576);return known?std::min(requested,available):requested;
}
inline int residency_last_mip(const ResidencyRegion &region){int largest=1;for(const auto &[w,h]:region.tiles)largest=std::max({largest,w,h});int mip=0;while(largest>1){largest/=2;++mip;}return mip;}
inline std::uint64_t residency_bytes(const ResidencyRegion &region,int mip){
 if(mip<0||mip>31)throw std::invalid_argument("Invalid residency mip");std::uint64_t bytes=0;
 for(const auto &[width,height]:region.tiles){if(width<1||height<1)throw std::invalid_argument("Invalid residency tile size");int w=std::max(1,width>>mip),h=std::max(1,height>>mip);for(;;){const auto level=resource_product(resource_product(w,h),region.bytes_per_texel);bytes=resource_sum(bytes,level);if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);}}
 return bytes;
}
// Display resolution is a document invariant. A budget is an admission/cache
// warning threshold; it must never silently substitute a coarser visible mip.
inline std::vector<ResidencyAllocation> plan_full_resolution(const std::vector<ResidencyRegion> &regions){
 std::vector<ResidencyAllocation> result;std::set<std::uint64_t> ids;
 for(const auto &r:regions){if(!ids.insert(r.id).second||r.tiles.empty()||r.bytes_per_texel<1||r.bytes_per_texel>16)throw std::invalid_argument("Invalid full-resolution residency request");result.push_back({r.id,residency_bytes(r,0),0});}
 return result;
}
inline std::vector<ResidencyAllocation> plan_texture_residency(const std::vector<ResidencyRegion> &regions,std::uint64_t budget){
 std::vector<ResidencyAllocation> plan;std::set<std::uint64_t> ids;std::uint64_t total=0;
 for(const auto &region:regions){if(!ids.insert(region.id).second||region.tiles.empty()||(region.bytes_per_texel<1||region.bytes_per_texel>16)||(region.requested_mip<0||region.requested_mip>31)||!std::isfinite(region.priority)||region.priority<0)throw std::invalid_argument("Invalid residency request");const auto mip=residency_last_mip(region);const auto bytes=residency_bytes(region,mip);if(bytes>budget-total)throw std::length_error("Viewport budget cannot hold fallback mip pages");total+=bytes;plan.push_back({region.id,bytes,mip});}
 // Benefit per byte favors cheap coarse pages before expensive fine detail.
 // Demand/priority comes from the view; hidden regions need not refine.
 for(;;){std::size_t best=regions.size();double score=-1;std::uint64_t best_bytes=0;
  for(std::size_t i=0;i<regions.size();++i){const auto &region=regions[i];const auto &allocation=plan[i];if(allocation.mip<=region.requested_mip||region.priority==0)continue;const auto bytes=residency_bytes(region,allocation.mip-1),extra=bytes-allocation.bytes;if(extra>budget-total)continue;const double candidate=region.priority/double(std::max<std::uint64_t>(1,extra));if(candidate>score||(candidate==score&&(best==regions.size()||region.id<regions[best].id))){best=i;score=candidate;best_bytes=bytes;}}
  if(best==regions.size())break;auto &allocation=plan[best];total+=best_bytes-allocation.bytes;allocation.bytes=best_bytes;--allocation.mip;
 }
 return plan;
}
}
