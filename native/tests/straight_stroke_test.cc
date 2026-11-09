// SPDX-License-Identifier: GPL-3.0-or-later
#include "StraightStroke.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main(){
  const auto e=bts::snap_straight_endpoint({200,200},{450,360});
  assert(std::abs(e[0]-450)<1e-10&&std::abs(e[1]-(200+250/std::sqrt(3.)))<1e-10);
  for(int x=-100;x<=100;x+=5)for(int y=-100;y<=100;y+=5){const auto p=bts::snap_straight_endpoint({0,0},{double(x),double(y)});assert(std::isfinite(p[0])&&std::isfinite(p[1]));if(x||y){const auto angle=std::atan2(p[1],p[0])/(std::numbers::pi/12);assert(std::abs(angle-std::round(angle))<1e-10);if(std::abs(x)>=std::abs(y))assert(std::abs(p[0]-x)<1e-10);else assert(std::abs(p[1]-y)<1e-10);}}
  std::puts("straight stroke: 30 degree reference endpoint, all quadrants, finite axes and preserved dominant coordinate PASS");
}
