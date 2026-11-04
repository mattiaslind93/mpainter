#pragma once
#include "SceneSnapshot.hh"
#include "SkewCorrection.hh"
#include <map>
#include <vector>
namespace bts {
struct SkewEdge {std::array<float,3> a,b;std::uint32_t object=0;};
// Use real polygon boundary flags and shared geometry identities. UV seams alone never
// become hard edges; split shading normals do. Open/nonmanifold boundaries are protected.
inline std::vector<SkewEdge> skew_hard_edges(const SceneSnapshot &scene){
  struct Use {SkewEdge segment;SkewNormal na,nb;};
  std::map<std::pair<std::uint64_t,std::uint64_t>,std::vector<Use>> uses;
  const auto normal=[](const UvFaceTriangle &t,int corner){if(t.has_normals)return skew_projection_normal(t.normals[corner],t.normals[corner],0);SkewNormal a,b,n;for(int i=0;i<3;++i){a[i]=t.positions[1][i]-t.positions[0][i];b[i]=t.positions[2][i]-t.positions[0][i];}n={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};return skew_projection_normal(n,n,0);};
  for(const auto &t:scene.uv_triangles)for(int a=0;a<3;++a){if(!(t.boundary_edges&(1<<a)))continue;const int b=(a+1)%3;auto ia=t.vertex_ids[a],ib=t.vertex_ids[b];if(ia==ib)continue;Use use{{t.positions[a],t.positions[b],std::uint32_t(ia>>32)},normal(t,a),normal(t,b)};if(ia>ib){std::swap(ia,ib);std::swap(use.segment.a,use.segment.b);std::swap(use.na,use.nb);}uses[{ia,ib}].push_back(use);}
  std::vector<SkewEdge> result;
  for(const auto &[key,edge]:uses){bool hard=edge.size()!=2;if(!hard){const auto dot=[](SkewNormal a,SkewNormal b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};hard=dot(edge[0].na,edge[1].na)<.9995f||dot(edge[0].nb,edge[1].nb)<.9995f;}if(hard)result.push_back(edge.front().segment);}
  return result;
}
}
