#pragma once
#include "PaintPixels.hh"
#include <cmath>
namespace bts {
inline void rotate_brush_stamp(PaintPixels::Stamp &stamp, double angle)
{
  if (angle == 0)
    return;
  const double c = std::cos(angle), s = std::sin(angle);
  const auto x = stamp.axis_x, y = stamp.axis_y;
  for (int i = 0; i < 2; ++i) {
    stamp.axis_x[i] = c * x[i] + s * y[i];
    stamp.axis_y[i] = -s * x[i] + c * y[i];
  }
  if (!stamp.surface)
    return;
  auto surface = std::make_shared<PaintPixels::SurfaceFootprint>(*stamp.surface);
  const auto tangent = surface->tangent, bitangent = surface->bitangent;
  for (int i = 0; i < 3; ++i) {
    surface->tangent[i] = c * tangent[i] + s * bitangent[i];
    surface->bitangent[i] = -s * tangent[i] + c * bitangent[i];
  }
  if (surface->unwrapped)
    for (auto &triangle : surface->triangles)
      for (auto &point : triangle.paint_coordinates) {
        const auto original = point;
        point = {c * original[0] + s * original[1], -s * original[0] + c * original[1]};
      }
  stamp.surface = std::move(surface);
}
}  // namespace bts
