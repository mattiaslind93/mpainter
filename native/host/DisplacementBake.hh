#pragma once
#include "ResourceAvailability.hh"
#include <limits>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>
namespace bts {
// Viewport bake geometry: simple subdivision, then (height - midlevel) * strength along the normal.
// Catmull-Clark on a flat cage stays flat before displacement, so a plane matches the viewport.
struct BakeDisplacement {
  int subdivision=0,width=0,rows=0;
  float midlevel=.5f,strength=0;
  std::map<int,std::vector<float>> samples;
};
struct BakeCorner {std::array<float,3> position{},normal{0,0,1};std::array<float,2> uv{};};
struct BakeTri {std::array<BakeCorner,3> corners{};};

inline float bake_height(const BakeDisplacement &field,float u,float v,int tile_hint=0){
  if(field.strength==0||field.samples.empty()||field.width<2||field.rows<2)return field.midlevel;
  const int tu=int(std::floor(u))-(u>0&&u==std::floor(u)?1:0),tv=int(std::floor(v))-(v>0&&v==std::floor(v)?1:0);
  const int tile=tile_hint?tile_hint:1001+tu+10*tv;
  const auto found=field.samples.find(tile);if(found==field.samples.end()||found->second.size()!=std::size_t(field.width)*field.rows)return field.midlevel;
  const float lu=std::clamp(u-(tile-1001)%10,0.f,1.f),lv=std::clamp(v-(tile-1001)/10,0.f,1.f),x=lu*(field.width-1),y=lv*(field.rows-1);
  const int x0=std::clamp(int(std::floor(x)),0,field.width-1),y0=std::clamp(int(std::floor(y)),0,field.rows-1);
  const int x1=std::min(x0+1,field.width-1),y1=std::min(y0+1,field.rows-1);const float tx=x-x0,ty=y-y0;
  const auto at=[&](int ix,int iy){return found->second[std::size_t(iy)*field.width+ix];};
  return std::lerp(std::lerp(at(x0,y0),at(x1,y0),tx),std::lerp(at(x0,y1),at(x1,y1),tx),ty);
}
inline void bake_displace_corner(BakeCorner &corner,const BakeDisplacement &field,int tile_hint=0){
  const float delta=(bake_height(field,corner.uv[0],corner.uv[1],tile_hint)-field.midlevel)*field.strength;
  float length=0;for(float n:corner.normal)length+=n*n;length=std::sqrt(length);if(length<1e-8f)return;
  for(int i=0;i<3;++i)corner.position[i]+=corner.normal[i]/length*delta;
}
inline BakeCorner bake_midpoint(const BakeCorner &a,const BakeCorner &b){
  BakeCorner m;float length=0;for(int i=0;i<3;++i){m.position[i]=(a.position[i]+b.position[i])*.5f;m.normal[i]=(a.normal[i]+b.normal[i])*.5f;length+=m.normal[i]*m.normal[i];}
  length=std::sqrt(length);if(length>1e-8f)for(float &n:m.normal)n/=length;for(int i=0;i<2;++i)m.uv[i]=(a.uv[i]+b.uv[i])*.5f;return m;
}
struct BakeSubdivisionPlan {std::uint64_t triangles=0,peak_bytes=0;};
inline BakeSubdivisionPlan bake_subdivision_plan(std::uint64_t triangles,int level){
  if(level<0||level>11)throw std::invalid_argument("Bake subdivision is outside Blender's native 0..11 range");
  std::uint64_t previous=0;
  for(int i=0;i<level;++i){previous=triangles;triangles=resource_product(triangles,4);}
  if(triangles>std::vector<BakeTri>{}.max_size())throw std::length_error("Subdivided bake triangles exceed addressable vector storage");
  // The old and replacement vectors coexist during each four-way split.
  return {triangles,resource_product(resource_sum(previous,triangles),sizeof(BakeTri))};
}
inline std::uint64_t bake_ray_tree_nodes(std::uint64_t triangles){
  // BLI_bvhtree_new(tree_type=4) uses signed-int leaf/branch/node counts.
  const auto branches=std::max<std::uint64_t>(1,resource_sum(triangles,1)/3);
  const auto nodes=resource_sum(resource_sum(triangles,branches),4);
  if(nodes>std::uint64_t(std::numeric_limits<int>::max()))throw std::length_error("Bake BVH nodes exceed Blender's signed-int indexing");
  return nodes;
}
inline std::uint64_t bake_ray_tree_bytes(std::uint64_t triangles){
  // Native node+pointer table+four children+six float bounds use <=112 bytes
  // per node on 64-bit hosts; allow alignment/allocation overhead as well.
  const auto tree=resource_sum(resource_product(bake_ray_tree_nodes(triangles),128),128);
  return resource_sum(tree,resource_product(triangles,sizeof(std::array<std::array<float,3>,3>)+2*sizeof(std::uint32_t)));
}
inline std::vector<BakeTri> displace_bake_mesh(const std::vector<BakeTri> &source,const BakeDisplacement &field,const ResourceAvailability &memory=ResourceAvailability{}){
  if(field.subdivision<0||field.subdivision>11)throw std::invalid_argument("Bake subdivision is outside Blender's native 0..11 range");
  const auto plan=bake_subdivision_plan(source.size(),field.strength==0?0:field.subdivision);

  if(field.strength==0)return source;
  std::vector<BakeTri> mesh=source;int level=field.subdivision;
  while(level>0){std::vector<BakeTri> next;next.reserve(std::size_t(resource_product(mesh.size(),4)));for(const auto &tri:mesh){const auto a=bake_midpoint(tri.corners[0],tri.corners[1]),b=bake_midpoint(tri.corners[1],tri.corners[2]),c=bake_midpoint(tri.corners[2],tri.corners[0]);next.push_back({tri.corners[0],a,c});next.push_back({tri.corners[1],b,a});next.push_back({tri.corners[2],c,b});next.push_back({a,b,c});}mesh.swap(next);--level;}
  for(auto &tri:mesh){float u=0,v=0;for(const auto &corner:tri.corners){u+=corner.uv[0]/3;v+=corner.uv[1]/3;}const int tile=1001+int(std::floor(u))+10*int(std::floor(v));for(auto &corner:tri.corners)bake_displace_corner(corner,field,tile);}return mesh;
}
inline float bake_cavity(const BakeDisplacement &field,float u,float v){
  if(field.strength==0||field.width<3||field.rows<3)return 0;
  const float du=1.f/float(field.width-1),dv=1.f/float(field.rows-1);
  const float center=bake_height(field,u,v),lap=bake_height(field,u-du,v)+bake_height(field,u+du,v)+bake_height(field,u,v-dv)+bake_height(field,u,v+dv)-4*center;
  return -lap*field.strength;
}
inline bool bake_ray_hit(const std::array<float,3> &origin,const std::array<float,3> &direction,const std::array<std::array<float,3>,3> &tri,float maximum,float &distance){
  std::array<float,3> e1,e2,p,s,q;for(int i=0;i<3;++i){e1[i]=tri[1][i]-tri[0][i];e2[i]=tri[2][i]-tri[0][i];}
  p={direction[1]*e2[2]-direction[2]*e2[1],direction[2]*e2[0]-direction[0]*e2[2],direction[0]*e2[1]-direction[1]*e2[0]};
  const float det=e1[0]*p[0]+e1[1]*p[1]+e1[2]*p[2];if(std::abs(det)<1e-8f)return false;const float inv=1.f/det;
  for(int i=0;i<3;++i)s[i]=origin[i]-tri[0][i];const float u=(s[0]*p[0]+s[1]*p[1]+s[2]*p[2])*inv;if(u<0||u>1)return false;
  q={s[1]*e1[2]-s[2]*e1[1],s[2]*e1[0]-s[0]*e1[2],s[0]*e1[1]-s[1]*e1[0]};const float v=(direction[0]*q[0]+direction[1]*q[1]+direction[2]*q[2])*inv;if(v<0||u+v>1)return false;
  const float t=(e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2])*inv;if(t<=1e-7f||t>=maximum||t>=distance)return false;distance=t;return true;
}
// Cosine hemisphere, same directions as the baker. 1 is fully open.
inline float bake_cosine_occlusion(const std::vector<std::array<std::array<float,3>,3>> &triangles,const std::array<float,3> &position,const std::array<float,3> &normal,int rays,float maximum,std::uint32_t seed){
  std::array<float,3> n=normal;float length=0;for(float v:n)length+=v*v;length=std::sqrt(length);if(length<1e-8f||rays<1||maximum<=0)return 1;for(float &v:n)v/=length;
  const float helper[3]={std::abs(n[2])>.9f?1.f:0.f,0,std::abs(n[2])>.9f?0.f:1.f};
  std::array<float,3> tangent{helper[1]*n[2]-helper[2]*n[1],helper[2]*n[0]-helper[0]*n[2],helper[0]*n[1]-helper[1]*n[0]};
  float tlen=0;for(float v:tangent)tlen+=v*v;tlen=std::sqrt(tlen);for(float &v:tangent)v/=tlen;
  const std::array<float,3> bitangent{n[1]*tangent[2]-n[2]*tangent[1],n[2]*tangent[0]-n[0]*tangent[2],n[0]*tangent[1]-n[1]*tangent[0]};
  const double phase=(seed*0.6180339887498949-std::floor(seed*0.6180339887498949))*6.283185307179586;double sum=0;constexpr float epsilon_scale=1e-5f;
  for(int i=0;i<rays;++i){const double z=std::sqrt((i+.5)/rays),radius=std::sqrt(1-z*z),angle=i*2.399963229728653+phase;std::array<float,3> direction,origin;for(int a=0;a<3;++a){direction[a]=float(n[a]*z+tangent[a]*radius*std::cos(angle)+bitangent[a]*radius*std::sin(angle));origin[a]=position[a]+n[a]*float(maximum*epsilon_scale);}float distance=maximum;for(const auto &tri:triangles)bake_ray_hit(origin,direction,tri,maximum,distance);sum+=distance/maximum;}
  return float(sum/rays);
}
}
