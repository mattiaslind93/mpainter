#pragma once
#include "ThumbnailMetadata.hh"
#include "GPU_shader.hh"
#include "GPU_texture.hh"
#include "GPU_compute.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "gpu_shader_create_info.hh"
#include "GpuReadbackFence.hh"
#include <memory>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>
namespace bts {
class GpuThumbnail {
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 using Buffer=std::unique_ptr<blender::gpu::StorageBuf,decltype(&blender::GPU_storagebuf_free)>;
 Shader shader_{nullptr,blender::GPU_shader_free};Buffer buffer_{nullptr,blender::GPU_storagebuf_free};
 GpuReadbackFence fence_;std::uint64_t ticket_=0;std::optional<ThumbnailMetadata> result_;
 public:
 struct Input {blender::gpu::Texture *image=nullptr;int width=0,height=0;};

 bool pending()const{return bool(fence_);}
 std::optional<ThumbnailMetadata> get()const{return result_;}
 void update(const ThumbnailRequest &request,const std::vector<Input> &inputs,std::uint64_t ticket){
  using namespace blender;if(pending())throw std::logic_error("Thumbnail packet still in flight");
  if(!shader_){using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");
   info.local_group_size(8,8).sampler(0,ImageType::Float2D,"source_img").storage_buf(0,Qualifier::write,"vec4","values[]")
    .push_constant(Type::int2_t,"source_size").push_constant(Type::int_t,"columns").push_constant(Type::int_t,"tile_index").push_constant(Type::int_t,"mask_mode");
   info.compute_source_generated=R"GLSL(
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,ivec2(48))))return;
 int tile=(p.y*columns/48)*columns+p.x*columns/48;if(tile!=tile_index)return;
 vec2 uv=fract((vec2(p)+.5)*float(columns)/48.0);uv.y=1-uv.y;
 ivec2 q=clamp(ivec2(uv*vec2(source_size)),ivec2(0),source_size-1);
 vec4 value=texelFetch(source_img,min(q,textureSize(source_img,0)-1),0);
 values[p.y*48+p.x]=mask_mode!=0?vec4(vec3(value.r),1):value;
})GLSL";
   shader_=Shader(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free);if(!shader_)throw std::runtime_error("Thumbnail shader failed");
  }
  if(!buffer_)buffer_=Buffer(GPU_storagebuf_create_ex(sizeof(ThumbnailMetadata::pixels),nullptr,GPU_USAGE_DYNAMIC,"BTS layer thumbnail"),GPU_storagebuf_free);if(!buffer_)throw std::bad_alloc();
  ThumbnailMetadata clear;GPU_storagebuf_update(buffer_.get(),clear.pixels.data());GPU_shader_bind(shader_.get());GPU_storagebuf_bind(buffer_.get(),0);
  const int columns=std::max(1,int(std::ceil(std::sqrt(inputs.size()))));GPU_shader_uniform_1i(shader_.get(),"columns",columns);GPU_shader_uniform_1i(shader_.get(),"mask_mode",request.mask);
  for(int i=0;i<int(inputs.size());++i){const auto &in=inputs[i];if(!in.image)continue;GPU_texture_bind(in.image,0);const int size[2]={in.width,in.height};GPU_shader_uniform_2iv(shader_.get(),"source_size",size);GPU_shader_uniform_1i(shader_.get(),"tile_index",i);GPU_compute_dispatch(shader_.get(),6,6,1);}
  GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);GPU_storagebuf_unbind(buffer_.get());GPU_texture_unbind_all();GPU_shader_unbind();fence_.submit(buffer_.get());ticket_=ticket;
 }
 void poll(){using namespace blender;if(!fence_)return;ThumbnailMetadata result;result.ticket=ticket_;if(!fence_.read(buffer_.get(),result.pixels.data()))return;result_=std::move(result);}
};
}
