#pragma once
#include "MaterialValues.hh"
namespace bts {
inline MaterialValues temporary_ink(MaterialValues primary,
                                    const std::array<float, 4> &secondary,
                                    bool active)
{
  if (active)
    primary.base_color = secondary;
  return primary;
}
inline float temporary_mask_ink(float primary, bool active)
{
  return active ? 1 - primary : primary;
}
inline float default_mask_ink(float background)
{
  return 1 - background;
}
}  // namespace bts
