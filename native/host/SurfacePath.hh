// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceAvailability.hh"
#include "BrushSettings.hh"
#include "RibbonSettings.hh"
#include "Symmetry.hh"
#include <string>
#include <vector>
namespace bts {
struct SurfacePathPoint {
  std::array<double, 3> position{}, normal{0, 0, 1};
  std::array<double, 2> uv{};
  float pressure = 1;
  bool smooth = false, custom = false, broken = false;
  std::array<double, 3> tangent_in{}, tangent_out{};
  bool operator==(const SurfacePathPoint &) const = default;
};
struct SurfacePath {
  std::string id, name = "Path 1";
  bool visible = true, closed = false, filled = false;
  std::uint64_t seed = 0;
  float bump_origin = .5f;  // Source working coordinate; v1-5 recipes used signed zero.
  BrushSettings brush;
  RibbonSettings ribbon;
  SymmetrySettings symmetry;
  std::vector<SurfacePathPoint> points;
  void validate() const
  {
    if (bump_origin != 0 && bump_origin != .5f)
      throw std::invalid_argument("Invalid path Bump coordinate");
    symmetry.validate();
    if (ribbon.enabled)
      ribbon.validate();
    if (filled && ribbon.enabled)
      throw std::invalid_argument("A path cannot be both Filled and Ribbon");
    const auto fail = []() { throw std::invalid_argument("Invalid surface path"); };
    if (id.size() != 36 || name.empty() || name.find(char(0)) != std::string::npos ||
        points.size() < 2)
      fail();

    for (int i = 0; i < 36; ++i) {
      if (i == 8 || i == 13 || i == 18 || i == 23) {
        if (id[i] != '-')
          fail();
      }
      else if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
        fail();
    }
    const auto range = [&](double value, double low, double high) {
      if (!std::isfinite(value) || value < low || value > high)
        fail();
    };
    for (const auto &point : points) {
      double normal = 0;
      for (int c = 0; c < 3; ++c) {
        range(point.position[c], -1e12, 1e12);
        range(point.normal[c], -1, 1);
        normal += point.normal[c] * point.normal[c];
      }
      if (normal < .99 || normal > 1.01)
        fail();
      for (double uv : point.uv)
        range(uv, -10000, 10000);
      range(point.pressure, 0, 1);
      for (const auto &tangent : {point.tangent_in, point.tangent_out}) {
        double length = 0, dot = 0;
        for (int c = 0; c < 3; ++c) {
          range(tangent[c], -1e12, 1e12);
          length += tangent[c] * tangent[c];
          dot += tangent[c] * point.normal[c];
        }
        if (point.custom && std::abs(dot) > 1e-5 * std::max(1., std::sqrt(length)))
          throw std::invalid_argument("Path handles must lie in their vertex normal plane");
      }
    }
    if (brush.erase || brush.smudge || brush.clone || brush.projected || brush.stencil.resource ||
        brush.alpha.kind == AlphaKind::Bitmap)
      throw std::invalid_argument(
          "Surface paths currently use built-in paint alphas without a stencil");
    range(brush.displacement_value, -1, 1);
    range(brush.diameter, .1, 100);
    range(brush.flow, 0, 100);
    range(brush.stroke_opacity, 0, 100);
    range(brush.spacing, 1, 500);
    range(brush.angle, -180, 180);
    range(brush.hardness, 0, 1);
    range(brush.minimum_size, 0, 100);
    range(brush.minimum_flow, 0, 100);
    range(brush.alpha.uniform, 0, 1);
    range(brush.mask_value, 0, 1);
    brush.jitter.validate();
    brush.alpha.dynamic.validate();
    if (int(brush.projection.alignment) < 0 || int(brush.projection.alignment) > 3)
      fail();
    range(brush.projection.angle, 0, 90);
    for (int c = 0; c < 3; ++c)
      range(brush.values.base_color[c], -65504, 65504);
    range(brush.values.base_color[3], 0, 1);
    range(brush.values.metallic, 0, 1);
    range(brush.values.roughness, 0, 1);
    range(brush.values.bump, -65504, 65504);
    range(brush.values.ambient_occlusion, 0, 1);
    for (float value : brush.values.normal)
      range(value, -1, 1);
    for (float value : brush.user_values)
      range(value, -65504, 65504);
  }
};
inline SurfacePath surface_path_in_origin(SurfacePath path, float origin)
{
  if (origin != 0 && origin != .5f)
    throw std::invalid_argument("Invalid target Bump coordinate");
  path.brush.values.bump += origin - path.bump_origin;
  path.bump_origin = origin;
  return path;
}

}  // namespace bts
