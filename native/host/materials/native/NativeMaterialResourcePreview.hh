// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialGpuLibrary.hh"
#include "GpuThumbnail.hh"
#include "MaterialSpherePreview.hh"
#include <cstring>
#include <numbers>
namespace bts {
// One small native channel dispatch/fenced packet per step. Uses the already
// uploaded graph/programs; no document readback, second source decode or renderer.
class NativeMaterialResourcePreview {
  using Texture = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  Texture surface_{nullptr, blender::GPU_texture_free},
      target_{nullptr, blender::GPU_texture_free}, ao_{nullptr, blender::GPU_texture_free};
  NativeMaterialGpuContext context_;
  std::unique_ptr<GpuThumbnail> packet_;
  FillProjection mapping_;
  std::size_t next_ = 0;
  std::vector<int> channels_;
  QImage image_;

 public:
  bool step(NativeMaterialGpuLibrary &library,
            const std::string &key,
            const NativeMaterialDescription &description)
  {
    using namespace blender;
    constexpr int side = 64, packet_side = ThumbnailMetadata::side;
    if (!target_) {
      // Evaluate procedural/native channels on the same JL01 UV surface used
      // to display them, including baked AO and object-space position/normal.
      // Mesh maps use a valid 64px extent; GpuThumbnail downsamples to its 48px packet.
      auto preview = detail::shaderball_uv_surface(side);
      constexpr int stride = sizeof(FillSurfacePoint) / sizeof(std::uint32_t),
                    texels = (stride + 3) / 4;
      std::vector<std::uint32_t> packed(side * side * texels * 4);
      auto &points = preview.atlas.tiles.at(1001);
      std::vector<float> ao(side * side);
      const auto &ao_pixels = preview.maps[3]->tiles.at(1001).pixels->rgba;
      for (std::size_t i = 0; i < points.size(); ++i) {
        points[i].match_object = 1;
        std::memcpy(packed.data() + i * stride, &points[i], sizeof(FillSurfacePoint));
        ao[i] = ao_pixels[i * 4];
      }
      surface_.reset(GPU_texture_create_2d_array("Native resource JL01 surface",
                                                 side * texels,
                                                 side,
                                                 1,
                                                 1,
                                                 gpu::TextureFormat::UINT_32_32_32_32,
                                                 GPU_TEXTURE_USAGE_SHADER_READ,
                                                 nullptr));
      target_.reset(GPU_texture_create_2d("Native resource channel preview",
                                          side,
                                          side,
                                          1,
                                          gpu::TextureFormat::SFLOAT_32_32_32_32,
                                          GPU_TEXTURE_USAGE_GENERAL,
                                          nullptr));
      ao_.reset(GPU_texture_create_2d("Native resource JL01 AO",
                                      side,
                                      side,
                                      1,
                                      gpu::TextureFormat::SFLOAT_32,
                                      GPU_TEXTURE_USAGE_SHADER_READ,
                                      nullptr));
      if (!surface_ || !target_ || !ao_)
        throw std::bad_alloc();
      GPU_texture_update(surface_.get(), GPU_DATA_UINT, packed.data());
      GPU_texture_update(ao_.get(), GPU_DATA_FLOAT, ao.data());
      context_ = library.default_context(key);
      context_.surface = surface_.get();
      context_.surface_size = {side, side};
      context_.surface_stride = stride;
      context_.ambient_occlusion = ao_.get();
      NativeMaterialObject object;
      object.owner = 1;
      object.bounds_min = {-1, -1, -1};
      object.bounds_max = {1, 1, 1};
      context_.objects = std::make_shared<NativeMaterialObjectTable>(
          std::vector<NativeMaterialObject>{object});
      for (int channel : library.effective_channels(key, description))
        if (channel >= 0 && channel <= 2)
          channels_.push_back(channel);
      if (std::find(channels_.begin(), channels_.end(), 0) == channels_.end())
        throw std::runtime_error("Native resource preview requires evaluated Color");
      return false;
    }
    if (packet_) {
      packet_->poll();
      const auto result = packet_->get();
      if (!result)
        return false;
      auto texture = std::make_shared<FillTexture>();
      texture->width = texture->height = packet_side;
      auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(packet_side * packet_side);
      for (int y = 0; y < packet_side; ++y)
        for (int x = 0; x < packet_side; ++x)
          (*pixels)[y * packet_side + x] = result->pixels[(packet_side - y - 1) * packet_side + x];
      texture->pixels = std::move(pixels);
      fill_channel_texture(mapping_, channels_[next_++]) = std::move(texture);
      packet_.reset();
      return false;
    }
    if (next_ < channels_.size()) {
      library.render(
          key, channels_[next_], description, target_.get(), context_, {0, 0, side, side});
      packet_ = std::make_unique<GpuThumbnail>();
      packet_->update({}, {{target_.get(), side, side}}, next_ + 1);
      return false;
    }
    MaterialValues base;
    base.base_color = {1, 1, 1, 1};
    image_ = material_sphere_preview(base, &mapping_, 64);
    return true;
  }
  const QImage &image() const
  {
    return image_;
  }
};
}  // namespace bts
