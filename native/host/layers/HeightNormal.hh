// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NormalMix.hh"
#include <limits>
namespace bts {
inline constexpr float default_bump_world_scale = 0.5f;
inline constexpr float bump_world_scale_factor = default_bump_world_scale / 100.0f;
// GPU float arithmetic flushes subnormal coverage. Do not turn an invisible
// triplanar tail into fully opaque derived normals in the CPU reference/export.
inline bool bump_normal_coverage(float alpha)
{
  return alpha >= std::numeric_limits<float>::min();
}
// Heights are in UV-space units scaled by bump_world_scale_factor (0.5 / 100).
// Sharp is central difference; Sobel averages orthogonally.
// The caller supplies a scalar reader, including any UDIM boundary policy.
template<class Sample>
NormalVector height_normal(BumpMethod method,
                           int resolution,
                           int x,
                           int y,
                           int tile,
                           const Sample &sample,
                           int height = 0,
                           float world_scale = default_bump_world_scale)
{
  if (!height)
    height = resolution;
  float du = 0, dv = 0;
  if (method == BumpMethod::Sharp) {
    du = sample(x + 1, y, tile) - sample(x - 1, y, tile);
    dv = sample(x, y + 1, tile) - sample(x, y - 1, tile);
  }
  else
    for (int offset = -1; offset <= 1; ++offset) {
      const float weight = offset == 0 ? .5f : .25f;
      du += weight * (sample(x + 1, y + offset, tile) - sample(x - 1, y + offset, tile));
      dv += weight * (sample(x + offset, y + 1, tile) - sample(x + offset, y - 1, tile));
    }
  const float scale = (world_scale / 100.0f) * .5f;
  return normalized_normal({-du * resolution * scale, -dv * height * scale, 1});
}
}  // namespace bts
