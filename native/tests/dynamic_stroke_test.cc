// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaintPixels.hh"
#include <cassert>
#include <iostream>
#include <set>
using namespace bts;
int main()
{
  BrushAlpha resource;
  resource.kind = AlphaKind::DynamicOutline;
  resource.dynamic.cycle = 4;
  DynamicStrokeSampler sampler(5);
  std::array<std::uint64_t, 8> hashes{};
  for (int i = 0; i < 8; ++i) {
    PaintPixels pixels;
    PaintPixels::Stamp stamp{{.5, .5}, {.4, 0}, {0, .4}};
    stamp.alpha = resource;
    stamp.alpha.dynamic.phase = sampler.phase(resource.dynamic);
    stamp.channels = {true, false, false, false, false};
    pixels.stamp(128, stamp);
    hashes[i] = pixels.hash(0);
    assert(stamp.alpha.dynamic.phase == double(i % 4) / 4);
  }
  assert(std::set<std::uint64_t>(hashes.begin(), hashes.begin() + 4).size() == 4);
  for (int i = 0; i < 4; ++i)
    assert(hashes[i] == hashes[i + 4]);
  resource.dynamic.random_start = true;
  sampler.reset(5);
  const double start = sampler.phase(resource.dynamic);
  assert(start != 0 && start >= 0 && start < 1);
  sampler.reset(5);
  assert(sampler.phase(resource.dynamic) == start);
  auto narrow = resource.dynamic, wide = narrow;
  narrow.width = .05;
  wide.width = .3;
  double small = 0, large = 0;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      small += narrow.sample((x + .5) / 64 - 1, (y + .5) / 64 - 1);
      large += wide.sample((x + .5) / 64 - 1, (y + .5) / 64 - 1);
    }
  assert(large > small * 3);
  resource.dynamic.cycle = 0;
  bool rejected = false;
  try {
    resource.dynamic.validate();
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout
      << "Own procedural dynamic alpha: four distinct recurring stamp shapes, deterministic beginning/random phase, width response and parameter bounds passed\n";
}
