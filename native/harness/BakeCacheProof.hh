// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuLayerPreview.hh"
#include "GPU_capabilities.hh"
#include "GPU_state.hh"
#include "MEM_guardedalloc.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
inline QJsonObject verify_bake_cache_release()
{
  using namespace blender;
  TextureSet set;
  set.texture_resolution = 1024;
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.base_color = {.17f, .42f, .71f, 1};
  for (int i = 0; i < 3; ++i) {
    TextureDocument::add_fill(set);
    set.layers.back().values.base_color = {.1f * i, .7f - .1f * i, .3f, 1};
    set.layers.back().opacity.fill(.3f);
  }
  TextureDocument::add_paint(set);
  PaintPixels::Stamp ink{{.53, .47}, {.43, 0}, {0, .34}};
  ink.values.base_color = {.79f, .31f, .52f, 1};
  ink.flow = .73;
  ink.hardness = .34;
  set.layers.back().pixels.stamp(1024, ink);
  const auto authored = set.layers.back().pixels;
  const auto authored_hash = authored.hash(0);
  auto output = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>(
      GPU_texture_create_2d("Bake cache proof",
                            1024,
                            1024,
                            1,
                            gpu::TextureFormat::SFLOAT_32_32_32_32,
                            GPU_TEXTURE_USAGE_GENERAL,
                            nullptr),
      GPU_texture_free);
  if (!output)
    throw std::runtime_error("Cache proof output allocation failed");
  GpuLayerPreview preview;
  const auto render = [&] {
    QElapsedTimer elapsed;
    elapsed.start();
    for (;;) {
      preview.begin_frame();
      const auto state = preview.update(set, 0, output.get());
      if (state == GpuPreviewUpdate::Updated)
        break;
      if (state == GpuPreviewUpdate::Unsupported || elapsed.elapsed() > 60000)
        throw std::runtime_error("Cache proof did not render");
    }
    GPU_finish();
    auto *pixels = static_cast<float *>(GPU_texture_read(output.get(), GPU_DATA_FLOAT, 0));
    if (!pixels)
      throw std::runtime_error("Cache proof readback failed");
    const auto hash = QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(pixels), 1024LL * 1024 * 16),
        QCryptographicHash::Sha256);
    MEM_delete(pixels);
    return hash;
  };
  const auto stats = [] {
    GPU_finish();
    int total = 0, free = 0;
    if (GPU_mem_stats_supported())
      GPU_mem_stats_get(&total, &free);
    return QJsonObject{{"total_kib", total},
                       {"free_kib", free},
                       {"tracked_texture_bytes", qint64(GPU_texture_memory_usage_get())}};
  };
  QJsonArray repeats;
  QByteArray first;
  for (int repeat = 0; repeat < 3; ++repeat) {
    const auto pixels = render();
    if (first.isEmpty())
      first = pixels;
    else if (first != pixels)
      throw std::runtime_error("Cache release changed authored composite output");
    const auto before = stats();
    const auto released = preview.release_bake_scratch();
    const auto after = stats();
    if (!released)
      throw std::runtime_error("No recomputable cache released");
    if (set.layers.back().pixels.hash(0) != authored_hash || authored.hash(0) != authored_hash)
      throw std::runtime_error("Cache release mutated authored history");
    const auto again = preview.release_bake_scratch();
    if (again)
      throw std::runtime_error("Repeated idle release retained cache owners");
    repeats.append(QJsonObject{{"repeat", repeat},
                               {"before", before},
                               {"after", after},
                               {"released_bytes", qint64(released)},
                               {"second_release_bytes", qint64(again)},
                               {"sha256", QString::fromLatin1(pixels.toHex())}});
  }
  preview.reset(true);
  GpuLayerPreview::collect_retired(true);
  return {{"resolution", 1024},
          {"layers", int(set.layers.size())},
          {"full_resolution_prefixes", true},
          {"authored_paint_and_COW_history_preserved", true},
          {"bit_exact_after_rebuild", true},
          {"repeats", repeats}};
}
}  // namespace bts
