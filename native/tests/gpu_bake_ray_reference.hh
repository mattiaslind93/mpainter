// Test-only reference retained from the pre-BVH GPU implementation.
#pragma once
#include "DisplacementBake.hh"
#include "FillProjection.hh"
#include "GPU_context.hh"
#include "GPU_compute.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "gpu_shader_create_info.hh"
#include <epoxy/gl.h>
#include <cstring>
#include <vector>
namespace bts {
// Brute-force cosine rays on the GPU. The CPU baker keeps its BVH when this dispatch is declined.
inline std::vector<float> legacy_gpu_bake_cosine_occlusion(const std::vector<std::array<std::array<float,3>,3>> &triangles,const std::vector<FillSurfacePoint> &points,int rays,float maximum,bool inward=false){
  using namespace blender;if(!GPU_context_active_get())throw std::logic_error("Bake rays require the active GPU context");
  if(triangles.empty()||points.empty()||rays<1||rays>512||maximum<=0)throw std::invalid_argument("Invalid GPU bake rays");
  const std::uint64_t tests=std::uint64_t(triangles.size())*points.size()*rays;if(tests>8000000ULL)throw std::length_error("GPU bake ray count exceeds the brute-force budget");
  std::vector<std::array<float,4>> tris(triangles.size()*3),ray_data;ray_data.reserve(points.size()*std::size_t(rays)*2);
  for(std::size_t i=0;i<triangles.size();++i)for(int k=0;k<3;++k){tris[i*3+k]={triangles[i][k][0],triangles[i][k][1],triangles[i][k][2],0};}
  std::vector<int> owners;owners.reserve(points.size()*rays);
  for(std::size_t index=0;index<points.size();++index){const auto &point=points[index];if(!point.valid)continue;std::array<float,3> n=point.normal;if(inward)for(float &v:n)v=-v;float length=0;for(float v:n)length+=v*v;length=std::sqrt(length);if(length<1e-8f)continue;for(float &v:n)v/=length;
    const float helper[3]={std::abs(n[2])>.9f?1.f:0.f,0,std::abs(n[2])>.9f?0.f:1.f};std::array<float,3> tangent{helper[1]*n[2]-helper[2]*n[1],helper[2]*n[0]-helper[0]*n[2],helper[0]*n[1]-helper[1]*n[0]};float tlen=0;for(float v:tangent)tlen+=v*v;tlen=std::sqrt(std::max(tlen,1e-12f));for(float &v:tangent)v/=tlen;
    const std::array<float,3> bitangent{n[1]*tangent[2]-n[2]*tangent[1],n[2]*tangent[0]-n[0]*tangent[2],n[0]*tangent[1]-n[1]*tangent[0]};
    const auto seed=std::uint32_t(index);const double phase=(seed*0.6180339887498949-std::floor(seed*0.6180339887498949))*6.283185307179586;
    for(int i=0;i<rays;++i){const double z=std::sqrt((i+.5)/rays),radius=std::sqrt(1-z*z),angle=i*2.399963229728653+phase;std::array<float,4> origin,direction;for(int a=0;a<3;++a){direction[a]=float(n[a]*z+tangent[a]*radius*std::cos(angle)+bitangent[a]*radius*std::sin(angle));origin[a]=point.position[a]+n[a]*float(maximum*1e-5f);}origin[3]=maximum;direction[3]=0;ray_data.push_back(origin);ray_data.push_back(direction);owners.push_back(int(index));}
  }
  if(owners.empty())return std::vector<float>(points.size(),1.f);
  gpu::shader::ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(64,1).storage_buf(0,gpu::shader::Qualifier::read,"vec4","tris[]").storage_buf(1,gpu::shader::Qualifier::read,"vec4","rays[]").storage_buf(2,gpu::shader::Qualifier::write,"float","hits[]").push_constant(gpu::shader::Type::int_t,"tri_count").push_constant(gpu::shader::Type::int_t,"ray_count");
  info.compute_source_generated=R"GLSL(
bool nearest(vec3 origin,vec3 direction,float maximum,inout float distance){for(int i=0;i<tri_count;++i){vec3 a=tris[i*3].xyz,e1=tris[i*3+1].xyz-a,e2=tris[i*3+2].xyz-a,p=cross(direction,e2);float det=dot(e1,p);if(abs(det)<1e-8)continue;float inv=1.0/det,u=dot(origin-a,p)*inv;if(u<0.0||u>1.0)continue;vec3 q=cross(origin-a,e1);float v=dot(direction,q)*inv;if(v<0.0||u+v>1.0)continue;float t=dot(e2,q)*inv;if(t>1e-7&&t<distance)distance=t;}return distance<maximum;}
void main(){uint id=gl_GlobalInvocationID.x;if(id>=uint(ray_count))return;vec4 o=rays[id*2u],d=rays[id*2u+1u];float distance=o.w;nearest(o.xyz,d.xyz,o.w,distance);hits[id]=distance/o.w;}
)GLSL";
  std::unique_ptr<gpu::Shader,decltype(&GPU_shader_free)> shader(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free);if(!shader)throw std::runtime_error("Bake ray shader failed");
  std::unique_ptr<gpu::StorageBuf,decltype(&GPU_storagebuf_free)> tri_buf(GPU_storagebuf_create_ex(int(tris.size()*sizeof(std::array<float,4>)),tris.data(),GPU_USAGE_STATIC,"BTS bake triangles"),GPU_storagebuf_free);
  std::unique_ptr<gpu::StorageBuf,decltype(&GPU_storagebuf_free)> ray_buf(GPU_storagebuf_create_ex(int(ray_data.size()*sizeof(std::array<float,4>)),ray_data.data(),GPU_USAGE_STATIC,"BTS bake rays"),GPU_storagebuf_free);
  std::unique_ptr<gpu::StorageBuf,decltype(&GPU_storagebuf_free)> hit_buf(GPU_storagebuf_create_ex(int(owners.size()*sizeof(float)),nullptr,GPU_USAGE_DYNAMIC,"BTS bake hits"),GPU_storagebuf_free);
  if(!tri_buf||!ray_buf||!hit_buf)throw std::bad_alloc();
  GPU_shader_bind(shader.get());GPU_storagebuf_bind(tri_buf.get(),0);GPU_storagebuf_bind(ray_buf.get(),1);GPU_storagebuf_bind(hit_buf.get(),2);
  const int tri_count=int(triangles.size()),ray_count=int(owners.size());GPU_shader_uniform_1i(shader.get(),"tri_count",tri_count);GPU_shader_uniform_1i(shader.get(),"ray_count",ray_count);
  GPU_compute_dispatch(shader.get(),(ray_count+63)/64,1,1);GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);GPU_storagebuf_unbind(tri_buf.get());GPU_storagebuf_unbind(ray_buf.get());GPU_storagebuf_unbind(hit_buf.get());GPU_shader_unbind();GPU_storagebuf_sync_to_host(hit_buf.get());
  GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)throw std::runtime_error("Bake ray fence failed");const auto state=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,10'000'000'000ULL);glDeleteSync(fence);if(state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED)throw std::runtime_error("Bake ray fence timed out");
  std::vector<float> hits(owners.size());GPU_storagebuf_read(hit_buf.get(),hits.data());std::vector<float> occlusion(points.size(),1.f);std::vector<int> counts(points.size());std::vector<float> sum(points.size());for(std::size_t i=0;i<owners.size();++i){sum[owners[i]]+=hits[i];++counts[owners[i]];}
  for(std::size_t i=0;i<occlusion.size();++i)if(counts[i])occlusion[i]=sum[i]/counts[i];return occlusion;
}
}
