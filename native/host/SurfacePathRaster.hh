#include "ResourceAvailability.hh"
#pragma once
#include "SurfacePathPaint.hh"
#include "SurfaceBrush.hh"
#include "BrushJitterStamp.hh"
#include "StrokeDirection.hh"
#include "StrokeHistory.hh"
namespace bts {
struct SurfacePathInk {
  StrokeRaster primary;
  std::array<TilePaintPixels, 8> users;
  TilePaintPixels displacement;
};
using SurfacePathContact = std::function<std::optional<SurfaceBrushSample>(
    const SurfacePathPoint &, float, bool, const BrushProjection &)>;
inline SurfacePathInk raster_surface_path(const SurfacePath &path,
                                          double extent,
                                          int resolution,
                                          const std::array<int, 8> &user_resolutions,
                                          const std::set<int> &tiles,
                                          const SurfacePathContact &contact,
                                          std::shared_ptr<const QuickMaskField> mask = {},
                                          int displacement_resolution = 0)
{
  path.validate();
  double maximum_diameter = 0;
  for (const auto &point : path.points)
    maximum_diameter = std::max(maximum_diameter, double(path.brush.diameter_at(point.pressure)));
  if (maximum_diameter <= 0 || path.brush.flow <= 0 || path.brush.stroke_opacity <= 0)
    return {};
  const auto samples = sample_surface_path(
      path, extent, [&](const auto &point) -> std::optional<SurfacePathPoint> {
        const auto hit = contact(point, 100, false, path.brush.projection);
        if (!hit)
          return {};
        auto result = point;
        for (int c = 0; c < 3; ++c)
          result.position[c] = hit->position[c];
        result.uv = hit->footprint.center;
        return result;
      });
  SurfacePathInk output;
  JitterSampler random(path.seed);
  DynamicStrokeSampler dynamic(path.seed);
  auto copies = symmetry_transforms(path.symmetry);
  copies.insert(copies.begin(), SymmetryTransform{});
  std::size_t total = 0;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto &point = samples[index];
    const auto jitter = random.sample(path.brush.jitter);
    const float diameter = path.brush.diameter_at(point.pressure) * float(jitter.size);
    if (diameter <= 0)
      continue;
    auto alpha = path.brush.alpha;
    if (alpha.kind == AlphaKind::DynamicOutline)
      alpha.dynamic.phase = dynamic.phase(alpha.dynamic);
    for (std::size_t copy_index = 0; copy_index < copies.size(); ++copy_index) {
      const auto &copy = copies[copy_index];
      ++total;
      auto transformed = point;
      transformed.position = copy.point(point.position);
      transformed.normal = copy.vector(point.normal);
      auto projection = path.brush.projection;
      projection.orientation = copy.orientation;
      projection.symmetry_ray = copy.orientation < 0;
      auto hit = contact(transformed, diameter, true, projection);
      if (!hit) {
        if (copy_index == 0)
          throw std::invalid_argument("Surface path lost its mesh contact");
        continue;
      }
      if (jitter.position != std::array<double, 2>{}) {
        auto shifted = transformed;
        if (hit->footprint.surface) {
          const auto &surface = *hit->footprint.surface;
          for (int c = 0; c < 3; ++c)
            shifted.position[c] = hit->position[c] +
                                  hit->radius * (jitter.position[0] * surface.tangent[c] +
                                                 jitter.position[1] * surface.bitangent[c]);
        }
        else {
          auto tangent = std::array<double, 3>{1, 0, 0};
          double dot = transformed.normal[0];
          for (int c = 0; c < 3; ++c)
            tangent[c] -= dot * transformed.normal[c];
          double length = 0;
          for (double v : tangent)
            length += v * v;
          if (length < 1e-12) {
            tangent = {0, 1, 0};
            dot = transformed.normal[1];
            for (int c = 0; c < 3; ++c)
              tangent[c] -= dot * transformed.normal[c];
            length = 0;
            for (double v : tangent)
              length += v * v;
          }
          for (double &v : tangent)
            v /= std::sqrt(length);
          const auto &n = transformed.normal;
          const std::array<double, 3> bitangent{n[1] * tangent[2] - n[2] * tangent[1],
                                                n[2] * tangent[0] - n[0] * tangent[2],
                                                n[0] * tangent[1] - n[1] * tangent[0]};
          for (int c = 0; c < 3; ++c)
            shifted.position[c] = hit->position[c] +
                                  hit->radius * (jitter.position[0] * tangent[c] +
                                                 jitter.position[1] * bitangent[c]);
        }
        auto scattered = contact(shifted, diameter, true, projection);
        if (!scattered)
          continue;
        hit = std::move(scattered);
      }
      auto stamp = hit->footprint;
      stamp.alpha = alpha;
      stamp.hardness = path.brush.hardness;
      stamp.flow = path.brush.flow_at(point.pressure) / 100 * float(jitter.flow);
      stamp.values = path.brush.values;
      stamp.channels = path.brush.channels;
      stamp.coverage_mask = mask;
      double angle = path.brush.angle * 3.14159265358979323846 / 180 + jitter.angle;
      if (path.brush.follow_path) {
        std::array<double, 3> delta{};
        if (index + 1 < samples.size())
          for (int c = 0; c < 3; ++c)
            delta[c] = samples[index + 1].position[c] - point.position[c];
        else if (index)
          for (int c = 0; c < 3; ++c)
            delta[c] = point.position[c] - samples[index - 1].position[c];
        delta = copy.vector(delta);
        if (stamp.surface) {
          double x = 0, y = 0;
          for (int c = 0; c < 3; ++c) {
            x += delta[c] * stamp.surface->tangent[c];
            y += delta[c] * stamp.surface->bitangent[c];
          }
          if (x * x + y * y > 1e-20)
            angle += std::atan2(y, x);
        }
        else if (index + 1 < samples.size())
          angle += std::atan2(samples[index + 1].uv[1] - point.uv[1],
                              samples[index + 1].uv[0] - point.uv[0]);
        else if (index)
          angle += std::atan2(point.uv[1] - samples[index - 1].uv[1],
                              point.uv[0] - samples[index - 1].uv[0]);
      }
      rotate_brush_stamp(stamp, angle);
      output.primary.paint.stamp(resolution, stamp, tiles);
      if (path.brush.displacement) {
        auto scalar = stamp;
        scalar.channels = {false, false, true, false, false};
        scalar.values.roughness = path.brush.displacement_value;
        output.displacement.stamp(
            displacement_resolution ? displacement_resolution : resolution, scalar, tiles);
      }
      if (path.brush.ao) {
        auto scalar = stamp;
        scalar.channels = {false, false, true, false, false};
        scalar.values.roughness = path.brush.values.ambient_occlusion;
        output.primary.ao.stamp(resolution, scalar, tiles);
      }
      for (int slot = 0; slot < 8; ++slot)
        if (path.brush.users[slot]) {
          auto scalar = stamp;
          scalar.channels = {false, false, true, false, false};
          scalar.values.roughness = path.brush.user_values[slot];
          output.users[slot].stamp(user_resolutions[slot], scalar, tiles);
        }
    }
  }
  return output;
}
}  // namespace bts
