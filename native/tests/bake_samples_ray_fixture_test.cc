// SPDX-License-Identifier: GPL-3.0-or-later
#include "BakeSamplesFixture.hh"
#include "GpuBakeBvh.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main(){
 using namespace bts;using namespace bake_samples_fixture;
 ResourceAvailability budget;budget.available_ram=1ULL<<30;budget.available_vram=1ULL<<30;budget.vram_known=true;
 for(bool inward:{false,true}){
  const auto clamped=reference(256,inward);
  for(int rays:{16,17,128,256,511,512}){
   const auto wanted=reference(rays,inward);double distinct=0;
   for(std::size_t i=0;i<wanted.size();++i){assert(std::isfinite(wanted[i])&&wanted[i]>0&&wanted[i]<1);distinct=std::max(distinct,std::abs(double(wanted[i])-clamped[i]));}
   if(rays!=256)assert(distinct>2e-6); // A silent256 replacement is measurably wrong.
   const auto plan=bake_bvh::plan(triangles().size(),positions.size(),rays,1ULL<<27,65535,budget);
   assert(plan.batch_points==positions.size());
   std::printf("BL_CPU_FIXTURE inward=%d rays=%d expected=%.9g,%.9g,%.9g,%.9g delta256=%.9g batch_points=%zu\n",int(inward),rays,wanted[0],wanted[1],wanted[2],wanted[3],distinct,plan.batch_points);
  }
 }
 std::puts("BL fixture CPU: occluded AO/Thickness, all exact counts distinguish256,512 reusable batch PASS; actualGPU pending");
}
