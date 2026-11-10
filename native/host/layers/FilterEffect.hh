// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts {
// Original working-value filters. Neighborhood blur/sharpen stay on the GPU.
struct FilterSettings {
  int resource = 0, steps = 4;
  float hue = 0, saturation = 1, value_scale = 1, brightness = 0, contrast = 1;
  float input_low = 0, input_high = 1, gamma = 1, output_low = 0, output_high = 1, radius = 1;
};
inline constexpr std::array<const char *, 11> filter_names{"Filter (empty)",
                                                           "Invert",
                                                           "Clamp",
                                                           "Posterize",
                                                           "Hue/Saturation/Value",
                                                           "Contrast/Brightness",
                                                           "Levels/Ramp",
                                                           "Blur",
                                                           "Sharpen",
                                                           "Native Warp",
                                                           "Native Slope Blur"};
inline float filter_fract(float value)
{
  return value - std::floor(value);
}
inline std::array<float, 3> filter_rgb_to_hsv(std::array<float, 3> color)
{
  const float maxc = std::max(color[0], std::max(color[1], color[2])),
              minc = std::min(color[0], std::min(color[1], color[2])), delta = maxc - minc;
  float hue = 0;
  if (delta > 1e-8f) {
    if (maxc == color[0])
      hue = (color[1] - color[2]) / delta;
    else if (maxc == color[1])
      hue = (color[2] - color[0]) / delta + 2;
    else
      hue = (color[0] - color[1]) / delta + 4;
    hue = filter_fract(hue / 6.f);
  }
  return {hue, maxc <= 1e-8f ? 0 : delta / maxc, maxc};
}
inline std::array<float, 3> filter_hsv_to_rgb(std::array<float, 3> color)
{
  const auto channel = [&](float offset) {
    const float wrapped = std::abs(filter_fract(color[0] + offset) * 6.f - 3.f);
    return color[2] * std::lerp(1.f, std::clamp(wrapped - 1.f, 0.f, 1.f), color[1]);
  };
  return {channel(0), channel(2.f / 3.f), channel(1.f / 3.f)};
}
inline float filter_scalar(const FilterSettings &filter, float value)
{
  if (filter.resource == 1)
    return 1 - value;
  if (filter.resource == 2)
    return std::clamp(value, 0.f, 1.f);
  if (filter.resource == 3)
    return std::round(std::clamp(value, 0.f, 1.f) * (filter.steps - 1)) / (filter.steps - 1);
  if (filter.resource == 5)
    return (value - .5f) * filter.contrast + .5f + filter.brightness;
  if (filter.resource == 6) {
    const float span = filter.input_high - filter.input_low;
    float mapped = std::abs(span) < 1e-6f ?
                       0.f :
                       std::clamp((value - filter.input_low) / span, 0.f, 1.f);
    mapped = std::pow(mapped, 1.f / std::max(filter.gamma, .01f));
    return filter.output_low + (filter.output_high - filter.output_low) * mapped;
  }
  return value;
}
inline std::array<float, 3> filter_rgb(const FilterSettings &filter, std::array<float, 3> value)
{
  if (filter.resource == 4) {
    auto hsv = filter_rgb_to_hsv(value);
    hsv[0] = filter_fract(hsv[0] + filter.hue);
    hsv[1] = std::clamp(hsv[1] * filter.saturation, 0.f, 1.f);
    hsv[2] = std::clamp(hsv[2] * filter.value_scale, 0.f, 1.f);
    return filter_hsv_to_rgb(hsv);
  }
  for (float &channel : value)
    channel = filter_scalar(filter, channel);
  return value;
}
inline float filter_value(const FilterSettings &filter, float value)
{
  return filter.resource == 4 ? filter_rgb(filter, {value, value, value})[0] :
                                filter_scalar(filter, value);
}
inline QJsonObject filter_json(const FilterSettings &filter)
{
  return {{"resource", filter.resource},
          {"steps", filter.steps},
          {"hue", filter.hue},
          {"saturation", filter.saturation},
          {"value_scale", filter.value_scale},
          {"brightness", filter.brightness},
          {"contrast", filter.contrast},
          {"input_low", filter.input_low},
          {"input_high", filter.input_high},
          {"gamma", filter.gamma},
          {"output_low", filter.output_low},
          {"output_high", filter.output_high},
          {"radius", filter.radius}};
}
inline FilterSettings read_filter(const QJsonValue &value)
{
  const auto fail = []() { throw std::invalid_argument("Invalid filter settings"); };
  if (!value.isObject())
    fail();
  const auto o = value.toObject();
  const auto integer = [&](const char *key, int low, int high) {
    const auto v = o[key];
    if (!v.isDouble() || v.toDouble() != v.toInt(low - 1) || v.toInt() < low || v.toInt() > high)
      fail();
    return v.toInt();
  };
  const auto optional = [&](const char *key, float fallback, float low, float high) {
    if (!o.contains(key))
      return fallback;
    const auto v = o[key];
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < low || v.toDouble() > high)
      fail();
    return float(v.toDouble());
  };
  FilterSettings filter;
  filter.resource = integer("resource", 0, 10);
  filter.steps = integer("steps", 2, 64);
  filter.hue = optional("hue", 0, -1, 1);
  filter.saturation = optional("saturation", 1, 0, 4);
  filter.value_scale = optional("value_scale", 1, 0, 4);
  filter.brightness = optional("brightness", 0, -1, 1);
  filter.contrast = optional("contrast", 1, 0, 4);
  filter.input_low = optional("input_low", 0, -8, 8);
  filter.input_high = optional("input_high", 1, -8, 8);
  filter.gamma = optional("gamma", 1, .01f, 8);
  filter.output_low = optional("output_low", 0, -8, 8);
  filter.output_high = optional("output_high", 1, -8, 8);
  filter.radius = optional("radius", 1, 1, 4);
  return filter;
}
}  // namespace bts
