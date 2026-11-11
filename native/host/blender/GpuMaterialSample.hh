// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialSample.hh"
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
// One tiny fenced packet, never a synchronous texture readback. The caller
// coalesces requests while this packet is in flight.
class GpuMaterialSample {
  using Shader = std::unique_ptr<blender::gpu::Shader, decltype(&blender::GPU_shader_free)>;
  using Buffer =
      std::unique_ptr<blender::gpu::StorageBuf, decltype(&blender::GPU_storagebuf_free)>;
  Shader shader_{nullptr, blender::GPU_shader_free};
  Buffer buffer_{nullptr, blender::GPU_storagebuf_free};
  GpuReadbackFence fence_;
  std::uint64_t ticket_ = 0;
  bool mask_ = false;
  std::optional<MaterialSample> result_;

 public:
  struct Input {
    blender::gpu::Texture *image = nullptr;
    std::array<float, 3> base{};
  };

  bool pending() const
  {
    return bool(fence_);
  }
  std::optional<MaterialSample> get() const
  {
    return result_;
  }
  void update(const MaterialSampleRequest &request,
              const std::array<Input, 6> &inputs,
              std::uint64_t ticket)
  {
    using namespace blender;
    if (pending())
      throw std::logic_error("Material sample packet still in flight");
    if (!shader_) {
      using namespace gpu::shader;
      ShaderCreateInfo info("pyGPU_Shader");
      info.local_group_size(1, 1)
          .sampler(0, ImageType::Float2D, "source_img")
          .storage_buf(0, Qualifier::write, "vec4", "values[]")
          .push_constant(Type::int2_t, "position")
          .push_constant(Type::float3_t, "base_value")
          .push_constant(Type::int_t, "channel")
          .push_constant(Type::int_t, "mask_mode");
      info.compute_source_generated = R"GLSL(
void main(){
 vec4 p=texelFetch(source_img,min(position,textureSize(source_img,0)-1),0);
 vec3 value=p.rgb+base_value*(1-p.a);
 if(channel==3){float d=dot(value,value);value=d>1e-20?value*inversesqrt(d):vec3(0,0,1);}
 values[channel]=mask_mode!=0?vec4(p.r):vec4(value,1);
}
)GLSL";
      shader_ = Shader(
          GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free);
      if (!shader_)
        throw std::runtime_error("Material sample shader failed");
    }
    if (!buffer_)
      buffer_ = Buffer(GPU_storagebuf_create_ex(
                           6 * 4 * sizeof(float), nullptr, GPU_USAGE_DYNAMIC, "BTS material pick"),
                       GPU_storagebuf_free);
    if (!buffer_)
      throw std::bad_alloc();
    std::array<std::array<float, 4>, 6> empty{};
    GPU_storagebuf_update(buffer_.get(), empty.data());
    GPU_shader_bind(shader_.get());
    GPU_storagebuf_bind(buffer_.get(), 0);
    const int position[2] = {request.x, request.y};
    GPU_shader_uniform_2iv(shader_.get(), "position", position);
    mask_ = !request.mask.isNull();
    GPU_shader_uniform_1i(shader_.get(), "mask_mode", mask_);
    for (int c = 0; c < (mask_ ? 1 : 6); ++c) {
      if (!inputs[c].image)
        continue;
      GPU_texture_bind(inputs[c].image, 0);
      GPU_shader_uniform_3fv(shader_.get(), "base_value", inputs[c].base.data());
      GPU_shader_uniform_1i(shader_.get(), "channel", c);
      GPU_compute_dispatch(shader_.get(), 1, 1, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);
    GPU_storagebuf_unbind(buffer_.get());
    GPU_texture_unbind_all();
    GPU_shader_unbind();
    fence_.submit(buffer_.get());
    ticket_ = ticket;
  }
  void poll()
  {
    using namespace blender;
    if (!fence_)
      return;
    std::array<std::array<float, 4>, 6> values{};
    if (!fence_.read(buffer_.get(), values.data()))
      return;
    MaterialSample result;
    result.ticket = ticket_;
    if (mask_)
      result.mask = values[0][0];
    else {
      result.material.base_color = values[0];
      result.material.metallic = values[1][0];
      result.material.roughness = values[2][0];
      std::copy_n(values[3].begin(), 3, result.material.normal.begin());
      result.material.bump = values[4][0];
      result.material.ambient_occlusion = values[5][0];
    }
    result_ = result;
  }
};
}  // namespace bts
