// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LayerCapture.hh"
#include "GPU_shader.hh"
#include "GPU_texture.hh"
#include "GPU_compute.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "gpu_shader_create_info.hh"
#include "GpuReadbackFence.hh"
#include <memory>
#include <optional>
#include <stdexcept>
namespace bts {
class GpuLayerCapture {
  using Shader = std::unique_ptr<blender::gpu::Shader, decltype(&blender::GPU_shader_free)>;
  using Buffer =
      std::unique_ptr<blender::gpu::StorageBuf, decltype(&blender::GPU_storagebuf_free)>;
  Shader shader_{nullptr, blender::GPU_shader_free};
  Buffer buffer_{nullptr, blender::GPU_storagebuf_free};
  GpuReadbackFence fence_;
  std::uint64_t ticket_ = 0;
  int count_ = 0;
  std::shared_ptr<std::vector<std::array<float, 4>>> pending_pixels_;
  std::optional<LayerCaptureResult> result_;

 public:
  struct Input {
    blender::gpu::Texture *image, *owners, *selection;
    int width, height, inclusion = 0;
    std::array<float, 3> completion_base{};
    int completion = 0;
  };

  bool pending() const
  {
    return bool(fence_);
  }
  std::optional<LayerCaptureResult> get() const
  {
    return result_;
  }
  void update(const LayerCaptureRequest &r, const Input &in, std::uint64_t ticket)
  {
    using namespace blender;
    if (pending())
      throw std::logic_error("Layer capture packet still in flight");
    if (r.width <= 0 || r.height <= 0 || r.width > 256 || r.height > 256 || r.canvas_width <= 0 ||
        r.canvas_height <= 0 || r.x < 0 || r.y < 0 || r.x + r.width > r.canvas_width ||
        r.y + r.height > r.canvas_height)
      throw std::invalid_argument("Layer capture chunk exceeds 256 squared texels");
    if (!shader_) {
      using namespace gpu::shader;
      ShaderCreateInfo info("pyGPU_Shader");
      info.local_group_size(8, 8)
          .sampler(0, ImageType::Float2D, "source_img")
          .sampler(1, ImageType::Uint2D, "owners_img")
          .sampler(2, ImageType::Uint2D, "selection_img")
          .storage_buf(0, Qualifier::write, "vec4", "values[]")
          .push_constant(Type::int2_t, "source_size")
          .push_constant(Type::int2_t, "canvas_size")
          .push_constant(Type::int2_t, "offset")
          .push_constant(Type::int2_t, "size")
          .push_constant(Type::int_t, "mask_mode")
          .push_constant(Type::int_t, "premultiplied")
          .push_constant(Type::int_t, "normal_mode")
          .push_constant(Type::int_t, "inclusion")
          .push_constant(Type::float3_t, "completion_base")
          .push_constant(Type::int_t, "completion_mode");
      info.compute_source_generated = R"GLSL(
 bool included(ivec2 p){if(inclusion==0)return true;if(inclusion==1)return false;uint owner=texelFetch(owners_img,p,0).r;if(owner==0u)return false;int low=0,high=textureSize(selection_img,0).x;while(low<high){int mid=(low+high)/2;uvec2 row=texelFetch(selection_img,ivec2(mid,0),0).rg;if(row.x<owner)low=mid+1;else high=mid;}if(low>=textureSize(selection_img,0).x)return false;uvec2 row=texelFetch(selection_img,ivec2(low,0),0).rg;return row.x==owner&&row.y!=0u;}
 void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,size)))return;ivec2 q=(p+offset)*source_size/canvas_size;
  vec4 value=texelFetch(source_img,min(q,textureSize(source_img,0)-1),0);
  if(mask_mode!=0)value=vec4(value.r,0,0,1);
  else {if(premultiplied!=0){value.rgb=value.a>0?value.rgb/value.a:vec3(0);if(normal_mode!=0)value.rgb=value.rgb*.5+.5;}if(!included(q))value=vec4(0);}
  if(completion_mode==1)value.rgb+=completion_base*(1-value.a);
  else if(completion_mode==2)value.rgb=value.a>0?value.rgb/value.a:completion_base;
  values[p.y*size.x+p.x]=value;
 })GLSL";
      shader_ = Shader(
          GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free);
      if (!shader_)
        throw std::runtime_error("Layer capture shader failed");
    }
    if (!buffer_)
      buffer_ = Buffer(
          GPU_storagebuf_create_ex(
              256 * 256 * 4 * sizeof(float), nullptr, GPU_USAGE_DYNAMIC, "BTS bounded layer bake"),
          GPU_storagebuf_free);
    if (!buffer_)
      throw std::bad_alloc();
    GPU_shader_bind(shader_.get());
    GPU_storagebuf_bind(buffer_.get(), 0);
    GPU_texture_bind(in.image, 0);
    GPU_texture_bind(in.owners, 1);
    GPU_texture_bind(in.selection, 2);
    const int source[2] = {in.width, in.height}, canvas[2] = {r.canvas_width, r.canvas_height},
              offset[2] = {r.x, r.y}, size[2] = {r.width, r.height};
    GPU_shader_uniform_2iv(shader_.get(), "source_size", source);
    GPU_shader_uniform_2iv(shader_.get(), "canvas_size", canvas);
    GPU_shader_uniform_2iv(shader_.get(), "offset", offset);
    GPU_shader_uniform_2iv(shader_.get(), "size", size);
    GPU_shader_uniform_1i(shader_.get(), "mask_mode", r.mask);
    GPU_shader_uniform_1i(shader_.get(), "premultiplied", r.contextual);
    GPU_shader_uniform_1i(shader_.get(), "normal_mode", r.channel == 3);
    GPU_shader_uniform_1i(shader_.get(), "inclusion", in.inclusion);
    GPU_shader_uniform_3fv(shader_.get(), "completion_base", in.completion_base.data());
    GPU_shader_uniform_1i(shader_.get(), "completion_mode", in.completion);
    GPU_compute_dispatch(shader_.get(), (r.width + 7) / 8, (r.height + 7) / 8, 1);
    GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);
    GPU_storagebuf_unbind(buffer_.get());
    GPU_texture_unbind_all();
    GPU_shader_unbind();
    fence_.submit(buffer_.get());
    ticket_ = ticket;
    count_ = r.width * r.height;
  }
  void poll()
  {
    using namespace blender;
    if (!fence_)
      return;
    if (!pending_pixels_)
      pending_pixels_ = std::make_shared<std::vector<std::array<float, 4>>>(256 * 256);
    if (!fence_.read(buffer_.get(), pending_pixels_->data()))
      return;
    pending_pixels_->resize(count_);
    result_ = LayerCaptureResult{ticket_, true, std::move(pending_pixels_)};
  }
};
}  // namespace bts
