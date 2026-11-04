#include "LayerMask.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
int main()
{
  bts::LayerMask mask;assert(mask.value()==1&&mask.value(0,0)==1);
  bts::PaintPixels::Stamp stamp{{1,.5},{.1,0},{0,.1},{}};
  stamp.hardness=1;stamp.flow=.25;stamp.values.roughness=0;stamp.channels={false,false,true,false,false};
  mask.pixels.stamp(128,stamp,{1001,1002});
  assert(std::abs(mask.value(127,64,1001)-.75)<1e-6);
  assert(mask.value(127,64,1001)==mask.value(0,64,1002));assert(mask.value(0,0)==1);
  const auto saved=mask;mask.background=0;assert(mask.value(127,64,1001)==0);
  stamp.flow=.5;stamp.values.roughness=1;mask.pixels.stamp(128,stamp,{1001,1002});
  assert(std::abs(mask.value(127,64,1001)-.5)<1e-6);
  assert(std::abs(saved.value(127,64,1001)-.75)<1e-6);
  const auto hash=mask.pixels.hash(2);mask.enabled=false;assert(mask.pixels.hash(2)==hash);
  // Stored pixel data survives a temporary bypass; composition chooses whether to use it.
  assert(mask.value(127,64,1001)==.5f);
  std::puts("mask-pages: white/black backgrounds, grayscale strokes, UDIM seam equality, COW and retained bypass data PASS");
}
