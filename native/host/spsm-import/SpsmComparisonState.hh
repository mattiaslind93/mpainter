// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
namespace bts {
// Explicit private comparison proof only. Reads existing baked CPU owners and
// bounded GPU probes; it does not bake, edit or evaluate paint recipes.
inline QJsonObject spsm_comparison_state(
    const TextureSet &set,
    const std::function<std::optional<std::array<float, 4>>(int, int, int, int)> &gpu)
{
  QJsonObject result;
  QJsonArray maps;
  bool all = true;
  for (int role : {1, 3, 4, 5, 6}) {
    if (!set.mesh_maps[role]) {
      all = false;
      continue;
    }
    std::size_t covered = 0;
    float low = 1e30f, high = -1e30f;
    QJsonArray tiles;
    for (const auto &[tile, plane] : set.mesh_maps[role]->tiles) {
      if (!plane.pixels)
        continue;
      const auto &rgba = plane.pixels->rgba;
      for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
        if (rgba[i + 3] > 0) {
          ++covered;
          const auto value = rgba[i] / rgba[i + 3];
          low = std::min(low, value);
          high = std::max(high, value);
        }
      tiles.append(tile);
    }
    maps.append(QJsonObject{{"role", role},
                            {"name", mesh_map_names[role]},
                            {"covered", qint64(covered)},
                            {"minimum", covered ? low : 0},
                            {"maximum", covered ? high : 0},
                            {"tiles", tiles}});
  }
  if (all)
    result["spsm_baked_maps"] = maps;
  QJsonArray samples;
  for (int channel : {0, 1, 2, 4}) {
    float low = 1e30f, high = -1e30f;
    int count = 0;
    QJsonArray points;
    for (int tile : set.tiles)
      for (int y = 1; y < 8; ++y)
        for (int x = 1; x < 8; ++x) {
          const int px = x * set.width(tile) / 8, py = y * set.rows(tile) / 8;
          if (auto value = gpu(channel, px, py, tile)) {
            QJsonArray pixel;
            for (float f : *value)
              pixel.append(f);
            points.append(QJsonObject{{"tile", tile}, {"x", px}, {"y", py}, {"rgba", pixel}});
            if ((*value)[3] > 0) {
              low = std::min(low, (*value)[0]);
              high = std::max(high, (*value)[0]);
              ++count;
            }
          }
        }
    samples.append(QJsonObject{{"channel", channel},
                               {"covered_probes", count},
                               {"minimum", count ? low : 0},
                               {"maximum", count ? high : 0},
                               {"points", points}});
  }
  result["spsm_gpu_grid"] = samples;
  QJsonArray layers;
  for (const auto &layer : set.layers) {
    QJsonArray generators;
    const auto collect = [&](const MeshGeneratorSettings &effect) {
      generators.append(QJsonObject{{"generator", effect.generator},
                                    {"name", mask_generator_names.at(effect.generator)},
                                    {"optional_inputs", effect.optional_inputs},
                                    {"grunge", effect.grunge_amount},
                                    {"scale", effect.grunge_scale}});
    };
    for (const auto &effect : layer.effects)
      if (effect.kind == ContentEffectKind::Generator)
        collect(effect);
    if (layer.mask)
      for (const auto &effect : layer.mask->effects)
        if (effect.kind == MaskEffectKind::Generator)
          collect(effect);
    layers.append(
        QJsonObject{{"name", QString::fromStdString(layer.name)},
                    {"visible", layer.visible},
                    {"native_generators", generators},
                    {"internal_normal", layer.channels[3]},
                    {"internal_normal_bitmap", bool(fill_channel_texture(layer.fill, 3))},
                    {"bump", layer.values.bump}});
  }
  result["spsm_native_layers"] = layers;
  result["spsm_bump_origin"] = set.bump_origin;
  result["spsm_internal_normal"] = set.channels[3];
  return result;
}
}  // namespace bts
