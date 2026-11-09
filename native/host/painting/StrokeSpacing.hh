// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace bts {
// Carries the distance to the next dab across input events. The emitter supplies the
// next distance, allowing projected diameter/pressure to vary along the stroke.
class StrokeSpacing {
 public:
  using Point = std::array<double, 2>;
  void reset(Point position, double distance)
  {
    validate(position);
    last_ = position;
    remaining_ = checked(distance);
  }
  template<class Emit> void advance(Point target, Emit emit_dab)
  {
    validate(target);
    auto position = last_;
    double remaining = remaining_;
    double dx = target[0] - position[0], dy = target[1] - position[1], length = std::hypot(dx, dy);
    if (!std::isfinite(length))
      throw std::overflow_error("Brush arc length is not finite");
    if (length == 0)
      return;
    const double ux = dx / length, uy = dy / length;

    while (length > 0 &&
           length + std::numeric_limits<double>::epsilon() * std::max(length, remaining) * 4 >=
               remaining)
    {

      const double step = std::min(length, remaining);
      if (length - step == length)
        throw std::overflow_error("Brush spacing cannot advance at this arc-length precision");
      position[0] += ux * step;
      position[1] += uy * step;
      length -= step;
      remaining = checked(emit_dab(position));
    }
    last_ = target;
    remaining_ = remaining - length;
  }

 private:
  Point last_{};
  double remaining_ = 1;
  static double checked(double distance)
  {
    if (!std::isfinite(distance) || distance <= 0)
      throw std::invalid_argument("Invalid brush stamp distance");
    return distance;
  }
  static void validate(Point p)
  {
    for (double value : p)
      if (!std::isfinite(value))
        throw std::invalid_argument("Invalid brush input position");
  }
};
}  // namespace bts
