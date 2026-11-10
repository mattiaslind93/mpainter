// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
namespace bts::height_image_reference {
// Diagnostic/export reference only. Production uses Blender's native image
// node and sampler; this independently samples literal raw source components.
using Pixel = std::array<float, 4>;
inline double sample(std::span<const Pixel> pixels,
                     int width,
                     int height,
                     double u,
                     double v,
                     bool linear,
                     bool repeat)
{
  if (width < 1 || height < 1 || pixels.size() != std::size_t(width) * height ||
      !std::isfinite(u) || !std::isfinite(v))
    throw std::invalid_argument("Invalid raw Height reference image");
  const auto coordinate = [&](double value) {
    return repeat ? value - std::floor(value) : std::clamp(value, 0., 1.);
  };
  u = coordinate(u);
  v = coordinate(v);
  const auto index = [&](int value, int size) {
    return repeat ? (value % size + size) % size : std::clamp(value, 0, size - 1);
  };
  const auto pixel = [&](int x, int y) {
    return double(pixels[std::size_t(index(y, height)) * width + index(x, width)][0]);
  };
  if (!linear)
    return pixel(int(std::floor(u * width)), int(std::floor(v * height)));
  const double x = u * width - .5, y = v * height - .5;
  const int left = int(std::floor(x)), bottom = int(std::floor(y));
  const double fx = x - left, fy = y - bottom;
  return std::lerp(std::lerp(pixel(left, bottom), pixel(left + 1, bottom), fx),
                   std::lerp(pixel(left, bottom + 1), pixel(left + 1, bottom + 1), fx),
                   fy);
}
inline constexpr int fixture_width = 4, fixture_height = 3;
inline std::array<Pixel, fixture_width * fixture_height> fixture_pixels()
{
  std::array<Pixel, fixture_width * fixture_height> result{};
  for (int y = 0; y < fixture_height; ++y)
    for (int x = 0; x < fixture_width; ++x)
      result[y * fixture_width + x] = {
          float(.271828 + 1.618033 * x - 2.414213 * y), float(5 - x), float(-3 - y), 1};
  return result;
}
}  // namespace bts::height_image_reference
