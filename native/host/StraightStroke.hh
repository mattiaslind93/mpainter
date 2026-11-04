#pragma once
#include <array>
#include <cmath>
#include <numbers>
namespace bts {
// App contract: 15 degree increments, preserving the dominant screen coordinate.
inline std::array<double,2> snap_straight_endpoint(std::array<double,2> anchor,std::array<double,2> end) {
  const double dx=end[0]-anchor[0],dy=end[1]-anchor[1];
  if(std::hypot(dx,dy)<1e-9)return end;
  constexpr double step=std::numbers::pi/12;
  const double angle=std::round(std::atan2(dy,dx)/step)*step;
  const double c=std::cos(angle),s=std::sin(angle);
  const double length=std::abs(dx)>=std::abs(dy)?dx/c:dy/s;
  return {anchor[0]+length*c,anchor[1]+length*s};
}
}
