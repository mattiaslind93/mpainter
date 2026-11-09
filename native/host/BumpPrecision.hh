#pragma once
#include "TextureDocument.hh"
namespace bts {
// Quantize before moving a legacy signed height around .5. Quantizing a second
// time in the gray coordinate would erase small but valid old 16F heights.
inline float precision_bump_output(const TextureSet &set, float displayed)
{
  MaterialValues value;
  value.bump = set.bump_storage(displayed);
  return set.bump_display(precision_channel(value, 4, set.precision[4])[0]);
}
inline float precision_bump_base(const TextureSet &set)
{
  return set.channels[4] ?
             set.bump_display(precision_channel(set.base_values, 4, set.precision[4])[0]) :
             .5f;
}
}  // namespace bts
