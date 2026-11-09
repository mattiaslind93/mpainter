// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtCore/qfloat16.h>
#include "MaterialValues.hh"
#include <algorithm>
#include <array>
#include <cmath>
namespace bts {
enum class ChannelPrecision { SRGB8, L8, RGB8, L16, RGB16, L16F, RGB16F, L32F, RGB32F };
inline constexpr std::array<const char *, 9> channel_precision_names{
    "sRGB8", "L8", "RGB8", "L16", "RGB16", "L16F", "RGB16F", "L32F", "RGB32F"};
inline constexpr std::array<ChannelPrecision, 6> default_channel_precision{
    ChannelPrecision::SRGB8,
    ChannelPrecision::L8,
    ChannelPrecision::L8,
    ChannelPrecision::RGB32F,
    ChannelPrecision::L16F,
    ChannelPrecision::L32F};
inline std::array<float, 3> precision_rgb(std::array<float, 3> value, ChannelPrecision precision)
{
  const bool scalar = precision == ChannelPrecision::L8 || precision == ChannelPrecision::L16 ||
                      precision == ChannelPrecision::L16F || precision == ChannelPrecision::L32F;
  // L uses fixed weighted linear-RGB grayscale. Scalar data already replicates RGB.
  if (scalar)
    value.fill(value[0] * .2126f + value[1] * .7152f + value[2] * .0722f);
  for (auto &v : value) {
    switch (precision) {
      case ChannelPrecision::SRGB8: {
        const float linear = std::clamp(v, 0.f, 1.f);
        const float encoded = linear <= .0031308f ? 12.92f * linear :
                                                    1.055f * std::pow(linear, 1.f / 2.4f) - .055f;
        const float q = std::round(encoded * 255) / 255;
        v = q <= .04045f ? q / 12.92f : std::pow((q + .055f) / 1.055f, 2.4f);
        break;
      }
      case ChannelPrecision::L8:
      case ChannelPrecision::RGB8:
        v = std::round(std::clamp(v, 0.f, 1.f) * 255) / 255;
        break;
      case ChannelPrecision::L16:
      case ChannelPrecision::RGB16:
        v = std::round(std::clamp(v, 0.f, 1.f) * 65535) / 65535;
        break;
      case ChannelPrecision::L16F:
      case ChannelPrecision::RGB16F:
        v = float(qfloat16(std::clamp(v, -65504.f, 65504.f)));
        break;
      case ChannelPrecision::L32F:
      case ChannelPrecision::RGB32F:
        break;
    }
  }
  return value;
}
inline std::array<float, 3> precision_channel(const MaterialValues &v,
                                              int channel,
                                              ChannelPrecision precision)
{
  std::array<float, 3> value;
  switch (channel) {
    case 0:
      value = {v.base_color[0], v.base_color[1], v.base_color[2]};
      break;
    case 1:
      value.fill(v.metallic);
      break;
    case 2:
      value.fill(v.roughness);
      break;
    case 3:
      for (int c = 0; c < 3; ++c)
        value[c] = v.normal[c] * .5f + .5f;
      break;
    case 4:
      value.fill(v.bump);
      break;
    default:
      value.fill(v.ambient_occlusion);
      break;
  }
  return precision_rgb(value, precision);
}

}  // namespace bts
