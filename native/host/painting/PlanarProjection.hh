// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FillProjection.hh"
namespace bts {
struct PlanarFrame {
  std::array<double, 3> position{}, normal{}, denominators{};
};
inline PlanarFrame fill_planar_frame(const FillProjection &p,
                                     const FillSurfacePoint &surface,
                                     const FillSurfaceAtlas &atlas)
{
  PlanarFrame f;
  for (int c = 0; c < 3; ++c) {
    f.position[c] = surface.position[c] - atlas.center[c] - p.offset3[c] * atlas.extent;
    f.normal[c] = surface.normal[c];
  }
  f.position = fill_inverse_rotation(f.position, p.rotation3);
  f.normal = fill_inverse_rotation(f.normal, p.rotation3);
  const auto size = fill_physical_size(p);
  for (int c = 0; c < 3; ++c) {
    const int physical_axis = c == 1 ? 2 : c == 2 ? 1 : 0;
    f.denominators[c] = p.scale3[c] *
                        (p.physical ? size[physical_axis] / p.cm_per_native : atlas.extent);
    f.position[c] /= f.denominators[c];
    f.normal[c] *= f.denominators[c];
  }
  double length = 0;
  for (double v : f.normal)
    length += v * v;
  if (length > 1e-20)
    for (double &v : f.normal)
      v /= std::sqrt(length);
  return f;
}
inline float fill_planar_coverage(const FillProjection &p, const PlanarFrame &f)
{
  const double depth = std::abs(f.position[1]);
  if (p.shape_crop && (std::abs(f.position[0]) > .5 || std::abs(f.position[2]) > .5 || depth > 1))
    return 0;
  double alpha = 1;
  if (p.depth_culling)
    alpha *= p.depth_hardness >= 1 ? (depth <= 1 ? 1 : 0) :
                                     std::clamp((1 - depth) / (1 - p.depth_hardness), 0., 1.);
  if (p.backface_culling) {
    const double threshold = std::cos(p.backface_angle * std::numbers::pi / 180),
                 facing = -f.normal[1];
    if (p.backface_angle < 1e-6)
      alpha *= facing >= 1 - 1e-8 ? 1 : 0;
    else {
      const double transition = std::max(1e-8, (1 - threshold) * (1 - p.backface_hardness));
      alpha *= std::clamp((facing - threshold) / transition, 0., 1.);
    }
  }
  return float(alpha);
}
inline std::array<float, 4> fill_planar_resource(const FillProjection &p, double u, double v)
{
  const bool repeat_u = p.wrap == 0 || p.wrap == 2, repeat_v = p.wrap == 0 || p.wrap == 3;
  if ((!repeat_u && (u < 0 || u > 1)) || (!repeat_v && (v < 0 || v > 1)))
    return {};
  if (p.texture) {
    const auto &texture = *p.texture;
    if (repeat_u)
      u -= std::floor(u);
    else
      u = std::clamp(u, .5 / texture.width, 1 - .5 / texture.width);
    if (repeat_v)
      v -= std::floor(v);
    else
      v = std::clamp(v, .5 / texture.height, 1 - .5 / texture.height);
    if (p.filtering == 2) {
      if (!texture.pixels)
        throw std::runtime_error("Projection texture resource is unresolved");
      auto value = texture.pixels->at(
          std::size_t(std::clamp(int(v * texture.height), 0, texture.height - 1)) * texture.width +
          std::clamp(int(u * texture.width), 0, texture.width - 1));
      if (!p.sample_alpha)
        value[3] = 1;
      return value;
    }
    return texture.sample(u, v, true, p.sample_alpha);
  }
  if (p.atom) {
    const float value = atom_pattern(p, u, v);
    return {value, value, value, 1};
  }
  return {1, 1, 1, 1};
}
inline std::array<float, 4> fill_planar_sample(const FillProjection &p,
                                               const FillSurfacePoint &surface,
                                               const FillSurfaceAtlas &atlas,
                                               bool resource = true)
{
  if (!surface.valid || atlas.extent <= 0)
    return {};
  const auto frame = fill_planar_frame(p, surface, atlas);
  const float coverage = fill_planar_coverage(p, frame);
  if (coverage <= 0)
    return {};
  if (!resource || (!p.atom && !p.texture))
    return {1, 1, 1, coverage};
  auto mapping = p;
  mapping.physical = false;
  std::array<float, 4> result{};
  const int samples = p.filtering == 0 ? 4 : 1;
  for (int sample = 0; sample < samples; ++sample) {
    const double dx = samples == 1 ? 0 : (sample & 1 ? .25 : -.25),
                 dy = samples == 1 ? 0 : (sample & 2 ? .25 : -.25);
    const auto uv = fill_uv_map(
        mapping,
        frame.position[0] + .5 + dx * surface.footprint / frame.denominators[0],
        frame.position[2] + .5 + dy * surface.footprint / frame.denominators[2]);
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
}  // namespace bts
