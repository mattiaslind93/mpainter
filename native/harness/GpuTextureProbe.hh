// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GPU_compute.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "GPU_texture_mip_range.hh"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include "gpu_shader_create_info.hh"
#include "MEM_guardedalloc.h"
#include <array>
#include <memory>
#include <span>
#include <vector>
namespace bts {
// Explicit test/diagnostic boundary: gather only requested native atlas texels.
// Production rendering never calls this or reads GPU textures back to the CPU.
inline std::vector<std::array<float, 4>> probe_gpu_texture(
    blender::gpu::Texture *source, std::span<const std::array<int, 3>> coordinates)
{
  using namespace blender;
  using namespace gpu::shader;
  if (coordinates.empty())
    return {};
  using Image = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
  using Program = std::unique_ptr<gpu::Shader, decltype(&GPU_shader_free)>;
  const bool array = GPU_texture_is_array(source);
  // A view aliases storage without inheriting the presentation mip range.
  // Both texture fetch and driver multi-image binding can otherwise address the
  // restricted base level rather than native level zero.
  Image view(GPU_texture_create_view("BTS diagnostic level zero",
                                     source,
                                     GPU_texture_format(source),
                                     0,
                                     1,
                                     0,
                                     std::max(1, GPU_texture_layer_count(source)),
                                     false,
                                     false),
             GPU_texture_free);
  if (!view)
    throw std::runtime_error("GPU diagnostic texture view failed");
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(64, 1);
  info.sampler(0, array ? ImageType::Float2DArray : ImageType::Float2D, "source_img")
      .sampler(1, ImageType::Int2D, "coordinates_img")
      .sampler(2, array ? ImageType::Float2DArray : ImageType::Float2D, "presentation_img")
      .image(0,
             gpu::TextureFormat::SFLOAT_32_32_32_32,
             Qualifier::write,
             ImageReadWriteType::image2D,
             "result_img");
  info.compute_source_generated =
      std::string(
          "void main(){int i=int(gl_GlobalInvocationID.x);if(i>=imageSize(result_img).x)return;if(i==textureSize(coordinates_img,0).x){imageStore(result_img,ivec2(i,0),vec4(vec2(textureSize(presentation_img,0).xy),0,0));return;}ivec3 p=texelFetch(coordinates_img,ivec2(i,0),0).xyz;imageStore(result_img,ivec2(i,0),texelFetch(source_img,") +
      (array ? "p" : "p.xy") + ",0));}";
  Program shader(
      GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
      GPU_shader_free);
  std::vector<std::array<int, 4>> positions;
  for (const auto &point : coordinates)
    positions.push_back({point[0], point[1], point[2], 0});
  const int count = int(positions.size());
  Image points(GPU_texture_create_2d("BTS diagnostic coordinates",
                                     count,
                                     1,
                                     1,
                                     gpu::TextureFormat::SINT_32_32_32_32,
                                     GPU_TEXTURE_USAGE_GENERAL,
                                     nullptr),
               GPU_texture_free);
  Image result(GPU_texture_create_2d("BTS diagnostic probes",
                                     count + 1,
                                     1,
                                     1,
                                     gpu::TextureFormat::SFLOAT_32_32_32_32,
                                     GPU_TEXTURE_USAGE_GENERAL,
                                     nullptr),
               GPU_texture_free);
  if (!shader || !points || !result)
    throw std::runtime_error("GPU diagnostic allocation failed");
  GPU_texture_update(points.get(), GPU_DATA_INT, positions.data());
  GPU_shader_bind(shader.get());
  GPU_texture_bind(view.get(), 0);
  GPU_texture_bind(points.get(), 1);
  GPU_texture_bind(source, 2);
  GPU_texture_image_bind(result.get(), 0);
  GPU_compute_dispatch(shader.get(), (count + 1 + 63) / 64, 1, 1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
  GPU_texture_unbind_all();
  GPU_texture_image_unbind_all();
  GPU_shader_unbind();
  auto *data = static_cast<float *>(GPU_texture_read(result.get(), GPU_DATA_FLOAT, 0));
  if (!data)
    throw std::runtime_error("GPU diagnostic readback failed");
  std::printf("BTS_GPU_PROBE_IMAGE width=%d height=%d sampler_width=%.0f sampler_height=%.0f\n",
              GPU_texture_width(source),
              GPU_texture_height(source),
              data[count * 4],
              data[count * 4 + 1]);
  std::vector<std::array<float, 4>> values(count);
  for (int i = 0; i < count; ++i)
    std::copy_n(data + i * 4, 4, values[i].begin());
  MEM_delete(data);
  return values;
}
inline int verify_gpu_texture_probe()
{
  using namespace blender;
  using Image = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
  for (bool array : {false, true}) {
    std::vector<float> data(32 * 32 * (array ? 2 : 1) * 4);
    for (int layer = 0; layer < (array ? 2 : 1); ++layer)
      for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
          const auto at = ((layer * 32 + y) * 32 + x) * 4;
          data[at] = x * .03f;
          data[at + 1] = y * .02f;
          data[at + 2] = layer * .7f;
          data[at + 3] = 1;
        }
    Image source(array ? GPU_texture_create_2d_array("BTS probe mip regression",
                                                     32,
                                                     32,
                                                     2,
                                                     6,
                                                     gpu::TextureFormat::SFLOAT_32_32_32_32,
                                                     GPU_TEXTURE_USAGE_GENERAL,
                                                     data.data()) :
                         GPU_texture_create_2d("BTS probe mip regression",
                                               32,
                                               32,
                                               6,
                                               gpu::TextureFormat::SFLOAT_32_32_32_32,
                                               GPU_TEXTURE_USAGE_GENERAL,
                                               data.data()),
                 GPU_texture_free);
    GPU_texture_update_mipmap_chain(source.get());
    GPU_texture_mipmap_range(source.get(), 3, 3);
    const std::array<std::array<int, 3>, 3> coordinates{
        {{0, 0, 0}, {17, 21, array ? 1 : 0}, {31, 31, 0}}};
    const auto values = probe_gpu_texture(source.get(), coordinates);
    for (int i = 0; i < 3; ++i) {
      const auto p = coordinates[i];
      const auto index = ((p[2] * 32 + p[1]) * 32 + p[0]) * 4;
      for (int c = 0; c < 4; ++c)
        if (std::abs(values[i][c] - data[index + c]) > 1e-6f)
          throw std::runtime_error(
              "GPU diagnostic sampled presentation mip instead of native storage");
    }
  }
  std::printf(
      "BTS_NATIVE_TEXTURE_PROBE_VERIFIED arrays=1 mip_base=3 alias_level_zero=1 unchanged_source=1\n");
  return 2;
}
}  // namespace bts
