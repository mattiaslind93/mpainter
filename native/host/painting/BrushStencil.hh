#pragma once
#include "BrushAlpha.hh"
#include "LensProjection.hh"
#include <array>
#include <optional>
#include <stdexcept>
#include <vector>
namespace bts {
struct BrushStencil {
  std::optional<BrushAlpha> resource;
  int tiling = 0;  // None, horizontal, vertical, both.
  std::array<double, 2> offset{};
  double scale = 1, angle = 0;
  bool overlay = true;
  bool same_image(const BrushStencil &other) const
  {
    if (tiling != other.tiling || offset != other.offset || scale != other.scale ||
        angle != other.angle || bool(resource) != bool(other.resource))
      return false;
    return !resource || (resource->kind == other.resource->kind &&
                         resource->inverted == other.resource->inverted &&
                         resource->uniform == other.resource->uniform &&
                         resource->bitmap == other.resource->bitmap);
  }
  void validate() const
  {
    if (tiling < 0 || tiling > 3 || !std::isfinite(scale) || scale < .01 || scale > 100 ||
        !std::isfinite(angle) || std::abs(angle) > 36000)
      throw std::invalid_argument("Invalid stencil transform");
    for (auto v : offset)
      if (!std::isfinite(v) || std::abs(v) > 10000)
        throw std::invalid_argument("Invalid stencil offset");
  }
  std::optional<std::array<double, 2>> coordinates(std::array<double, 2> screen) const
  {
    const double radians = angle * 3.14159265358979323846 / 180, c = std::cos(radians),
                 s = std::sin(radians);
    const double x = screen[0] - offset[0], y = screen[1] - offset[1];
    const auto bitmap = resource ? resource->bitmap : nullptr;
    const double aspect = bitmap && bitmap->height ? double(bitmap->width) / bitmap->height : 1;
    double u = (c * x + s * y) / (scale * aspect), v = (-s * x + c * y) / scale;
    if (tiling & 1)
      u -= 2 * std::floor((u + 1) / 2);
    if (tiling & 2)
      v -= 2 * std::floor((v + 1) / 2);
    if (std::abs(u) > 1 || std::abs(v) > 1)
      return {};
    return std::array<double, 2>{u, v};
  }
  float sample(std::array<double, 2> screen) const
  {
    if (!resource)
      return 1;
    const auto uv = coordinates(screen);
    return uv ? resource->sample((*uv)[0], (*uv)[1], 1) : 0;
  }
};
struct StencilTriangle {
  std::array<std::array<double, 2>, 3> uv;
  std::array<std::array<double, 3>, 3> position;
};
struct StencilMapping {
  BrushStencil stencil;
  bool world = false;
  LensProjection lens;
  std::array<double, 16> projection{};  // Row-major clip-from-world.
  double viewport_aspect = 1, uv_extent = 1;
  std::array<double, 2> uv_center{.5, .5};
  std::vector<StencilTriangle> uv_world;
  std::optional<std::array<double, 2>> screen_world(std::array<double, 3> point) const
  {
    std::array<double, 4> clip{};
    for (int row = 0; row < 4; ++row) {
      clip[row] = projection[row * 4 + 3];
      for (int axis = 0; axis < 3; ++axis)
        clip[row] += projection[row * 4 + axis] * point[axis];
    }
    if (clip[3] <= 1e-12)
      return {};
    const auto displayed = lens.to_display(
        {(clip[0] / clip[3] + 1) * .5, (clip[1] / clip[3] + 1) * .5});
    if (!displayed || lens.alpha(*displayed) <= 0)
      return {};
    return std::array<double, 2>{(2 * (*displayed)[0] - 1) * viewport_aspect,
                                 2 * (*displayed)[1] - 1};
  }
  std::optional<std::array<double, 2>> screen_uv(std::array<double, 2> uv) const
  {
    if (!world)
      return std::array<double, 2>{2 * (uv[0] - uv_center[0]) / uv_extent,
                                   2 * (uv[1] - uv_center[1]) / uv_extent};
    // Scene painting with UV alignment still samples the stencil in camera space.
    for (const auto &triangle : uv_world) {
      const auto a = triangle.uv[0], b = triangle.uv[1], c = triangle.uv[2];
      const double x = b[0] - a[0], y = b[1] - a[1], u = c[0] - a[0], v = c[1] - a[1],
                   det = x * v - y * u;
      if (std::abs(det) < 1e-16)
        continue;
      const double w1 = ((uv[0] - a[0]) * v - (uv[1] - a[1]) * u) / det,
                   w2 = (x * (uv[1] - a[1]) - y * (uv[0] - a[0])) / det, w0 = 1 - w1 - w2;
      if (std::min({w0, w1, w2}) < -1e-7)
        continue;
      std::array<double, 3> point{};
      for (int axis = 0; axis < 3; ++axis)
        point[axis] = w0 * triangle.position[0][axis] + w1 * triangle.position[1][axis] +
                      w2 * triangle.position[2][axis];
      return screen_world(point);
    }
    return {};
  }
  float sample_world(std::array<double, 3> point) const
  {
    const auto screen = screen_world(point);
    return screen ? stencil.sample(*screen) : 0;
  }
  float sample_uv(std::array<double, 2> uv) const
  {
    const auto screen = screen_uv(uv);
    return screen ? stencil.sample(*screen) : 0;
  }
};
}  // namespace bts
