#include "TextureResidencyPolicy.hh"
#include <cassert>
#include <iostream>
#include <random>
using namespace bts;
int main(){
 // Native 1D UDIM descriptors report height == layers == 2: count it once.
 assert(native_texture_storage_bytes(1,2,2,0,2,1,16)==64);
 assert(native_texture_storage_bytes(1,8,3,0,3,4,16)==720);
 assert(native_texture_storage_bytes(2,512,256,2,2,10,16)==5592416);
 assert(native_texture_storage_bytes(3,4,4,4,1,3,4)==292);
 bool storage_overflow=false;try{native_texture_storage_bytes(3,std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),1,1,16);}catch(const std::overflow_error &){storage_overflow=true;}assert(storage_overflow);

 assert(coherent_vram_report(1,0)&&coherent_vram_report(128*1024,64*1024));
 assert(!coherent_vram_report(0,0)&&!coherent_vram_report(1,-1)&&!coherent_vram_report(1,2));
 assert(automatic_residency_budget(128*1024,64*1024)==32ULL*1024*1024);
 assert(automatic_residency_budget(0,0,0)==0); // no invented emergency allowance.
 assert(residency_budget_override(1,128ULL<<30,true)==1ULL<<20);
 assert(residency_budget_override(65536,128ULL<<30,true)==64ULL<<30);
 assert(residency_budget_override(65536,4ULL<<30,true)==4ULL<<30);
 assert(residency_budget_override(1,0,true)==0);
 assert(residency_budget_override(65536,0,false)==64ULL<<30);
 bool overflow=false;try{residency_budget_override(std::numeric_limits<std::uint64_t>::max(),0,false);}catch(const std::overflow_error &){overflow=true;}assert(overflow);
 ResidencyRegion wide{999,{{100000,1}},8,0,1};assert(residency_bytes(wide,0)>800000);assert(plan_full_resolution({wide})[0].mip==0);
 ResidencyRegion oversized{1000,{{std::numeric_limits<int>::max(),std::numeric_limits<int>::max()}},16,0,1};overflow=false;try{residency_bytes(oversized,0);}catch(const std::overflow_error &){overflow=true;}assert(overflow);
 const std::uint64_t gib=1ULL<<30;AuthoringCacheBudget cache;
 // Retain an existing cache without spending the same free bytes repeatedly.
 assert(cache.update(1,3*gib,8*gib,true,0)==3*gib);
 assert(cache.update(1,3*gib,8*gib,true,0)==3*gib);
 for(std::uint64_t epoch=2;epoch<100;++epoch)assert(cache.update(epoch,3*gib,8*gib,true,0)==3*gib);
 assert(cache.update(100,4*gib,0,true,100*gib)==3*gib);
 assert(cache.update(101,0,0,false,32*gib)==2*gib);
 AuthoringCacheBudget fresh;
 assert(fresh.update(1,0,16*gib,true,0)==4*gib);
 for(std::uint64_t epoch=2;epoch<300;++epoch)assert(fresh.update(epoch,4*gib,12*gib,true,0)==4*gib);
 assert(fresh.update(300,4*gib,gib,true,0)==3*gib);
 assert(fresh.update(301,3*gib,2*gib,true,0)==3*gib);
 assert(fresh.update(302,3*gib,13*gib,true,0)==4*gib);
 AuthoringCacheBudget zero;assert(zero.update(1,0,0,true,100*gib)==0);assert(!zero.admit_active(65536,0));
 AuthoringCacheBudget active;
 assert(active.update(1,0,8*gib,true,0)==2*gib);
 assert(active.admit_active(3*gib,2*gib));
 assert(active.admit_active(5*gib,3*gib));
 assert(!active.admit_active(7*gib,5*gib)); // reserve + spent bytes, no stale-report reuse.
 assert(coherent_vram_report(24*1024*1024,0));assert(automatic_residency_budget(24*1024*1024,0,1ULL<<30,4096)==4096);
 assert(automatic_residency_budget(24*1024*1024,16*1024*1024)==8ULL*1024*1024*1024);assert(automatic_residency_budget(-745283688,32767)==0);assert(coherent_vram_report(1620575096,32764));assert(automatic_residency_budget(1620575096,32764)==std::uint64_t(32764)*512);assert(automatic_residency_budget(0,0)==0);assert(automatic_residency_budget(1024*1024,2*1024*1024)==0);
 assert(automatic_residency_budget(0,0,16ULL*1024*1024*1024)==4ULL*1024*1024*1024);
 assert(automatic_residency_budget(24*1024*1024,8*1024*1024,0,1024ULL*1024*1024)==5ULL*1024*1024*1024);
 assert(automatic_residency_budget(24*1024*1024,4*1024*1024)<automatic_residency_budget(24*1024*1024,8*1024*1024));
 std::vector<ResidencyRegion> regions;for(unsigned i=0;i<32;++i)regions.push_back({i,{{2048,2048}},8,0,i==0?16.:1.});
 const std::uint64_t budget=32ULL*1024*1024;auto plan=plan_texture_residency(regions,budget);std::uint64_t total=0;for(const auto &a:plan)total+=a.bytes;assert(total<=budget&&plan[0].mip<plan[1].mip);
 for(auto &r:regions){r.priority=r.id==31?16:0;r.requested_mip=r.id==31?0:7;}auto moved=plan_texture_residency(regions,budget);assert(moved.back().mip<plan.back().mip&&moved.front().mip>plan.front().mip);
 ResidencyRegion udim{99,{{2048,1024},{512,2048},{1024,1024}},16,0,1};assert(residency_bytes(udim,0)>std::uint64_t(2048*1024+512*2048+1024*1024)*16);
 std::mt19937 random(146);for(int trial=0;trial<500;++trial){for(auto &r:regions){r.priority=random()%50;r.requested_mip=random()%8;}const auto cap=4096+(random()%(64*1024*1024));auto p=plan_texture_residency(regions,cap);std::uint64_t sum=0;for(std::size_t i=0;i<p.size();++i){sum+=p[i].bytes;assert(p[i].bytes==residency_bytes(regions[i],p[i].mip));}assert(sum<=cap);}
 // Seven 2-tile 2K half-float RGBA channels fit in a real VRAM budget.
 std::vector<ResidencyRegion> channels;for(int i=0;i<7;++i)channels.push_back({unsigned(i),{{2048,2048},{2048,2048}},8,0,i<5?4.:.5});
 const auto fixed=plan_full_resolution(channels);for(const auto &a:fixed)assert(a.mip==0);
 const auto full=plan_texture_residency(channels,8ULL*1024*1024*1024);for(const auto &a:full)assert(a.mip==0);
 auto old_channels=channels;for(auto &r:old_channels)r.bytes_per_texel=16;const auto old=plan_texture_residency(old_channels,512ULL*1024*1024);int coarse=0;for(const auto &a:old)coarse+=a.mip>0;assert(coarse>0);
 for(unsigned bpp:{1,2,3,4,6,8,12,16}){ResidencyRegion r{1,{{4096,2048}},bpp,0,1};assert(plan_texture_residency({r},1024ULL*1024*1024).front().mip==0);}
 std::cout<<"Seven-channel 2x2K before/after: old_coarse="<<coarse<<" after_coarse=0; actual format component sizes PASS\n";
 bool rejected=false;try{plan_texture_residency(regions,1);}catch(const std::length_error &){rejected=true;}assert(rejected);regions.push_back(regions.front());rejected=false;try{plan_texture_residency(regions,budget);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 std::cout<<"Residency budget, mip overhead, view-demand eviction, UDIM sizes and randomized plans PASS\n";
}
