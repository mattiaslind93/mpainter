// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace bts {
struct SceneCamera {
  std::uint32_t session_id = 0;
  std::string name;
  std::array<float, 3> position{}, forward{0, 1, 0}, up{0, 0, 1};
  float vertical_fov = 45, aspect = 0, near_clip = .1f, far_clip = 1000, orthographic_extent = 2;
  bool orthographic = false;
  bool operator==(const SceneCamera &) const = default;
};
}  // namespace bts
