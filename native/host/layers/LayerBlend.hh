// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include "NormalVector.hh"
#include <array>
#include <cmath>
namespace bts {
// Existing ordinals remain stable for packed projects and scalar custom channels.
enum class LayerBlend {
  Normal,
  Replace,
  Multiply,
  Screen,
  Disabled,
  Divide,
  InverseDivide,
  Darken,
  Lighten,
  LinearDodge,
  Subtract,
  InverseSubtract,
  Difference,
  Exclusion,
  SignedAddition,
  Overlay,
  SoftLight,
  HardLight,
  VividLight,
  LinearLight,
  PinLight,
  Tint,
  Saturation,
  Color,
  Value,
  NormalCombine,
  NormalDetail,
  NormalInverseDetail,
  Passthrough
};
inline constexpr std::array<const char *, 29> layer_blend_names{"Normal",
                                                                "Replace",
                                                                "Multiply",
                                                                "Screen",
                                                                "Disable",
                                                                "Divide",
                                                                "Inverse Divide",
                                                                "Darken",
                                                                "Lighten",
                                                                "Linear Dodge",
                                                                "Subtract",
                                                                "Inverse Subtract",
                                                                "Difference",
                                                                "Exclusion",
                                                                "Signed Addition",
                                                                "Overlay",
                                                                "Soft Light",
                                                                "Hard Light",
                                                                "Vivid Light",
                                                                "Linear Light",
                                                                "Pin Light",
                                                                "Tint",
                                                                "Saturation",
                                                                "Color",
                                                                "Value",
                                                                "Normal Map Combine",
                                                                "Normal Map Detail",
                                                                "Normal Map Inverse Detail",
                                                                "Passthrough"};
inline constexpr std::array<const char *, 29> layer_blend_labels{
    "Norm", "Repl", "Mul",  "Scrn", "Disa", "Div",  "Idiv", "Drk",  "Lgt",  "Ldge",
    "Sub",  "Isub", "Diff", "Excl", "Sadd", "Ovrl", "Slgt", "Hlgt", "Vlgt", "Llgt",
    "Plgt", "Tint", "Strt", "Col",  "Val",  "NMcb", "NMdt", "NMid", "Pthr"};
inline float blend_divide(float numerator, float denominator)
{
  if (denominator == 0)
    return numerator > 0 ? 1.f : 0.f;
  return float(std::clamp(double(numerator) / denominator, -65504., 65504.));
}
// Straight values; coverage and group isolation are handled by the compositor.
inline float blend_value(LayerBlend mode, float source, float backdrop)
{
  switch (mode) {
    case LayerBlend::Multiply:
      return source * backdrop;
    case LayerBlend::Screen:
      return 1 - (1 - source) * (1 - backdrop);
    case LayerBlend::Divide:
      return blend_divide(backdrop, source);
    case LayerBlend::InverseDivide:
      return blend_divide(source, backdrop);
    case LayerBlend::Darken:
      return std::min(source, backdrop);
    case LayerBlend::Lighten:
      return std::max(source, backdrop);
    case LayerBlend::LinearDodge:
      return backdrop + source;
    case LayerBlend::Subtract:
      return backdrop - source;
    case LayerBlend::InverseSubtract:
      return source - backdrop;
    case LayerBlend::Difference:
      return std::abs(backdrop - source);
    case LayerBlend::Exclusion:
      return backdrop + source - 2 * backdrop * source;
    case LayerBlend::SignedAddition:
      return backdrop + source - .5f;
    case LayerBlend::Overlay:
      return backdrop <= .5f ? 2 * backdrop * source : 1 - 2 * (1 - backdrop) * (1 - source);
    case LayerBlend::HardLight:
      return source <= .5f ? 2 * backdrop * source : 1 - 2 * (1 - backdrop) * (1 - source);
    case LayerBlend::SoftLight: {
      if (source <= .5f)
        return backdrop - (1 - 2 * source) * backdrop * (1 - backdrop);
      const float d = backdrop <= .25f ? ((16 * backdrop - 12) * backdrop + 4) * backdrop :
                                         std::sqrt(backdrop);
      return backdrop + (2 * source - 1) * (d - backdrop);
    }
    case LayerBlend::VividLight:
      if (source <= .5f)
        return backdrop >= 1 ? 1 :
               source <= 0   ? 0 :
                               1 - std::min(1.f, blend_divide(1 - backdrop, 2 * source));
      else
        return backdrop <= 0 ? 0 :
               source >= 1   ? 1 :
                               std::min(1.f, blend_divide(backdrop, 2 * (1 - source)));
    case LayerBlend::LinearLight:
      return backdrop + 2 * source - 1;
    case LayerBlend::PinLight:
      return source <= .5f ? std::min(backdrop, 2 * source) : std::max(backdrop, 2 * source - 1);
    case LayerBlend::Tint:
    case LayerBlend::Saturation:
    case LayerBlend::Color:
      return backdrop;
    default:
      return source;
  }
}
inline std::array<float, 3> blend_rgb_to_hsv(std::array<float, 3> rgb)
{
  for (auto &v : rgb)
    v = std::max(0.f, v);
  const float high = *std::max_element(rgb.begin(), rgb.end()),
              low = *std::min_element(rgb.begin(), rgb.end()), delta = high - low;
  float hue = 0;
  if (delta > 1e-8f) {
    if (high == rgb[0])
      hue = (rgb[1] - rgb[2]) / delta;
    else if (high == rgb[1])
      hue = (rgb[2] - rgb[0]) / delta + 2;
    else
      hue = (rgb[0] - rgb[1]) / delta + 4;
    hue /= 6;
    hue -= std::floor(hue);
  }
  return {hue, high > 1e-8f ? delta / high : 0, high};
}
inline std::array<float, 3> blend_hsv_to_rgb(const std::array<float, 3> &hsv)
{
  const float hue = (hsv[0] - std::floor(hsv[0])) * 6, s = hsv[1], v = hsv[2];
  const int sector = int(hue);
  const float f = hue - sector, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  switch (sector % 6) {
    case 0:
      return {v, t, p};
    case 1:
      return {q, v, p};
    case 2:
      return {p, v, t};
    case 3:
      return {p, q, v};
    case 4:
      return {t, p, v};
    default:
      return {v, p, q};
  }
}
inline bool normal_blend_mode(LayerBlend mode)
{
  return mode >= LayerBlend::NormalCombine && mode <= LayerBlend::NormalInverseDetail;
}
inline NormalVector blend_normal(LayerBlend mode, NormalVector source, NormalVector backdrop)
{
  if (mode == LayerBlend::NormalCombine)
    return whiteout_normal(backdrop, source);
  if (mode == LayerBlend::NormalDetail)
    return reoriented_normal(backdrop, source);
  if (mode == LayerBlend::NormalInverseDetail)
    return reoriented_normal(source, backdrop);
  return normalized_normal(source);
}
inline std::array<float, 3> blend_rgb(LayerBlend mode,
                                      const std::array<float, 3> &source,
                                      const std::array<float, 3> &backdrop)
{
  if (normal_blend_mode(mode)) {
    NormalVector a, b;
    for (int c = 0; c < 3; ++c) {
      a[c] = source[c] * 2 - 1;
      b[c] = backdrop[c] * 2 - 1;
    }
    auto result = blend_normal(mode, a, b);
    for (auto &v : result)
      v = v * .5f + .5f;
    return result;
  }
  if (mode >= LayerBlend::Tint && mode <= LayerBlend::Value) {
    const auto top = blend_rgb_to_hsv(source);
    auto result = blend_rgb_to_hsv(backdrop);
    if (mode == LayerBlend::Tint) {
      if (top[1] <= 1e-8f || top[2] <= 1e-8f)
        return backdrop;
      result[0] = top[0];
    }
    else if (mode == LayerBlend::Saturation)
      result[1] = top[1];
    else if (mode == LayerBlend::Color) {
      result[0] = top[0];
      result[1] = top[1];
    }
    else
      result[2] = top[2];
    return blend_hsv_to_rgb(result);
  }
  std::array<float, 3> result;
  for (int c = 0; c < 3; ++c)
    result[c] = blend_value(mode, source[c], backdrop[c]);
  return result;
}
}  // namespace bts
