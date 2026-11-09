// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <iterator>
namespace bts {
// Conservative per-tile proof. A constant stack, or an opaque flat fill that
// covers the varying stack below it, can be evaluated once and broadcast by GPU.
// Unsupported effects/projections stay on the authoritative sampled compositor.
inline bool uniform_channel(const TextureSet &set, int channel, int tile)
{
  const auto &layers = evaluation_layers(set);
  for (const auto &layer : layers)
    if (layer.mask && layer.mask->channel_dependent())
      return false;
  for (auto i = layers.rbegin(); i != layers.rend(); ++i) {
    const auto &layer = *i;
    const bool enabled = channel == 5 ? layer.ao_enabled : layer.channels[channel];
    const auto blend = channel == 5 ? layer.ao_blending : layer.blending[channel];
    const float opacity = channel == 5 ? layer.ao_opacity : layer.opacity[channel];
    if (!layer.visible || !enabled || blend == LayerBlend::Disabled ||
        (opacity <= 0 && blend != LayerBlend::Replace))
      continue;
    if (layer.depth || layer.kind == LayerKind::Folder)
      return false;
    if (layer.geometry.type == 1) {
      if (!layer.geometry.includes_tile(tile))
        continue;
    }
    else {
      if (!layer.geometry.meshes.empty())
        return false;
      if (!layer.geometry.default_include)
        continue;
    }
    if (std::any_of(layer.effects.begin(), layer.effects.end(), [](const auto &effect) {
          return effect.enabled;
        }))
      return false;
    if (layer.kind == LayerKind::Paint) {
      const bool empty = channel == 5 ? layer.ao_pixels.tile(tile).page_count(2) == 0 :
                                        layer.pixels.tile(tile).page_count(channel) == 0;
      if (empty && blend != LayerBlend::Replace)
        continue;
      if (!empty)
        return false;
    }
    if (layer.mask && layer.mask->enabled) {
      if (!layer.mask->pixels.empty() || !layer.mask->effects.empty())
        return false;
    }
    if (fill_spatial_projection(layer.fill) || fill_channel_texture(layer.fill, channel) ||
        (channel == 0 && layer.fill.atom))
      return false;
    const float mask = layer.mask && layer.mask->enabled ? layer.mask->background : 1;
    // Nonlinear HDR arithmetic below could produce non-finite values; keep
    // those stacks sampled rather than assuming multiplication by zero hides them.
    const bool safe_below = std::all_of(
        layers.begin(), std::prev(i.base()), [&](const auto &below) {
          const auto mode = channel == 5 ? below.ao_blending : below.blending[channel];
          return mode == LayerBlend::Normal || mode == LayerBlend::Replace ||
                 mode == LayerBlend::Disabled;
        });
    if (opacity == 1 && mask == 1 &&
        (blend == LayerBlend::Normal || blend == LayerBlend::Replace) && safe_below)
      return true;
  }
  return true;
}
inline void copy_material_channel(MaterialValues &to, const MaterialValues &from, int channel)
{
  switch (channel) {
    case 0:
      to.base_color = from.base_color;
      break;
    case 1:
      to.metallic = from.metallic;
      break;
    case 2:
      to.roughness = from.roughness;
      break;
    case 3:
      to.normal = from.normal;
      break;
    case 4:
      to.bump = from.bump;
      break;
    case 5:
      to.ambient_occlusion = from.ambient_occlusion;
      break;
  }
}
inline void copy_composite_channel(UniformComposite &to, const UniformComposite &from, int channel)
{
  copy_material_channel(to.material, from.material, channel);
  copy_material_channel(to.authored, from.authored, channel);
  if (channel < 5)
    to.coverage[channel] = from.coverage[channel];
  else
    to.ao_coverage = from.ao_coverage;
}
}  // namespace bts
