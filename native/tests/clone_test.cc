// SPDX-License-Identifier: GPL-3.0-or-later
#include "CloneStroke.hh"
#include "PaintRecording.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  TilePaintPixels source;
  PaintPixels::Stamp cross{{.25, .5}, {.09, 0}, {0, .09}};
  cross.alpha.kind = AlphaKind::Plus;
  cross.channels = {true, false, true, false, false};
  cross.values.base_color = {.2, .5, .8, 1};
  cross.values.roughness = .8;
  source.stamp(128, cross);
  auto seed = cross;
  seed.axis_x = {.1, 0};
  seed.axis_y = {0, .1};
  seed.alpha = {};
  PaintPixels::Stamp target{{.7, .5}, {.1, 0}, {0, .1}};
  target.channels = cross.channels;
  target.values.base_color = {1, 0, 0, 1};
  CloneStroke fixed(source, 128, seed, true);
  TilePaintPixels copied;
  copied.stamp(128, fixed.dab(target));
  auto pixel = copied.sample(0, 89, 64);
  assert(pixel[3] > .95);
  for (int c = 0; c < 3; ++c)
    assert(std::abs(pixel[c] / pixel[3] - cross.values.base_color[c]) < 1e-5);
  assert(std::abs(copied.sample(2, 89, 64)[0] / copied.sample(2, 89, 64)[3] - .8) < 1e-5);
  assert(copied.sample(0, 77, 52)[3] == 0);
  target.center = {.7, .8};
  copied.stamp(128, fixed.dab(target));
  assert(copied.sample(0, 89, 102)[3] > .95 && source.sample(0, 32, 64)[3] > .99);
  assert(source.sample(0, 89, 102)[3] == 0);
  CloneStroke relative(source, 128, seed, false);
  target.center = {.6, .5};
  relative.dab(target);
  target.center = {.8, .5};
  auto advanced = relative.dab(target);
  assert(advanced.cloned->sample(0, 0, 0)[3] == 0);
  assert(std::abs(relative.last_source()->center[0] - .45) < 1e-12);
  assert(fixed.last_source()->center == seed.center);
  // Transparent source never erases existing destination ink.
  const auto before = copied;
  copied.stamp(128, advanced);
  assert(copied.hash(0) == before.hash(0));
  auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
  surface->center = {-.5, 0, 0};
  surface->radius = .2;
  surface->triangles = {{{{{0, 0}, {1, 0}, {1, 1}}}, {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}}}, {}},
                        {{{{0, 0}, {1, 1}, {0, 1}}}, {{{-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}}, {}}};
  seed.surface = surface;
  CloneStroke world(source, 128, seed, true);
  target.center = {.7, .5};
  auto dest = std::make_shared<PaintPixels::SurfaceFootprint>(*surface);
  dest->center = {.4, 0, 0};
  target.surface = dest;
  TilePaintPixels native;
  native.stamp(128, world.dab(target));
  assert(native.sample(0, 89, 64)[3] > .95);
  for (int c = 0; c < 3; ++c)
    assert(std::abs(native.sample(0, 89, 64)[c] / native.sample(0, 89, 64)[3] -
                    cross.values.base_color[c]) < 1e-5);
  auto history = append_stroke({}, {}, {source, {}}, {source, {}}, true, false, 1);
  auto output = source;
  output.over_stroke(source, native, .5, {0, 0, 128, 128});
  auto ledger = append_stroke(history, {source, {}}, {output, {}}, {native, {}}, true, false, .5);
  assert(stroke_hashes(replay_strokes(*ledger, 128)) == stroke_hashes({output, {}}));
  for (bool absolute : {false, true}) {
    seed.surface.reset();
    target.surface.reset();
    target.center = {.6, .5};
    CloneStroke lazy_clone(source, 128, seed, absolute), cpu_clone(source, 128, seed, absolute);
    TilePaintPixels lazy_pixels, cpu_pixels;
    for (int i = 0; i < 20; ++i) {
      target.center = {.6 + i * .007, .5};
      cpu_pixels.stamp(128, cpu_clone.dab(target));
    }
    const auto reads = local_paint_materializations;
    {
      PaintRecordingScope recording;
      for (int i = 0; i < 20; ++i) {
        target.center = {.6 + i * .007, .5};
        lazy_pixels.stamp(128, lazy_clone.dab(target));
      }
      assert(local_paint_materializations == reads);
    }
    for (int c = 0; c < 5; ++c)
      assert(lazy_pixels.hash(c) == cpu_pixels.hash(c));
  }
  std::cout
      << "Clone sampled shape/PBR, transparent source-over, repeated absolute copies, relative source displacement, immutable original, UV/world and packed stroke replay passed\n";
}
