#pragma once
#include <array>
#include <cmath>

namespace bts {
// Mesh directions are data, not admission criteria. Use double intermediates
// so tiny/large finite vectors do not underflow/overflow during normalization.
inline std::array<float, 3> native_surface_unit(std::array<float, 3> v,
                                                std::array<float, 3> fallback = {0, 0, 1})
{
  const double length = std::hypot(double(v[0]), double(v[1]), double(v[2]));
  if (!(length > 0) || !std::isfinite(length))
    return fallback;
  for (auto &x : v)
    x = float(x / length);
  return v;
}
inline std::array<float, 3> native_surface_tangent(std::array<float, 3> n)
{
  // Cross with the least parallel coordinate axis.
  int axis = 0;
  for (int i = 1; i < 3; ++i)
    if (std::abs(n[i]) < std::abs(n[axis]))
      axis = i;
  std::array<float, 3> t{};
  t[(axis + 1) % 3] = n[(axis + 2) % 3];
  t[(axis + 2) % 3] = -n[(axis + 1) % 3];
  return native_surface_unit(t, {1, 0, 0});
}
inline std::array<float, 4> native_surface_tangent(std::array<float, 4> t, std::array<float, 3> n)
{
  n = native_surface_unit(n);
  const double length = std::hypot(double(t[0]), double(t[1]), double(t[2]));
  const double cross_length = std::hypot(double(t[1]) * n[2] - double(t[2]) * n[1],
                                         double(t[2]) * n[0] - double(t[0]) * n[2],
                                         double(t[0]) * n[1] - double(t[1]) * n[0]);
  const float sign = std::isfinite(t[3]) && t[3] < 0 ? -1.f : 1.f;
  if (std::isfinite(length) && length > 0 && cross_length > 1e-6 * length)
    return {t[0], t[1], t[2], sign};  // Preserve valid stock Mikk directions exactly.
  const auto direction = native_surface_tangent(n);
  return {direction[0], direction[1], direction[2], sign};
}
}  // namespace bts
