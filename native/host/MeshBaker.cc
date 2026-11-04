#include "TextureExtent.hh"
#include "MeshBaker.hh"
#include "TextureMemory.hh"
#include "MeshBakeFilter.hh"
#include "GpuBakeRays.hh"
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
  ~RayScene(){if(tree)BLI_bvhtree_free(tree);}
  static void raycast(void *userdata,int index,const BVHTreeRay *ray,BVHTreeRayHit *hit){const auto &query=*static_cast<const Query *>(userdata);const auto &scene=*query.scene;if(const auto match=scene.matches.find(query.object);match!=scene.matches.end()&&!match->second.contains(scene.objects[index]))return;const auto &triangle=scene.triangles[index];if(scene.ignore_backfaces.contains(scene.objects[index])){float normal[3];normal_tri_v3(normal,triangle[0].data(),triangle[1].data(),triangle[2].data());if(dot_v3v3(normal,ray->direction)>=0)return;}float distance,uv[2];if(isect_ray_tri_v3(ray->origin,ray->direction,triangle[0].data(),triangle[1].data(),triangle[2].data(),&distance,uv)&&distance>1e-7f&&distance<hit->dist){hit->index=index;hit->dist=distance;}}
  float distance(const float origin[3],const float direction[3],float maximum,std::uint32_t object=0) const{Query query{this,object};BVHTreeRayHit hit{};hit.index=-1;hit.dist=maximum;BLI_bvhtree_ray_cast(tree,origin,direction,0,&hit,raycast,&query);return hit.index<0?maximum:hit.dist;}
};
float ray_average(const RayScene &scene,const FillSurfacePoint &point,int rays,float maximum,float epsilon,bool inward,std::uint32_t seed){
  float normal[3],tangent[3],bitangent[3];copy_v3_v3(normal,point.normal.data());if(inward)negate_v3(normal);const float helper[3]={std::abs(normal[2])>.9f?1.f:0.f,0,std::abs(normal[2])>.9f?0.f:1.f};cross_v3_v3v3(tangent,helper,normal);normalize_v3(tangent);cross_v3_v3v3(bitangent,normal,tangent);float origin[3];madd_v3_v3v3fl(origin,point.position.data(),normal,epsilon);double sum=0;
  const double phase=(seed*0.6180339887498949-std::floor(seed*0.6180339887498949))*std::numbers::pi*2;
  for(int i=0;i<rays;++i){const double z=std::sqrt((i+.5)/rays),radius=std::sqrt(1-z*z),angle=i*2.399963229728653+phase;float direction[3];for(int axis=0;axis<3;++axis)direction[axis]=float(normal[axis]*z+tangent[axis]*radius*std::cos(angle)+bitangent[axis]*radius*std::sin(angle));sum+=scene.distance(origin,direction,maximum,point.match_object)/maximum;}
  return float(sum/rays);
}
bool displaced(const MeshBakeSettings &settings){for(const auto &[uid,field]:settings.displacement)if(field.strength>0)return true;return false;}
struct DisplacementBakePlan {std::uint64_t triangles=0,bytes=0;};
DisplacementBakePlan displacement_bake_plan(const SceneSnapshot &snapshot,const std::vector<MeshBakeInput> &inputs,const MeshBakeSettings &settings,std::uint64_t working_bytes){
  DisplacementBakePlan result;std::uint64_t temporary=0,atlas_bytes=0;
  for(const auto &tri:snapshot.uv_triangles){
    const auto field=settings.displacement.find(snapshot.materials.at(tri.material).session_id);
    const auto plan=bake_subdivision_plan(1,field!=settings.displacement.end()&&field->second.strength>0?field->second.subdivision:0);
    result.triangles=resource_sum(result.triangles,plan.triangles);
    temporary=std::max(temporary,resource_sum(plan.peak_bytes,sizeof(BakeTri)));
  }
  if(result.triangles>std::vector<UvFaceTriangle>{}.max_size())throw std::length_error("Displaced bake snapshot exceeds addressable vector storage");
  for(const auto &input:inputs){
    const auto field=settings.displacement.find(input.material_uid);
    if(field==settings.displacement.end()||field->second.strength<=0||!input.atlas)continue;
    for(const auto &[tile,points]:input.atlas->tiles)atlas_bytes=resource_sum(atlas_bytes,resource_product(points.size(),sizeof(FillSurfacePoint)+sizeof(float)));
    for(const auto &[tile,curve]:input.atlas->curvature)atlas_bytes=resource_sum(atlas_bytes,resource_product(curve.size(),sizeof(float)));
  }
  // Before copying the snapshot, admit its copy, exact expanded output, the
  // largest per-triangle split, all replacement atlases and later ray scratch.
  result.bytes=resource_sum(working_bytes,resource_sum(atlas_bytes,temporary));
  result.bytes=resource_sum(result.bytes,resource_product(resource_sum(snapshot.uv_triangles.size(),result.triangles),sizeof(UvFaceTriangle)));
  if(settings.enabled[int(MeshMapRole::AmbientOcclusion)]||settings.enabled[int(MeshMapRole::Thickness)])result.bytes=resource_sum(result.bytes,bake_ray_tree_bytes(result.triangles));
  require_texture_ram(result.bytes,"Expanded displacement bake geometry, atlases and ray buffers");
  return result;
}
void apply_bake_displacement(SceneSnapshot &snapshot,std::vector<MeshBakeInput> &inputs,const MeshBakeSettings &settings,std::uint64_t expanded_triangles){
  const ResourceBudget split_memory;
  std::vector<UvFaceTriangle> triangles;triangles.reserve(std::size_t(expanded_triangles));
  for(const auto &tri:snapshot.uv_triangles){const auto uid=snapshot.materials.at(tri.material).session_id;const auto field=settings.displacement.find(uid);if(field==settings.displacement.end()||field->second.strength<=0){triangles.push_back(tri);continue;}BakeTri source;for(int k=0;k<3;++k){source.corners[k].position=tri.positions[k];source.corners[k].uv=tri.corners[k];source.corners[k].normal=tri.has_normals?tri.normals[k]:std::array<float,3>{0,0,1};}for(const auto &part:displace_bake_mesh({source},field->second,split_memory)){auto out=tri;out.has_normals=true;for(int k=0;k<3;++k){out.positions[k]=part.corners[k].position;out.corners[k]=part.corners[k].uv;out.normals[k]=part.corners[k].normal;}triangles.push_back(out);}}
  snapshot.uv_triangles=std::move(triangles);if(!snapshot.uv_triangles.empty()){snapshot.minimum=snapshot.maximum=snapshot.uv_triangles.front().positions[0];snapshot.has_bounds=true;for(const auto &tri:snapshot.uv_triangles)for(const auto &p:tri.positions)for(int a=0;a<3;++a){snapshot.minimum[a]=std::min(snapshot.minimum[a],p[a]);snapshot.maximum[a]=std::max(snapshot.maximum[a],p[a]);}}
  for(auto &input:inputs){const auto field=settings.displacement.find(input.material_uid);if(field==settings.displacement.end()||field->second.strength<=0||!input.atlas)continue;auto atlas=std::make_shared<FillSurfaceAtlas>(*input.atlas);for(auto &[tile,points]:atlas->tiles){const int width=atlas->resolution,height=atlas->height?atlas->height:atlas->resolution;auto &curve=atlas->curvature[tile];if(curve.size()!=points.size())curve.assign(points.size(),0);const int tu=(tile-1001)%10,tv=(tile-1001)/10;for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto &point=points[std::size_t(y)*width+x];if(!point.valid)continue;const float u=(x+.5f)/width+tu,v=(y+.5f)/height+tv;BakeCorner corner;corner.position=point.position;corner.normal=point.normal;corner.uv={u,v};bake_displace_corner(corner,field->second);point.position=corner.position;curve[std::size_t(y)*width+x]+=bake_cavity(field->second,u,v);}for(int y=1;y<height-1;++y)for(int x=1;x<width-1;++x){auto &point=points[std::size_t(y)*width+x];const auto &left=points[std::size_t(y)*width+x-1],&right=points[std::size_t(y)*width+x+1],&down=points[std::size_t(y-1)*width+x],&up=points[std::size_t(y+1)*width+x];if(!point.valid||!left.valid||!right.valid||!down.valid||!up.valid)continue;const std::array<float,3> e1{right.position[0]-left.position[0],right.position[1]-left.position[1],right.position[2]-left.position[2]},e2{up.position[0]-down.position[0],up.position[1]-down.position[1],up.position[2]-down.position[2]},n{e1[1]*e2[2]-e1[2]*e2[1],e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]};float length=0,alignment=0;for(int a=0;a<3;++a){length+=n[a]*n[a];alignment+=n[a]*point.normal[a];}length=std::sqrt(length);if(length<1e-8f)continue;for(int a=0;a<3;++a)point.normal[a]=(alignment<0?-n[a]:n[a])/length;}}input.atlas=std::move(atlas);}
}
void precompute_gpu_rays(const SceneSnapshot &snapshot,const std::vector<MeshBakeInput> &inputs,MeshBakeSettings &settings){
  if(!settings.with_gpu||(!settings.enabled[int(MeshMapRole::AmbientOcclusion)]&&!settings.enabled[int(MeshMapRole::Thickness)]))return;
  double extent=0;for(int i=0;i<3;++i)extent=std::max(extent,double(snapshot.maximum[i]-snapshot.minimum[i]));extent=std::max(extent,1e-5);
  std::vector<std::array<std::array<float,3>,3>> triangles;triangles.reserve(snapshot.uv_triangles.size());for(const auto &tri:snapshot.uv_triangles)triangles.push_back(tri.positions);
  settings.with_gpu([&]{for(const auto &input:inputs)for(const auto &[tile,points]:input.atlas->tiles){if(settings.enabled[int(MeshMapRole::AmbientOcclusion)])settings.gpu_ao[input.material_uid][tile]=gpu_bake_cosine_occlusion(triangles,points,settings.rays,float(extent));if(settings.enabled[int(MeshMapRole::Thickness)])settings.gpu_thickness[input.material_uid][tile]=gpu_bake_cosine_occlusion(triangles,points,settings.rays,float(extent*.1),true);}});
}

}
MeshBakeJob::MeshBakeJob(SceneSnapshot snapshot,std::vector<MeshBakeInput> inputs,MeshBakeSettings settings)
 :MeshBakeJob(std::make_shared<const SceneSnapshot>(std::move(snapshot)),std::move(inputs),std::move(settings)){}
MeshBakeJob::MeshBakeJob(std::shared_ptr<const SceneSnapshot> source,std::vector<MeshBakeInput> inputs,MeshBakeSettings settings){
  if(!source)throw std::invalid_argument("Missing bake source");const auto &snapshot=*source;
  if(!settings.use_project_mesh&&!settings.external_source)throw std::invalid_argument("Enable the project mesh as bake source");if(!valid_sparse_texture_extent(settings.resolution,32,true)||!valid_sparse_texture_extent(settings.rows(),32,true)||(settings.sampling!=1&&settings.sampling!=2&&settings.sampling!=4&&settings.sampling!=8)||settings.rays<1||settings.rays>512||settings.dilation<0||settings.dilation>64||inputs.empty()||snapshot.uv_triangles.empty())throw std::invalid_argument("Invalid mesh bake settings");
  for(const auto &input:inputs){if(!input.atlas||input.atlas->resolution!=settings.resolution*settings.sampling||(input.atlas->height?input.atlas->height:input.atlas->resolution)!=settings.rows()*settings.sampling)throw std::invalid_argument("Bake input atlas is missing");for(int role=0;role<7;++role)if(settings.enabled[role]){for(const auto &[tile,surface]:input.atlas->tiles){tasks.push_back(std::string(mesh_map_names[role])+" — "+input.name+" / "+std::to_string(tile));}}}
  if(tasks.empty())throw std::length_error("Selected mesh maps are empty");
  // Completed maps live in the disk cache; budget only simultaneous scratch.
  const auto output_bytes=resource_product(resource_product(settings.resolution,settings.rows()),16);
  const auto sampled_bytes=resource_product(output_bytes,resource_product(settings.sampling,settings.sampling));
  const auto working_bytes=std::max(resource_sum(sampled_bytes,output_bytes),resource_product(output_bytes,4));
  require_texture_ram(working_bytes,"Mesh bake working buffers");
  if(displaced(settings)){const auto plan=displacement_bake_plan(*source,inputs,settings,working_bytes);auto mutated=std::make_shared<SceneSnapshot>(*source);apply_bake_displacement(*mutated,inputs,settings,plan.triangles);try{precompute_gpu_rays(*mutated,inputs,settings);}catch(const std::exception &error){std::fprintf(stderr,"BTS_BAKE_GPU_RAYS_FAILED %s\n",error.what());settings.gpu_ao.clear();settings.gpu_thickness.clear();}source=std::move(mutated);}
  worker_=std::jthread([this,source=std::move(source),inputs=std::move(inputs),settings,working_bytes]{
    try{const auto &snapshot=*source;
      std::optional<RayScene> scene;if(settings.enabled[int(MeshMapRole::AmbientOcclusion)]||settings.enabled[int(MeshMapRole::Thickness)])scene.emplace(snapshot,settings);double extent=0,radius_squared=0;std::array<double,3> center;for(int i=0;i<3;++i){const double size=snapshot.maximum[i]-snapshot.minimum[i];extent=std::max(extent,size);radius_squared+=size*size*.25;center[i]=(snapshot.maximum[i]+snapshot.minimum[i])*.5;}extent=std::max(extent,1e-5);const double diameter=std::max(2*std::sqrt(radius_squared),1e-5);int task=0;
      for(const auto &input:inputs){MeshBakeResult output;output.material_uid=input.material_uid;
        for(int role=0;role<7;++role)if(settings.enabled[role]){if(cancelled)throw std::runtime_error("Bake cancelled");MeshMap map;map.name=std::string(mesh_map_names[role])+" "+input.name;
          for(const auto &[tile,surface]:input.atlas->tiles){require_texture_ram(working_bytes,"Mesh bake working buffers");current=task;rows=0;const int resolution=settings.resolution*settings.sampling,height=settings.rows()*settings.sampling;std::vector<float> pixels(std::size_t(resolution)*height*4,0);const auto curvature=input.atlas->curvature.find(tile);float curvature_scale=0.15f;if(curvature!=input.atlas->curvature.end()){std::vector<float> mags;mags.reserve(curvature->second.size()/4);for(float value:curvature->second)if(std::abs(value)>1e-4f)mags.push_back(std::abs(value));if(!mags.empty()){const std::size_t idx=std::min(mags.size()-1,std::size_t(mags.size()*0.97));std::nth_element(mags.begin(),mags.begin()+idx,mags.end());curvature_scale=std::max(0.15f,mags[idx]);}}
            threading::parallel_for(IndexRange(height),4,[&](const IndexRange range){for(int y:range){if(cancelled)return;for(int x=0;x<resolution;++x){const auto index=std::size_t(y)*resolution+x;const auto &point=surface[index];if(!point.valid)continue;float rgb[3]{0,0,0};switch(MeshMapRole(role)){
              case MeshMapRole::Normal:std::copy_n(point.baked_normal.begin(),3,rgb);break;
              case MeshMapRole::WorldNormal:for(int i=0;i<3;++i)rgb[i]=point.normal[i]*.5f+.5f;break;
              case MeshMapRole::Id:if(settings.external_source)std::copy_n(point.source_color.begin(),3,rgb);else std::copy_n(input.source_material.base_color.begin(),3,rgb);break;
              case MeshMapRole::AmbientOcclusion:if(const auto gpu=settings.gpu_ao.find(input.material_uid);gpu!=settings.gpu_ao.end())if(const auto tile_ao=gpu->second.find(tile);tile_ao!=gpu->second.end()&&tile_ao->second.size()==surface.size()){rgb[0]=rgb[1]=rgb[2]=tile_ao->second[index];break;}rgb[0]=rgb[1]=rgb[2]=ray_average(*scene,point,settings.rays,float(extent),float(extent*1e-5),false,std::uint32_t(index));break;
              case MeshMapRole::Curvature:rgb[0]=rgb[1]=rgb[2]=curvature==input.atlas->curvature.end()?.5f:std::clamp(.5f+.5f*curvature->second[index]/curvature_scale,0.f,1.f);break;
              case MeshMapRole::Position:for(int i=0;i<3;++i)rgb[i]=float(std::clamp((point.position[i]-center[i])/diameter+.5,0.,1.));break;
              case MeshMapRole::Thickness:if(const auto gpu=settings.gpu_thickness.find(input.material_uid);gpu!=settings.gpu_thickness.end())if(const auto tile_ao=gpu->second.find(tile);tile_ao!=gpu->second.end()&&tile_ao->second.size()==surface.size()){rgb[0]=rgb[1]=rgb[2]=tile_ao->second[index];break;}rgb[0]=rgb[1]=rgb[2]=ray_average(*scene,point,settings.rays,float(extent*.1),float(extent*1e-5),true,std::uint32_t(index));break;
            }auto *pixel=pixels.data()+index*4;std::copy_n(rgb,3,pixel);pixel[3]=1;}++rows;}});
            if(cancelled)throw std::runtime_error("Bake cancelled");
            if(role==int(MeshMapRole::Thickness)){float low=1,high=0;for(std::size_t i=0;i<surface.size();++i)if(surface[i].valid){low=std::min(low,pixels[i*4]);high=std::max(high,pixels[i*4]);}if(high-low>1e-7f)for(std::size_t i=0;i<surface.size();++i)if(surface[i].valid)for(int c=0;c<3;++c)pixels[i*4+c]=(pixels[i*4+c]-low)/(high-low);}
            pixels=filter_mesh_bake(std::move(pixels),resolution,height,settings.resolution,settings.rows(),settings.dilation,settings.diffusion,role);auto data=std::make_shared<MeshMapPixels>(settings.resolution,std::move(pixels),settings.rows());map.tiles.emplace(tile,MeshMapPlane{settings.resolution,data->hash,std::move(data),settings.rows()});++task;completed=task;
          }output.maps[role]=std::move(map);
        }result.push_back(std::move(output));
      }
    }catch(const std::exception &exception){error=exception.what();result.clear();}finished.store(true,std::memory_order_release);
  });
}
MeshBakeJob::~MeshBakeJob(){cancel();if(worker_.joinable())worker_.join();}
}
