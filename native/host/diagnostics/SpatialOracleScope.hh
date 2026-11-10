// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <optional>
#include <stdexcept>
namespace bts {
class SpatialOracleUnavailable final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};
// The complete CPU document evaluator has no spatial-prefix reference. Inspect
// every producer, including hidden anchor producers and nested mask Groups;
// conservative unavailability is preferable to comparing against identity.
inline std::optional<std::string> spatial_oracle_unavailable(const TextureSet &set)
{
  for (const auto &layer : evaluation_layers(set)) {
    for (const auto &effect : layer.effects)
      if (effect.enabled && effect.kind == ContentEffectKind::Filter &&
          effect.filter.resource >= 7)
        return "content neighborhood filter in layer " + layer.name;
    if (layer.mask)
      for (const auto &effect : mask_effects_recursive(layer.mask->effects))
        if (effect.enabled && effect.kind == MaskEffectKind::Filter && effect.filter.resource >= 7)
          return "mask neighborhood filter, including Group/anchor producers, in layer " +
                 layer.name;
  }
  return {};
}
}  // namespace bts
