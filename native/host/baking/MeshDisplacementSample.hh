// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
namespace bts {
// Match the shader's filtered premultiplied height + uncovered Midlevel.
// Filtering the associated value and coverage first avoids UV-edge fringes.
inline float sample_mesh_displacement(std::span<const float> rgba,
                                      int width,
                                      int height,
                                      float u,
                                      float v,
                                      float midlevel,
                                      bool repeat)
{
  if (width <= 0 || height <= 0 || rgba.size() != std::size_t(width) * height * 4 ||
      !std::isfinite(u) || !std::isfinite(v) || !std::isfinite(midlevel))
    throw std::invalid_argument("Invalid displacement sampling input");
  if (repeat) {
    u -= std::floor(u);
    v -= std::floor(v);
  }
  else {
    u = std::clamp(u, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);
  }
  const float sx = u * width - .5f, sy = v * height - .5f;
  const int x = int(std::floor(sx)), y = int(std::floor(sy));
  const auto at = [&](int px, int py) {
    px = repeat ? (px % width + width) % width : std::clamp(px, 0, width - 1);
    py = repeat ? (py % height + height) % height : std::clamp(py, 0, height - 1);
    const auto offset = (std::size_t(py) * width + px) * 4;
    return rgba[offset] + midlevel * (1 - rgba[offset + 3]);
  };
  return std::lerp(std::lerp(at(x, y), at(x + 1, y), sx - x),
                   std::lerp(at(x, y + 1), at(x + 1, y + 1), sx - x),
                   sy - y);
}
}  // namespace bts
