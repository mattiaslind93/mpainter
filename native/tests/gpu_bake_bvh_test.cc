#include "GpuBakeBvh.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <set>
int main(){
 using namespace bts;using namespace bts::bake_bvh;
 std::vector<Triangle> triangles;
 for(int i=0;i<1001;++i){const float x=float(i%13),y=float(i/13);triangles.push_back({{{x,y,1},{x+1,y,1},{x,y+1,1}}});}
 const auto nodes=build(triangles);assert(nodes.size()==2*triangles.size()-1);
 std::set<std::uint32_t> leaves;
 for(std::size_t i=0;i<nodes.size();++i){const auto &node=nodes[i];assert(node.escape>i&&node.escape<=nodes.size());
  if(node.triangle==std::numeric_limits<std::uint32_t>::max())assert(node.escape>i+1);
  else {assert(node.triangle<triangles.size()&&leaves.insert(node.triangle).second);for(const auto &vertex:triangles[node.triangle])for(int axis=0;axis<3;++axis)assert(vertex[axis]>=node.lower[axis]&&vertex[axis]<=node.upper[axis]);}
 }
 assert(leaves.size()==triangles.size()&&nodes.front().escape==nodes.size());
 ResourceAvailability budget;budget.available_ram=1ULL<<40;budget.available_vram=1ULL<<40;budget.vram_known=true;
 const auto p=plan(1001,1000000000,256,2ULL*1024*1024*1024,65535,budget);assert(p.batch_points>0&&p.batch_points<=1000000000);
 // Total sample/triangle work is deliberately unrelated to admission. Only
 // live buffers, encoded shader indices and actual device limits apply.
 assert(p.cpu<=budget.available_ram&&p.gpu<=budget.available_vram);
 const auto abundant=p.batch_points;budget.available_vram=16ULL*1024*1024;const auto small=plan(1001,1000000000,256,2ULL*1024*1024*1024,65535,budget);assert(small.batch_points<abundant&&small.gpu<=budget.available_vram);
 bool rejected=false;try{plan(1001,1000000,256,64,65535,budget);}catch(const std::length_error &){rejected=true;}assert(rejected);
 rejected=false;budget.available_ram=4096;try{plan(1001,1000000,256,2ULL*1024*1024*1024,65535,budget);}catch(const std::length_error &){rejected=true;}assert(rejected);
 Frame out,in;assert(frame(out,{0,0,0},{0,0,2},123,2,false));assert(frame(in,{0,0,0},{0,0,2},123,2,true));
 std::array<float,4> origin,direction;ray(out,0,8,origin,direction);assert(origin[2]==2*1e-5f&&origin[3]==2&&direction[3]==0);
 ray(in,0,8,origin,direction);assert(origin[2]==-2*1e-5f);
 std::puts("GPU bake BVH: stackless node layout/escapes/leaves, no total-work cap, SSBO/RAM admission and original origin epsilon PASS");
}
