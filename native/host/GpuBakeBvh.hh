#pragma once
#include "ResourceAvailability.hh"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>
namespace bts::bake_bvh {
using Triangle=std::array<std::array<float,3>,3>;
// std430: vec3 + uint, vec3 + uint. Preorder children are consecutive;
// escape skips a complete subtree, so traversal never needs a fixed stack.
struct alignas(16) Node {
 std::array<float,3> lower{};std::uint32_t escape=0;
 std::array<float,3> upper{};std::uint32_t triangle=std::numeric_limits<std::uint32_t>::max();
};
static_assert(sizeof(Node)==32&&offsetof(Node,escape)==12&&offsetof(Node,triangle)==28);
inline std::uint64_t node_count(std::uint64_t triangles){return triangles?resource_product(triangles,2)-1:0;}
struct Plan {std::size_t batch_points=0;std::uint64_t cpu=0,gpu=0;};
inline Plan plan(std::uint64_t triangles,std::uint64_t points,int rays,std::uint64_t ssbo,int groups,const ResourceAvailability &budget){
 if(!triangles||!points||rays<1)throw std::invalid_argument("Invalid GPU bake batch dimensions");
 const auto nodes=node_count(triangles),tri_bytes=resource_product(triangles,48),node_bytes=resource_product(nodes,sizeof(Node));
 if(!ssbo||groups<1)throw std::runtime_error("GPU bake requires reported SSBO and compute dispatch limits");
 if(nodes>std::uint64_t(std::numeric_limits<int>::max())||triangles>std::numeric_limits<std::uint32_t>::max()/3)throw std::length_error("GPU bake BVH exceeds shader node/triangle index representation");
 if(tri_bytes>ssbo||node_bytes>ssbo)throw std::length_error("GPU bake geometry exceeds the driver SSBO block size");
 const auto fixed_gpu=resource_sum(resource_sum(tri_bytes,node_bytes),64); // Counter, staging and block alignment.
 const auto fixed_cpu=resource_sum(resource_sum(fixed_gpu,resource_product(triangles,4)),resource_product(points,sizeof(float)));
 // Ray32B, continuation8B, hit4B and readback staging4B per sample.
 const auto ray_bytes=resource_product(std::uint64_t(rays),48),point_cpu=resource_sum(ray_bytes,sizeof(std::size_t));
 const auto ram=budget.ram()>fixed_cpu?budget.ram()-fixed_cpu:0;
 const auto vram=budget.vram()>fixed_gpu?budget.vram()-fixed_gpu:0;
 // A reusable dispatch granularity, never a limit on the complete bake.
 auto batch=std::min<std::uint64_t>({points,ssbo/resource_product(std::uint64_t(rays),32),ram/point_cpu,
   resource_product(std::uint64_t(groups),64)/std::uint64_t(rays),std::uint64_t(std::numeric_limits<int>::max())/std::uint64_t(rays)});
 if(budget.vram_known)batch=std::min(batch,vram/ray_bytes);
 const auto cpu=resource_sum(fixed_cpu,resource_product(std::max<std::uint64_t>(batch,1),point_cpu));
 const auto gpu=resource_sum(fixed_gpu,resource_product(std::max<std::uint64_t>(batch,1),ray_bytes));

 if(!batch)throw std::length_error("GPU bake cannot fit one point in the driver SSBO/dispatch representation");
 return {std::size_t(batch),cpu,gpu};
}
inline std::vector<Node> build(const std::vector<Triangle> &triangles){
 const auto count=node_count(triangles.size());
 if(count>std::uint64_t(std::numeric_limits<int>::max()))throw std::length_error("GPU bake BVH node indices exceed signed shader count");
 std::vector<Node> nodes(static_cast<std::size_t>(count));std::vector<std::uint32_t> order(triangles.size());std::iota(order.begin(),order.end(),0);
 std::uint32_t next=0;
 const auto append=[&](auto &&self,std::size_t begin,std::size_t end)->void{
  const auto index=next++;auto &node=nodes[index];node.lower.fill(std::numeric_limits<float>::max());node.upper.fill(std::numeric_limits<float>::lowest());
  for(auto at=begin;at<end;++at)for(const auto &vertex:triangles[order[at]])for(int axis=0;axis<3;++axis){if(!std::isfinite(vertex[axis]))throw std::invalid_argument("GPU bake geometry contains non-finite coordinates");node.lower[axis]=std::min(node.lower[axis],vertex[axis]);node.upper[axis]=std::max(node.upper[axis],vertex[axis]);}
  if(end-begin==1)node.triangle=order[begin];
  else {
   int axis=0;for(int a=1;a<3;++a)if(double(node.upper[a])-node.lower[a]>double(node.upper[axis])-node.lower[axis])axis=a;
   const auto middle=begin+(end-begin)/2;
   std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](auto a,auto b){const auto center=[&](auto t){return double(triangles[t][0][axis])+triangles[t][1][axis]+triangles[t][2][axis];};const auto ca=center(a),cb=center(b);return ca==cb?a<b:ca<cb;});
   self(self,begin,middle);self(self,middle,end);
  }
  node.escape=next;
  // Conservative bounds prevent roundoff at a triangle/box boundary from
  // changing the original intersection result. This only adds candidate tests.
  for(int axis=0;axis<3;++axis){const double pad=std::max(1e-6,double(std::max(std::abs(node.lower[axis]),std::abs(node.upper[axis])))*1e-6);node.lower[axis]=float(std::max(double(std::numeric_limits<float>::lowest()),double(node.lower[axis])-pad));node.upper[axis]=float(std::min(double(std::numeric_limits<float>::max()),double(node.upper[axis])+pad));}
 };
 if(!triangles.empty())append(append,0,triangles.size());return nodes;
}
struct Frame {
 std::array<float,3> position,n,tangent,bitangent;
 double phase=0;float maximum=0;
};
inline bool frame(Frame &frame,const std::array<float,3> &position,std::array<float,3> n,std::uint32_t seed,float maximum,bool inward){
 if(inward)for(float &v:n)v=-v;float length=0;for(float v:n)length+=v*v;length=std::sqrt(length);if(length<1e-8f)return false;for(float &v:n)v/=length;
 const float helper[3]={std::abs(n[2])>.9f?1.f:0.f,0,std::abs(n[2])>.9f?0.f:1.f};
 std::array<float,3> tangent{helper[1]*n[2]-helper[2]*n[1],helper[2]*n[0]-helper[0]*n[2],helper[0]*n[1]-helper[1]*n[0]};float tlen=0;for(float v:tangent)tlen+=v*v;tlen=std::sqrt(std::max(tlen,1e-12f));for(float &v:tangent)v/=tlen;
 frame={position,n,tangent,{n[1]*tangent[2]-n[2]*tangent[1],n[2]*tangent[0]-n[0]*tangent[2],n[0]*tangent[1]-n[1]*tangent[0]},(seed*0.6180339887498949-std::floor(seed*0.6180339887498949))*6.283185307179586,maximum};return true;
}
inline void ray(const Frame &frame,int index,int rays,std::array<float,4> &origin,std::array<float,4> &direction){
 const double z=std::sqrt((index+.5)/rays),radius=std::sqrt(1-z*z),angle=index*2.399963229728653+frame.phase;
 for(int a=0;a<3;++a){direction[a]=float(frame.n[a]*z+frame.tangent[a]*radius*std::cos(angle)+frame.bitangent[a]*radius*std::sin(angle));origin[a]=frame.position[a]+frame.n[a]*float(frame.maximum*1e-5f);}origin[3]=frame.maximum;direction[3]=0;
}
}
