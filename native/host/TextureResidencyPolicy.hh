#pragma once
#include "ResourceAvailability.hh"
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
inline std::uint64_t available_texture_storage(int total_kb,int free_kb,std::uint64_t available_ram=0,std::uint64_t resident=0){
 // KiB counters must agree with each other. A device's capacity is not a
 // validity test: both small devices and large coherent reports are usable.
 if(coherent_vram_report(total_kb,free_kb))return resource_sum(resident,resource_product(std::uint64_t(free_kb),1024));
 return available_ram;
}
// Reusable owned storage and current free memory form the available cache capacity.
struct AuthoringCacheBudget {
 std::uint64_t generation=~std::uint64_t(0),limit=0,observed_owned=0,observed_free=0;
 bool report_valid=false;
 std::uint64_t update(std::uint64_t serial,std::uint64_t owned,std::uint64_t free,bool valid,std::uint64_t ram){
  if(serial!=generation){generation=serial;observed_owned=owned;observed_free=free;report_valid=valid;limit=resource_sum(owned,valid?free:ram);}

  return limit;
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
inline int residency_last_mip(const ResidencyRegion &region){int largest=1;for(const auto &[w,h]:region.tiles)largest=std::max({largest,w,h});int mip=0;while(largest>1){largest/=2;++mip;}return mip;}
inline std::uint64_t residency_bytes(const ResidencyRegion &region,int mip){
 if(mip<0||mip>31)throw std::invalid_argument("Invalid residency mip");std::uint64_t bytes=0;
 for(const auto &[width,height]:region.tiles){if(width<1||height<1)throw std::invalid_argument("Invalid residency tile size");int w=std::max(1,width>>mip),h=std::max(1,height>>mip);for(;;){const auto level=resource_product(resource_product(w,h),region.bytes_per_texel);bytes=resource_sum(bytes,level);if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);}}
 return bytes;
}
// Display resolution is a document invariant; memory pressure never substitutes
// a coarser visible mip.
inline std::vector<ResidencyAllocation> plan_full_resolution(const std::vector<ResidencyRegion> &regions){
 std::vector<ResidencyAllocation> result;std::set<std::uint64_t> ids;
 for(const auto &r:regions){if(!ids.insert(r.id).second||r.tiles.empty()||r.bytes_per_texel<1||r.bytes_per_texel>16)throw std::invalid_argument("Invalid full-resolution residency request");result.push_back({r.id,residency_bytes(r,0),0});}
 return result;
}
}
