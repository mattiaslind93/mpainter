#pragma once
// Diagnostic only: actual compiled derived programs, both formats and targets.
// Binary-fraction inputs make destination-half rounding exact without changing
// the existing 3e-5 numeric bound. No interactive readback is introduced.
#include "GpuLayerPreview.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace bts {
class GpuDerivedWarmupNumeric {
  using Image = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  static void require(bool okay, const char *message)
  {
    if (!okay)
      throw std::runtime_error(message);
  }
  TextureSet set;
  Image target{nullptr, blender::GPU_texture_free};
  QJsonArray variants;
  std::uint64_t components = 0;
  double maximum = 0;
  int stage = 0;

 public:
  explicit GpuDerivedWarmupNumeric(const TextureSet &source) : set(source)
  {
    set.tiles = {1001, 1002};
    set.texture_resolution = 64;
    set.ao_channel = true;
    set.ao_mixing = AoMixing::Multiply;
    set.precision[5] = ChannelPrecision::L32F;
    set.channels[3] = true;
    set.channels[4] = false;
    set.precision[3] = ChannelPrecision::RGB32F;
    set.base_values.normal = {0, 0, 1};
    set.normal_format = NormalMapFormat::OpenGL;
    require(set.layers.size() == 2 && set.layers.back().mask.has_value(),
            "Derived fixture needs the existing spatial mask");
    for (auto &layer : set.layers) {
      layer.ao_enabled = true;
      layer.channels[3] = true;
      layer.values.normal = {0, 0, 1};
    }
    set.layers.front().values.ambient_occlusion = .5f;
    set.layers.back().values.ambient_occlusion = .25f;
    set.layers.front().ao_opacity = 1;
    set.layers.back().ao_opacity = .625f;
    set.mesh_maps[3] = MeshMap{"Derived worker AO fixture", {}};
    set.mesh_maps[0] = MeshMap{"Derived worker flat-normal fixture", {}};
    for (int tile : set.tiles)
      for (int role : {0, 3}) {
        std::vector<float> pixels(64 * 64 * 4);
        for (int i = 0; i < 64 * 64; ++i) {
          for (int c = 0; c < 3; ++c)
            pixels[4 * i + c] = role == 3 ? (tile == 1001 ? .25f : .75f) : (c == 2 ? 1.f : .5f);
          pixels[4 * i + 3] = 1;
        }
        auto storage = std::make_shared<MeshMapPixels>(64, std::move(pixels));
        set.mesh_maps[role]->tiles[tile] = MeshMapPlane{64, storage->hash, storage, 64};
      }
  }
  // One resumable variant/channel per diagnostic timer; preserve the actual
  // production upload budget and keep its Pending state across callbacks.
  bool advance(GpuLayerPreview &preview)
  {
    using namespace blender;
    if (stage == 8)
      return true;
    const int variant = stage / 2, channel = stage % 2 ? 3 : 5;
    const auto format = variant < 2 ? gpu::TextureFormat::SFLOAT_16_16_16_16 :
                                      gpu::TextureFormat::SFLOAT_32_32_32_32;
    const bool array = variant % 2;
    const int width = array ? 64 : 128;
    if (!target) {
      set.id =
          QUuid::createUuid();  // Distinct outputs cannot inherit an older target's dirty state.
      target.reset(array ? GPU_texture_create_2d_array("Worker derived oracle",
                                                       64,
                                                       64,
                                                       2,
                                                       1,
                                                       format,
                                                       GPU_TEXTURE_USAGE_GENERAL,
                                                       nullptr) :
                           GPU_texture_create_2d("Worker derived oracle",
                                                 128,
                                                 64,
                                                 1,
                                                 format,
                                                 GPU_TEXTURE_USAGE_GENERAL,
                                                 nullptr));
    }
    require(bool(target), "Derived worker target allocation failed");
    const std::map<int, std::array<int, 3>> regions{
        {1001, {0, 0, 0}},
        {1002, array ? std::array<int, 3>{0, 0, 1} : std::array<int, 3>{64, 0, 0}}};
    preview.begin_frame();
    const auto updated = preview.update_tiles(set, channel, target.get(), regions);
    require(updated != GpuPreviewUpdate::Unsupported, "Ready derived worker output was rejected");
    if (updated == GpuPreviewUpdate::Pending)
      return false;
    auto *pixels = static_cast<float *>(GPU_texture_read(target.get(), GPU_DATA_FLOAT, 0));
    const auto free = qScopeGuard([&] {
      if (pixels)
        MEM_delete(pixels);
    });
    require(pixels, "Derived worker readback failed");
    double error = 0;
    for (int tile : set.tiles)
      for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
          const int z = array ? tile - 1001 : 0, tx = x + (array ? 0 : (tile - 1001) * 64);
          const double mask = double((x / 16 + 2 * (y / 16)) % 4 + 1) / 4 * .75,
                       weight = mask * .625;
          const double ao = (.5 + (.25 - .5) * weight) * (tile == 1001 ? .25 : .75);
          for (int c = 0; c < 4; ++c) {
            const double expected = c == 3 ? 1 : channel == 5 ? ao : c == 2 ? 1 : .5;
            const double actual = pixels[((z * 64 + y) * width + tx) * 4 + c];
            require(std::isfinite(actual), "Nonfinite derived worker output");
            error = std::max(error, std::abs(actual - expected));
            ++components;
          }
        }
    maximum = std::max(maximum, error);
    variants.append(QJsonObject{
        {"format", format == gpu::TextureFormat::SFLOAT_16_16_16_16 ? "RGBA16F" : "RGBA32F"},
        {"array", array},
        {"channel", channel},
        {"tiles", 2},
        {"maximum_error", error}});
    require(error <= 3e-5,
            "Derived worker output disagrees with independent binary-fraction oracle");
    ++stage;
    if (stage % 2 == 0)
      target.reset();
    return stage == 8;
  }
  QJsonObject result() const
  {
    require(stage == 8, "Derived worker fixture is not complete");
    require(components == 262144, "Derived worker component count mismatch");
    return {{"finite_components", qint64(components)},
            {"maximum_error", maximum},
            {"fixed_tolerance", 3e-5},
            {"variants", variants},
            {"normal_scope",
             "Flat authored and baked Normal only; no new nonflat Bump/shaded appearance claim"}};
  }
};
}  // namespace bts
