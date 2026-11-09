// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayerMask.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
using bts::PaintPixels;
int main()
{
  bts::TilePaintPixels before,stroke,result;
  PaintPixels::Stamp paint{{1,.5},{.2,0},{0,.2},{}};paint.hardness=1;paint.values.roughness=.8;
  before.stamp(128,paint,{1001,1002});const auto original=before;
  paint.flow=.5;paint.channels={false,false,true,false,false};
  const auto bounds=stroke.stamp(128,paint,{1001,1002});result=before;
  result.over_stroke(before,stroke,.4,bounds,true);
  for(int tile:{1001,1002}) {
    const int x=tile==1001?127:0;const auto pixel=result.sample(2,x,64,tile);
    assert(std::abs(pixel[3]-.8)<1e-6&&std::abs(pixel[0]-.64)<1e-6);
  }
  assert(result.hash(0)==before.hash(0));const auto hash=result.hash(2);
  result.over_stroke(before,stroke,.4,bounds,true);assert(result.hash(2)==hash); // whole stroke cap, not repeated attenuation
  assert(before.hash(2)==original.hash(2));
  stroke={};paint.flow=1;paint.channels={true,true,true,true,true};
  stroke.stamp(128,paint,{1001,1002});result=before;
  result.over_stroke(before,stroke,1,bounds,true);assert(result.empty()&&result.tiles().empty());
  assert(result.hash(2)==bts::TilePaintPixels{}.hash(2)); // canonical empty state survives save/open
  result={};assert(result.over_stroke(result,stroke,1,bounds,true).empty());assert(result.tiles().empty());
  bts::LayerMask mask;mask.background=.25;mask.pixels=before;
  mask.pixels.over_stroke(before,stroke,1,bounds,true);assert(mask.value(127,64)==.25);
  std::puts("eraser: associated value/coverage attenuation, independent channels, UDIM seam, whole-stroke opacity, COW, empty-page pruning, empty no-op and mask background PASS");
}
