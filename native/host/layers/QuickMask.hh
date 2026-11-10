// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TilePaintPixels.hh"
namespace bts {
// Temporary scalar field, independent of document layers and packed project data.
struct QuickMaskField {
  TilePaintPixels pixels;
  bool inverted = false;
  int resolution = 256;
  float sample_uv(double u, double v, int output_resolution = 0, int output_height = 0) const
  {
    if (!std::isfinite(u) || !std::isfinite(v))
      return 0;
    const int column = int(std::floor(u)), row = int(std::floor(v));
    if (column < 0 || column >= 10 || row < 0 || row >= 100)
      return 0;
    const int width = output_resolution ? output_resolution : resolution,
              height = output_height ? output_height : width;
    const auto pixel = pixels.sample_resampled(2,
                                               std::clamp(int((u - column) * width), 0, width - 1),
                                               std::clamp(int((v - row) * height), 0, height - 1),
                                               width,
                                               1001 + column + row * 10,
                                               height);
    const float value = std::clamp(pixel[0] + 1 - pixel[3], 0.f, 1.f);
    return inverted ? 1 - value : value;
  }
  float author_value(float displayed) const
  {
    return inverted ? 1 - displayed : displayed;
  }
};
}  // namespace bts
