// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushJitter.hh"
#include "BrushJitterStamp.hh"
#include <cassert>
#include <iostream>
#include <limits>
int main()
{
  using namespace bts;
  JitterSampler a(19), b(19);
  BrushJitter settings;
  for (int i = 0; i < 5; ++i) {
    auto dab = a.sample(settings);
    assert(dab.size == 1 && dab.flow == 1 && dab.angle == 0 &&
           dab.position == (std::array<double, 2>{}));
  }
  settings = {80, 80, 90, 100};
  double min_size = 1, max_size = 0, min_flow = 1, max_flow = 0, min_angle = 4, max_angle = -4;
  for (int i = 0; i < 1000; ++i) {
    const auto x = a.sample(settings), y = b.sample(settings);
    assert(x.size == y.size && x.flow == y.flow && x.angle == y.angle && x.position == y.position);
    assert(x.size >= .2 - 1e-7 && x.size <= 1 && x.flow >= .2 - 1e-7 && x.flow <= 1 &&
           std::abs(x.angle) <= 1.570797 && std::hypot(x.position[0], x.position[1]) <= 1);
    min_size = std::min(min_size, x.size);
    max_size = std::max(max_size, x.size);
    min_flow = std::min(min_flow, x.flow);
    max_flow = std::max(max_flow, x.flow);
    min_angle = std::min(min_angle, x.angle);
    max_angle = std::max(max_angle, x.angle);
  }
  assert(max_size - min_size > .7 && max_flow - min_flow > .7 && max_angle - min_angle > 3);
  JitterSampler c(20), d(20);
  auto without_size = settings;
  without_size.size = 0;
  for (int i = 0; i < 20; ++i) {
    const auto x = c.sample(settings), y = d.sample(without_size);
    assert(y.size == 1 && x.flow == y.flow && x.angle == y.angle && x.position == y.position);
  }
  for (float invalid : {-1.f,
                        101.f,
                        std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()})
  {
    auto bad = settings;
    bad.position = invalid;
    bool rejected = false;
    try {
      a.sample(bad);
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  PaintPixels::Stamp stamp{{.5, .5}, {.5, 0}, {0, .5}};
  stamp.channels = {false, false, true, false, false};
  stamp.values.roughness = 1;
  stamp.alpha.kind = AlphaKind::Plus;
  PaintPixels original, rotated;
  original.stamp(128, stamp);
  auto turned = stamp;
  rotate_brush_stamp(turned, .7853981633974483);
  rotated.stamp(128, turned);
  assert(original.hash(2) != rotated.hash(2));
  auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
  surface->center = {0, 0, 0};
  surface->radius = 1;
  PaintPixels::SurfaceTriangle left{
      {{{0, 0}, {1, 0}, {1, 1}}}, {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}}}, {}};
  PaintPixels::SurfaceTriangle right{
      {{{0, 0}, {1, 1}, {0, 1}}}, {{{-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}}, {}};
  surface->triangles = {left, right};
  stamp.surface = surface;
  rotate_brush_stamp(stamp, .7853981633974483);
  PaintPixels tangent;
  tangent.stamp(128, stamp);
  assert(tangent.hash(2) == rotated.hash(2));
  assert(surface->tangent == (std::array<double, 3>{1, 0, 0}));
  surface->unwrapped = true;
  for (auto &triangle : surface->triangles)
    for (int i = 0; i < 3; ++i)
      triangle.paint_coordinates[i] = {triangle.position[i][0], triangle.position[i][1]};
  stamp.surface = surface;
  stamp.axis_x = {.5, 0};
  stamp.axis_y = {0, .5};
  rotate_brush_stamp(stamp, .7853981633974483);
  PaintPixels wrapped;
  wrapped.stamp(128, stamp);
  assert(wrapped.hash(2) == rotated.hash(2));
  assert(surface->triangles[0].paint_coordinates[0] == (std::array<double, 2>{-1, -1}));
  std::cout
      << "Jitter identity, bounded independent deterministic draws, invalid inputs, UV/tangent/unwrapped alpha rotation and immutable footprints passed\n";
}
