// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
namespace bts {
// Application preferences. Scene cameras keep their own file-authored planes until detached.
struct ViewportClipping {
  double near_plane = .01;
  double far_plane = 1000;
  static constexpr double minimum_near = 1e-6;
  static constexpr double maximum_plane = 1e12;
  bool valid() const
  {
    return std::isfinite(near_plane) && std::isfinite(far_plane) && near_plane >= minimum_near &&
           far_plane > near_plane && far_plane <= maximum_plane;
  }
  bool operator==(const ViewportClipping &) const = default;
};
}  // namespace bts
