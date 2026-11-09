#pragma once
#include "SceneSnapshot.hh"
#include <array>
#include <vector>
namespace bts {
struct GeometryClipVertex {
  std::array<double, 2> uv;
  std::array<double, 3> position;
};
// Clip UVs and the corresponding world positions together, including faces spanning tiles.
inline std::vector<GeometryClipVertex> geometry_tile_polygon(const UvFaceTriangle &triangle,
                                                             int tile)
{
  std::vector<GeometryClipVertex> polygon;
  for (int i = 0; i < 3; ++i) {
    GeometryClipVertex vertex;
    for (int c = 0; c < 2; ++c)
      vertex.uv[c] = triangle.corners[i][c];
    for (int c = 0; c < 3; ++c)
      vertex.position[c] = triangle.positions[i][c];
    polygon.push_back(vertex);
  }
  const int number = tile - 1001;
  const std::array<double, 2> origin{double(number % 10), double(number / 10)};
  for (int axis = 0; axis < 2; ++axis)
    for (int side = 0; side < 2; ++side) {
      if (polygon.empty())
        return {};
      std::vector<GeometryClipVertex> result;
      const double boundary = origin[axis] + side;
      const auto inside = [&](const GeometryClipVertex &v) {
        return side ? v.uv[axis] <= boundary : v.uv[axis] >= boundary;
      };
      auto previous = polygon.back();
      bool before = inside(previous);
      for (const auto &current : polygon) {
        const bool after = inside(current);
        if (before != after) {
          const double t = (boundary - previous.uv[axis]) / (current.uv[axis] - previous.uv[axis]);
          GeometryClipVertex edge;
          for (int c = 0; c < 2; ++c)
            edge.uv[c] = previous.uv[c] + t * (current.uv[c] - previous.uv[c]);
          for (int c = 0; c < 3; ++c)
            edge.position[c] = previous.position[c] +
                               t * (current.position[c] - previous.position[c]);
          result.push_back(edge);
        }
        if (after)
          result.push_back(current);
        previous = current;
        before = after;
      }
      polygon = std::move(result);
    }
  return polygon;
}
}  // namespace bts
