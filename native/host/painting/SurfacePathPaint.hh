// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SurfacePathCurve.hh"
#include "StrokeSpacing.hh"
#include "ResourceAvailability.hh"
#include <functional>
#include <optional>
namespace bts {
// Reproject dense samples onto the actual mesh before spacing stamps by world arc length.
// The native host supplies the evaluated mesh contact; the document never owns a BVH.
inline std::vector<SurfacePathDisplaySample> dense_surface_path(
    const SurfacePath &path,
    double extent,
    const std::function<std::optional<SurfacePathPoint>(const SurfacePathPoint &)> &project)
{
  path.validate();
  if (!std::isfinite(extent) || extent <= 0)
    throw std::invalid_argument("Invalid surface path extent");
  const ResourceAvailability memory;
  std::vector<SurfacePathDisplaySample> dense;
  double minimum_pressure = 1;
  for (const auto &point : path.points)
    minimum_pressure = std::min(minimum_pressure, double(point.pressure));
  const double step = std::max(extent * 1e-5,
                               extent * path.brush.diameter_at(float(minimum_pressure)) / 100 *
                                   path.brush.spacing / 100 * .4);
  const std::size_t segments = path.closed ? path.points.size() : path.points.size() - 1;
  for (std::size_t segment = 0; segment < segments; ++segment) {
    const auto &a = path.points[segment], &b = path.points[(segment + 1) % path.points.size()];
    double length = path_length(path_delta(b.position, a.position));
    if (a.smooth || b.smooth || a.custom || b.custom) {
      const auto controls = surface_path_controls(path, segment);
      length = 0;
      for (int i = 1; i < 4; ++i)
        length += path_length(path_delta(controls[i], controls[i - 1]));
    }
    const double count = std::max(1., std::ceil(length / step));
    if (!std::isfinite(count) || count >= double(std::numeric_limits<std::size_t>::max()))
      throw std::overflow_error("Surface path sample count overflows address space");
    for (std::size_t i = segment ? 1 : 0; i <= std::size_t(count); ++i) {
      const double t = double(i) / count;
      auto point = surface_path_at(path, segment, t);
      auto hit = project(point);
      if (!hit)
        throw std::invalid_argument(
            "Surface path leaves the selected mesh; add an intermediate point");
      hit->pressure = point.pressure;
      dense.push_back({*hit, segment, t});
    }
  }
  return dense;
}
inline std::vector<SurfacePathPoint> sample_surface_path(
    const SurfacePath &path,
    double extent,
    const std::function<std::optional<SurfacePathPoint>(const SurfacePathPoint &)> &project)
{
  const ResourceAvailability memory;
  const auto samples = dense_surface_path(path, extent, project);
  std::vector<SurfacePathPoint> dense, result;
  dense.reserve(samples.size());
  for (const auto &sample : samples)
    dense.push_back(sample.point);
  if (dense.empty())
    return result;
  StrokeSpacing spacing;
  double arc = 0, last_arc = 0;
  // Walk dense projected segments, interpolating only between adjacent surface contacts.
  const auto add = [&](const SurfacePathPoint &point) {
    result.push_back(point);
    return std::max(extent * 1e-5,
                    extent * path.brush.diameter_at(point.pressure) / 100 * path.brush.spacing /
                        100);
  };
  spacing.reset({0, 0}, add(dense.front()));
  for (std::size_t i = 1; i < dense.size(); ++i) {
    const auto &a = dense[i - 1], &b = dense[i];
    last_arc = arc;
    double length = 0;
    for (int c = 0; c < 3; ++c)
      length += std::pow(b.position[c] - a.position[c], 2);
    arc += std::sqrt(length);
    spacing.advance({arc, 0}, [&](StrokeSpacing::Point position) {
      const double t = arc == last_arc ?
                           0 :
                           std::clamp((position[0] - last_arc) / (arc - last_arc), 0., 1.);
      auto point = a;
      double norm = 0;
      for (int c = 0; c < 3; ++c) {
        point.position[c] = std::lerp(a.position[c], b.position[c], t);
        point.normal[c] = std::lerp(a.normal[c], b.normal[c], t);
        norm += point.normal[c] * point.normal[c];
      }
      if (norm < 1e-12)
        throw std::invalid_argument("Invalid interpolated surface path normal");
      for (double &value : point.normal)
        value /= std::sqrt(norm);
      for (int c = 0; c < 2; ++c)
        point.uv[c] = std::lerp(a.uv[c], b.uv[c], t);
      point.pressure = float(std::lerp(a.pressure, b.pressure, t));
      return add(point);
    });
  }
  return result;
}
}  // namespace bts
