#include "ResourceAvailability.hh"
#include "SurfaceBrush.hh"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <cstdlib>
#include "Performance.hh"
#include "BKE_attribute.hh"
#include "BKE_bvhutils.hh"
#include "BKE_layer.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_mapping.hh"
#include "BKE_object.hh"
#include "BKE_scene.hh"
#include "BLI_listbase.h"
#include "BLI_math_geom.h"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
namespace bts {
using namespace blender;
class SurfaceBrushCache {
 public:
 struct Entry {
  Array<int> offsets,indices;std::vector<BrushFaceFrame> frames;
  std::vector<std::uint32_t> visited,primary,materials;
  std::vector<signed char> material_values;
  const void *corners=nullptr,*edges=nullptr;int faces_num=-1,edges_num=-1,corners_num=-1;
  std::uint32_t epoch=0;
 };
 std::map<const Mesh *,Entry> meshes;unsigned oracle_checks=0;
};
std::shared_ptr<SurfaceBrushCache> make_surface_brush_cache(){return std::make_shared<SurfaceBrushCache>();}
void reset_surface_brush_cache(SurfaceBrushCache &cache){cache.meshes.clear();cache.oracle_checks=0;}
namespace {
std::shared_ptr<const PaintPixels::SurfaceFootprint> connected_footprint(const Mesh &mesh,const Object &object,
    float3 center,float radius,int seed,std::uint32_t selected,float3 tangent,float3 bitangent,float3 normal,float3 view_direction,bool square,const BrushProjection &projection,SurfaceBrushCache *cache)
{
  const auto faces=mesh.faces();const auto vertices=mesh.corner_verts(),edges=mesh.corner_edges();
  const auto positions=mesh.vert_positions();const auto triangles=mesh.corner_tris();
  const auto attributes=mesh.attributes();
  const VArraySpan<float2> uv=*attributes.lookup<float2>(mesh.active_or_default_uv_map_name(),bke::AttrDomain::Corner);
  const auto material_indices=*attributes.lookup_or_default<int>("material_index",bke::AttrDomain::Face,0);
  const int slots=BKE_object_material_count_eval(object,mesh.id);
  // Original cages are immutable between host geometry resets. Reuse Blender's
  // adjacency result and sparse per-query scratch; no O(all faces) clearing per dab.
  SurfaceBrushCache::Entry local;
  if(cache&&cache->meshes.size()>16&&!cache->meshes.contains(&mesh))cache->meshes.clear();
  auto &scratch=cache?cache->meshes[&mesh]:local;
  if(scratch.faces_num!=mesh.faces_num||scratch.edges_num!=mesh.edges_num||scratch.corners_num!=mesh.corners_num||scratch.corners!=vertices.data()||scratch.edges!=edges.data()){
   bke::mesh::build_edge_to_face_map(faces,edges,mesh.edges_num,scratch.offsets,scratch.indices);
   scratch.frames.resize(mesh.faces_num);scratch.visited.assign(mesh.faces_num,0);scratch.primary.assign(mesh.faces_num,0);scratch.materials.assign(mesh.faces_num,0);scratch.material_values.resize(mesh.faces_num);
   scratch.faces_num=mesh.faces_num;scratch.edges_num=mesh.edges_num;scratch.corners_num=mesh.corners_num;scratch.corners=vertices.data();scratch.edges=edges.data();scratch.epoch=0;
   if(cache&&std::getenv("BTS_STROKE_TARGET_PROFILE"))std::printf("BTS_SURFACE_CACHE_BUILD faces=%d edges=%d corners=%d\n",mesh.faces_num,mesh.edges_num,mesh.corners_num);
  }
  if(++scratch.epoch==0){std::fill(scratch.visited.begin(),scratch.visited.end(),0);std::fill(scratch.primary.begin(),scratch.primary.end(),0);std::fill(scratch.materials.begin(),scratch.materials.end(),0);scratch.epoch=1;}
  const auto epoch=scratch.epoch;
  const GroupedSpan<int> edge_faces{OffsetIndices<int>{scratch.offsets},scratch.indices};
  auto &material_cache=scratch.material_values;
  const auto allowed=[&](int face) {
    auto &cached=material_cache[face];if(scratch.materials[face]==epoch)return bool(cached);scratch.materials[face]=epoch;
    auto *material=slots?BKE_object_material_get_eval(object,mesh.id,short(std::clamp(material_indices[face],0,slots-1)+1)):nullptr;
    cached=material&&DEG_get_original(material)->id.session_uid==selected;return bool(cached);
  };
  const auto seam=[&](int face,int a,int b) {
    const auto range=faces[face];const auto next=[&](int corner){return corner+1==range.one_after_last()?range.start():corner+1;};
    const int edge_corner=next(a)==b?a:next(b)==a?b:-1;
    if(edge_corner<0)return false; // Internal tessellation edge, not a UV boundary.
    for(int other:edge_faces[edges[edge_corner]])if(other!=face&&allowed(other)) {
      int match_a=-1,match_b=-1;
      for(int corner:faces[other]){if(vertices[corner]==vertices[a])match_a=corner;if(vertices[corner]==vertices[b])match_b=corner;}
      if(match_a>=0&&match_b>=0&&math::distance_squared(uv[a],uv[match_a])<1e-12f&&math::distance_squared(uv[b],uv[match_b])<1e-12f)return false;
    }
    return true;
  };
  auto result=std::make_shared<PaintPixels::SurfaceFootprint>();result->center={center.x,center.y,center.z};result->radius=radius;result->tangent={tangent.x,tangent.y,tangent.z};result->bitangent={bitangent.x,bitangent.y,bitangent.z};
  result->normal={normal.x,normal.y,normal.z};result->view_direction={view_direction.x,view_direction.y,view_direction.z};result->projected=true;
  result->unwrapped=projection.alignment==BrushAlignment::TangentWrap;result->planar_fade=projection.alignment==BrushAlignment::TangentPlanar;result->cull=projection.cull;result->cull_angle=projection.angle;
  const auto normal_matrix=math::transpose(math::invert(object.object_to_world()));
  const auto centroid=[&](int face){float3 center(0);for(int corner:faces[face])center+=math::transform_point(object.object_to_world(),positions[vertices[corner]]);return center/float(faces[face].size());};
  const auto face_normal=[&](int face){const auto corners=faces[face];return math::normalize(math::transform_direction(normal_matrix,math::cross(positions[vertices[corners[1]]]-positions[vertices[corners[0]]],positions[vertices[corners[2]]]-positions[vertices[corners[0]]])));};
  auto &frames=scratch.frames;frames[seed].x=BrushFaceFrame::vector(tangent);frames[seed].y=BrushFaceFrame::vector(bitangent);frames[seed].offset={-math::dot(center,tangent),-math::dot(center,bitangent)};
  const float search_radius=radius*(square?std::sqrt(3.f):1.f);
  auto &visited=scratch.visited;auto &primary=scratch.primary;std::vector<int> queue{seed};visited[seed]=primary[seed]=epoch;
  int next_component=0;
  for(std::size_t index=0;;++index) {
    if(index==queue.size()) {
      if(projection.cull)break;
      int next=-1;while(next_component<mesh.faces_num){const int face=next_component++;if(visited[face]!=epoch&&allowed(face)){next=face;break;}}
      if(next<0)break;
      visited[next]=epoch;primary[next]=0;frames[next]=frames[seed];queue.push_back(next);
    }
    const int face=queue[index];if(!allowed(face))continue;
    bool intersects=false;
    for(int triangle_index:bke::mesh::face_triangles_range(faces,face)) {
      const auto corners=triangles[triangle_index];std::array<float3,3> world;
      for(int i=0;i<3;++i)world[i]=math::transform_point(object.object_to_world(),positions[vertices[corners[i]]]);
      float3 closest;closest_on_tri_to_point_v3(closest,center,world[0],world[1],world[2]);
      if(math::distance_squared(closest,center)>search_radius*search_radius)continue;
      intersects=true;

      PaintPixels::SurfaceTriangle triangle;
      const auto n=face_normal(face);triangle.normal={n.x,n.y,n.z};triangle.bounded_depth=primary[face]!=epoch;
      for(int i=0;i<3;++i) {
        const auto coord=uv[corners[i]];triangle.uv[i]={coord.x,coord.y};triangle.position[i]={world[i].x,world[i].y,world[i].z};triangle.paint_coordinates[i]=frames[face].map(world[i]);
        triangle.seam_edges[i]=seam(face,corners[i],corners[(i+1)%3]);
      }
      result->triangles.push_back(std::move(triangle));
    }
    if(intersects)for(int corner:faces[face])for(int neighbor:edge_faces[edges[corner]])if(visited[neighbor]!=epoch&&allowed(neighbor)) {
      visited[neighbor]=epoch;primary[neighbor]=primary[face];
      const auto edge=mesh.edges()[edges[corner]];const auto a=math::transform_point(object.object_to_world(),positions[edge[0]]),b=math::transform_point(object.object_to_world(),positions[edge[1]]);
      frames[neighbor]=frames[face].neighbor(BrushFaceFrame::vector(a),BrushFaceFrame::vector(b),BrushFaceFrame::vector(centroid(face)),BrushFaceFrame::vector(centroid(neighbor)),BrushFaceFrame::vector(face_normal(neighbor)));
      queue.push_back(neighbor);
    }
  }
  return result;
}
}
std::optional<SurfaceBrushSample> surface_brush_stamp(Main &main,Scene &scene,Depsgraph &depsgraph,
  const View3D &view,std::uint32_t selected,const std::array<float,3> &origin_array,
  const std::array<float,3> &direction_array,float diameter,bool coverage,bool square,const BrushProjection &projection,const SourceMeshMap *source_meshes,SurfaceBrushCache *cache) {
  if (!std::isfinite(diameter) || diameter<=0 || diameter>100) return {};
  for(float v:origin_array)if(!std::isfinite(v))return {};
  for(float v:direction_array)if(!std::isfinite(v))return {};
  if(math::length_squared(float3(direction_array.data()))<1e-12f)return {};
  const float3 origin(origin_array.data()),direction=math::normalize(float3(direction_array.data()));
  BKE_scene_graph_evaluated_ensure(&depsgraph,&main);
  auto *layer=DEG_get_input_view_layer(&depsgraph);BKE_view_layer_synced_ensure(main,&scene,layer);
  float nearest=std::numeric_limits<float>::max();std::optional<SurfaceBrushSample> result;
  const Mesh *hit_mesh=nullptr;const Object *hit_object=nullptr;int hit_face=-1;float3 hit_tangent{},hit_bitangent{},hit_normal{};
  for (const Base &base:*BKE_view_layer_object_bases_get(layer)) {
    if (base.object->type!=OB_MESH || !BKE_base_is_visible(&view,&base)) continue;
    const auto *object=DEG_get_evaluated(&depsgraph,base.object);
    if (!(BKE_object_visibility(object,DAG_EVAL_VIEWPORT)&OB_VISIBLE_SELF)) continue;
    const auto *mesh=BKE_object_get_evaluated_mesh(object);if(source_meshes){const auto found=source_meshes->find(base.object->id.session_uid);if(found!=source_meshes->end())mesh=found->second;}if (!mesh) continue;
    bool invertible=false;const auto inverse=math::invert(object->object_to_world(),invertible);if (!invertible) continue;
    const auto local_origin=math::transform_point(inverse,origin);
    const auto local_direction=math::normalize(math::transform_direction(inverse,direction));
    auto bvh=mesh->bvh_corner_tris();if (!bvh.tree) continue;
    BVHTreeRayHit hit{};hit.index=-1;hit.dist=std::numeric_limits<float>::max();
    if (BLI_bvhtree_ray_cast(bvh.tree,local_origin,local_direction,0,&hit,bvh.raycast_callback,&bvh)<0) continue;
    const auto world_hit=math::transform_point(object->object_to_world(),float3(hit.co));
    const float distance=math::dot(world_hit-origin,direction);if (distance<0 || distance>=nearest) continue;
    if(!projection.symmetry_ray){nearest=distance;result.reset();hit_mesh=nullptr;} // Original input rays respect scene occluders.
    const auto corners=mesh->corner_tris()[hit.index];const auto verts=mesh->corner_verts();const auto positions=mesh->vert_positions();
    const auto attrs=mesh->attributes();const int face=mesh->corner_tri_faces()[hit.index];
    const auto material_indices=*attrs.lookup_or_default<int>("material_index",bke::AttrDomain::Face,0);
    const int slots=BKE_object_material_count_eval(*object,mesh->id);
    if (!slots) continue;
    auto *material=BKE_object_material_get_eval(*object,mesh->id,short(std::clamp(material_indices[face],0,slots-1)+1));
    if (!material || DEG_get_original(material)->id.session_uid!=selected) continue;
    if(projection.symmetry_ray){nearest=distance;result.reset();hit_mesh=nullptr;}
    const auto uv_name=mesh->active_or_default_uv_map_name();if (uv_name.is_empty()) continue;
    const VArraySpan<float2> uv=*attrs.lookup<float2>(uv_name,bke::AttrDomain::Corner);if (uv.is_empty()) continue;
    const auto p0=positions[verts[corners[0]]],p1=positions[verts[corners[1]]],p2=positions[verts[corners[2]]];
    const auto e1=math::transform_direction(object->object_to_world(),p1-p0),e2=math::transform_direction(object->object_to_world(),p2-p0);
    const auto normal=math::normalize(math::transform_direction(math::transpose(inverse),math::cross(p1-p0,p2-p0)));if (projection.alignment!=BrushAlignment::UV&&projection.cull&&math::dot(normal,direction)>=0) continue;
    const double a=math::dot(e1,e1),b=math::dot(e1,e2),c=math::dot(e2,e2),det=a*c-b*b;
    if (det<=1e-20) continue;
    const auto bounds=mesh->bounds_min_max();if (!bounds) continue;
    float3 low(std::numeric_limits<float>::max()),high(-std::numeric_limits<float>::max());
    for (int i=0;i<8;++i) {
      float3 corner;for(int axis=0;axis<3;++axis)corner[axis]=(i&(1<<axis))?bounds->max[axis]:bounds->min[axis];
      const auto world=math::transform_point(object->object_to_world(),corner);low=math::min(low,world);high=math::max(high,world);
    }
    const auto size=high-low;const float radius=std::max({size.x,size.y,size.z})*diameter/200;
    float3 tangent(projection.camera_right.data()),bitangent(projection.camera_up.data());
    if(projection.alignment!=BrushAlignment::Camera) {
      tangent-=normal*math::dot(normal,tangent);if(math::length_squared(tangent)<1e-12f)tangent=math::cross(float3(projection.camera_up.data()),normal);
      tangent=math::normalize(tangent);bitangent=math::cross(normal,tangent)*projection.orientation;
    }
    const auto uv0=uv[corners[0]],du=uv[corners[1]]-uv0,dv=uv[corners[2]]-uv0;
    const auto map=[&](const float3 &displacement) {
      const double d=math::dot(displacement,e1),e=math::dot(displacement,e2);
      const double u=(d*c-e*b)/det,v=(e*a-d*b)/det;
      return std::array<double,2>{du.x*u+dv.x*v,du.y*u+dv.y*v};
    };
    float weights[3];interp_weights_tri_v3(weights,p0,p1,p2,hit.co);
    const auto center=uv0*weights[0]+uv[corners[1]]*weights[1]+uv[corners[2]]*weights[2];
    hit_mesh=mesh;hit_object=object;hit_face=face;hit_tangent=tangent;hit_bitangent=bitangent;hit_normal=projection.alignment==BrushAlignment::Camera?math::normalize(math::cross(tangent,bitangent)):normal;
    result=SurfaceBrushSample{PaintPixels::Stamp{{center.x,center.y},map(tangent*radius),map(bitangent*radius),{}},{world_hit.x,world_hit.y,world_hit.z},radius,(std::uint64_t(DEG_get_original(object)->id.session_uid)<<32)|std::uint32_t(hit.index)};
  }
  if(result&&projection.alignment==BrushAlignment::UV) {
    const auto &x=result->footprint.axis_x,&y=result->footprint.axis_y;const double radius=std::sqrt(std::abs(x[0]*y[1]-x[1]*y[0]));
    result->footprint.axis_x={radius,0};result->footprint.axis_y={0,radius};
  }
  else if(result&&coverage){
    result->footprint.surface=connected_footprint(*hit_mesh,*hit_object,float3(result->position.data()),result->radius,hit_face,selected,hit_tangent,hit_bitangent,hit_normal,direction,square,projection,cache);
    if(cache&&std::getenv("BTS_SURFACE_CACHE_VERIFY")&&cache->oracle_checks++<8){
      const auto reference=connected_footprint(*hit_mesh,*hit_object,float3(result->position.data()),result->radius,hit_face,selected,hit_tangent,hit_bitangent,hit_normal,direction,square,projection,nullptr);
      const auto &actual=*result->footprint.surface;if(actual.triangles.size()!=reference->triangles.size())throw std::logic_error("Surface cache triangle count differs from uncached traversal");
      for(std::size_t i=0;i<actual.triangles.size();++i){const auto &a=actual.triangles[i],&b=reference->triangles[i];if(a.uv!=b.uv||a.position!=b.position||a.paint_coordinates!=b.paint_coordinates||a.normal!=b.normal||a.seam_edges!=b.seam_edges||a.bounded_depth!=b.bounded_depth)throw std::logic_error("Surface cache differs from uncached traversal");}
      std::printf("BTS_SURFACE_CACHE_ORACLE triangles=%zu exact=1\n",actual.triangles.size());
    }
  }
  return result;
}
}
