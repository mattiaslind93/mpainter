#pragma once
#include "PaintPixels.hh"
namespace bts {
// Metadata clipping only: bound the square sampled by the brush, rather than
// copying every painted page of a coarse triangle that covers an entire tile.
using BrushUvBounds = std::array<double, 4>;
inline std::vector<BrushUvBounds> brush_uv_bounds(const PaintPixels::Stamp &stamp)
{
  std::vector<BrushUvBounds> result;
  const auto rectangle = [&](const auto &vertices) {
    BrushUvBounds b{1e30, 1e30, -1e30, -1e30};
    for (const auto &v : vertices) {
      b[0] = std::min(b[0], v[0]);
      b[1] = std::min(b[1], v[1]);
      b[2] = std::max(b[2], v[0]);
      b[3] = std::max(b[3], v[1]);
    }
    if (b[0] <= b[2] && b[1] <= b[3])
      result.push_back(b);
  };
  if (!stamp.surface) {
    std::array<std::array<double, 2>, 4> corners;
    int i = 0;
    for (int x : {-1, 1})
      for (int y : {-1, 1})
        corners[i++] = {stamp.center[0] + x * stamp.axis_x[0] + y * stamp.axis_y[0],
                        stamp.center[1] + x * stamp.axis_x[1] + y * stamp.axis_y[1]};
    rectangle(corners);
    return result;
  }
  const auto &surface = *stamp.surface;
  for (const auto &triangle : surface.triangles) {
    if (surface.cull &&
        !accepts_brush_normal(triangle.normal, surface.view_direction, surface.cull_angle))
      continue;
    using Vertex = std::array<double, 5>;
    std::array<Vertex, 3> original;
    int vertex_index = 0;
    for (int i = 0; i < 3; ++i) {
      Vertex v{triangle.uv[i][0], triangle.uv[i][1], 0, 0, 0};
      for (int c = 0; c < 3; ++c) {
        const double d = (triangle.position[i][c] - surface.center[c]) / surface.radius;
        v[2] += d * surface.tangent[c];
        v[3] += d * surface.bitangent[c];
        v[4] += d * surface.normal[c];
      }
      if (surface.unwrapped) {
        v[2] = triangle.paint_coordinates[i][0] / surface.radius;
        v[3] = triangle.paint_coordinates[i][1] / surface.radius;
      }
      original[vertex_index++] = v;
    }
    const int axes = (!surface.unwrapped || triangle.bounded_depth) ? 5 : 4;
    bool inside = true, outside = false;
    for (int axis = 2; axis < axes; ++axis)
      for (int sign : {-1, 1}) {
        bool all_outside = true;
        for (const auto &v : original) {
          const bool contains = 1 - sign * v[axis] >= 0;
          inside &= contains;
          all_outside &= !contains;
        }
        outside |= all_outside;
      }
    if (outside)
      continue;
    if (inside) {
      rectangle(original);
      continue;
    }
    std::vector<Vertex> polygon(original.begin(), original.end());
    for (int axis = 2; axis < ((!surface.unwrapped || triangle.bounded_depth) ? 5 : 4); ++axis)
      for (int sign : {-1, 1}) {
        std::vector<Vertex> clipped;
        if (polygon.empty())
          break;
        auto previous = polygon.back();
        double a = 1 - sign * previous[axis];
        for (const auto &current : polygon) {
          const double b = 1 - sign * current[axis];
          if ((a >= 0) != (b >= 0)) {
            const double t = a / (a - b);
            Vertex crossing;
            for (int c = 0; c < 5; ++c)
              crossing[c] = std::lerp(previous[c], current[c], t);
            clipped.push_back(crossing);
          }
          if (b >= 0)
            clipped.push_back(current);
          previous = current;
          a = b;
        }
        polygon = std::move(clipped);
      }
    rectangle(polygon);
  }
  return result;
}
}  // namespace bts
