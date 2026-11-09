// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilterEffect.hh"
#include "LayerBlend.hh"
#include <cstdint>
#include <limits>
#include <vector>

namespace bts::diagnostic_spatial {
// Explicit diagnostic/export reference only. No document evaluator or interactive
// renderer calls this helper. Every operation reads an immutable preceding plane.
using Pixel = std::array<float, 4>;
struct Plane {
  int width = 0, height = 0;
  std::vector<Pixel> pixels;
  const Pixel &at(int x, int y) const
  {
    return pixels.at(std::size_t(y) * width + x);
  }
};
enum class Domain { Scalar, RGB, Mask };  // Scalar content uses replicated RGB.
inline void validate(const Plane &plane)
{
  if (plane.width <= 0 || plane.height <= 0 ||
      plane.pixels.size() != std::size_t(plane.width) * plane.height)
    throw std::invalid_argument("Invalid diagnostic spatial plane");
  for (const auto &pixel : plane.pixels)
    for (float value : pixel)
      if (!std::isfinite(value))
        throw std::invalid_argument("Nonfinite diagnostic spatial sample");
}
namespace detail {
// Independent implementation of the documented native value-noise construction.
// The production GLSL and NativeGrunge.hh are deliberately not called here.
inline float lattice(int x, int y, int octave)
{
  std::uint32_t bits = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u +
                       std::uint32_t(octave * 17) * 1442695041u;
  bits = (bits ^ (bits >> 13)) * 1274126177u;
  bits ^= bits >> 16;
  return float(bits & 65535u) / 65535.f;
}
inline float noise(float x, float y)
{
  float sum = 0, weight = .533333333f;
  for (int octave = 0; octave < 4; ++octave) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    float u = x - ix, v = y - iy;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    const float low = lattice(ix, iy, octave) * (1 - u) + lattice(ix + 1, iy, octave) * u;
    const float high = lattice(ix, iy + 1, octave) * (1 - u) + lattice(ix + 1, iy + 1, octave) * u;
    sum += weight * (low * (1 - v) + high * v);
    x = x * 2.03f + 3.1f;
    y = y * 2.03f + 1.7f;
    weight *= .5f;
  }
  return sum;
}
inline int displaced(int center, float offset, int extent)
{
  // Tests avoid exact half ties unless the production rounding policy is pinned.
  const double rounded = std::round(double(offset));
  if (!std::isfinite(rounded) || std::abs(rounded) > std::numeric_limits<int>::max() - extent)
    throw std::invalid_argument("Diagnostic displacement exceeds integer domain");
  return std::clamp(center + int(rounded), 0, extent - 1);
}
}  // namespace detail
inline Plane filter(const Plane &source,
                    const FilterSettings &settings,
                    Domain domain,
                    LayerBlend blend = LayerBlend::Normal,
                    float opacity = 1)
{
  validate(source);
  if (settings.resource < 7 || settings.resource > 10 || !std::isfinite(settings.radius) ||
      settings.radius < 1 || settings.radius > 4 || !std::isfinite(settings.value_scale) ||
      settings.value_scale < 0 || settings.value_scale > 4 || !std::isfinite(settings.hue) ||
      settings.hue < -1 || settings.hue > 1 || !std::isfinite(opacity) || opacity < 0 ||
      opacity > 1)
    throw std::invalid_argument("Invalid diagnostic spatial filter settings");
  if (domain == Domain::Scalar)
    for (const auto &pixel : source.pixels)
      if (pixel[0] != pixel[1] || pixel[0] != pixel[2])
        throw std::invalid_argument("Diagnostic scalar content must replicate RGB");
  if (blend == LayerBlend::Disabled || opacity == 0)
    return source;
  Plane result = source;
  const auto sample = [&](int x, int y) {
    const auto &p = source.at(std::clamp(x, 0, source.width - 1),
                              std::clamp(y, 0, source.height - 1));
    return domain == Domain::Mask ? std::array<float, 3>{p[0], p[0], p[0]} :
                                    std::array<float, 3>{p[0], p[1], p[2]};
  };
  for (int y = 0; y < source.height; ++y)
    for (int x = 0; x < source.width; ++x) {
      const auto center = sample(x, y);
      std::array<float, 3> filtered{};
      if (settings.resource <= 8) {
        const int radius = std::clamp(int(settings.radius + .5f), 1, 4);
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx) {
            const auto p = sample(x + dx, y + dy);
            for (int c = 0; c < 3; ++c)
              filtered[c] += p[c];
          }
        const float count = float((radius * 2 + 1) * (radius * 2 + 1));
        for (int c = 0; c < 3; ++c) {
          filtered[c] /= count;
          if (settings.resource == 8)
            filtered[c] = center[c] + (center[c] - filtered[c]);
        }
      }
      else {
        float dx, dy;
        if (settings.resource == 9) {
          const float u = (x + .5f) / source.width, v = (y + .5f) / source.height,
                      scale = settings.radius * 4;
          const float nx = detail::noise(u * scale, v * scale) * 2 - 1;
          const float ny = detail::noise((u + .37f) * scale, (v + .61f) * scale) * 2 - 1;
          const float angle = settings.hue * 6.28318530718f;
          dx = std::cos(angle) * nx - std::sin(angle) * ny;
          dy = std::sin(angle) * nx + std::cos(angle) * ny;
        }
        else {
          const auto left = sample(x - 1, y), right = sample(x + 1, y), down = sample(x, y - 1),
                     up = sample(x, y + 1);
          if (domain == Domain::Mask) {
            dx = right[0] - left[0];
            dy = up[0] - down[0];
          }
          else {
            constexpr float weights[3] = {.2126f, .7152f, .0722f};
            dx = dy = 0;
            for (int c = 0; c < 3; ++c) {
              dx += (right[c] - left[c]) * weights[c];
              dy += (up[c] - down[c]) * weights[c];
            }
          }
        }
        const int count = settings.resource == 10 ? 5 : 1;
        for (int i = 0; i < count; ++i) {
          const float amount = settings.value_scale * 12 * (count == 5 ? float(i) / 4 : 1);
          const auto p = sample(detail::displaced(x, dx * amount, source.width),
                                detail::displaced(y, dy * amount, source.height));
          for (int c = 0; c < 3; ++c)
            filtered[c] += p[c];
        }
        for (float &value : filtered)
          value /= count;
      }
      const auto mixed = domain == Domain::RGB ?
                             blend_rgb(blend, filtered, center) :
                             std::array<float, 3>{blend_value(blend, filtered[0], center[0]),
                                                  blend_value(blend, filtered[1], center[1]),
                                                  blend_value(blend, filtered[2], center[2])};
      auto &out = result.pixels[std::size_t(y) * source.width + x];
      for (int c = 0; c < 3; ++c)
        out[c] = center[c] * (1 - opacity) + mixed[c] * opacity;
      if (domain == Domain::Mask)
        out = {out[0], 0, 0, 1};  // Matches a raw mask stage, not resolved/clipped output.
    }
  return result;
}
inline Plane group(const Plane &parent, const Plane &children, LayerBlend blend, float opacity)
{
  // Both inputs are raw mask stage planes (R=value, G=B=0, A=1).
  validate(parent);
  validate(children);
  if (parent.width != children.width || parent.height != children.height ||
      !std::isfinite(opacity) || opacity < 0 || opacity > 1)
    throw std::invalid_argument("Invalid diagnostic mask group");
  for (const auto *plane : {&parent, &children})
    for (const auto &pixel : plane->pixels)
      if (pixel[1] != 0 || pixel[2] != 0 || pixel[3] != 1)
        throw std::invalid_argument("Diagnostic group requires raw mask stage layout");
  if (blend == LayerBlend::Disabled || opacity == 0)
    return parent;
  Plane result = parent;
  for (std::size_t i = 0; i < result.pixels.size(); ++i) {
    const float under = parent.pixels[i][0], child = std::clamp(children.pixels[i][0], 0.f, 1.f);
    result.pixels[i] = {
        under * (1 - opacity) + blend_value(blend, child, under) * opacity, 0, 0, 1};
  }
  return result;
}
}  // namespace bts::diagnostic_spatial
