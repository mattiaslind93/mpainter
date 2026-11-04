#pragma once
#include "NativeFragmentGeometry.hh"
#include "NativeMaterialFragmentGpuCompiler.hh"
#include "ResourceAvailability.hh"
#include "BLI_kdopbvh.hh"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
namespace bts {
using NativeWorldUvTriangle=std::array<std::array<float,2>,3>;
// Strict convex-interior SAT. Shared vertices/edges have zero intersection
// area and are admitted; identical or partly overlapping interiors reject.
inline bool native_world_uv_interior_overlap(const NativeWorldUvTriangle &a,const NativeWorldUvTriangle &b){
 const auto orientation=[](const auto &a,const auto &b,const auto &c){return (double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]);};
 const auto separated=[&](const auto &left,const auto &right){for(int i=0;i<3;++i){const auto &p=left[i],&q=left[(i+1)%3];const double sign=orientation(p,q,left[(i+2)%3]);if(sign==0)return true;bool interior=false;for(const auto &v:right)interior|=orientation(p,q,v)*(sign>0?1:-1)>0;if(!interior)return true;}return false;};
 return !separated(a,b)&&!separated(b,a);
}
// Worker geometry admission, once per immutable revision. Reuse Blender's BVH
// range traversal; no all-pair overlap result, texel raster or per-edit mesh work.
inline void require_native_world_unique_uv(const NativeFragmentGeometry &geometry,const std::atomic_bool &cancel){
 std::size_t count=0;for(const auto &stream:geometry.streams())if(std::any_of(stream.requests.begin(),stream.requests.end(),[](const auto &r){return r.kind==NativeFragmentUvKind::DefaultRender;}))count=resource_sum(count,geometry.vertices(&stream-geometry.streams().data()).size()/3);
 if(count>std::size_t(std::numeric_limits<int>::max()))throw std::length_error("Native world UV admission exceeds Blender BVH index API");
 // Triangle table plus BVH nodes, temporary range traversal and allocator credit.

 std::vector<NativeWorldUvTriangle> triangles;triangles.reserve(count);
 for(std::size_t i=0;i<geometry.streams().size();++i){const auto &stream=geometry.streams()[i];if(!std::any_of(stream.requests.begin(),stream.requests.end(),[](const auto &r){return r.kind==NativeFragmentUvKind::DefaultRender;}))continue;const auto vertices=geometry.vertices(i);for(std::size_t j=0;j<vertices.size();j+=3){if(cancel.load())throw std::runtime_error("Native world geometry cancelled");triangles.push_back({vertices[j].raster_uv,vertices[j+1].raster_uv,vertices[j+2].raster_uv});}}
 if(triangles.empty())throw std::invalid_argument("Native world geometry has no target UV triangles");
 std::unique_ptr<blender::BVHTree,blender::BVHTreeDeleter> tree(blender::BLI_bvhtree_new(int(triangles.size()),0,4,6));if(!tree)throw std::bad_alloc();
 for(std::size_t i=0;i<triangles.size();++i){float points[3][3]{};for(int c=0;c<3;++c){points[c][0]=triangles[i][c][0];points[c][1]=triangles[i][c][1];}blender::BLI_bvhtree_insert(tree.get(),int(i),points[0],3);}blender::BLI_bvhtree_balance(tree.get());
 struct Query{const std::vector<NativeWorldUvTriangle> *triangles;std::size_t index=0;bool overlap=false;};Query query{&triangles};
 const auto visit=[](void *raw,int index,const float *,float){auto &q=*static_cast<Query *>(raw);if(!q.overlap&&std::size_t(index)>q.index)q.overlap=native_world_uv_interior_overlap((*q.triangles)[q.index],(*q.triangles)[index]);};
 for(std::size_t i=0;i<triangles.size();++i){if(cancel.load())throw std::runtime_error("Native world geometry cancelled");query.index=i;std::array<double,2> minimum{triangles[i][0][0],triangles[i][0][1]},maximum=minimum;for(const auto &p:triangles[i])for(int c=0;c<2;++c){minimum[c]=std::min(minimum[c],double(p[c]));maximum[c]=std::max(maximum[c],double(p[c]));}float center[3]{float((minimum[0]+maximum[0])*.5),float((minimum[1]+maximum[1])*.5),0};double radius_double=0;for(const auto &point:triangles[i])radius_double=std::max(radius_double,std::hypot(double(point[0])-center[0],double(point[1])-center[1]));const float radius=std::nextafter(float(radius_double),std::numeric_limits<float>::infinity());if(!std::isfinite(radius))throw std::invalid_argument("Native world target UV range exceeds finite Blender BVH radius");blender::BLI_bvhtree_range_query(tree.get(),center,radius,visit,&query);if(query.overlap)throw std::invalid_argument("Native world Normal requires unique target UV interiors; overlapping surfaces cannot share an absolute-world vector texture");}
}
}
