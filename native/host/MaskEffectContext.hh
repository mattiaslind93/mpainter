#pragma once
#include "LayerMask.hh"
#include "MaskEffectPath.hh"
namespace bts {
template<class Predicate>
bool any_mask_effect(const std::vector<MaskEffect> &effects, Predicate &&predicate)
{
  for (const auto &effect : mask_effects_recursive(effects))
    if (predicate(effect))
      return true;
  return false;
}
// A Group is an isolated mask stack starting at zero. A nested Levels editor
// sees the preceding siblings in that stack, before any ancestor Group mix.
inline std::optional<LayerMask> mask_effect_input(const LayerMask &mask, const QUuid &id)
{
  const auto path = find_mask_effect_path(mask.effects, id);
  if (!path)
    return std::nullopt;
  const auto *siblings = &mask.effects;
  for (std::size_t depth = 0; depth + 1 < path->size(); ++depth) {
    const auto &parent = siblings->at((*path)[depth]);
    if (!parent.group_effects)
      return std::nullopt;
    siblings = parent.group_effects.get();
  }
  LayerMask input = path->size() == 1 ? mask : LayerMask{};
  if (path->size() > 1)
    input.background = 0;
  input.effects.assign(siblings->begin(), siblings->begin() + path->back());
  return input;
}
}  // namespace bts
