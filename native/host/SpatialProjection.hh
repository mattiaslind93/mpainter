#pragma once
#include "PlanarProjection.hh"
#include "WarpProjection.hh"
namespace bts {
inline std::array<double, 2> fill_spherical_uv(const std::array<double, 3> &q)
{
  const double radius = std::hypot(q[0], q[1], q[2]);
  if (radius < 1e-12)
    return {.5, .5};
  // App convention: native Z is up; the longitude seam is on positive Y.
  return {.5 + std::atan2(q[0], -q[1]) / (2 * std::numbers::pi),
          .5 + std::asin(std::clamp(q[2] / radius, -1., 1.)) / std::numbers::pi};
}
inline std::array<float, 4> fill_spherical_sample(const FillProjection &p,
                                                  const FillSurfacePoint &surface,
                                                  const FillSurfaceAtlas &atlas,
                                                  bool resource = true)
{
  if (!surface.valid || atlas.extent <= 0)
    return {};
  const auto frame = fill_planar_frame(p, surface, atlas);
  const double radius = std::hypot(frame.position[0], frame.position[1], frame.position[2]);
  if (p.shape_crop && radius > .5)
    return {};
  if (!resource || (!p.atom && !p.texture))
    return {1, 1, 1, 1};
  auto mapping = p;
  mapping.physical = false;
  std::array<float, 4> result{};
  const int samples = p.filtering == 0 ? 4 : 1;
  // Bounded four-sample filtering in angular space. Pole and center limits are
  // stable and do not change the projection volume when UV tiling changes.
  const auto center = fill_spherical_uv(frame.position);
  const double footprint = surface.footprint /
                           std::max(1e-8, std::abs(frame.denominators[0]) * std::max(radius, .01));
  for (int sample = 0; sample < samples; ++sample) {
    const double dx = samples == 1 ? 0 : (sample & 1 ? .25 : -.25),
                 dy = samples == 1 ? 0 : (sample & 2 ? .25 : -.25);
    const auto uv = fill_uv_map(mapping,
                                center[0] + dx * footprint / (2 * std::numbers::pi),
                                center[1] + dy * footprint / std::numbers::pi);
    const auto pixel = fill_planar_resource(p, uv[0], uv[1]);
    for (int c = 0; c < 3; ++c)
      result[c] += pixel[c] * pixel[3] / samples;
    result[3] += pixel[3] / samples;
  }
  if (result[3] > 1e-8f)
    for (int c = 0; c < 3; ++c)
      result[c] /= result[3];
  return result;
}
inline std::array<double, 2> fill_cylindrical_uv(const std::array<double, 3> &q, double angle)
{
  const double theta = std::hypot(q[0], q[1]) < 1e-12 ? 0 : std::atan2(q[0], -q[1]);
  return {.5 + theta / (angle * std::numbers::pi / 180), q[2] + .5};
}
inline float fill_cylindrical_coverage(const FillProjection &p, const PlanarFrame &f)
{
  const double radius = std::hypot(f.position[0], f.position[1]);
  const auto uv = fill_cylindrical_uv(f.position, p.cylinder_angle);
  if (p.shape_crop && (radius > .5 || std::abs(f.position[2]) > .5 || uv[0] < 0 || uv[0] > 1))
    return 0;
  if (!p.backface_culling)
    return 1;
  // Radial surface normals accept outward sides and reject caps/back faces.
  if (radius < 1e-12)
    return 0;
  const double facing = (f.normal[0] * f.position[0] + f.normal[1] * f.position[1]) / radius;
  const double threshold = std::cos(p.backface_angle * std::numbers::pi / 180);
  if (p.backface_angle < 1e-6)
    return facing >= 1 - 1e-8 ? 1 : 0;
  return float(std::clamp(
      (facing - threshold) / std::max(1e-8, (1 - threshold) * (1 - p.backface_hardness)), 0., 1.));
}
inline std::array<float, 4> fill_cylindrical_sample(const FillProjection &p,
                                                    const FillSurfacePoint &surface,
                                                    const FillSurfaceAtlas &atlas,
                                                    bool resource = true)
{
  if (!surface.valid || atlas.extent <= 0)
    return {};
  const auto frame = fill_planar_frame(p, surface, atlas);
  const float coverage = fill_cylindrical_coverage(p, frame);
  if (coverage <= 0)
    return {};
  if (!resource || (!p.atom && !p.texture))
    return {1, 1, 1, coverage};
  auto mapping = p;
  mapping.physical = false;
  std::array<float, 4> result{};
  const int samples = p.filtering == 0 ? 4 : 1;
  const auto center = fill_cylindrical_uv(frame.position, p.cylinder_angle);
  const double radius = std::max(.01, std::hypot(frame.position[0], frame.position[1])),
               du = surface.footprint / (std::max(1e-8,
                                                  std::min(std::abs(frame.denominators[0]),
                                                           std::abs(frame.denominators[1]))) *
                                         radius * p.cylinder_angle * std::numbers::pi / 180),
               dv = surface.footprint / std::max(1e-8, std::abs(frame.denominators[2]));
  for (int sample = 0; sample < samples; ++sample) {
    const double dx = samples == 1 ? 0 : (sample & 1 ? .25 : -.25),
                 dy = samples == 1 ? 0 : (sample & 2 ? .25 : -.25);
    const auto uv = fill_uv_map(mapping, center[0] + dx * du, center[1] + dy * dv);
    const auto pixel = fill_planar_resource(p, uv[0], uv[1]);
    for (int c = 0; c < 3; ++c)
      result[c] += pixel[c] * pixel[3] / samples;
    result[3] += pixel[3] / samples;
  }
  if (result[3] > 1e-8f)
    for (int c = 0; c < 3; ++c)
      result[c] /= result[3];
  result[3] *= coverage;
  return result;
}
inline std::array<float, 4> fill_spatial_sample_base(const FillProjection &p,
                                                     const FillSurfacePoint &surface,
                                                     const FillSurfaceAtlas &atlas,
                                                     bool resource = true)
{
  if (p.mode == 5)
    return fill_warp_sample(p, surface, atlas, resource);
  if (p.mode == 4)
    return fill_cylindrical_sample(p, surface, atlas, resource);
  if (p.mode == 3)
    return fill_spherical_sample(p, surface, atlas, resource);
  return fill_planar_sample(p, surface, atlas, resource);
}
inline std::array<float, 4> fill_spatial_sample(const FillProjection &p,
                                                const FillSurfacePoint &surface,
                                                const FillSurfaceAtlas &atlas,
                                                bool resource = true)
{
  if (!fill_surface_projection(p) || !p.symmetry || !p.symmetry->constraint.enabled)
    return fill_spatial_sample_base(p, surface, atlas, resource);
  auto mapping = p;
  std::array<double, 3> origin;
  for (int i = 0; i < 3; ++i)
    origin[i] = atlas.center[i] + p.offset3[i] * atlas.extent;
  const auto folded = fill_symmetric_surface(mapping, surface, origin);
  return fill_spatial_sample_base(mapping, folded, atlas, resource);
}
}  // namespace bts
