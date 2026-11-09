// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PaintPixels.hh"
#include "SurfaceBrushBounds.hh"
#include <algorithm>
#include <cmath>
namespace bts {
inline std::vector<PaintPixels::Bounds> surface_brush_regions(int size,
                                                              const PaintPixels::Stamp &s)
{
  std::vector<PaintPixels::Bounds> result;
  const auto clip = [&](double v) { return int(std::clamp(v, 0., double(size))); };
  for (const auto &uv : brush_uv_bounds(s)) {
    PaintPixels::Bounds b{clip(std::floor(uv[0] * size) - 2),
                          clip(std::floor(uv[1] * size) - 2),
                          clip(std::ceil(uv[2] * size) + 2),
                          clip(std::ceil(uv[3] * size) + 2)};
    if (!b.empty())
      result.push_back(b);
  }
  return result;
}
inline PaintPixels::Bounds brush_footprint_bounds(int size, const PaintPixels::Stamp &s)
{
  const auto clip = [&](double v) { return int(std::clamp(v, 0., double(size))); };
  if (s.flow <= 0)
    return {};
  if (s.surface) {
    PaintPixels::Bounds out{size, size, 0, 0};
    // Dirty pages follow the brush contact, not the complete UV triangle. Keep
    // the existing two-texel guard for filtering and seam coverage.
    for (const auto &b : surface_brush_regions(size, s)) {
      out.x0 = std::min(out.x0, b.x0);
      out.y0 = std::min(out.y0, b.y0);
      out.x1 = std::max(out.x1, b.x1);
      out.y1 = std::max(out.y1, b.y1);
    }
    return out.empty() ? PaintPixels::Bounds{} : out;
  }
  const double det = s.axis_x[0] * s.axis_y[1] - s.axis_x[1] * s.axis_y[0];
  if (std::abs(det) < 1e-16)
    return {};
  const bool round = s.alpha.kind == AlphaKind::Shape;
  const double x = round ? std::hypot(s.axis_x[0], s.axis_y[0]) :
                           std::abs(s.axis_x[0]) + std::abs(s.axis_y[0]);
  const double y = round ? std::hypot(s.axis_x[1], s.axis_y[1]) :
                           std::abs(s.axis_x[1]) + std::abs(s.axis_y[1]);
  return {clip(std::floor((s.center[0] - x) * size)),
          clip(std::floor((s.center[1] - y) * size)),
          clip(std::ceil((s.center[0] + x) * size)),
          clip(std::ceil((s.center[1] + y) * size))};
}
inline bool gpu_basic_brush_supported(const PaintPixels::Stamp &s)
{
  return (!s.coverage_mask || (std::floor(s.stencil_uv_offset[0]) == s.stencil_uv_offset[0] &&
                               std::floor(s.stencil_uv_offset[1]) == s.stencil_uv_offset[1])) &&
         int(s.alpha.kind) <= int(AlphaKind::DynamicOutline);
}
}  // namespace bts
