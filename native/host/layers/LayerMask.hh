#pragma once
#include "TilePaintPixels.hh"
#include "StrokeHistory.hh"
#include "MaskEffect.hh"
#include "MaskEffectPath.hh"
#include <algorithm>
namespace bts {
// Sparse scalar paint over a uniform background. Channel 2 stores premultiplied data,
// reusing the brush's coverage math without interpreting mask values as color.
struct LayerMask {
  std::vector<MaskEffect> effects;
  int author_resolution = 0;
  float background = 1;
  bool enabled = true;
  TilePaintPixels pixels;
  std::shared_ptr<const StrokeHistory> strokes;
  bool uses_material_channels() const
  {
    return any_active_mask_effect(effects, [](const auto &e) {
      return e.enabled && e.kind == MaskEffectKind::Compare &&
             e.blending != LayerBlend::Disabled && (e.left < 2 || e.right < 2);
    });
  }
  bool channel_dependent() const
  {
    return any_active_mask_effect(effects, [](const auto &e) {
      return e.enabled && ((e.kind == MaskEffectKind::Compare && (e.left < 2 || e.right < 2)) ||
                           (e.kind == MaskEffectKind::Generator && generator_has_anchors(e)) ||
                           (e.kind == MaskEffectKind::Fill && !e.anchor_source.isNull() &&
                            e.blending != LayerBlend::Disabled));
    });
  }
  float value(int x = -1,
              int y = -1,
              int tile = 1001,
              int output_resolution = 0,
              int output_height = 0,
              const MeshMap *id_map = nullptr,
              const MeshMaps *maps = nullptr,
              const MaskCompareInputs *compare = nullptr,
              const QUuid *stop = nullptr) const
  {
    const auto pixel = x < 0 || y < 0 ? PaintPixels::Pixel{} :
                       output_resolution > 0 ?
                                        pixels.sample_resampled(
                                            2, x, y, output_resolution, tile, output_height) :
                                        pixels.sample(2, x, y, tile);
    float result = std::clamp(pixel[0] + background * (1 - pixel[3]), 0.f, 1.f);
    const int width = output_resolution ? output_resolution : std::max(1, pixels.resolution()),
              height = output_height ? output_height : width;
    for (const auto &effect : effects)
      if (effect.enabled) {
        if (effect.kind == MaskEffectKind::Anchor) {
          if (stop && effect.id == *stop)
            return std::clamp(result, 0.f, 1.f);
          if (compare && compare->layer_index >= 0) {
            if (!compare->anchors)
              compare->anchors = std::make_shared<MaskAnchorSamples>();
            (*compare->anchors)[effect.id] = {std::clamp(result, 0.f, 1.f), compare->layer_index};
          }
        }
        else if (effect.kind == MaskEffectKind::Group && effect.blending != LayerBlend::Disabled) {
          LayerMask group;
          group.background = 0;
          if (effect.group_effects)
            group.effects = *effect.group_effects;
          const float value = group.value(
              x, y, tile, output_resolution, output_height, id_map, maps, compare);
          result = std::lerp(result, blend_value(effect.blending, value, result), effect.opacity);
        }
        else if (effect.kind == MaskEffectKind::ColorSelection)
          result = color_selection(
              effect,
              x < 0 || y < 0 ? std::array<float, 4>{} :
                               sample_id_map(id_map, (x + .5) / width, (y + .5) / height, tile));
        else if (effect.kind == MaskEffectKind::Filter && effect.filter.resource &&
                 effect.blending != LayerBlend::Disabled)
          result = std::lerp(
              result,
              blend_value(effect.blending, filter_value(effect.filter, result), result),
              effect.opacity);
        else if (effect.kind == MaskEffectKind::Paint && effect.blending != LayerBlend::Disabled) {
          const auto ink = x < 0 || y < 0 ?
                               PaintPixels::Pixel{} :
                               effect.paint.pixels.sample_resampled(2, x, y, width, tile, height);
          if (ink[3] > 0)
            result = std::lerp(result,
                               blend_value(effect.blending, ink[0] / ink[3], result),
                               ink[3] * effect.opacity);
        }
        else if (effect.kind == MaskEffectKind::Levels && effect.blending != LayerBlend::Disabled)
        {
          const float adjusted = levels_value(effect.levels, result);
          result = std::lerp(
              result, blend_value(effect.blending, adjusted, result), effect.opacity);
        }
        else if (effect.kind == MaskEffectKind::Fill && effect.blending != LayerBlend::Disabled) {
          float value = 0;
          if (effect.anchor_source.isNull())
            value = mask_fill_sample(effect.mapping,
                                     effect.fill,
                                     x,
                                     y,
                                     width,
                                     height,
                                     tile,
                                     compare ? compare->projection : nullptr,
                                     compare ? compare->cm_per_native : 1,
                                     compare ? compare->native_per_uv :
                                               std::array<double, 2>{1, 1});
          else if (compare && compare->anchors) {
            const auto found = compare->anchors->find(effect.anchor_source);
            if (found != compare->anchors->end() && found->second.layer < compare->layer_index)
              value = found->second.value;
            else if (compare->reference.resolve) {
              const auto sample = anchor_components(
                  compare->reference.resolve(compare->reference, effect.anchor_reference()),
                  effect.anchor_reference());
              value = std::clamp(sample[0] * sample[3], 0.f, 1.f);
            }
          }
          else if (compare && compare->reference.resolve) {
            const auto sample = anchor_components(
                compare->reference.resolve(compare->reference, effect.anchor_reference()),
                effect.anchor_reference());
            value = std::clamp(sample[0] * sample[3], 0.f, 1.f);
          }
          result = std::lerp(result, blend_value(effect.blending, value, result), effect.opacity);
        }
        else if (effect.kind == MaskEffectKind::Compare && effect.blending != LayerBlend::Disabled)
        {
          const float value = compare_mask_values(
              effect,
              mask_compare_operand(effect, effect.left, result, compare),
              mask_compare_operand(effect, effect.right, result, compare));
          result = std::lerp(result, blend_value(effect.blending, value, result), effect.opacity);
        }
        else if (effect.kind == MaskEffectKind::Generator &&
                 effect.blending != LayerBlend::Disabled)
        {
          if (const auto value = generated_mask(effect,
                                                maps,
                                                x < 0 ? .5 : (x + .5) / width,
                                                y < 0 ? .5 : (y + .5) / height,
                                                tile,
                                                compare ? &compare->reference : nullptr,
                                                compare ? compare->projection : nullptr))
            result = std::lerp(
                result, blend_value(effect.blending, *value, result), effect.opacity);
        }
      }
    return std::clamp(result, 0.f, 1.f);
  }
};
}  // namespace bts
