// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhysicalScale.hh"
#include "SceneSnapshot.hh"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts {
void validate_units(const UnitSettings &settings, const SourceUnits &source)
{
  for (double value :
       {settings.custom_cm_per_file_unit, source.native_per_file_unit, source.cm_per_file_unit})
    if (!std::isfinite(value) || value < 1e-12 || value > 1e12)
      throw std::invalid_argument("Unit scale must be positive and finite (1e-12 to 1e12)");
}
double centimeters_per_native_unit(const UnitSettings &settings, const SourceUnits &source)
{
  validate_units(settings, source);
  return (settings.internal ? source.cm_per_file_unit : settings.custom_cm_per_file_unit) /
         source.native_per_file_unit;
}
std::array<double, 2> native_length_per_uv(const SceneSnapshot &scene, std::uint32_t uid)
{
  const auto material = std::find_if(scene.materials.begin(),
                                     scene.materials.end(),
                                     [&](const auto &m) { return m.session_id == uid; });
  if (material == scene.materials.end())
    return {1, 1};
  const auto index = std::size_t(material - scene.materials.begin());
  double area = 0, u_length = 0, v_length = 0;
  for (const auto &triangle : scene.uv_triangles) {
    if (triangle.material != index)
      continue;
    const auto &a = triangle.corners[0], &b = triangle.corners[1], &c = triangle.corners[2];
    const double u1 = b[0] - a[0], v1 = b[1] - a[1], u2 = c[0] - a[0], v2 = c[1] - a[1],
                 det = u1 * v2 - u2 * v1, weight = std::abs(det) * .5;
    if (weight < 1e-16)
      continue;
    double du = 0, dv = 0;
    for (int axis = 0; axis < 3; ++axis) {
      const double p1 = triangle.positions[1][axis] - triangle.positions[0][axis],
                   p2 = triangle.positions[2][axis] - triangle.positions[0][axis],
                   u = (p1 * v2 - p2 * v1) / det, v = (p2 * u1 - p1 * u2) / det;
      du += u * u;
      dv += v * v;
    }
    if (!std::isfinite(du) || !std::isfinite(dv))
      throw std::runtime_error("Nonfinite mesh UV metric");
    if (du < 1e-20 || dv < 1e-20)
      continue;
    area += weight;
    u_length += weight * du;
    v_length += weight * dv;
  }
  return area > 0 ? std::array<double, 2>{std::sqrt(u_length / area), std::sqrt(v_length / area)} :
                    std::array<double, 2>{1, 1};
}
}  // namespace bts
