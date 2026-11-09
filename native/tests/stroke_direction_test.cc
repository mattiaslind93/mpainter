// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeDirection.hh"
#include "BrushJitterStamp.hh"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){using namespace bts;StrokeDirection direction;
  assert(!direction.uv({0,0}));assert(std::abs(*direction.uv({1,0}))<1e-12);assert(std::abs(*direction.uv({1,1})-1.5707963267948966)<1e-12);assert(!direction.uv({1,1}));assert(std::abs(*direction.uv({0,1})-3.141592653589793)<1e-12);
  direction.reset();assert(!direction.uv({.5,.5}));
  const std::array<double,3> tangent{0,1,0},bitangent{0,0,1};assert(!direction.world({0,0,0},tangent,bitangent));assert(std::abs(*direction.world({0,1,1},tangent,bitangent)-.7853981633974483)<1e-12);assert(!direction.world({1,1,1},tangent,bitangent));
  PaintPixels off,on;PaintPixels::Stamp stamp{{.7,.6},{.15,0},{0,.15}};stamp.alpha.kind=AlphaKind::Plus;stamp.channels={false,false,true,false,false};stamp.values.roughness=1;
  off.stamp(128,stamp);direction.reset();assert(!direction.uv({.3,.3}));const auto angle=direction.uv(stamp.center);assert(angle);rotate_brush_stamp(stamp,*angle);on.stamp(128,stamp);assert(off.hash(2)!=on.hash(2));
  std::cout<<"Dab direction reset, stationary rejection, UV turns, local world frame and actual path-aligned alpha coverage passed\n";
}
