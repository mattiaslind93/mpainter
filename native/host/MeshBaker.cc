#include "TextureExtent.hh"
#include "MeshBaker.hh"
#include "GpuBakeRays.hh"
#include "TextureMemory.hh"
#include "MeshBakeFilter.hh"
#include "SurfaceCurvature.hh"
#include "TexturePadding.hh"
#include <algorithm>
#include <cstdio>
#include <numbers>
#include <limits>
#include "BLI_kdopbvh.hh"
#include "BLI_math_geom.h"
#include "BLI_math_vector.h"
#include "BLI_task.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "RE_bake.h"
#include "MEM_guardedalloc.h"
namespace bts {
using namespace blender;
namespace {
struct RayScene {
  std::vector<std::array<std::array<float,3>,3>> triangles;
  std::vector<std::uint32_t> objects;
  std::map<std::uint32_t,std::set<std::uint32_t>> matches;std::set<std::uint32_t> ignore_backfaces;
  struct Query {const RayScene *scene;std::uint32_t object;};
  BVHTree *tree=nullptr;
  explicit RayScene(const SceneSnapshot &snapshot,const MeshBakeSettings &settings):matches(settings.matched_objects),ignore_backfaces(settings.ignore_backface_objects){
    triangles.reserve(snapshot.uv_triangles.size());for(const auto &triangle:snapshot.uv_triangles){triangles.push_back(triangle.positions);objects.push_back(std::uint32_t(triangle.vertex_ids[0]>>32));}
    tree=BLI_bvhtree_new(int(triangles.size()),0,4,6);if(!tree)throw std::runtime_error("Cannot build native bake BVH");for(int i=0;i<int(triangles.size());++i)BLI_bvhtree_insert(tree,i,triangles[i][0].data(),3);BLI_bvhtree_balance(tree);
  }
  struct NearestQuery {const RayScene *scene;const FillSurfacePoint *point;};
  static void closest(void *data,int index,const float co[3],BVHTreeNearest *nearest){
    const auto &q=*static_cast<NearestQuery *>(data);const auto &s=*q.scene;
    if(const auto match=s.matches.find(q.point->match_object);match!=s.matches.end()&&!match->second.contains(s.objects[index]))return;
    if(q.point->match_object&&s.matches.empty()&&q.point->match_object!=s.objects[index])return;
    const auto &tri=s.triangles[index];float p[3],n[3];normal_tri_v3(n,tri[0].data(),tri[1].data(),tri[2].data());
    if(dot_v3v3(n,q.point->normal.data())<0)return;
    closest_on_tri_to_point_v3(p,co,tri[0].data(),tri[1].data(),tri[2].data());const float d=len_squared_v3v3(p,co);
    if(d<nearest->dist_sq){nearest->index=index;nearest->dist_sq=d;copy_v3_v3(nearest->co,p);copy_v3_v3(nearest->no,n);}
  }
  BVHTreeNearest nearest(const FillSurfacePoint &point)const{
    NearestQuery query{this,&point};BVHTreeNearest nearest{};nearest.index=-1;nearest.dist_sq=std::numeric_limits<float>::max();
    BLI_bvhtree_find_nearest(tree,point.position.data(),&nearest,closest,&query);return nearest;
  }
  ~RayScene(){if(tree)BLI_bvhtree_free(tree);}
  static void raycast(void *userdata,int index,const BVHTreeRay *ray,BVHTreeRayHit *hit){const auto &query=*static_cast<const Query *>(userdata);const auto &scene=*query.scene;if(const auto match=scene.matches.find(query.object);match!=scene.matches.end()&&!match->second.contains(scene.objects[index]))return;const auto &triangle=scene.triangles[index];if(scene.ignore_backfaces.contains(scene.objects[index])){float normal[3];normal_tri_v3(normal,triangle[0].data(),triangle[1].data(),triangle[2].data());if(dot_v3v3(normal,ray->direction)>=0)return;}float distance;if(bake_bvh::surface_hit(triangle,ray->origin,ray->direction,distance)&&distance<hit->dist){hit->index=index;hit->dist=distance;}}
  float distance(const float origin[3],const float direction[3],float maximum,std::uint32_t object=0) const{Query query{this,object};BVHTreeRayHit hit{};hit.index=-1;hit.dist=maximum;BLI_bvhtree_ray_cast(tree,origin,direction,0,&hit,raycast,&query);return hit.index<0?maximum:hit.dist;}
};
float ray_average(const RayScene &scene,const FillSurfacePoint &point,const std::array<float,3> &geometric,int rays,float maximum,float epsilon,bool inward,std::uint32_t seed){
  bake_bvh::Frame frame;if(!bake_bvh::frame(frame,point.position,point.normal,seed,maximum,inward))return 1;
  float sum=0;
  for(int i=0;i<rays;++i){std::array<float,4> origin,direction;bake_bvh::surface_ray(frame,i,rays,geometric,inward,epsilon,origin,direction);
    sum+=scene.distance(origin.data(),direction.data(),maximum,point.match_object)>=maximum?1.f:0.f;
  }
  return sum/rays;
}
bool displaced(const MeshBakeSettings &settings){for(const auto &[uid,field]:settings.displacement)if(field.strength!=0)return true;return false;}



}
SceneSnapshot prepare_bake_surface(const SceneSnapshot &source,const MeshBakeSettings &settings){
  if(source.uv_triangles.empty())throw std::invalid_argument("Empty bake surface");
  SceneSnapshot snapshot=source;
  if(displaced(settings)){
    std::vector<UvFaceTriangle> triangles;
    std::uint64_t count=0;
    for(const auto &tri:source.uv_triangles){
      const auto field=settings.displacement.find(source.materials.at(tri.material).session_id);
      const int level=source.evaluated_subdivision||field==settings.displacement.end()||field->second.strength==0?0:field->second.subdivision;
      count=resource_sum(count,bake_subdivision_plan(1,level).triangles);
    }
    bake_ray_tree_nodes(count);triangles.reserve(std::size_t(count));
    for(const auto &tri:source.uv_triangles){
      const auto found=settings.displacement.find(source.materials.at(tri.material).session_id);
      if(found==settings.displacement.end()||found->second.strength==0){triangles.push_back(tri);continue;}
      const auto &field=found->second;
      BakeTri base;const auto face=normalized_normal(SurfaceCurvature::cross(SurfaceCurvature::sub(tri.positions[1],tri.positions[0]),SurfaceCurvature::sub(tri.positions[2],tri.positions[0])));
      for(int k=0;k<3;++k){base.corners[k].position=tri.positions[k];base.corners[k].normal=tri.has_normals?tri.normals[k]:face;base.corners[k].uv=tri.corners[k];}
      std::vector<BakeTri> parts;
      if(source.evaluated_subdivision){
        float u=0,v=0;for(const auto &corner:base.corners){u+=corner.uv[0]/3;v+=corner.uv[1]/3;}
        const int tile=1001+int(std::floor(u))+10*int(std::floor(v));
        for(auto &corner:base.corners)bake_displace_corner(corner,field,tile);
        parts.push_back(base);
      }else parts=displace_bake_mesh({base},field);
      for(const auto &part:parts){
        auto out=tri;
        if(!source.evaluated_subdivision&&field.subdivision>0)out.boundary_edges=7;
        for(int k=0;k<3;++k){out.positions[k]=part.corners[k].position;out.corners[k]=part.corners[k].uv;out.normals[k]=part.corners[k].normal;}triangles.push_back(out);
      }
    }
    snapshot.uv_triangles=std::move(triangles);
    snapshot.minimum=snapshot.maximum=snapshot.uv_triangles.front().positions[0];
    for(const auto &tri:snapshot.uv_triangles)for(const auto &p:tri.positions)for(int a=0;a<3;++a){snapshot.minimum[a]=std::min(snapshot.minimum[a],p[a]);snapshot.maximum[a]=std::max(snapshot.maximum[a],p[a]);}
  }
  SurfaceCurvature smooth(snapshot,true);
  return snapshot;
}
MeshBakeJob::MeshBakeJob(SceneSnapshot snapshot,std::vector<MeshBakeInput> inputs,MeshBakeSettings settings)
 :MeshBakeJob(std::make_shared<const SceneSnapshot>(std::move(snapshot)),std::move(inputs),std::move(settings)){}
MeshBakeJob::MeshBakeJob(std::shared_ptr<const SceneSnapshot> source,std::vector<MeshBakeInput> inputs,MeshBakeSettings settings){
  require_texture_resolution(settings.resolution,"Bake width",32,true);require_texture_resolution(settings.rows(),"Bake height",32,true);
  if(!source)throw std::invalid_argument("Missing bake source");const auto &snapshot=*source;
  if(!settings.use_project_mesh&&!settings.external_source)throw std::invalid_argument("Enable the project mesh as bake source");if(!valid_sparse_texture_extent(settings.resolution,32,true)||!valid_sparse_texture_extent(settings.rows(),32,true)||(settings.sampling!=1&&settings.sampling!=2&&settings.sampling!=4&&settings.sampling!=8)||settings.rays<1||settings.rays>512||settings.dilation<0||settings.dilation>64||inputs.empty()||snapshot.uv_triangles.empty())throw std::invalid_argument("Invalid mesh bake settings");
  for(float value:{settings.curvature_radius,settings.curvature_scale,settings.ao_distance,settings.thickness_distance,settings.ray_offset,settings.normal_detail})if(!std::isfinite(value)||value<0)throw std::invalid_argument("Invalid world-space bake parameter");
  for(const auto &input:inputs){if(!input.atlas||input.atlas->resolution!=settings.resolution*settings.sampling||(input.atlas->height?input.atlas->height:input.atlas->resolution)!=settings.rows()*settings.sampling)throw std::invalid_argument("Bake input atlas is missing");for(int role=0;role<7;++role)if(settings.enabled[role]){for(const auto &[tile,surface]:input.atlas->tiles){tasks.push_back(std::string(mesh_map_names[role])+" — "+input.name+" / "+std::to_string(tile));}}}
  if(tasks.empty())throw std::length_error("Selected mesh maps are empty");
  // Completed maps live in the disk cache; budget only simultaneous scratch.
  const auto output_bytes=resource_product(resource_product(settings.resolution,settings.rows()),16);
  const auto sampled_bytes=resource_product(output_bytes,resource_product(settings.sampling,settings.sampling));
  const auto working_bytes=std::max(resource_sum(sampled_bytes,output_bytes),resource_product(output_bytes,4));

  worker_=std::jthread([this,source=std::move(source),inputs=std::move(inputs),settings,working_bytes]()mutable{
    try{auto snapshot=prepare_bake_surface(*source,settings);
      // Raster positions and ray triangles must be the same displaced surface.
      if(settings.use_project_mesh&&(displaced(settings)||settings.enabled[3]||settings.enabled[4]||settings.enabled[6]))for(auto &input:inputs){
        std::set<int> tiles;for(const auto &[tile,points]:input.atlas->tiles)tiles.insert(tile);
        TexturePadding geometry(snapshot,input.material_uid);
        input.atlas=geometry.projection_geometry(settings.resolution*settings.sampling,tiles,false,settings.rows()*settings.sampling);
      }
      SurfaceCurvature curvature_field(snapshot,false);

      std::optional<RayScene> scene;scene.emplace(snapshot,settings);double extent=0,radius_squared=0;std::array<double,3> center;for(int i=0;i<3;++i){const double size=snapshot.maximum[i]-snapshot.minimum[i];extent=std::max(extent,size);radius_squared+=size*size*.25;center[i]=(snapshot.maximum[i]+snapshot.minimum[i])*.5;}extent=std::max(extent,1e-5);const double diameter=std::max(2*std::sqrt(radius_squared),1e-5);
      if(settings.enabled[int(MeshMapRole::Curvature)])curvature_field.calculate_pointiness(settings.curvature_radius);
      const float offset=settings.ray_offset>0?settings.ray_offset:float(extent*1e-6);
      int task=0;
      for(const auto &input:inputs){MeshBakeResult output;output.material_uid=input.material_uid;
        for(int role=0;role<7;++role)if(settings.enabled[role]){if(cancelled)throw std::runtime_error("Bake cancelled");MeshMap map;map.name=std::string(mesh_map_names[role])+" "+input.name;
          for(const auto &[tile,surface]:input.atlas->tiles){current=task;rows=0;std::printf("BTS_BAKE_TASK uid=%u tile=%d role=%d width=%d height=%d scratch_estimate=%llu available_ram=%llu\n",input.material_uid,tile,role,settings.resolution,settings.rows(),(unsigned long long)working_bytes,(unsigned long long)texture_available_ram_bytes());const int resolution=settings.resolution*settings.sampling,height=settings.rows()*settings.sampling;std::vector<float> gpu_values;
            std::vector<std::array<float,3>> geometric(surface.size());std::vector<float> curvature(surface.size(),0);
            threading::parallel_for(IndexRange(surface.size()),256,[&](const IndexRange range){for(std::size_t i:range)if(surface[i].valid){
              geometric[i]=surface[i].normal;const auto nearest=scene->nearest(surface[i]);if(nearest.index<0)continue;
              std::copy_n(nearest.no,3,geometric[i].begin());
              const auto &tri=snapshot.uv_triangles[nearest.index];float weights[3];interp_weights_tri_v3(weights,tri.positions[0].data(),tri.positions[1].data(),tri.positions[2].data(),nearest.co);
              for(int k=0;k<3;++k)curvature[i]+=weights[k]*curvature_field.vertices[curvature_field.corners[nearest.index][k]].curvature;
            }});
            const bool inward=role==int(MeshMapRole::Thickness);
            const float maximum=inward?(settings.thickness_distance>0?settings.thickness_distance:float(extent*.25)):(settings.ao_distance>0?settings.ao_distance:float(extent));
            const bool ray_map=role==int(MeshMapRole::AmbientOcclusion)||role==int(MeshMapRole::Thickness);
            if(ray_map&&settings.with_gpu&&settings.matched_objects.empty()&&settings.ignore_backface_objects.empty()){
              std::string gpu_error;GpuBakeRayStats stats;
              const bool success=request_gpu([&]{gpu_values=gpu_bake_cosine_occlusion(scene->triangles,surface,settings.rays,maximum,inward,&stats,nullptr,offset,true,geometric);},settings.with_gpu,gpu_error);
              if(cancelled)throw std::runtime_error("Bake cancelled");
              if(success){++gpu_maps;std::printf("BTS_BAKE_GPU_MAP uid=%u tile=%d role=%d batches=%llu buffers_released=1\n",input.material_uid,tile,role,(unsigned long long)stats.batches);}
              else{gpu_values.clear();cpu_fallback=true;std::fprintf(stderr,"BTS_BAKE_GPU_FALLBACK uid=%u tile=%d role=%d reason=%s; continuing full-resolution CPU bake\n",input.material_uid,tile,role,gpu_error.c_str());}
            }
            if(ray_map&&gpu_values.empty())++cpu_ray_maps;
            std::vector<float> pixels(std::size_t(resolution)*height*4,0);
            threading::parallel_for(IndexRange(height),4,[&](const IndexRange range){for(int y:range){if(cancelled)return;for(int x=0;x<resolution;++x){const auto index=std::size_t(y)*resolution+x;const auto &point=surface[index];if(!point.valid)continue;float rgb[3]{0,0,0};switch(MeshMapRole(role)){
              case MeshMapRole::Normal:std::copy_n(point.baked_normal.begin(),3,rgb);break;
              case MeshMapRole::WorldNormal:for(int i=0;i<3;++i)rgb[i]=point.normal[i]*.5f+.5f;break;
              case MeshMapRole::Id:if(settings.external_source)std::copy_n(point.source_color.begin(),3,rgb);else std::copy_n(input.source_material.base_color.begin(),3,rgb);break;
              case MeshMapRole::AmbientOcclusion:if(!gpu_values.empty()){rgb[0]=rgb[1]=rgb[2]=gpu_values[index];break;}rgb[0]=rgb[1]=rgb[2]=ray_average(*scene,point,geometric[index],settings.rays,maximum,offset,false,std::uint32_t(index));break;
              case MeshMapRole::Curvature:rgb[0]=rgb[1]=rgb[2]=std::clamp(.5f+settings.curvature_scale*(curvature[index]-.5f),0.f,1.f);break;
              case MeshMapRole::Position:for(int i=0;i<3;++i)rgb[i]=float(std::clamp((point.position[i]-center[i])/diameter+.5,0.,1.));break;
              case MeshMapRole::Thickness:if(!gpu_values.empty()){rgb[0]=rgb[1]=rgb[2]=gpu_values[index];break;}rgb[0]=rgb[1]=rgb[2]=ray_average(*scene,point,geometric[index],settings.rays,maximum,offset,true,std::uint32_t(index));break;
            }auto *pixel=pixels.data()+index*4;std::copy_n(rgb,3,pixel);pixel[3]=1;}++rows;}});
            if(cancelled)throw std::runtime_error("Bake cancelled");
            if(ray_map&&settings.denoise)denoise_mesh_bake(pixels,surface,resolution,height);

            pixels=filter_mesh_bake(std::move(pixels),resolution,height,settings.resolution,settings.rows(),settings.dilation,settings.diffusion,role);auto data=std::make_shared<MeshMapPixels>(settings.resolution,std::move(pixels),settings.rows());map.tiles.emplace(tile,MeshMapPlane{settings.resolution,data->hash,std::move(data),settings.rows()});++task;completed=task;
          }output.maps[role]=std::move(map);
        }result.push_back(std::move(output));
      }
    }catch(const std::exception &exception){error=exception.what();result.clear();}finished.store(true,std::memory_order_release);
  });
}
bool MeshBakeJob::request_gpu(const std::function<void()> &work,const std::function<void(const std::function<void()> &)> &owner,std::string &error){
 std::unique_lock lock(gpu_mutex_);if(cancelled)return false;gpu_complete_=false;
 gpu_work_=[&]{try{owner(work);}catch(const std::exception &failure){error=failure.what();}catch(...){error="Unknown GPU failure";}};
 gpu_ready_.wait(lock,[&]{return gpu_complete_||(cancelled&&!gpu_running_);});
 if(cancelled){gpu_work_={};return false;}return error.empty();
}
void MeshBakeJob::process_gpu(){
 std::function<void()> work;{std::lock_guard lock(gpu_mutex_);if(!gpu_work_)return;if(cancelled){gpu_work_={};gpu_ready_.notify_all();return;}gpu_running_=true;work=std::move(gpu_work_);}
 work();{std::lock_guard lock(gpu_mutex_);gpu_running_=false;gpu_complete_=true;}gpu_ready_.notify_all();
}
MeshBakeJob::~MeshBakeJob(){cancel();if(worker_.joinable())worker_.join();}
}
