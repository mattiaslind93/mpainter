// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SurfacePath.hh"
#include <vector>
namespace bts {
// Clip triangle metadata, not pixels, to a convex chart bound. The returned UV
// box is conservative even for curved/concave contours; it only narrows which
// 64px pages may contain authored samples.
struct PathClipVertex {
  PathVector position;
  std::array<double, 2> uv;
};
struct PathClipPlane {
  PathVector normal;
  double maximum = 0;
};
inline std::optional<std::array<double, 4>> path_triangle_bounds(
    const std::array<PathClipVertex, 3> &triangle, const std::vector<PathClipPlane> &planes)
{
  std::vector<PathClipVertex> polygon(triangle.begin(), triangle.end());
  for (const auto &plane : planes) {
    if (polygon.empty())
      return {};
    const auto distance = [&](const PathClipVertex &p) {
      double result = -plane.maximum;
      for (int c = 0; c < 3; ++c)
        result += p.position[c] * plane.normal[c];
      return result;
    };
    std::vector<PathClipVertex> clipped;
    auto previous = polygon.back();
    double before = distance(previous);
    for (const auto &current : polygon) {
      const double after = distance(current);
      if ((before <= 0) != (after <= 0)) {
        const double t = before / (before - after);
        PathClipVertex at;
        for (int c = 0; c < 3; ++c)
          at.position[c] = std::lerp(previous.position[c], current.position[c], t);
        for (int c = 0; c < 2; ++c)
          at.uv[c] = std::lerp(previous.uv[c], current.uv[c], t);
        clipped.push_back(at);
      }
      if (after <= 0)
        clipped.push_back(current);
      previous = current;
      before = after;
    }
    polygon = std::move(clipped);
  }
  if (polygon.empty())
    return {};
  std::array<double, 4> bounds{1e100, 1e100, -1e100, -1e100};
  for (const auto &p : polygon)
    for (int c = 0; c < 2; ++c) {
      bounds[c] = std::min(bounds[c], p.uv[c]);
      bounds[c + 2] = std::max(bounds[c + 2], p.uv[c]);
    }
  return bounds;
}
}  // namespace bts
