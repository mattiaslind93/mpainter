// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshBakeFilter.hh"
#include "DilationFill.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "MEM_guardedalloc.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <OpenImageDenoise/oidn.hpp>
namespace bts {
namespace {
using Buffer = std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)>;
Buffer buffer(int width, int height)
{
  Buffer result(blender::IMB_allocImBuf(width, height, blender::ImBufFlags::FloatData),
                blender::IMB_freeImBuf);
  if (!result || !result->float_data())
    throw std::runtime_error("Cannot allocate bake filtering buffer");
  return result;
}
// Push/pull diffusion uses Blender's associated-alpha box pyramid and bilinear reconstruction.
// Only previously empty texels are filled. Authored island texels are never blurred.
void diffuse(blender::ImBuf &image)
{
  std::vector<Buffer> levels;
  const blender::ImBuf *previous = &image;
  while (previous->x > 1 || previous->y > 1) {
    Buffer next(blender::IMB_scale_into_new(previous,
                                            std::max(1, previous->x / 2),
                                            std::max(1, previous->y / 2),
                                            blender::IMBScaleFilter::Box),
                blender::IMB_freeImBuf);
    if (!next)
      throw std::runtime_error("Cannot allocate bake diffusion pyramid");
    previous = next.get();
    levels.push_back(std::move(next));
  }
  for (int level = int(levels.size()) - 1; level >= 0; --level) {
    auto &target = level == 0 ? image : *levels[level - 1];
    Buffer up(blender::IMB_scale_into_new(
                  levels[level].get(), target.x, target.y, blender::IMBScaleFilter::Bilinear),
              blender::IMB_freeImBuf);
    if (!up)
      throw std::runtime_error("Cannot reconstruct bake diffusion pyramid");
    auto *pixels = target.float_data_for_write();
    const auto *coarse = up->float_data();
    for (std::size_t i = 0; i < std::size_t(target.x) * target.y * 4; i += 4)
      if (pixels[i + 3] == 0 && coarse[i + 3] > 1e-12f) {
        for (int c = 0; c < 3; ++c)
          pixels[i + c] = coarse[i + c] / coarse[i + 3];
        pixels[i + 3] = 1;
      }
  }
}
// One committed OIDN filter per working-tile size. Interior tiles all share a size, so the
// network is built once per map instead of once per tile, which matters most on a GPU device.
struct DenoiseTile {
  oidn::FilterRef filter;
  oidn::BufferRef color, albedo, normal, output;
};
void denoise_tiles(oidn::DeviceRef &device,
                   std::vector<float> &pixels,
                   const ScratchVector<float> &original,
                   std::span<const FillSurfacePoint> surface,
                   int width,
                   int height)
{
  const auto check = [&] {
    const char *message = nullptr;
    if (device.getError(message) != oidn::Error::None)
      throw std::runtime_error(std::string("Mesh-map OIDN: ") +
                               (message ? message : "unknown error"));
  };
  std::map<std::pair<int, int>, DenoiseTile> tiles;
  // Fixed working tiles with overlap keep 8K maps from allocating 8K OIDN scratch.
  constexpr int tile_size = 512, halo = 64;
  for (int y0 = 0; y0 < height; y0 += tile_size)
    for (int x0 = 0; x0 < width; x0 += tile_size) {
      const int left = std::max(0, x0 - halo), bottom = std::max(0, y0 - halo),
                right = std::min(width, x0 + tile_size + halo),
                top = std::min(height, y0 + tile_size + halo), w = right - left, h = top - bottom;
      const std::size_t count = std::size_t(w) * h, bytes = count * 3 * sizeof(float);
      std::vector<float> color(count * 3), normal(count * 3), output(count * 3);
      std::vector<int> owner(count, -1), queue;
      queue.reserve(count);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const int local = y * w + x;
          const auto index = std::size_t(y + bottom) * width + x + left;
          if (surface[index].valid) {
            owner[local] = local;
            queue.push_back(local);
          }
        }
      if (queue.empty())
        continue;
      // Extend valid data into the empty UV gutter before denoising, never black.
      for (std::size_t q = 0; q < queue.size(); ++q) {
        const int i = queue[q], x = i % w, y = i / w;
        for (const int n : {x > 0 ? i - 1 : -1,
                            x + 1 < w ? i + 1 : -1,
                            y > 0 ? i - w : -1,
                            y + 1 < h ? i + w : -1})
          if (n >= 0 && owner[n] < 0) {
            owner[n] = owner[i];
            queue.push_back(n);
          }
      }
      for (std::size_t i = 0; i < count; ++i) {
        const auto src = std::size_t(owner[i] / w + bottom) * width + owner[i] % w + left;
        for (int c = 0; c < 3; ++c) {
          color[i * 3 + c] = original[src * 4 + c];
          normal[i * 3 + c] = surface[src].normal[c];
        }
      }
      auto &tile = tiles[{w, h}];
      if (!tile.filter) {
        // Device buffers work on every OIDN device; host pointers only on the CPU one.
        tile.color = device.newBuffer(bytes);
        tile.albedo = device.newBuffer(bytes);
        tile.normal = device.newBuffer(bytes);
        tile.output = device.newBuffer(bytes);
        check();
        const std::vector<float> white(count * 3, 1.f);
        tile.albedo.write(0, bytes, white.data());
        tile.filter = device.newFilter("RT");
        tile.filter.setImage("color", tile.color, oidn::Format::Float3, w, h);
        tile.filter.setImage("albedo", tile.albedo, oidn::Format::Float3, w, h);
        tile.filter.setImage("normal", tile.normal, oidn::Format::Float3, w, h);
        tile.filter.setImage("output", tile.output, oidn::Format::Float3, w, h);
        tile.filter.set("hdr", true);
        tile.filter.set("inputScale", 1.f);
        tile.filter.set("cleanAux", true);
        tile.filter.commit();
        check();
      }
      tile.color.write(0, bytes, color.data());
      tile.normal.write(0, bytes, normal.data());
      tile.filter.execute();
      tile.output.read(0, bytes, output.data());
      check();
      for (int y = y0; y < std::min(height, y0 + tile_size); ++y)
        for (int x = x0; x < std::min(width, x0 + tile_size); ++x) {
          const auto index = std::size_t(y) * width + x;
          if (!surface[index].valid)
            continue;
          const auto local = std::size_t(y - bottom) * w + x - left;
          const float value = (output[local * 3] + output[local * 3 + 1] + output[local * 3 + 2]) /
                              3;
          if (!std::isfinite(value))
            throw std::runtime_error("Mesh-map denoiser produced a non-finite value");
          for (int c = 0; c < 3; ++c)
            pixels[index * 4 + c] = value;
        }
    }
}
// The fastest OIDN device this machine has: Metal, CUDA, HIP or SYCL when the module and a
// supported GPU are present. Null when only the CPU device is available.
oidn::DeviceRef gpu_denoise_device()
{
  try {
    auto device = oidn::newDevice(oidn::DeviceType::Default);
    if (!device)
      return {};
    device.commit();
    const char *message = nullptr;
    if (device.getError(message) != oidn::Error::None)
      return {};
    if (oidn::DeviceType(device.get<int>("type")) == oidn::DeviceType::CPU)
      return {};
    return device;
  }
  catch (const std::exception &) {
    return {};
  }
}
}  // namespace
void denoise_mesh_bake(std::vector<float> &pixels,
                       std::span<const FillSurfacePoint> surface,
                       int width,
                       int height)
{
  float low = std::numeric_limits<float>::max(), high = std::numeric_limits<float>::lowest();
  for (std::size_t i = 0; i < surface.size(); ++i)
    if (surface[i].valid) {
      low = std::min(low, pixels[4 * i]);
      high = std::max(high, pixels[4 * i]);
    }
  if (high - low < 1e-6f)
    return;  // Preserve exactly constant visibility, including open planes.
  // Retain the unfiltered input until every tile is done: overlap is deterministic.
  ScratchVector<float> original(pixels.size());
  std::copy(pixels.begin(), pixels.end(), original.begin());
  if (auto device = gpu_denoise_device()) {
    try {
      denoise_tiles(device, pixels, original, surface, width, height);
      return;
    }
    catch (const std::exception &) {
      // A GPU device that fails mid-map (driver, memory) must not fail the bake.
      std::copy(original.begin(), original.end(), pixels.begin());
    }
  }
  auto device = oidn::newDevice(oidn::DeviceType::CPU);
  device.set("numThreads", 6);
  device.commit();
  denoise_tiles(device, pixels, original, surface, width, height);
}
std::vector<float> filter_mesh_bake(std::vector<float> pixels,
                                    int source_width,
                                    int source_height,
                                    int width,
                                    int height,
                                    int dilation,
                                    bool diffusion,
                                    int role)
{
  if (pixels.size() != std::size_t(source_width) * source_height * 4)
    throw std::invalid_argument("Invalid supersampled bake buffer");
  if (source_width == width && source_height == height && !dilation && !diffusion) {
    if (role == 0 || role == 1)
      for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const float alpha = pixels[i + 3];
        if (alpha <= 0)
          continue;
        float vector[3], length = 0;
        for (int c = 0; c < 3; ++c) {
          vector[c] = pixels[i + c] / alpha * 2 - 1;
          length += vector[c] * vector[c];
        }
        length = std::sqrt(length);
        if (length > 1e-8f)
          for (int c = 0; c < 3; ++c)
            pixels[i + c] = (vector[c] / length * .5f + .5f) * alpha;
      }
    return pixels;
  }
  auto image = buffer(width, height);
  if (source_width == width && source_height == height)
    std::copy(pixels.begin(), pixels.end(), image->float_data_for_write());
  else
    blender::IMB_scale_box(pixels.data(),
                           {source_width, source_height},
                           4,
                           image->float_data_for_write(),
                           {width, height},
                           true);
  pixels.clear();
  pixels.shrink_to_fit();
  auto *data = image->float_data_for_write();
  if (role == 0 || role == 1)
    for (std::size_t i = 0; i < std::size_t(width) * height * 4; i += 4) {
      const float alpha = data[i + 3];
      if (alpha <= 0)
        continue;
      float vector[3], length = 0;
      for (int c = 0; c < 3; ++c) {
        vector[c] = data[i + c] / alpha * 2 - 1;
        length += vector[c] * vector[c];
      }
      length = std::sqrt(length);
      if (length > 1e-8f)
        for (int c = 0; c < 3; ++c)
          data[i + c] = (vector[c] / length * .5f + .5f) * alpha;
    }
  if (dilation) {
    std::vector<char> mask(std::size_t(width) * height);
    for (std::size_t i = 0; i < mask.size(); ++i)
      mask[i] = data[i * 4 + 3] > 0 ? FILTER_MASK_USED : 0;
    dilate_rgba(data, mask.data(), width, height, dilation);
  }
  if (diffusion)
    diffuse(*image);
  return {image->float_data(), image->float_data() + std::size_t(width) * height * 4};
}
}  // namespace bts
