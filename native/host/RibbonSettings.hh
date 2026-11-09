#include "ResourceAvailability.hh"
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace bts {
// Scene-linear ACEScg, unassociated RGBA. Resources are immutable and shared by Undo.
struct RibbonImage {
  int width = 0, height = 0;
  std::string name;
  std::vector<std::array<float, 4>> pixels;
  void validate() const
  {
    if (width < 1 || height < 1 || pixels.size() != std::size_t(width) * height || name.empty())
      throw std::invalid_argument("Invalid ribbon image dimensions or storage");
    for (const auto &pixel : pixels) {
      for (float v : pixel)
        if (!std::isfinite(v) || std::abs(v) > 65504)
          throw std::invalid_argument("Invalid ribbon image value");
      if (pixel[3] < 0 || pixel[3] > 1)
        throw std::invalid_argument("Invalid ribbon image coverage");
    }
  }
  std::array<float, 4> sample(double u, double v) const
  {
    const double x = std::clamp(u, 0., 1.) * width - .5, y = std::clamp(v, 0., 1.) * height - .5;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const auto at = [&](int a, int b) {
      auto p =
          pixels[std::size_t(std::clamp(b, 0, height - 1)) * width + std::clamp(a, 0, width - 1)];
      for (int c = 0; c < 3; ++c)
        p[c] *= p[3];
      return p;
    };
    const auto a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    std::array<float, 4> p;
    for (int k = 0; k < 4; ++k)
      p[k] = std::lerp(std::lerp(a[k], b[k], float(x - ix)),
                       std::lerp(c[k], d[k], float(x - ix)),
                       float(y - iy));
    if (p[3] > 1e-8f)
      for (int k = 0; k < 3; ++k)
        p[k] /= p[3];
    return p;
  }
};
inline std::shared_ptr<const RibbonImage> default_ribbon_image()
{
  static const auto image = []() {
    auto p = std::make_shared<RibbonImage>();
    p->width = 128;
    p->height = 32;
    p->name = "Chevron trim";
    p->pixels.resize(128 * 32);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 128; ++x) {
        const bool edge = y < 3 || y >= 29;
        const int chevron = (x + std::abs(y - 16) * 2) % 32;
        auto &v = p->pixels[y * 128 + x];
        v = edge         ? std::array<float, 4>{.03, .06, .09, 1} :
            chevron < 12 ? std::array<float, 4>{.85, .33, .025, 1} :
                           std::array<float, 4>{.08, .17, .25, 1};
        if (x < 8)
          v = {.7, .04, .02, 1};
        if (x >= 120)
          v = {.03, .5, .14, 1};
      }
    return p;
  }();
  return image;
}
enum class RibbonStretch { Stretch, KeepAspect };
enum class RibbonTiling { None, Auto, Custom };
enum class RibbonCorner { Miter, Round, Bevel, Cut };
enum class RibbonRatio { Width, Length };
enum class RibbonAlignment { Start, Center, End };
enum class RibbonAlpha { Normal, Max, Add };
struct RibbonSettings {
  bool enabled = false, flip = false, omit_ends = true, offsets_only = false, mirror = false,
       remove_clipped = false;
  RibbonStretch stretch = RibbonStretch::Stretch;
  RibbonTiling tiling = RibbonTiling::Auto;
  RibbonCorner corner = RibbonCorner::Miter;
  RibbonRatio ratio = RibbonRatio::Width;
  RibbonAlignment alignment = RibbonAlignment::Start;
  RibbonAlpha alpha = RibbonAlpha::Max;
  int orientation = 0, count = 1;
  double aspect = 1, start_offset = .1, end_offset = .1;
  std::shared_ptr<const RibbonImage> image = default_ribbon_image();
  void validate() const
  {
    const auto in = [](int v, int n) { return v >= 0 && v < n; };
    if (!in(int(stretch), 2) || !in(int(tiling), 3) || !in(int(corner), 4) || !in(int(ratio), 2) ||
        !in(int(alignment), 3) || !in(int(alpha), 3) || !in(orientation, 4) || count < 1 ||
        count > 256 || !std::isfinite(aspect) || aspect < .01 || aspect > 100 ||
        !std::isfinite(start_offset) || !std::isfinite(end_offset) || start_offset < 0 ||
        end_offset < 0 || start_offset + end_offset >= 1 || !image)
      throw std::invalid_argument("Invalid ribbon parameters");
    image->validate();
  }
  double image_aspect() const
  {
    const double value = double(image->width) / image->height;
    return (orientation % 2 ? 1 / value : value) * aspect;
  }
};
struct RibbonLayout {
  double period = 0, width = 0, start = 0, total = 0;
  int count = 0;
  bool preserve = false;
  double natural = 0;
  static RibbonLayout make(const RibbonSettings &s, double length, double width, bool closed)
  {
    RibbonLayout l;
    if (!std::isfinite(length) || !std::isfinite(width) || length <= 1e-12 || width <= 1e-12)
      return l;
    l.total = length;
    l.width = width;
    l.natural = width * s.image_aspect();
    l.count = s.tiling == RibbonTiling::None ?
                  1 :
              s.tiling == RibbonTiling::Custom ?
                  s.count :
                  std::max(1,
                           int(std::min(256.,
                                        (s.stretch == RibbonStretch::KeepAspect &&
                                                 s.ratio == RibbonRatio::Width ?
                                             std::ceil(length / l.natural) :
                                             std::round(length / l.natural)))));
    l.preserve = s.offsets_only && !(closed && s.omit_ends);
    if (s.stretch == RibbonStretch::Stretch) {
      l.period = length / l.count;
      return l;
    }
    l.period = l.natural;
    if (s.ratio == RibbonRatio::Length) {
      l.period = length / l.count;
      l.width = l.period / s.image_aspect();
    }
    const double span = l.period * l.count;
    l.start = (length - span) * (s.alignment == RibbonAlignment::Start  ? 0 :
                                 s.alignment == RibbonAlignment::Center ? .5 :
                                                                          1);
    return l;
  }
  std::optional<std::array<double, 2>> coordinates(const RibbonSettings &s,
                                                   double arc,
                                                   double across) const
  {
    if (period <= 0 || std::abs(across) > 1 || arc < 0 || arc > total)
      return {};
    double u = (arc - start) / period;
    int tile = int(std::floor(u));
    if (u == count) {
      tile = count - 1;
      u = double(count);
    }
    if (tile < 0 || tile >= count)
      return {};
    if (s.stretch == RibbonStretch::KeepAspect && s.remove_clipped &&
        s.ratio == RibbonRatio::Width &&
        (start + tile * period < -1e-8 || start + (tile + 1) * period > total + 1e-8))
      return {};
    u -= tile;
    if (preserve) {
      double a = s.start_offset * natural, b = s.end_offset * natural;
      if (a + b < period) {
        const double x = u * period;
        if (x < a && a > 0)
          u = x / natural;
        else if (x > period - b && b > 0)
          u = 1 - (period - x) / natural;
        else
          u = s.start_offset + (x - a) / (period - a - b) * (1 - s.start_offset - s.end_offset);
      }
    }
    if (s.mirror && (tile % 2))
      u = 1 - u;
    double v = (across + 1) * .5;
    if (s.flip)
      v = 1 - v;
    for (int i = 0; i < s.orientation; ++i) {
      const double x = u;
      u = v;
      v = 1 - x;
    }
    return std::array<double, 2>{u, v};
  }
};
}  // namespace bts
