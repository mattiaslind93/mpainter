// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuLayerChannel.hh"
namespace bts {
inline constexpr std::array<int, 6> gpu_mask_map_roles{2, 5, 4, 3, 6, 1};
inline constexpr int gpu_mask_map_slot(int index)
{
  return index < 3 ? 6 + index : 8 + index;
}
inline void gpu_need_generator_maps(std::array<bool, 6> &needed, int generator)
{
  const auto roles = mask_generator_inputs(generator);
  for (int i = 0; i < 6; ++i)
    needed[i] |= bool(roles & (1u << gpu_mask_map_roles[i]));
}
inline void gpu_need_generator_maps(std::array<bool, 6> &needed,
                                    const MeshGeneratorSettings &effect)
{
  gpu_need_generator_maps(needed, effect.generator);
  if (effect.triplanar_grunge || effect.surface_fallback) {
    needed[1] = true;
    needed[5] = true;
  }
}
// Small immutable parameter rows; no pixel evaluation on the UI thread.
using GpuEffectRow = std::array<std::array<float, 4>, 10>;
inline void gpu_need_generator_row_maps(std::array<bool, 6> &needed, const GpuEffectRow &row)
{
  gpu_need_generator_maps(needed, int(row[1][0]));
  if ((int(row[2][1]) & 6) != 0) {
    needed[1] = true;
    needed[5] = true;
  }
}
inline GpuEffectRow gpu_generator_row(const MeshGeneratorSettings &effect,
                                      const MeshMaps &maps,
                                      float opacity = 1,
                                      LayerBlend blend = LayerBlend::Normal)
{
  GpuEffectRow row{};
  row[0] = {3, float(blend), opacity, float(effect.response)};
  row[1] = {float(effect.generator), float(effect.axis), effect.contrast, effect.bias};
  row[2] = {float(effect.invert),
            float(int(generator_resources_present(maps, effect)) |
                  (effect.surface_fallback ? 2 : 0) | (effect.triplanar_grunge ? 4 : 0)),
            float(effect.curve),
            float(effect.map_filter)};
  row[3] = {effect.light_direction[0],
            effect.light_direction[1],
            effect.light_direction[2],
            effect.dirt_curvature};
  row[7] = {effect.grunge_amount,
            effect.grunge_scale,
            float(effect.grunge_seed),
            float(effect.grunge_pattern)};
  row[8] = {effect.ao_influence,
            effect.curvature_influence,
            effect.position_influence,
            effect.normal_influence};
  row[9][0] = effect.curvature_radius;
  row[6][2] = effect.thickness_influence;
  unsigned available = 0;
  for (int role = 0; role < 7; ++role)
    if (maps[role])
      available |= 1u << role;
  row[6][3] = float(available);
  return row;
}
inline GpuEffectRow gpu_effect_row(const LevelsEffect &effect, int channel)
{
  GpuEffectRow row{};
  if (effect.kind == ContentEffectKind::Fill) {
    row[0] = {8, float(effect.blending), effect.opacity, 0};
    row[1] = {effect.fill_value[0], effect.fill_value[1], effect.fill_value[2], 0};
    return row;
  }
  if (effect.kind == ContentEffectKind::LinearColorTransform) {
    if (channel != 0 || !effect.linear_color)
      throw std::invalid_argument("Invalid linear Color GPU effect");
    row[0][0] = 7;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        row[1 + r][c] = effect.enabled ? effect.linear_color->to_aces[c * 3 + r] : float(r == c);
    return row;
  }
  row[0] = {effect.kind == ContentEffectKind::Filter ? 1.f : 0.f,
            float(effect.blending),
            effect.opacity,
            float(effect.clamp)};
  const auto curve = [&](int index, const LevelCurve &c) {
    row[1 + index * 2] = {c.input_low, c.input_high, 1 / c.gamma, c.output_low};
    row[2 + index * 2] = {c.output_high, 0, 0, 0};
  };
  curve(0, effect);
  for (int c = 0; c < 3; ++c)
    curve(c + 1, rgb_levels_channel(channel) ? effect.rgb_curves[c] : LevelCurve{});
  if (effect.kind == ContentEffectKind::Filter) {
    const auto &filter = effect.filter;
    row[1] = {float(filter.resource), float(filter.steps), 0, 0};
    row[2] = {filter.hue, filter.saturation, filter.value_scale, filter.brightness};
    row[3] = {filter.contrast, filter.input_low, filter.input_high, filter.gamma};
    row[4] = {filter.output_low, filter.output_high, filter.radius, 0};
  }
  return row;
}
inline bool gpu_mask_effect_supported(const MaskEffect &effect)
{
  if (!effect.enabled || effect.kind == MaskEffectKind::Anchor)
    return true;
  if (effect.kind == MaskEffectKind::Group) {
    if (effect.group_effects)
      for (const auto &child : mask_effects_recursive(*effect.group_effects))
        if (child.kind != MaskEffectKind::Group && !gpu_mask_effect_supported(child))
          return false;
    return true;
  }
  if (effect.kind == MaskEffectKind::Levels || effect.kind == MaskEffectKind::Filter ||
      effect.kind == MaskEffectKind::Paint)
    return true;
  if (effect.kind == MaskEffectKind::ColorSelection || effect.kind == MaskEffectKind::Generator)
    return true;
  if (effect.kind == MaskEffectKind::Compare)
    return true;
  return effect.kind == MaskEffectKind::Fill;
}
inline std::vector<GpuEffectRow> gpu_content_effects(const TextureLayer &layer, int channel)
{
  std::vector<GpuEffectRow> rows;
  for (const auto &effect : layer.effects)
    if ((effect.enabled || effect.kind == ContentEffectKind::LinearColorTransform) &&
        effect.channel == channel &&
        (effect.kind == ContentEffectKind::Levels || effect.kind == ContentEffectKind::Filter ||
         effect.kind == ContentEffectKind::LinearColorTransform))
      rows.push_back(gpu_effect_row(effect, channel));
  return rows;
}
inline void gpu_append_mask_effects(std::vector<GpuEffectRow> &rows,
                                    const TextureLayer &layer,
                                    const TextureSet &set,
                                    int tile = 1001)
{
  if (!layer.mask || !layer.mask->enabled)
    return;
  for (const auto &effect : layer.mask->effects) {
    if (!effect.enabled || effect.kind == MaskEffectKind::Anchor ||
        effect.kind == MaskEffectKind::Paint)
      continue;
    if (effect.kind == MaskEffectKind::Levels) {
      auto levels = effect.levels;
      levels.opacity = effect.opacity;
      levels.blending = effect.blending;
      rows.push_back(gpu_effect_row(levels, 2));
      continue;
    }
    if (effect.kind == MaskEffectKind::ColorSelection) {
      GpuEffectRow row{};
      row[0] = {4, 0, 0, 0};
      row[1] = {float(effect.use_id_map), effect.output, effect.hardness, effect.tolerance};
      row[2][0] = float(effect.colors.size());
      rows.push_back(row);
      for (std::size_t first = 0; first < effect.colors.size(); first += 8) {
        GpuEffectRow colors{};
        colors[0][0] = -1;
        for (std::size_t i = first; i < std::min(first + 8, effect.colors.size()); ++i) {
          const auto &c = effect.colors[i];
          colors[1 + i - first] = {c[0], c[1], c[2], 0};
        }
        rows.push_back(colors);
      }
      continue;
    }
    if (effect.kind == MaskEffectKind::Generator) {
      rows.push_back(gpu_generator_row(effect, set.mesh_maps, effect.opacity, effect.blending));
      continue;
    }
    if (effect.kind == MaskEffectKind::Compare) {
      GpuEffectRow row{};
      row[0] = {5, float(effect.blending), effect.opacity, 0};
      row[1] = {float(effect.left), float(effect.right), float(effect.operation), effect.constant};
      row[2] = {effect.hardness, effect.tolerance, float(effect.compare_channel), 0};
      row[3] = {effect.compare_offset, effect.compare_width, float(effect.compare_invert), 0};
      row[4] = {effect.compare_ramp[0],
                effect.compare_ramp[1],
                effect.compare_ramp[2],
                effect.compare_ramp[3]};
      row[5][0] = effect.compare_ramp[4];
      rows.push_back(row);
      continue;
    }
    if (effect.kind == MaskEffectKind::Fill && effect.mapping.projection.atom) {
      auto mapping = effect.mapping.projection;
      mapping.native_per_uv = set.native_per_uv;
      mapping.cm_per_native = set.cm_per_native;
      const double u = (tile - 1001) % 10, v = (tile - 1001) / 10;
      const auto a = fill_uv_map(mapping, u, v), b = fill_uv_map(mapping, u + 1, v),
                 c = fill_uv_map(mapping, u, v + 1);
      GpuEffectRow row{};
      row[0] = {6, float(effect.blending), effect.opacity, 0};
      row[1] = {effect.fill,
                float(effect.mapping.filtering == 1 || mapping.filtering > 0),
                float(effect.mapping.wrap),
                0};
      row[2] = {float(b[0] - a[0]), float(c[0] - a[0]), float(a[0]), 0};
      row[3] = {float(b[1] - a[1]), float(c[1] - a[1]), float(a[1]), 0};
      row[4] = {float(mapping.border_width),
                float(mapping.middle_dot),
                float(mapping.border_dots),
                float(mapping.invert)};
      rows.push_back(row);
      continue;
    }
    GpuEffectRow row{};
    row[0] = {effect.kind == MaskEffectKind::Filter ? 1.f : 2.f,
              float(effect.blending),
              effect.opacity,
              0};
    row[1] = {float(effect.filter.resource), float(effect.filter.steps), effect.fill, 0};
    row[2] = {effect.filter.hue,
              effect.filter.saturation,
              effect.filter.value_scale,
              effect.filter.brightness};
    row[3] = {effect.filter.contrast,
              effect.filter.input_low,
              effect.filter.input_high,
              effect.filter.gamma};
    row[4] = {effect.filter.output_low, effect.filter.output_high, effect.filter.radius, 0};
    rows.push_back(row);
  }
}
}  // namespace bts
