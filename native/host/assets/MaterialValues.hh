// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
namespace bts {
enum class Channel { BaseColor, Metallic, Roughness, Normal, Bump };
inline constexpr std::array<const char *, 5> channel_names{
    "Base Color", "Metallic", "Roughness", "Normal", "Bump"};
struct MaterialValues {
  std::array<float, 4> base_color{
      0.5f, 0.5f, 0.5f, 1};  // Legacy neutral fallback; explicit new base/fill defaults below.
  float metallic = 0;
  float roughness = 0.3f;
  std::array<float, 3> normal{0, 0, 1};  // Tangent-space data, not a color.
  float bump = 0.5f;  // Fixed artist/output neutral; legacy project storage is adapted per set.
  float ambient_occlusion = 1;
  bool operator==(const MaterialValues &) const = default;
};
inline MaterialValues new_base_values()
{
  MaterialValues value;
  value.base_color = {1, 1, 1, 1};
  return value;
}
}  // namespace bts
