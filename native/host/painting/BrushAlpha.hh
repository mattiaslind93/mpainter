// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DynamicStroke.hh"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
namespace bts {
struct AlphaBitmap {
  int width = 0, height = 0;
  std::string name;
  std::vector<float> pixels;  // Raw scalar data, bottom row first.
  float sample(double u, double v, bool bilinear = true) const
  {
    if (width < 1 || height < 1 || pixels.size() != std::size_t(width) * height)
      return 0;
    const double x = std::clamp(u, 0., 1.) * width - .5, y = std::clamp(v, 0., 1.) * height - .5;
    const auto at = [&](int px, int py) {
      return pixels[std::size_t(std::clamp(py, 0, height - 1)) * width +
                    std::clamp(px, 0, width - 1)];
    };
    if (!bilinear)
      return at(int(std::floor(x + .5)), int(std::floor(y + .5)));
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    return std::lerp(std::lerp(at(ix, iy), at(ix + 1, iy), float(x - ix)),
                     std::lerp(at(ix, iy + 1), at(ix + 1, iy + 1), float(x - ix)),
                     float(y - iy));
  }
};
enum class AlphaKind { Shape, Plus, Uniform, Bitmap, DynamicOutline };
struct BrushAlpha {
  AlphaKind kind = AlphaKind::Shape;
  DynamicStrokeParameters dynamic;
  bool inverted = false;
  float uniform = 1;
  std::shared_ptr<const AlphaBitmap> bitmap;
  std::string name() const
  {
    switch (kind) {
      case AlphaKind::DynamicOutline:
        return "Square To Circle";
      case AlphaKind::Shape:
        return "Shape";
      case AlphaKind::Plus:
        return "Sign Plus";
      case AlphaKind::Uniform:
        return "uniform color";
      case AlphaKind::Bitmap:
        return bitmap ? bitmap->name : "Missing bitmap";
    }
    return {};
  }
  float sample(double u, double v, float hardness = 0) const
  {
    if (std::abs(u) > 1 || std::abs(v) > 1)
      return 0;
    float value = 0;
    switch (kind) {
      case AlphaKind::DynamicOutline:
        value = dynamic.sample(u, v);
        break;
      case AlphaKind::Shape: {
        const double r = std::hypot(u, v);
        if (r >= 1)
          return 0;
        const double t = hardness == 1 ? 1 : std::clamp((1 - r) / (1 - hardness), 0., 1.);
        value = float(t * t * (3 - 2 * t));
        break;
      }
      case AlphaKind::Plus:
        value = std::abs(u) <= .22 || std::abs(v) <= .22 ? 1 : 0;
        break;
      case AlphaKind::Uniform:
        value = std::clamp(uniform, 0.f, 1.f);
        break;
      case AlphaKind::Bitmap:
        value = bitmap ? bitmap->sample((u + 1) * .5, (v + 1) * .5) : 0;
        break;
    }
    return inverted ? 1 - value : value;
  }
};
}  // namespace bts
