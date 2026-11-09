// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Independent planar geometry oracle for the fragment-only Bump witness.
// Input positions are evaluated world-space geometry, not assumed unit UV axes.
#include <array>
#include <cmath>
#include <stdexcept>
namespace bts::pd149_bump_test {
using Vector = std::array<double, 3>;
inline double dot(Vector a, Vector b)
{
  double value = 0;
  for (int c = 0; c < 3; ++c)
    value += a[c] * b[c];
  return value;
}
inline Vector add(Vector a, Vector b)
{
  for (int c = 0; c < 3; ++c)
    a[c] += b[c];
  return a;
}
inline Vector scale(Vector a, double b)
{
  for (auto &v : a)
    v *= b;
  return a;
}
inline Vector cross(Vector a, Vector b)
{
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Vector normalized(Vector a)
{
  const double length = std::sqrt(dot(a, a));
  if (!std::isfinite(length) || length < 1e-12)
    throw std::invalid_argument("Bump reference has degenerate geometry");
  return scale(a, 1 / length);
}
struct Plane {
  Vector du, dv, normal;
};
inline Plane plane(const std::array<Vector, 3> &positions,
                   const std::array<std::array<double, 2>, 3> &uv)
{
  const auto e1 = add(positions[1], scale(positions[0], -1)),
             e2 = add(positions[2], scale(positions[0], -1));
  const double u1 = uv[1][0] - uv[0][0], v1 = uv[1][1] - uv[0][1], u2 = uv[2][0] - uv[0][0],
               v2 = uv[2][1] - uv[0][1], det = u1 * v2 - u2 * v1;
  if (!std::isfinite(det) || std::abs(det) < 1e-12)
    throw std::invalid_argument("Bump reference requires nondegenerate UVs");
  Plane result;
  result.du = scale(add(scale(e1, v2), scale(e2, -v1)), 1 / det);
  result.dv = scale(add(scale(e2, u1), scale(e1, -u2)), 1 / det);
  result.normal = normalized(cross(e1, e2));
  return result;
}
inline Vector response(
    const Plane &p, double height_u, double height_v, double distance, double strength)
{
  if (!std::isfinite(height_u) || !std::isfinite(height_v) || !std::isfinite(distance) ||
      distance < 0 || !std::isfinite(strength) || strength < 0 || strength > 1)
    throw std::invalid_argument("Bump reference settings outside witness contract");
  const double a = dot(p.du, p.du), b = dot(p.du, p.dv), c = dot(p.dv, p.dv), det = a * c - b * b;
  if (!std::isfinite(det) || det < 1e-18)
    throw std::invalid_argument("Bump reference requires independent surface tangents");
  const auto gradient = add(scale(p.du, (c * height_u - b * height_v) / det),
                            scale(p.dv, (a * height_v - b * height_u) / det));
  const auto bumped = normalized(add(p.normal, scale(gradient, -distance)));
  return normalized(add(scale(p.normal, 1 - strength), scale(bumped, strength)));
}
}  // namespace bts::pd149_bump_test
