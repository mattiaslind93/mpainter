#pragma once
#include "TextureDocument.hh"
namespace bts {
inline float worn_metal_wear(float curvature)
{
  const float t = std::clamp((curvature - .65f) / .30f, 0.f, 1.f);
  return t * t * (3 - 2 * t);
}
inline float worn_metal_dirt(float ao)
{
  const float occlusion = 1 - std::clamp(ao, 0.f, 1.f);
  return occlusion * occlusion;
}
inline std::array<float, 3> worn_metal_color(float curvature, float ao)
{
  const float wear = worn_metal_wear(curvature), dirt = worn_metal_dirt(ao);
  const std::array<float, 3> coat{.03, .08, .16}, metal{.45, .45, .45}, soil{.02, .012, .005};
  std::array<float, 3> rgb;
  for (int i = 0; i < 3; ++i)
    rgb[i] = (coat[i] * (1 - wear) + metal[i] * wear) * (1 - dirt) + soil[i] * dirt;
  return rgb;
}
bool worn_metal_ready(const TextureSet &);
QUuid apply_worn_metal(TextureSet &, int resolution);
}  // namespace bts
