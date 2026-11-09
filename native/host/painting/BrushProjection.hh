// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace bts {
enum class BrushAlignment { Camera, TangentWrap, TangentPlanar, UV };
struct BrushProjection {
  BrushAlignment alignment = BrushAlignment::TangentWrap;
  bool cull = true;
  float angle = 90;
  float orientation = 1;
  bool symmetry_ray = false;
  std::array<float, 3> camera_right{1, 0, 0}, camera_up{0, 1, 0};
};
inline bool accepts_brush_normal(const std::array<double, 3> &normal,
                                 const std::array<double, 3> &direction,
                                 double angle)
{
  double dot = 0, norm = 0, ray = 0;
  for (int i = 0; i < 3; ++i) {
    dot -= normal[i] * direction[i];
    norm += normal[i] * normal[i];
    ray += direction[i] * direction[i];
  }
  if (norm < 1e-20 || ray < 1e-20)
    return false;
  return dot / std::sqrt(norm * ray) + 1e-7 >= std::cos(angle * 3.14159265358979323846 / 180);
}
// Rigid face unfolding retains a shared edge's length and puts neighboring face
// interiors on opposite sides of it. Coordinates are world units at the contact plane.
struct BrushFaceFrame {
  using V3 = std::array<double, 3>;
  using V2 = std::array<double, 2>;
  V3 x{1, 0, 0}, y{0, 1, 0};
  V2 offset{};
  template<class P> V2 map(const P &p) const
  {
    V2 result = offset;
    for (int i = 0; i < 3; ++i) {
      result[0] += x[i] * p[i];
      result[1] += y[i] * p[i];
    }
    return result;
  }
  template<class P> static V3 vector(const P &p)
  {
    return {double(p[0]), double(p[1]), double(p[2])};
  }
  static V3 sub(V3 a, V3 b)
  {
    for (int i = 0; i < 3; ++i)
      a[i] -= b[i];
    return a;
  }
  static double dot(V3 a, V3 b)
  {
    double r = 0;
    for (int i = 0; i < 3; ++i)
      r += a[i] * b[i];
    return r;
  }
  static V3 cross(V3 a, V3 b)
  {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  }
  static V3 unit(V3 v)
  {
    const double n = std::sqrt(dot(v, v));
    if (n < 1e-20)
      return {};
    for (double &x : v)
      x /= n;
    return v;
  }
  BrushFaceFrame neighbor(V3 a, V3 b, V3 parent_center, V3 child_center, V3 child_normal) const
  {
    const auto flat_a = map(a), flat_b = map(b), flat_parent = map(parent_center);
    const auto edge = unit(sub(b, a)), perp = unit(cross(child_normal, edge));
    const double dx = flat_b[0] - flat_a[0], dy = flat_b[1] - flat_a[1],
                 length = std::hypot(dx, dy);
    if (length < 1e-20)
      return *this;
    const V2 flat_edge{dx / length, dy / length};
    const double parent_side = dx * (flat_parent[1] - flat_a[1]) -
                               dy * (flat_parent[0] - flat_a[0]);
    const double child_side = dot(sub(child_center, a), perp);
    const double sign = parent_side * child_side > 0 ? -1 : 1;
    const V2 flat_perp{-flat_edge[1] * sign, flat_edge[0] * sign};
    BrushFaceFrame result;
    for (int i = 0; i < 3; ++i) {
      result.x[i] = edge[i] * flat_edge[0] + perp[i] * flat_perp[0];
      result.y[i] = edge[i] * flat_edge[1] + perp[i] * flat_perp[1];
    }
    result.offset = {flat_a[0] - dot(result.x, a), flat_a[1] - dot(result.y, a)};
    return result;
  }
};
}  // namespace bts
