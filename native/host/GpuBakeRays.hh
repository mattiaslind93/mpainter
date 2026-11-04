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
#include <epoxy/gl.h>
#include <memory>
#include <vector>
namespace bts {
struct GpuBakeRayStats {std::uint64_t batches=0,dispatches=0,peak_batch_rays=0;};
// Geometry is uploaded once. Each reusable batch preserves the previous CPU
// cosine samples and accumulation order, while the GPU traverses a balanced BVH.
inline std::vector<float> gpu_bake_cosine_occlusion(const std::vector<bake_bvh::Triangle> &triangles,const std::vector<FillSurfacePoint> &points,int rays,float maximum,bool inward=false,GpuBakeRayStats *statistics=nullptr){
 using namespace blender;
 if(!GPU_context_active_get())throw std::logic_error("Bake rays require the active GPU context");
 if(triangles.empty()||points.empty()||rays<1||rays>512||!std::isfinite(maximum)||maximum<=0)throw std::invalid_argument("Invalid GPU bake rays");
 if(GPU_max_compute_shader_storage_blocks()<6)throw std::length_error("GPU bake needs six compute SSBO bindings; driver provides fewer");
 if(statistics)*statistics={};
 const auto plan=bake_bvh::plan(triangles.size(),points.size(),rays,GPU_max_storage_buffer_size(),GPU_max_work_group_count(0),ResourceBudget{});
 auto nodes=bake_bvh::build(triangles);
 std::vector<std::array<float,4>> tris(std::size_t(resource_product(triangles.size(),3)));
 for(std::size_t i=0;i<triangles.size();++i)for(int k=0;k<3;++k)tris[i*3+k]={triangles[i][k][0],triangles[i][k][1],triangles[i][k][2],0};
 gpu::shader::ShaderCreateInfo info("pyGPU_Shader");
 info.typedef_source_generated="struct BakeNode {vec3 lower;uint escape;vec3 upper;uint triangle;};\nstruct BakeRayState {uint index;float distance;};\n";
 info.local_group_size(64,1).storage_buf(0,gpu::shader::Qualifier::read,"BakeNode","nodes[]").storage_buf(1,gpu::shader::Qualifier::read,"vec4","tris[]").storage_buf(2,gpu::shader::Qualifier::read,"vec4","rays[]").storage_buf(3,gpu::shader::Qualifier::write,"float","hits[]").storage_buf(4,gpu::shader::Qualifier::read_write,"BakeRayState","states[]").storage_buf(5,gpu::shader::Qualifier::read_write,"uint","unfinished[]").push_constant(gpu::shader::Type::int_t,"node_count").push_constant(gpu::shader::Type::int_t,"ray_count").push_constant(gpu::shader::Type::int_t,"reset_rays");
 info.compute_source_generated=R"GLSL(
bool bounds_hit(BakeNode node,vec3 origin,vec3 direction,float distance){
 float lower=0.0,upper=distance;
 for(int axis=0;axis<3;++axis){
  if(direction[axis]==0.0){if(origin[axis]<node.lower[axis]||origin[axis]>node.upper[axis])return false;}
  else {float a=(node.lower[axis]-origin[axis])/direction[axis],b=(node.upper[axis]-origin[axis])/direction[axis];lower=max(lower,min(a,b));upper=min(upper,max(a,b));if(upper<lower)return false;}
 }
 return true;
}
void triangle_hit(uint triangle,vec3 origin,vec3 direction,inout float distance){
 uint i=triangle*3u;
 vec3 a=tris[i].xyz,e1=tris[i+1u].xyz-a,e2=tris[i+2u].xyz-a,p=cross(direction,e2);
 float det=dot(e1,p);if(abs(det)<1e-8)return;float inv=1.0/det,u=dot(origin-a,p)*inv;if(u<0.0||u>1.0)return;
 vec3 q=cross(origin-a,e1);float v=dot(direction,q)*inv;if(v<0.0||u+v>1.0)return;float t=dot(e2,q)*inv;if(t>1e-7&&t<distance)distance=t;
}
void main(){
 uint id=gl_GlobalInvocationID.x;if(id>=uint(ray_count))return;
 vec4 o=rays[id*2u],d=rays[id*2u+1u];float distance=reset_rays!=0?o.w:states[id].distance;uint index=reset_rays!=0?0u:states[id].index;
 // Dispatch granularity only: remaining traversal resumes without a work cap.
 for(int visited=0;visited<256&&index<uint(node_count);++visited){
  BakeNode node=nodes[index];
  if(!bounds_hit(node,o.xyz,d.xyz,distance)){index=node.escape;continue;}
  if(node.triangle!=0xffffffffu){triangle_hit(node.triangle,o.xyz,d.xyz,distance);index=node.escape;}
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
   for(int sample=0;sample<rays;++sample)bake_bvh::ray(frame,sample,rays,ray_data[(offset+sample)*2],ray_data[(offset+sample)*2+1]);
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
   GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)throw std::runtime_error("Bake BVH fence failed");
   const auto state=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,10'000'000'000ULL);glDeleteSync(fence);
   if(state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED)throw std::runtime_error("Bake BVH traversal slice fence timed out");
   GPU_storagebuf_read(counter_buf.get(),unfinished.data());reset=false;if(statistics)++statistics->dispatches;
  }while(unfinished[0]);
  GPU_storagebuf_sync_to_host(hit_buf.get());
  GPU_storagebuf_read(hit_buf.get(),hits.data());
  for(std::size_t owner=0;owner<owners.size();++owner){float sum=0;for(int sample=0;sample<rays;++sample)sum+=hits[owner*std::size_t(rays)+sample];occlusion[owners[owner]]=sum/rays;}
 }
 return occlusion;
}
}
