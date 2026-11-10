// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushFootprintBounds.hh"
#include "BrushJitterStamp.hh"
#include "TilePaintPixels.hh"
#include "PaintRecording.hh"
#include <cassert>
#include <cstdio>
using namespace bts;
PaintPixels::Stamp surface_stamp(
    double x = .35, double y = .35, double z = 0, bool unwrapped = false, bool bounded = false)
{
  PaintPixels::Stamp stamp{{x, y}, {.025, 0}, {0, .025}, {}};
  stamp.channels = {true, false, false, false, false};
  stamp.values.base_color = {.2f, .7f, .4f, 1};
  stamp.flow = .8;
  stamp.hardness = .2;
  auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
  surface->center = {x, y, 0};
  surface->radius = .025;
  surface->unwrapped = unwrapped;
  for (const auto &points : {std::array<std::array<double, 2>, 3>{{{0, 0}, {1, 0}, {1, 1}}},
                             std::array<std::array<double, 2>, 3>{{{0, 0}, {1, 1}, {0, 1}}}})
  {
    PaintPixels::SurfaceTriangle triangle;
    triangle.uv = points;
    triangle.bounded_depth = bounded;
    for (int i = 0; i < 3; ++i) {
      triangle.position[i] = {points[i][0], points[i][1], z};
      triangle.paint_coordinates[i] = {points[i][0] - x, points[i][1] - y};
    }
    surface->triangles.push_back(triangle);
  }
  stamp.surface = surface;
  return stamp;
}
int main()
{
  for (bool unwrapped : {false, true})
    for (bool bounded : {false, true})
      for (double z : {0., .2})
        for (double angle : {0., .37}) {
          auto stamp = surface_stamp(.35, .35, z, unwrapped, bounded);
          rotate_brush_stamp(stamp, angle);
          PaintPixels cpu, recorded;
          cpu.stamp(128, stamp);
          {
            PaintRecordingScope gpu;
            const auto bounds = recorded.stamp(128, stamp);
            if (!bounds.empty()) {
              assert(bounds.x1 - bounds.x0 < 20 && bounds.y1 - bounds.y0 < 20);
              assert(recorded.page_count(0) <= 4);
            }
          }
          for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
              assert(cpu.sample(0, x, y) == recorded.sample(0, x, y));
        }
  PaintPixels pixels;
  PaintPixels::Stamp far{{.9, .9}, {.01, 0}, {0, .01}, {}};
  far.channels = {true, false, false, false, false};
  far.values.base_color = {1, 0, 0, 1};
  const auto dab = surface_stamp();
  {
    PaintRecordingScope gpu;
    pixels.stamp(4096, far);
    const auto before = pixels.page_versions();
    const auto bounds = pixels.stamp(4096, dab);
    assert(bounds.x1 - bounds.x0 < 220 && bounds.y1 - bounds.y0 < 220);
    const auto after = pixels.page_versions();
    const std::uint32_t key = 57u * 65536 + 57;
    assert(before[0].at(key) == after[0].at(key));
    assert(after[0].size() < 40);
  }
  auto tiled = dab;
  tiled.center[0] += 1;
  auto surface = std::make_shared<PaintPixels::SurfaceFootprint>(*tiled.surface);
  for (auto &triangle : surface->triangles)
    for (auto &uv : triangle.uv)
      uv[0] += 1;
  tiled.surface = surface;
  TilePaintPixels tiles;
  {
    PaintRecordingScope gpu;
    tiles.stamp_working(4096, tiled, {1001, 1002});
    assert(tiles.tile(1001).empty());
    assert(tiles.tile(1002).page_count(0) < 40);
  }
  assert(tiles.sample(0, 1434, 1434, 1002)[3] > 0);
  // A single contact may cross distant UV islands. Keep their gap untouched.
  auto split = dab;
  auto islands = std::make_shared<PaintPixels::SurfaceFootprint>(*split.surface);
  for (int i = 0; i < 2; ++i)
    for (auto &uv : islands->triangles[i].uv)
      for (double &v : uv)
        v = v * .25 + (i ? .65 : .1);
  split.surface = islands;
  PaintPixels scattered;
  far.center = {.5, .5};
  {
    PaintRecordingScope gpu;
    scattered.stamp(4096, far);
    const auto before = scattered.page_versions();
    scattered.stamp(4096, split);
    const auto after = scattered.page_versions();
    assert(after[0].size() < 20);
    const auto middle = 32u * 65536 + 32;
    assert(before[0].at(middle) == after[0].at(middle));
  }
  PaintPixels oracle, recorded;
  oracle.stamp(128, split);
  {
    PaintRecordingScope gpu;
    recorded.stamp(128, split);
  }
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x)
      assert(oracle.sample(0, x, y) == recorded.sample(0, x, y));
  auto distant = surface_stamp(2, 2);
  {
    PaintRecordingScope gpu;
    PaintPixels empty;
    assert(empty.stamp(4096, distant).empty());
  }
  std::puts(
      "Surface dirty bounds: coarse 4K brush-local pages, unchanged far revision, rotated/depth/unwrapped exact CPU oracle and UDIM1002 PASS");
}
