// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaintPixels.hh"
#include "TilePaintPixels.hh"
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
  using namespace bts;
  auto projection = std::make_shared<ProjectionMapping>();
  projection->source.weave = true;
  projection->source.values.base_color = {.025, .06, .16, 1};
  projection->source.values.roughness = .8824;
  PaintPixels::Stamp stamp{{.5, .5}, {.5, 0}, {0, .5}};
  stamp.alpha.kind = AlphaKind::Uniform;
  stamp.channels = {true, true, true, false, true};
  stamp.flow = .5;
  stamp.projected = projection;
  PaintPixels uv;
  uv.stamp(128, stamp);
  float minimum = 1, maximum = 0;
  for (int y = 0; y < 128; y += 3)
    for (int x = 0; x < 128; x += 3) {
      const auto expected = projection->source.sample(
          {2 * (x + .5) / 128 - 1, 2 * (y + .5) / 128 - 1});
      assert(expected);
      const auto color = uv.sample(0, x, y), rough = uv.sample(2, x, y),
                 height = uv.sample(4, x, y);
      for (int c = 0; c < 3; ++c)
        assert(std::abs(color[c] - expected->base_color[c] * .5) < 1e-7);
      assert(std::abs(rough[0] - expected->roughness * .5) < 1e-7 &&
             std::abs(height[0] - expected->bump * .5) < 1e-7);
      minimum = std::min(minimum, rough[0]);
      maximum = std::max(maximum, rough[0]);
    }
  assert(maximum - minimum > .03 && uv.page_count(3) == 0);
  auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
  surface->center = {0, 0, 0};
  surface->radius = 1;
  PaintPixels::SurfaceTriangle left{
      {{{0, 0}, {1, 0}, {1, 1}}}, {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}}}, {}};
  PaintPixels::SurfaceTriangle right{
      {{{0, 0}, {1, 1}, {0, 1}}}, {{{-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}}, {}};
  surface->triangles = {left, right};
  projection->mapping.world = true;
  projection->mapping.projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  stamp.surface = surface;
  PaintPixels world;
  world.stamp(128, stamp);
  for (int c : {0, 1, 2, 4})
    assert(uv.hash(c) == world.hash(c));
  for (const auto &triangle : surface->triangles)
    projection->mapping.uv_world.push_back({triangle.uv, triangle.position});
  stamp.surface.reset();
  PaintPixels aligned;
  aligned.stamp(128, stamp);
  assert(aligned.hash(0) == uv.hash(0));
  projection->scalar_override = -.25f;
  PaintPixels scalar;
  scalar.stamp(128, stamp);
  assert(scalar.sample(2, 64, 64)[0] == -.125f);
  projection->scalar_override.reset();
  projection->source.transform.scale = .25;
  PaintPixels clipped;
  clipped.stamp(128, stamp);
  assert(clipped.sample(0, 0, 0)[3] == 0 && clipped.sample(0, 64, 64)[3] == .5);
  projection->source.transform.tiling = 3;
  assert(projection->source.sample({2.1, 0}) && projection->source.sample({0, 2.1}));
  auto tile = std::make_shared<ProjectionMapping>();
  tile->source.transform.offset = {2, 0};
  stamp.projected = tile;
  stamp.center = {1.5, .5};
  TilePaintPixels tiles;
  tiles.stamp(128, stamp, {1001, 1002});
  assert(tiles.tile(1001).empty() && tiles.sample(0, 64, 64, 1002)[3] == .5);
  const auto hash = tiles.hash(0);
  tile->source.density = 0;
  bool rejected = false;
  try {
    tiles.stamp(128, stamp, {1001, 1002});
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected && tiles.hash(0) == hash);
  std::cout
      << "Per-texel projected PBR color/roughness/signed height, disabled normal, UV/world/UV-aligned equivalence, clipping/tiling, UDIM and atomic rejection passed\n";
}
