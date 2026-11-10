// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace bts {
// Count changed components, including RGB cleared after alpha sanitization.
inline std::size_t sanitize_readback_pixel(float *rgba)
{
  std::size_t fixes = 0;
  for (int c = 0; c < 4; ++c) {
    const float value = std::isnan(rgba[c]) ?
                            0.f :
                            std::clamp(rgba[c], c == 3 ? 0.f : -65504.f, c == 3 ? 1.f : 65504.f);
    if (value != rgba[c]) {
      rgba[c] = value;
      ++fixes;
    }
  }
  if (rgba[3] == 0)
    for (int c = 0; c < 3; ++c)
      if (rgba[c] != 0) {
        rgba[c] = 0;
        ++fixes;
      }
  return fixes;
}
}  // namespace bts
