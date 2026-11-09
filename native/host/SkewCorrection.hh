#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace bts {
using SkewNormal = std::array<float, 3>;
// Painting stores a scalar amount, not arbitrary vectors. Red/0 uses the averaged cage normal;
// green/1 uses the geometric face normal. This changes bake projection only, never low geometry.
inline SkewNormal skew_projection_normal(SkewNormal average, SkewNormal geometric, float weight)
{
  const auto normalized = [](SkewNormal n) {
    double length = 0;
    for (float v : n) {
      if (!std::isfinite(v))
        return SkewNormal{};
      length += double(v) * v;
    }
    if (length <= 1e-20)
      return SkewNormal{};
    for (float &v : n)
      v = float(v / std::sqrt(length));
    return n;
  };
  average = normalized(average);
  geometric = normalized(geometric);
  if (!std::isfinite(weight))
    weight = 0;
  weight = std::clamp(weight, 0.f, 1.f);
  SkewNormal result;
  for (int i = 0; i < 3; ++i)
    result[i] = std::lerp(average[i], geometric[i], weight);
  result = normalized(result);
  return result == SkewNormal{} ? (weight >= .5f ? geometric : average) : result;
}
// Geometry distance, not UV distance. Contrast sharpens a smooth edge-protection falloff.
inline float skew_edge_weight(float weight, double distance, double radius, double contrast)
{
  if (!std::isfinite(weight) || std::isnan(distance) || distance < 0 || !std::isfinite(radius) ||
      !std::isfinite(contrast))
    return 0;
  weight = std::clamp(weight, 0.f, 1.f);
  if (radius <= 0 || std::isinf(distance))
    return weight;
  const double x = std::clamp(distance / radius, 0., 1.);
  const double smooth = x * x * (3 - 2 * x);
  return float(weight * std::pow(smooth, std::exp2(std::clamp(contrast, -1., 1.) * 4)));
}
}  // namespace bts
