// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace bts {
struct SkewSurfaceVector {
  std::array<float, 3> position, average, geometric;
  double u = 0, v = 0;
  int tile = 1001;
  std::uint32_t object = 0;
};
}  // namespace bts
