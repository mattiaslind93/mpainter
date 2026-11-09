#pragma once
#include "GpuBakeBvh.hh"
#include "DisplacementBake.hh"
#include "FillProjection.hh"
#include "GPU_context.hh"
#include "GPU_capabilities.hh"
#include "GPU_compute.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "gpu_shader_create_info.hh"
#ifndef __APPLE__
# include <epoxy/gl.h>
#endif
#include <memory>
#include <bit>
#include <map>
#include <set>
#include <span>
#include <vector>
namespace bts {
struct GpuBakeRayFilter {
 std::span<const std::uint32_t> objects;
 const std::map<std::uint32_t,std::set<std::uint32_t>> &matches;
 const std::set<std::uint32_t> &ignore_backfaces;
};
struct GpuBakeRayStats {std::uint64_t batches=0,dispatches=0,peak_batch_rays=0;};
// Geometry is uploaded once. Each reusable batch preserves the previous CPU
// cosine samples and accumulation order, while the GPU traverses a balanced BVH.
inline std::vector<float> gpu_bake_cosine_occlusion(const std::vector<bake_bvh::Triangle> &triangles,std::span<const FillSurfacePoint> points,int rays,float maximum,bool inward=false,GpuBakeRayStats *statistics=nullptr,const ResourceAvailability *availability=nullptr,float origin_epsilon=0,bool visibility=false,std::span<const std::array<float,3>> geometric={},const GpuBakeRayFilter *filter=nullptr){
 using namespace blender;
 if(!GPU_context_active_get())throw std::logic_error("Bake rays require the active GPU context");
 if(triangles.empty()||points.empty()||rays<1||rays>512||!std::isfinite(maximum)||maximum<=0)throw std::invalid_argument("Invalid GPU bake rays");
 if(GPU_max_compute_shader_storage_blocks()<6)throw std::length_error("GPU bake needs six compute SSBO bindings; driver provides fewer");
 if(filter&&filter->objects.size()!=triangles.size())throw std::invalid_argument("GPU bake object IDs must match triangles");
 if(!geometric.empty()&&geometric.size()!=points.size())throw std::invalid_argument("GPU bake geometric normals must match points");
 if(statistics)*statistics={};
 const auto plan=bake_bvh::plan(triangles.size(),points.size(),rays,GPU_max_storage_buffer_size(),GPU_max_work_group_count(0),availability?*availability:ResourceAvailability{});
 auto nodes=bake_bvh::build(triangles);
 std::vector<std::array<float,4>> tris(std::size_t(resource_product(triangles.size(),3)));
 for(std::size_t i=0;i<triangles.size();++i)for(int k=0;k<3;++k)tris[i*3+k]={triangles[i][k][0],triangles[i][k][1],triangles[i][k][2],0};
 // Pack IDs into unused vec4 lanes. Offset zero means unrestricted; an
 // explicit empty match set still rejects every object, exactly like RayScene.
 std::map<std::uint32_t,std::uint32_t> match_offsets;
 if(filter){
  for(std::size_t i=0;i<triangles.size();++i){tris[i*3][3]=std::bit_cast<float>(filter->objects[i]);tris[i*3+1][3]=filter->ignore_backfaces.contains(filter->objects[i])?1.f:0.f;}
  for(const auto &[object,allowed]:filter->matches){
   if(tris.size()+allowed.size()+1>std::numeric_limits<std::uint32_t>::max())throw std::length_error("GPU bake match table too large");
   match_offsets[object]=std::uint32_t(tris.size());tris.push_back({std::bit_cast<float>(std::uint32_t(allowed.size())),0,0,0});
   for(auto id:allowed)tris.push_back({std::bit_cast<float>(id),0,0,0});
  }
 }
 if(tris.size()>GPU_max_storage_buffer_size()/sizeof(tris[0]))throw std::length_error("GPU bake triangle/filter buffer exceeds driver limit");
 gpu::shader::ShaderCreateInfo info("pyGPU_Shader");
 // Match the 32-byte CPU Node: Metal float3 occupies 16 bytes, packed_float3
 // occupies 12. Blender aliases packed_float3 to float3/vec3 on GLSL backends.
 // BakeRayState is uint+float (8 bytes). PointVertex is vec4/uvec4 only.
 info.typedef_source_generated="struct BakeNode {packed_float3 lower;uint escape;packed_float3 upper;uint triangle;};\nstruct BakeRayState {uint index;float distance;};\n";
 info.local_group_size(64,1).storage_buf(0,gpu::shader::Qualifier::read,"BakeNode","nodes[]").storage_buf(1,gpu::shader::Qualifier::read,"vec4","tris[]").storage_buf(2,gpu::shader::Qualifier::read,"vec4","rays[]").storage_buf(3,gpu::shader::Qualifier::write,"float","hits[]").storage_buf(4,gpu::shader::Qualifier::read_write,"BakeRayState","states[]").storage_buf(5,gpu::shader::Qualifier::read_write,"uint","unfinished[]").push_constant(gpu::shader::Type::int_t,"node_count").push_constant(gpu::shader::Type::int_t,"ray_count").push_constant(gpu::shader::Type::int_t,"reset_rays");
 info.compute_source_generated=filter?(GPU_backend_get_type()==GPU_BACKEND_METAL?R"GLSL(
#define BAKE_PRECISE precise
#define BAKE_INVERSE(x) (1.0/(x))
float bake_dot(vec3 a,vec3 b){precise float value=(a.x*b.x+a.y*b.y)+a.z*b.z;return value;}
vec3 bake_cross(vec3 a,vec3 b){precise vec3 value=vec3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);return value;}
)GLSL":R"GLSL(
#define BAKE_PRECISE precise
#define BAKE_INVERSE(x) float(double(1.0)/double(x))
float bake_dot(vec3 a,vec3 b){precise float value=(a.x*b.x+a.y*b.y)+a.z*b.z;return value;}
vec3 bake_cross(vec3 a,vec3 b){precise vec3 value=vec3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);return value;}
)GLSL"):"#define BAKE_PRECISE\n#define BAKE_INVERSE(x) (1.0/(x))\n#define bake_dot dot\n#define bake_cross cross\n";
 info.compute_source_generated+=R"GLSL(
bool bounds_hit(BakeNode node,vec3 origin,vec3 direction,float distance){
 float lower=0.0,upper=distance;
 for(int axis=0;axis<3;++axis){
  if(direction[axis]==0.0){if(origin[axis]<node.lower[axis]||origin[axis]>node.upper[axis])return false;}
  else {float a=(node.lower[axis]-origin[axis])/direction[axis],b=(node.upper[axis]-origin[axis])/direction[axis];lower=max(lower,min(a,b));upper=min(upper,max(a,b));if(upper<lower)return false;}
 }
 return true;
}
void triangle_hit(uint triangle,vec3 origin,vec3 direction,uint match_offset,inout float distance){
 uint i=triangle*3u;
 if(match_offset!=0u){
  uint object=floatBitsToUint(tris[i].w),count=floatBitsToUint(tris[match_offset].x);bool allowed=false;
  for(uint j=0u;j<count;++j)if(floatBitsToUint(tris[match_offset+1u+j].x)==object){allowed=true;break;}
  if(!allowed)return;
 }
 BAKE_PRECISE vec3 a=tris[i].xyz,e1=tris[i+1u].xyz-a,e2=tris[i+2u].xyz-a,p=bake_cross(direction,e2);
 if(tris[i+1u].w!=0.0&&bake_dot(bake_cross(e1,e2),direction)>=0.0)return;
 BAKE_PRECISE float det=bake_dot(e1,p);if(det==0.0||abs(det)<1e-8*sqrt(bake_dot(e1,e1)*bake_dot(e2,e2)))return;BAKE_PRECISE float inv=BAKE_INVERSE(det),u=bake_dot(origin-a,p)*inv;if(u<0.0||u>1.0)return;
 BAKE_PRECISE vec3 q=bake_cross(origin-a,e1);BAKE_PRECISE float v=bake_dot(direction,q)*inv;if(v<0.0||u+v>1.0)return;BAKE_PRECISE float t=bake_dot(e2,q)*inv;if(t>0.0&&t<distance)distance=t;
}
void main(){
 uint id=gl_GlobalInvocationID.x;if(id>=uint(ray_count))return;
 vec4 o=rays[id*2u],d=rays[id*2u+1u];float distance=reset_rays!=0?o.w:states[id].distance;uint index=reset_rays!=0?0u:states[id].index;
 // Dispatch granularity only: remaining traversal resumes without a work cap.
 for(int visited=0;visited<256&&index<uint(node_count);++visited){
  BakeNode node=nodes[index];
  if(!bounds_hit(node,o.xyz,d.xyz,distance)){index=node.escape;continue;}
  if(node.triangle!=0xffffffffu){triangle_hit(node.triangle,o.xyz,d.xyz,floatBitsToUint(d.w),distance);index=node.escape;}
  else ++index;
 }
 states[id].index=index;states[id].distance=distance;
 if(index<uint(node_count))atomicAdd(unfinished[0],1u);
 else hits[id]=distance/o.w;
}
)GLSL";
 using Shader=std::unique_ptr<gpu::Shader,decltype(&GPU_shader_free)>;
 using Buffer=std::unique_ptr<gpu::StorageBuf,decltype(&GPU_storagebuf_free)>;
 Shader shader(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free);
 if(!shader)throw std::runtime_error("Bake BVH shader failed");
 const auto capacity=std::size_t(resource_product(plan.batch_points,std::uint64_t(rays)));
 std::vector<std::array<float,4>> ray_data(capacity*2);
 std::vector<float> hits(capacity),occlusion(points.size(),1.f);
 std::vector<std::size_t> owners;owners.reserve(plan.batch_points);
 Buffer node_buf(GPU_storagebuf_create_ex(nodes.size()*sizeof(bake_bvh::Node),nodes.data(),GPU_USAGE_STATIC,"BTS bake BVH"),GPU_storagebuf_free);
 Buffer tri_buf(GPU_storagebuf_create_ex(tris.size()*sizeof(tris[0]),tris.data(),GPU_USAGE_STATIC,"BTS bake triangles"),GPU_storagebuf_free);
 Buffer ray_buf(GPU_storagebuf_create_ex(ray_data.size()*sizeof(ray_data[0]),nullptr,GPU_USAGE_DYNAMIC,"BTS reusable bake rays"),GPU_storagebuf_free);
 Buffer hit_buf(GPU_storagebuf_create_ex(hits.size()*sizeof(float),nullptr,GPU_USAGE_DYNAMIC,"BTS reusable bake hits"),GPU_storagebuf_free);
 Buffer state_buf(GPU_storagebuf_create_ex(capacity*(sizeof(std::uint32_t)+sizeof(float)),nullptr,GPU_USAGE_DYNAMIC,"BTS reusable bake continuation"),GPU_storagebuf_free);
 Buffer counter_buf(GPU_storagebuf_create_ex(16,nullptr,GPU_USAGE_DYNAMIC,"BTS bake unfinished counter"),GPU_storagebuf_free);
 if(!node_buf||!tri_buf||!ray_buf||!hit_buf||!state_buf||!counter_buf)throw std::bad_alloc();
 std::size_t point=0;
 while(point<points.size()){
  owners.clear();
  while(point<points.size()&&owners.size()<plan.batch_points){
   const auto index=point++;const auto &value=points[index];bake_bvh::Frame frame;
   if(!value.valid||!bake_bvh::frame(frame,value.position,value.normal,std::uint32_t(index),maximum,inward))continue;
   const auto offset=owners.size()*std::size_t(rays);
   for(int sample=0;sample<rays;++sample){auto &origin=ray_data[(offset+sample)*2];auto &direction=ray_data[(offset+sample)*2+1];if(!geometric.empty())bake_bvh::surface_ray(frame,sample,rays,geometric[index],inward,origin_epsilon,origin,direction);else{bake_bvh::ray(frame,sample,rays,origin,direction);if(origin_epsilon>0)for(int axis=0;axis<3;++axis)origin[axis]=frame.position[axis]+frame.n[axis]*origin_epsilon;}}
   const auto match=match_offsets.find(value.match_object);
   const float packed=std::bit_cast<float>(match==match_offsets.end()?0u:match->second);
   for(int sample=0;sample<rays;++sample)ray_data[(offset+sample)*2+1][3]=packed;
   owners.push_back(index);
  }
  if(owners.empty())continue;
  GPU_storagebuf_update(ray_buf.get(),ray_data.data());
  const int count=int(owners.size()*std::size_t(rays));
  if(statistics){++statistics->batches;statistics->peak_batch_rays=std::max(statistics->peak_batch_rays,std::uint64_t(count));}
  bool reset=true;std::array<std::uint32_t,4> unfinished{};
  do{
   unfinished.fill(0);GPU_storagebuf_update(counter_buf.get(),unfinished.data());
   GPU_shader_bind(shader.get());GPU_storagebuf_bind(node_buf.get(),0);GPU_storagebuf_bind(tri_buf.get(),1);GPU_storagebuf_bind(ray_buf.get(),2);GPU_storagebuf_bind(hit_buf.get(),3);GPU_storagebuf_bind(state_buf.get(),4);GPU_storagebuf_bind(counter_buf.get(),5);
   GPU_shader_uniform_1i(shader.get(),"node_count",int(nodes.size()));GPU_shader_uniform_1i(shader.get(),"ray_count",count);GPU_shader_uniform_1i(shader.get(),"reset_rays",reset);
   GPU_compute_dispatch(shader.get(),unsigned((std::uint64_t(count)+63)/64),1,1);GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);
   for(auto *buffer:{node_buf.get(),tri_buf.get(),ray_buf.get(),hit_buf.get(),state_buf.get(),counter_buf.get()})GPU_storagebuf_unbind(buffer);GPU_shader_unbind();GPU_storagebuf_sync_to_host(counter_buf.get());
#ifdef __APPLE__
   // Metal owns its command buffers; a GL fence cannot synchronize this backend.
   // This synchronous bake already waits for every traversal slice on Linux.
   GPU_finish();
#else
   GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)throw std::runtime_error("Bake BVH fence failed");
   const auto state=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,10'000'000'000ULL);glDeleteSync(fence);
   if(state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED)throw std::runtime_error("Bake BVH traversal slice fence timed out");
#endif
   GPU_storagebuf_read(counter_buf.get(),unfinished.data());reset=false;if(statistics)++statistics->dispatches;
  }while(unfinished[0]);
  GPU_storagebuf_sync_to_host(hit_buf.get());
  GPU_storagebuf_read(hit_buf.get(),hits.data());
  for(std::size_t owner=0;owner<owners.size();++owner){float sum=0;for(int sample=0;sample<rays;++sample){const float value=hits[owner*std::size_t(rays)+sample];sum+=visibility?(value>=1?1.f:0.f):value;}occlusion[owners[owner]]=sum/rays;}
 }
 return occlusion;
}
}
