// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FillTexture.hh"
#include "DiskScratch.hh"
#include "AnchorReference.hh"
#include "FillSymmetry.hh"
#include "WarpGrid.hh"
#include "WarpGeometry.hh"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <numbers>
#include <map>
#include <vector>
#include <stdexcept>
namespace bts {
struct NativeMaterialDescription;
// Editable mapping and source parameters, separate from the generated channel image.
struct FillProjection {
  std::array<double, 2> scale{1, 1}, offset{0, 0};
  bool linked = true, manipulator = true;
  double rotation = 0;
  std::shared_ptr<const FillTexture> texture;  // Legacy Base Color slot.
  using ChannelTextures = std::array<std::shared_ptr<const FillTexture>, 14>;
  std::shared_ptr<ChannelTextures> channel_textures;  // Stable authored IDs1..14; shared mapping.
  // File alpha is opt-in for data channels. Generated flattened layers opt in explicitly.
  std::array<bool, 15> alpha_mask{true};
  std::array<AnchorReference, 15>
      anchors{};        // Matching-UV live sources, exclusive with image/Atom.
  float bump_bias = 0;  // Per-binding source-coordinate conversion; shared pixels stay immutable.
  bool sample_alpha = true;          // Evaluation-only active-channel policy, never serialized.
  bool legacy_alpha_policy = false;  // Resolve generated flattened maps from pre-64 projects.
  bool atom = false, invert = false, border_dots = true, middle_dot = true;
  double border_width = .9;
  int mode = 0;  // UV / triplanar / planar / spherical / cylindrical / Warp / Match per UV Tile.
  std::shared_ptr<const WarpGrid> warp;
  std::shared_ptr<const WarpGeometrySettings> warp_geometry;
  mutable std::shared_ptr<const WarpGeometryCache>
      warp_fit;  // Owner-prepared derived surface, never serialized.
  double warp_depth = 1, warp_handle_size = 6;
  bool warp_edit = false, warp_surface = true;
  std::array<float, 3> warp_color{.08f, .45f, .7f};  // Scene-linear picker color for the HUD.
  int filtering = 0, wrap = 0;  // HQ / Sharp / Nearest; Repeat / None / Horizontal / Vertical.
  bool shape_crop = false, depth_culling = true, backface_culling = false;
  double cylinder_angle = 360;  // Product default; angular circumference in degrees.
  double depth_hardness = .75, backface_angle = 90, backface_hardness = .5;
  double hardness = .5;
  std::array<double, 3> offset3{0, 0, 0}, rotation3{0, 0, 0}, scale3{1, 1, 1};
  bool linked3 = false;
  int gizmo = 0;  // Translate / Rotate / Scale; presentation only.
  double gizmo_size = .4;
  bool physical = false, custom_size = true;
  std::array<double, 3> asset_size_cm{0, 0, 0}, custom_size_cm{10, 10, 10};
  // Runtime geometry/unit context. Never serialized as authored transforms.
  std::array<double, 2> native_per_uv{1, 1};
  double cm_per_native = 1;
  std::shared_ptr<const FillSymmetry> symmetry;
  std::shared_ptr<const NativeMaterialDescription>
      native_material;  // Authored values only, no Main/GPU pointers.
  std::string
      material_resource;  // Packed native PBR material identity; empty is split input mode.
};
inline bool fill_surface_projection(const FillProjection &p)
{
  return p.mode >= 1 && p.mode <= 5;
}
inline bool fill_spatial_projection(const FillProjection &p)
{
  return p.mode >= 2 && p.mode <= 5;
}
inline std::array<float, 4> fill_tile_match_sample(
    const FillProjection &p, int x, int y, int width, int height, int tile)
{
  if (!p.texture)
    return {1, 1, 1, 1};
  auto texture = p.texture;
  if (texture->tiles) {
    const auto member = texture->tiles->find(tile);
    if (member == texture->tiles->end())
      return {};
    texture = member->second;
  }
  if (p.filtering == 2) {
    if (!texture->pixels)
      throw std::runtime_error("Fill texture is not resolved from the packed project resource");
    const int tx = std::clamp(
                  int(std::floor((x + .5) * texture->width / width)), 0, texture->width - 1),
              ty = std::clamp(
                  int(std::floor((y + .5) * texture->height / height)), 0, texture->height - 1);
    auto value = (*texture->pixels)[std::size_t(ty) * texture->width + tx];
    if (!p.sample_alpha)
      value[3] = 1;
    return value;
  }
  std::array<float, 4> result{};
  for (double dx : {.25, .75})
    for (double dy : {.25, .75}) {
      const auto value = texture->sample(
          (x + dx) / width, (y + dy) / height, false, p.sample_alpha);
      for (int c = 0; c < 3; ++c)
        result[c] += value[c] * value[3] * .25f;
      result[3] += value[3] * .25f;
    }
  if (result[3] > 1e-8f)
    for (int c = 0; c < 3; ++c)
      result[c] /= result[3];
  return result;
}
inline const std::shared_ptr<const FillTexture> &fill_channel_texture(const FillProjection &p,
                                                                      int channel)
{
  static const std::shared_ptr<const FillTexture> empty;
  return channel == 0       ? p.texture :
         p.channel_textures ? p.channel_textures->at(channel - 1) :
                              empty;
}
inline std::shared_ptr<const FillTexture> &fill_channel_texture(FillProjection &p, int channel)
{
  if (channel == 0)
    return p.texture;
  if (!p.channel_textures)
    p.channel_textures = std::make_shared<FillProjection::ChannelTextures>();
  else if (!(p.channel_textures.use_count() == 1))
    p.channel_textures = std::make_shared<FillProjection::ChannelTextures>(*p.channel_textures);
  return p.channel_textures->at(channel - 1);
}
inline bool fill_channel_bound(const FillProjection &p, int channel)
{
  return bool(fill_channel_texture(p, channel)) || !p.anchors.at(channel).source.isNull();
}
inline bool fill_has_anchors(const FillProjection &p)
{
  return std::any_of(
      p.anchors.begin(), p.anchors.end(), [](const auto &ref) { return !ref.source.isNull(); });
}
inline bool fill_has_texture(const FillProjection &p)
{
  return bool(p.texture) ||
         (p.channel_textures && std::any_of(p.channel_textures->begin(),
                                            p.channel_textures->end(),
                                            [](const auto &texture) { return bool(texture); }));
}
inline std::array<double, 3> fill_physical_size(const FillProjection &p)
{
  return p.custom_size ? p.custom_size_cm : p.asset_size_cm;
}
inline std::array<double, 2> fill_effective_uv_scale(const FillProjection &p)
{
  auto scale = p.scale;
  if (p.physical && p.mode == 0) {
    const auto size = fill_physical_size(p);
    for (int i = 0; i < 2; ++i)
      scale[i] *= p.native_per_uv[i] * p.cm_per_native / size[i];
  }
  return scale;
}
inline void validate_fill_projection(const FillProjection &p)
{
  for (int channel = 0; channel < 15; ++channel) {
    const auto &ref = p.anchors[channel];
    if (ref.channel < 0 || ref.channel > 14 || ref.component < 0 || ref.component > 4)
      throw std::invalid_argument("Invalid fill anchor extraction");
    if (!ref.source.isNull() && (fill_channel_texture(p, channel) || (channel == 0 && p.atom)))
      throw std::invalid_argument("Choose one fill channel resource");
  }
  if (p.symmetry)
    p.symmetry->validate();
  if (p.warp)
    p.warp->validate();
  if (p.warp_geometry)
    p.warp_geometry->validate();
  if (!std::isfinite(p.warp_depth) || p.warp_depth < .0001 || p.warp_depth > 1000 ||
      !std::isfinite(p.warp_handle_size) || p.warp_handle_size < 2 || p.warp_handle_size > 24)
    throw std::invalid_argument("Invalid Warp settings");
  for (float color : p.warp_color)
    if (!std::isfinite(color) || color < 0 || color > 65504)
      throw std::invalid_argument("Invalid Warp grid color");
  if (!std::isfinite(p.cylinder_angle) || p.cylinder_angle < 1 || p.cylinder_angle > 360)
    throw std::invalid_argument("Invalid cylinder angle");
  if (p.filtering < 0 || p.filtering > 2 || p.wrap < 0 || p.wrap > 3 ||
      !std::isfinite(p.depth_hardness) || p.depth_hardness < 0 || p.depth_hardness > 1 ||
      !std::isfinite(p.backface_angle) || p.backface_angle < 0 || p.backface_angle > 180 ||
      !std::isfinite(p.backface_hardness) || p.backface_hardness < 0 || p.backface_hardness > 1)
    throw std::invalid_argument("Invalid spatial projection options");
  if (p.channel_textures)
    for (const auto &texture : *p.channel_textures)
      if (texture)
        texture->validate();
  if (p.texture) {
    p.texture->validate();
    if (p.atom)
      throw std::invalid_argument("Choose one fill resource");
  }
  for (double v : p.scale)
    if (!std::isfinite(v) || std::abs(v) < .001 || std::abs(v) > 1000)
      throw std::invalid_argument("Invalid fill UV scale");
  for (double v : p.offset)
    if (!std::isfinite(v) || std::abs(v) > 1000)
      throw std::invalid_argument("Invalid fill UV offset");
  if (!std::isfinite(p.rotation) || std::abs(p.rotation) > 360000 ||
      !std::isfinite(p.border_width) || p.border_width < 0 || p.border_width > 1)
    throw std::invalid_argument("Invalid fill projection parameters");
  if (p.mode < 0 || p.mode > 6 || p.gizmo < 0 || p.gizmo > 2 || !std::isfinite(p.hardness) ||
      p.hardness < 0 || p.hardness > 1 || !std::isfinite(p.gizmo_size) || p.gizmo_size < .01 ||
      p.gizmo_size > 10)
    throw std::invalid_argument("Invalid projection settings");
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(p.offset3[i]) || std::abs(p.offset3[i]) > 1e6 ||
        !std::isfinite(p.rotation3[i]) || std::abs(p.rotation3[i]) > 360000 ||
        !std::isfinite(p.scale3[i]) || std::abs(p.scale3[i]) < .001 ||
        std::abs(p.scale3[i]) > 1000)
      throw std::invalid_argument("Invalid 3D projection transform");
  if (p.linked3 && (p.scale3[0] != p.scale3[1] || p.scale3[1] != p.scale3[2]))
    throw std::invalid_argument("Linked 3D scales differ");
  if (p.linked && p.scale[0] != p.scale[1])
    throw std::invalid_argument("Linked fill scales differ");
  for (double v : p.custom_size_cm)
    if (!std::isfinite(v) || v < 1e-6 || v > 1e9)
      throw std::invalid_argument("Invalid custom physical material size");
  for (double v : p.asset_size_cm)
    if (!std::isfinite(v) || v < 0 || v > 1e9)
      throw std::invalid_argument("Invalid embedded material size");
  if (p.physical)
    for (double v : fill_physical_size(p))
      if (v <= 0)
        throw std::invalid_argument("Physical scaling requires nonzero material dimensions");
}
inline std::array<double, 2> fill_uv_map(const FillProjection &p, double u, double v)
{
  const double angle = -p.rotation * std::numbers::pi / 180, c = std::cos(angle),
               s = std::sin(angle), x = u - .5 - p.offset[0], y = v - .5 - p.offset[1];
  if (p.physical && p.mode == 0) {
    const auto size = fill_physical_size(p);
    const double px = x * p.native_per_uv[0] * p.cm_per_native,
                 py = y * p.native_per_uv[1] * p.cm_per_native;
    return {(c * px - s * py) * p.scale[0] / size[0] + .5,
            (s * px + c * py) * p.scale[1] / size[1] + .5};
  }
  const auto scale = fill_effective_uv_scale(p);
  return {(c * x - s * y) * scale[0] + .5, (s * x + c * y) * scale[1] + .5};
}
inline std::array<float, 4> fill_texture_uv_sample(
    const FillProjection &p, int x, int y, int resolution, int tile = 1001, int height = 0)
{
  if (!height)
    height = resolution;
  std::array<float, 4> sum{};
  for (double dx : {.25, .75})
    for (double dy : {.25, .75}) {
      const auto uv = fill_uv_map(
          p, (x + dx) / resolution + (tile - 1001) % 10, (y + dy) / height + (tile - 1001) / 10);
      auto pixel = p.texture->sample(uv[0], uv[1], true, p.sample_alpha);
      for (int c = 0; c < 3; ++c)
        sum[c] += pixel[c] * pixel[3] * .25f;
      sum[3] += pixel[3] * .25f;
    }
  if (sum[3] > 1e-8f)
    for (int c = 0; c < 3; ++c)
      sum[c] /= sum[3];
  return sum;
}
inline std::array<double, 2> fill_uv_frame(const FillProjection &p, double u, double v)
{
  const auto scale = fill_effective_uv_scale(p);
  const double angle = p.rotation * std::numbers::pi / 180, c = std::cos(angle),
               s = std::sin(angle), x = (u - .5) / scale[0], y = (v - .5) / scale[1];
  if (p.physical && p.mode == 0) {
    const auto size = fill_physical_size(p);
    const double px = (u - .5) * size[0] / p.scale[0], py = (v - .5) * size[1] / p.scale[1];
    return {(c * px - s * py) / (p.native_per_uv[0] * p.cm_per_native) + .5 + p.offset[0],
            (s * px + c * py) / (p.native_per_uv[1] * p.cm_per_native) + .5 + p.offset[1]};
  }
  return {c * x - s * y + .5 + p.offset[0], s * x + c * y + .5 + p.offset[1]};
}
inline float atom_pattern(const FillProjection &p, double u, double v)
{
  u -= std::floor(u);
  v -= std::floor(v);
  const double x = (u - .5) * 2, y = (v - .5) * 2;
  // Original analytic atom: three elliptical orbits, a nucleus and orbit dots.
  double distance = 1e9;
  for (int i = 0; i < 3; ++i) {
    const double a = i * std::numbers::pi / 3, c = std::cos(a), s = std::sin(a),
                 rx = c * x - s * y, ry = s * x + c * y;
    const double ellipse = std::hypot(rx / .82, ry / .31);
    distance = std::min(distance, std::abs(ellipse - 1) * .31);
  }
  float value = distance < (.008 + .035 * p.border_width) ? 1 : 0;
  if (p.middle_dot && std::hypot(x, y) < .09)
    value = 1;
  if (p.border_dots)
    for (int i = 0; i < 3; ++i) {
      const double a = i * std::numbers::pi * 2 / 3;
      if (std::hypot(x - .82 * std::cos(a), y - .82 * std::sin(a)) < .07)
        value = 1;
    }
  return p.invert ? 1 - value : value;
}
inline float fill_uv_sample(
    const FillProjection &p, int x, int y, int resolution, int tile = 1001, int height = 0)
{
  if (!height)
    height = resolution;
  float sum = 0;
  for (double dx : {.25, .75})
    for (double dy : {.25, .75}) {
      const auto uv = fill_uv_map(
          p, (x + dx) / resolution + (tile - 1001) % 10, (y + dy) / height + (tile - 1001) / 10);
      sum += atom_pattern(p, uv[0], uv[1]);
    }
  return sum * .25f;
}
}  // namespace bts

namespace bts {
struct FillSurfacePoint {
  std::array<float, 3> position{}, normal{0, 0, 1};
  float footprint = 0;
  bool valid = false;
  std::array<float, 3> baked_normal{.5, .5, 1}, source_color{};
  std::uint32_t match_object = 0;
};
struct FillSurfaceAtlas {
  int resolution = 0, height = 0;
  double extent = 1;
  std::array<double, 3> center{};
  std::map<int, ScratchVector<FillSurfacePoint>> tiles;
  std::map<int, std::vector<float>> curvature;
};
inline std::array<double, 3> fill_inverse_rotation(std::array<double, 3> v,
                                                   const std::array<double, 3> &angles)
{
  for (int axis = 2; axis >= 0; --axis) {
    const double a = -angles[axis] * std::numbers::pi / 180, c = std::cos(a), s = std::sin(a);
    const int x = (axis + 1) % 3, y = (axis + 2) % 3;
    const auto before = v;
    v[x] = c * before[x] - s * before[y];
    v[y] = s * before[x] + c * before[y];
  }
  return v;
}
// Canonical bitmap-triplanar numeric policy: the CPU evaluates the same
// packed Float32 frame/UV rows and texel fractions as the native GPU. No authored
// transform or resource format changes; UV and analytic Atom retain their paths.
struct FillBitmapTriplanarFrame {
  std::array<std::array<float, 3>, 3> rotation{};
  std::array<float, 3> offset{}, scale{}, inverse_denominator{}, map_u{}, map_v{};
  float hardness = 1;
};
inline float fill_bitmap_dot(const std::array<float, 3> &row, const std::array<float, 3> &v)
{
  return (row[0] * v[0] + row[1] * v[1]) + row[2] * v[2];
}
inline FillBitmapTriplanarFrame fill_bitmap_triplanar_frame(const FillProjection &p,
                                                            double extent,
                                                            double cm_per_native)
{
  FillBitmapTriplanarFrame frame;
  for (int column = 0; column < 3; ++column) {
    std::array<double, 3> basis{};
    basis[column] = 1;
    basis = fill_inverse_rotation(basis, p.rotation3);
    for (int row = 0; row < 3; ++row)
      frame.rotation[row][column] = float(basis[row]);
  }
  const auto size = fill_physical_size(p);
  for (int k = 0; k < 3; ++k) {
    frame.offset[k] = float(p.offset3[k]);
    frame.scale[k] = float(p.scale3[k]);
    frame.inverse_denominator[k] = float(
        1 / (p.scale3[k] * (p.physical ? size[k] / cm_per_native : extent)));
  }
  frame.hardness = float(1 + p.hardness * 15);
  auto mapping = p;
  mapping.offset = {0, 0};
  mapping.physical = false;
  const auto a = fill_uv_map(mapping, 0, 0), b = fill_uv_map(mapping, 1, 0),
             c = fill_uv_map(mapping, 0, 1);
  frame.map_u = {float(b[0] - a[0]), float(c[0] - a[0]), float(a[0])};
  frame.map_v = {float(b[1] - a[1]), float(c[1] - a[1]), float(a[1])};
  return frame;
}
inline FillSurfacePoint fill_symmetric_surface(FillProjection &mapping,
                                               const FillSurfacePoint &surface,
                                               const std::array<double, 3> &projection_origin)
{
  if (!fill_surface_projection(mapping) || !mapping.symmetry ||
      !mapping.symmetry->constraint.enabled)
    return surface;
  const auto folded = fold_fill_surface(mapping.symmetry.get(), surface, projection_origin);
  if (folded.copy & 1) {
    if (mapping.symmetry->flip_u)
      mapping.scale[0] *= -1;
    if (mapping.symmetry->flip_v)
      mapping.scale[1] *= -1;
  }
  return folded.surface;
}
inline float fill_triplanar_sample_base(const FillProjection &p,
                                        const FillSurfacePoint &surface,
                                        double extent)
{
  if (!surface.valid || extent <= 0)
    return 0;
  std::array<double, 3> position, normal;
  for (int i = 0; i < 3; ++i) {
    position[i] = surface.position[i] - p.offset3[i];
    normal[i] = surface.normal[i];
  }
  position = fill_inverse_rotation(position, p.rotation3);
  normal = fill_inverse_rotation(normal, p.rotation3);
  auto denominators = p.scale3;
  const auto size = fill_physical_size(p);
  for (int i = 0; i < 3; ++i) {
    denominators[i] *= p.physical ? size[i] / p.cm_per_native : extent;
    position[i] /= denominators[i];
    normal[i] *= p.scale3[i];
  }
  // App contract: normalized abs-normal weights sharpen continuously with Hardness.
  std::array<double, 3> weights;
  double total = 0;
  for (int i = 0; i < 3; ++i) {
    weights[i] = std::pow(std::abs(normal[i]), 1 + p.hardness * 15);
    total += weights[i];
  }
  if (total < 1e-20)
    return 0;
  auto mapping = p;
  mapping.offset = {0, 0};
  mapping.physical = false;
  float value = 0;
  for (int axis = 0; axis < 3; ++axis) {
    const int x = axis == 0 ? 1 : 0, y = axis == 2 ? 1 : 2;
    float plane = 0;
    for (double dx : {-.25, .25})
      for (double dy : {-.25, .25}) {
        const auto uv = fill_uv_map(mapping,
                                    position[x] + .5 + dx * surface.footprint / denominators[x],
                                    position[y] + .5 + dy * surface.footprint / denominators[y]);
        plane += atom_pattern(mapping, uv[0], uv[1]);
      }
    value += float(weights[axis] / total) * plane * .25f;
  }
  return value;
}
inline float fill_triplanar_sample(const FillProjection &p,
                                   const FillSurfacePoint &surface,
                                   double extent)
{
  if (!fill_surface_projection(p) || !p.symmetry || !p.symmetry->constraint.enabled)
    return fill_triplanar_sample_base(p, surface, extent);
  auto mapping = p;
  const auto folded = fill_symmetric_surface(mapping, surface, p.offset3);
  return fill_triplanar_sample_base(mapping, folded, extent);
}
}  // namespace bts

namespace bts {
inline std::array<float, 4> fill_texture_triplanar_sample_base(const FillProjection &p,
                                                               const FillSurfacePoint &surface,
                                                               double extent)
{
  std::array<float, 4> result{};
  if (!surface.valid || extent <= 0)
    return result;
  const auto frame = fill_bitmap_triplanar_frame(p, extent, p.cm_per_native);
  std::array<float, 3> delta, position, normal, weights;
  for (int k = 0; k < 3; ++k)
    delta[k] = surface.position[k] - frame.offset[k];
  for (int k = 0; k < 3; ++k) {
    position[k] = fill_bitmap_dot(frame.rotation[k], delta) * frame.inverse_denominator[k];
    normal[k] = fill_bitmap_dot(frame.rotation[k], surface.normal) * frame.scale[k];
    weights[k] = std::pow(std::abs(normal[k]), frame.hardness);
  }
  const float total = (weights[0] + weights[1]) + weights[2];
  if (total < 1e-20f)
    return result;
  for (int axis = 0; axis < 3; ++axis) {
    const int x = axis == 0 ? 1 : 0, y = axis == 2 ? 1 : 2;
    const float weight = (weights[axis] / total) * .25f;
    for (float dx : {-.25f, .25f})
      for (float dy : {-.25f, .25f}) {
        const std::array<float, 3> point{
            (position[x] + .5f) + (dx * surface.footprint) * frame.inverse_denominator[x],
            (position[y] + .5f) + (dy * surface.footprint) * frame.inverse_denominator[y],
            1};
        const auto pixel = p.texture->sample_projected(fill_bitmap_dot(frame.map_u, point),
                                                       fill_bitmap_dot(frame.map_v, point),
                                                       true,
                                                       p.sample_alpha);
        for (int c = 0; c < 3; ++c)
          result[c] += (pixel[c] * pixel[3]) * weight;
        result[3] += pixel[3] * weight;
      }
  }
  if (result[3] > 1e-8f)
    for (int c = 0; c < 3; ++c)
      result[c] /= result[3];
  return result;
}
inline std::array<float, 4> fill_texture_triplanar_sample(const FillProjection &p,
                                                          const FillSurfacePoint &surface,
                                                          double extent)
{
  if (!fill_surface_projection(p) || !p.symmetry || !p.symmetry->constraint.enabled)
    return fill_texture_triplanar_sample_base(p, surface, extent);
  auto mapping = p;
  const auto folded = fill_symmetric_surface(mapping, surface, p.offset3);
  return fill_texture_triplanar_sample_base(mapping, folded, extent);
}
}  // namespace bts
