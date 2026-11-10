// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushPresetThumbnailStroke.hh"
#include "PaintRecording.hh"
#include <cassert>
#include <limits>
using namespace bts;
int main()
{
  const auto cpu_before = local_paint_materializations;
  BrushSettings brush;
  const auto original = brush;
  const auto first = brush_thumbnail_stroke(brush), again = brush_thumbnail_stroke(brush);
  assert(!first.stamps.empty() && first.stamps.size() == again.stamps.size());
  bool curved = false, pressure_varies = false;
  for (std::size_t i = 0; i < first.stamps.size(); ++i) {
    const auto &a = first.stamps[i], &b = again.stamps[i];
    assert(a.center == b.center && a.axis_x == b.axis_x && a.axis_y == b.axis_y &&
           a.flow == b.flow);
    assert((a.values.base_color == std::array<float, 4>({1, 1, 1, 1})));
    if (i) {
      curved |= std::abs(a.center[1] - first.stamps.front().center[1]) > .05;
      pressure_varies |= a.axis_x != first.stamps.front().axis_x;
    }
  }
  assert(curved && pressure_varies && brush.values == original.values &&
         brush.diameter == original.diameter);
  auto sparse = brush;
  sparse.spacing = 90;
  assert(brush_thumbnail_stroke(sparse).stamps.size() < first.stamps.size());
  auto flow = brush;
  flow.pressure_flow = true;
  const auto pressure = brush_thumbnail_stroke(flow);
  bool varies = false;
  for (const auto &stamp : pressure.stamps)
    varies |= stamp.flow != pressure.stamps.front().flow;
  assert(varies);
  auto jitter = brush;
  jitter.jitter = {50, 20, 180, 30};
  const auto scattered = brush_thumbnail_stroke(jitter);
  assert(scattered.stamps.front().center != first.stamps.front().center);
  assert(brush_thumbnail_stroke(jitter).stamps.front().axis_x == scattered.stamps.front().axis_x);
  auto dynamic = brush;
  dynamic.alpha.kind = AlphaKind::DynamicOutline;
  const auto phases = brush_thumbnail_stroke(dynamic);
  assert(phases.stamps.size() > 2 &&
         phases.stamps[0].alpha.dynamic.phase != phases.stamps[1].alpha.dynamic.phase);
  assert(dynamic.alpha.dynamic.phase == 0);
  auto following = brush;
  following.follow_path = true;
  const auto directed = brush_thumbnail_stroke(following);
  assert(!directed.stamps.empty() && directed.stamps.front().axis_x[1] != 0);
  auto lazy = brush;
  lazy.lazy_mouse = true;
  assert(brush_thumbnail_stroke(lazy).stamps.back().center != first.stamps.back().center);
  auto ink = brush;
  ink.values.base_color = {.1, .2, .9, 1};
  ink.mask_value = .2;
  ink.erase = ink.smudge = ink.clone = true;
  assert(brush_thumbnail_source_key(ink) == brush_thumbnail_source_key(brush));
  assert(brush_thumbnail_stroke(ink).stamps.front().values == first.stamps.front().values);
  auto stencil = brush;
  stencil.stencil.resource = BrushAlpha{};
  const auto with_stencil = brush_thumbnail_stroke(stencil);
  assert(with_stencil.stamps.front().stencil &&
         with_stencil.stamps.front().stencil->stencil.same_image(stencil.stencil));
  assert(brush_thumbnail_source_key(stencil) != brush_thumbnail_source_key(brush));
  auto projected = brush;
  projected.projected = ProjectionMaterial{};
  projected.projected->weave = true;
  const auto projected_ink = projected.projected->values;
  const auto projection = brush_thumbnail_stroke(projected);
  assert(projection.stamps.front().projected && projection.stamps.front().projected->source.weave);
  assert(projected.projected->values == projected_ink);
  auto invalid = brush;
  invalid.spacing = std::numeric_limits<float>::quiet_NaN();
  bool rejected = false;
  try {
    brush_thumbnail_stroke(invalid);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  assert(local_paint_materializations == cpu_before);
}
