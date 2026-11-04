#pragma once
#include "SceneSnapshot.hh"
#include "PaintPixels.hh"
#include <algorithm>
#include <bit>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
namespace bts {
enum class PolygonFillMode {Triangle,Polygon,Mesh,Island,Auto};
inline PolygonFillMode polygon_fill_mode(PolygonFillMode mode,bool uv){return mode==PolygonFillMode::Auto?(uv?PolygonFillMode::Island:PolygonFillMode::Mesh):mode;}
inline bool uv_triangle_contains(const UvFaceTriangle &t,double u,double v) {
  const auto &a=t.corners[0],&b=t.corners[1],&c=t.corners[2];const double x=b[0]-a[0],y=b[1]-a[1],z=c[0]-a[0],w=c[1]-a[1],det=x*w-y*z;
  if(std::abs(det)<1e-20)return false;const double p=((u-a[0])*w-(v-a[1])*z)/det,q=(x*(v-a[1])-y*(u-a[0]))/det;
  return p>=-1e-8&&q>=-1e-8&&p+q<=1+1e-8;
}
// Indices refer to the native evaluated snapshot. UV islands join shared native
// vertex edges only when both UV endpoints match; a UV seam breaks the flood.
inline std::vector<std::size_t> polygon_fill_selection(const SceneSnapshot &scene,std::size_t material,PolygonFillMode mode,std::span<const std::size_t> seeds) {
  std::set<std::size_t> selected;
  for(auto index:seeds)if(index<scene.uv_triangles.size()&&scene.uv_triangles[index].material==material)selected.insert(index);
  if(selected.empty())return {};
  if(mode==PolygonFillMode::Auto)throw std::invalid_argument("Resolve polygon fill Auto against the clicked view first");
  std::set<std::uint64_t> objects;for(const auto &t:scene.uv_triangles)objects.insert(t.polygon_id>>32);
  if(mode==PolygonFillMode::Mesh&&objects.size()==1){
    // Connected elements are native topology, independent of UV seams/materials.
    std::unordered_map<std::uint64_t,std::vector<std::size_t>> adjacent;adjacent.reserve(scene.uv_triangles.size());
    for(std::size_t i=0;i<scene.uv_triangles.size();++i)for(auto v:scene.uv_triangles[i].vertex_ids)adjacent[v].push_back(i);
    std::vector<bool> visited(scene.uv_triangles.size());std::vector<std::size_t> queue(selected.begin(),selected.end());for(auto i:queue)visited[i]=true;
    for(std::size_t p=0;p<queue.size();++p)for(auto v:scene.uv_triangles[queue[p]].vertex_ids)for(auto next:adjacent.at(v))if(!visited[next]){visited[next]=true;queue.push_back(next);if(scene.uv_triangles[next].material==material)selected.insert(next);}
  }else if(mode==PolygonFillMode::Polygon||mode==PolygonFillMode::Mesh){std::set<std::uint64_t> ids;for(auto i:selected)ids.insert(mode==PolygonFillMode::Polygon?scene.uv_triangles[i].polygon_id:scene.uv_triangles[i].polygon_id>>32);for(std::size_t i=0;i<scene.uv_triangles.size();++i){const auto &t=scene.uv_triangles[i];if(t.material==material&&ids.contains(mode==PolygonFillMode::Polygon?t.polygon_id:t.polygon_id>>32))selected.insert(i);}}
  if(mode==PolygonFillMode::Island) {
    using Corner=std::array<std::uint64_t,3>;using Edge=std::array<Corner,2>;std::map<Edge,std::vector<std::size_t>> edges;
    const auto edge=[](const UvFaceTriangle &t,int i){const int j=(i+1)%3;const auto corner=[&](int k){const auto &uv=t.corners[k];return Corner{t.vertex_ids[k],std::bit_cast<std::uint32_t>(uv[0]==0?0.f:uv[0]),std::bit_cast<std::uint32_t>(uv[1]==0?0.f:uv[1])};};auto a=corner(i),b=corner(j);if(b<a)std::swap(a,b);return Edge{a,b};};
    for(std::size_t i=0;i<scene.uv_triangles.size();++i)if(scene.uv_triangles[i].material==material)for(int k=0;k<3;++k)edges[edge(scene.uv_triangles[i],k)].push_back(i);
    std::vector<std::size_t> queue(selected.begin(),selected.end());for(std::size_t p=0;p<queue.size();++p)for(int k=0;k<3;++k)for(auto next:edges[edge(scene.uv_triangles[queue[p]],k)])if(selected.insert(next).second)queue.push_back(next);
  }
  return {selected.begin(),selected.end()};
}
inline PaintPixels::Stamp polygon_fill_stamp(const SceneSnapshot &scene,std::span<const std::size_t> selection,float value) {
  if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Invalid polygon fill grayscale");
  auto footprint=std::make_shared<PaintPixels::SurfaceFootprint>();footprint->center={0,0,0};footprint->radius=1;
  for(auto index:selection){const auto &source=scene.uv_triangles.at(index);PaintPixels::SurfaceTriangle t{};for(int k=0;k<3;++k){t.uv[k]={source.corners[k][0],source.corners[k][1]};}footprint->triangles.push_back(t);}
  PaintPixels::Stamp stamp{};stamp.surface=std::move(footprint);stamp.values.roughness=value;stamp.channels={false,false,true,false,false};stamp.alpha.kind=AlphaKind::Uniform;stamp.alpha.uniform=1;return stamp;
}
}
