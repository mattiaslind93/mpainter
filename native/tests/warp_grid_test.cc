// SPDX-License-Identifier: GPL-3.0-or-later
#include "WarpGrid.hh"
#include "WarpIO.hh"
#include "SpatialProjection.hh"
#include <cassert>
#include <iostream>
using namespace bts;
static void near(const WarpVector &a, const WarpVector &b, double tolerance = 1e-9)
{
  for (int c = 0; c < 3; ++c)
    assert(std::abs(a[c] - b[c]) < tolerance);
}
int main()
{
  WarpGrid grid;
  grid.validate();
  assert(grid.undeformed());
  for (double v : {-.2, 0., .13, .5, .84, 1., 1.2})
    for (double u : {-.1, 0., .17, .5, .93, 1., 1.1}) {
      const auto point = grid.evaluate(u, v);
      near(point.position, {u - .5, -.5, v - .5});
      near(point.du, {1, 0, 0});
      near(point.dv, {0, 0, 1});
      near(point.normal(), {0, -1, 0});
      const auto projected = grid.project(warp_add(point.position, point.normal(), .25));
      assert(projected && std::abs(projected->u - u) < 1e-8 && std::abs(projected->v - v) < 1e-8 &&
             std::abs(projected->depth - .25) < 1e-8);
    }
  grid.move(4, {.12, -.38, .08});
  assert(!grid.undeformed());
  for (double v : {.1, .35, .6, .9})
    for (double u : {.1, .4, .7, .9}) {
      const auto sample = grid.evaluate(u, v);
      const auto projected = grid.project(warp_add(sample.position, sample.normal(), .025));
      assert(projected && std::abs(projected->u - u) < 1e-5 && std::abs(projected->v - v) < 1e-5);
    }
  const WarpVector metric{4, .25, 9}, stretch{2, .5, 3};
  for (double u : {.2, .6, .85})
    for (double v : {.2, .4, .8}) {
      const auto sample = grid.evaluate(u, v);
      auto normal = sample.normal();
      for (int c = 0; c < 3; ++c)
        normal[c] /= stretch[c];
      normal = warp_normal(normal);
      auto point = sample.position;
      for (int c = 0; c < 3; ++c)
        point[c] += normal[c] * .03 / stretch[c];
      const auto projected = grid.project(point, metric);
      assert(projected && std::abs(projected->u - u) < 1e-5 && std::abs(projected->v - v) < 1e-5 &&
             std::abs(projected->depth - .03) < 1e-5);
    }
  const auto before = grid;
  grid.split(true, .31);
  grid.split(false, .73);
  assert(grid.width() == 4 && grid.height() == 4);
  for (int j = 0; j <= 20; ++j)
    for (int i = 0; i <= 20; ++i) {
      const double u = i / 20., v = j / 20.;
      const auto a = before.evaluate(u, v), b = grid.evaluate(u, v);
      near(a.position, b.position);
      near(a.du, b.du);
      near(a.dv, b.dv);
      near(a.duv, b.duv);
    }
  auto manual = before;
  manual.automatic_tangents = false;
  manual.at(1, 1).du = {.6, .3, .1};
  manual.at(1, 1).dv = {-.1, -.2, 1.2};
  const auto snapshot = manual;
  manual.split(true, .28);
  manual.split(false, .62);
  for (int j = 0; j < 20; ++j)
    for (int i = 0; i < 20; ++i) {
      const auto a = snapshot.evaluate(i / 19., j / 19.), b = manual.evaluate(i / 19., j / 19.);
      near(a.position, b.position);
      near(a.du, b.du);
      near(a.dv, b.dv);
    }
  bool rejected = false;
  try {
    grid.move(0, {NAN, 0, 0});
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  near(grid.at(0, 0).position, before.at(0, 0).position);
  grid.reset_vertices();
  assert(grid.undeformed());
  WarpGrid degenerate;
  for (auto &node : degenerate.nodes) {
    node.position = {};
    node.du = node.dv = node.duv = {};
  }
  assert(!degenerate.project({.1, 0, .2}));
  FillProjection projection;
  projection.mode = 5;
  projection.warp = std::make_shared<const WarpGrid>(before);
  projection.warp_depth = .7;
  projection.warp_edit = true;
  projection.warp_surface = false;
  projection.warp_handle_size = 9;
  projection.warp_color = {.1f, .6f, .8f};
  const auto json = warp_settings_json(projection);
  FillProjection restored;
  restored.mode = 5;
  read_warp_settings(restored, json);
  assert(warp_settings_json(restored) == json);
  const auto kept = warp_settings_json(restored);
  auto bad = json;
  bad["depth"] = -1;
  rejected = false;
  try {
    read_warp_settings(restored, bad);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected && warp_settings_json(restored) == kept);
  FillSurfaceAtlas atlas;
  atlas.extent = 1;
  FillSurfacePoint surface;
  surface.valid = true;
  surface.position = {0, 0, 0};
  surface.normal = {0, -1, 0};
  surface.footprint = .001f;
  FillProjection flat;
  flat.mode = 5;
  flat.filtering = 2;
  auto source = std::make_shared<FillTexture>();
  source->width = source->height = 8;
  auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(64);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      (*pixels)[y * 8 + x] = {x / 7.f, y / 7.f, 0, 1};
  source->pixels = pixels;
  flat.texture = source;
  auto sampled = fill_spatial_sample(flat, surface, atlas);
  assert(sampled[0] > .4 && sampled[0] < .7 && sampled[3] == 1);
  auto shifted = std::make_shared<WarpGrid>();
  shifted->move(4, {.18, -.5, 0});
  flat.warp = shifted;
  const auto bent = fill_spatial_sample(flat, surface, atlas);
  assert(bent[0] < sampled[0]);
  flat.warp_depth = .1;
  assert(fill_spatial_sample(flat, surface, atlas)[3] == 0);
  flat.warp_depth = 1;
  flat.warp.reset();
  surface.position = {.8f, 0, 0};
  flat.shape_crop = true;
  assert(fill_spatial_sample(flat, surface, atlas)[3] == 0);
  flat.shape_crop = false;
  flat.wrap = 1;
  assert(fill_spatial_sample(flat, surface, atlas)[3] == 0);
  flat.wrap = 0;
  assert(fill_spatial_sample(flat, surface, atlas)[3] > 0);
  // A regular grid with one tangent rotated 22 degrees needs more than 24
  // Gauss-Newton iterations. It is a valid surface point, not a crop hole.
  WarpGrid slow(4, 5);
  slow.automatic_tangents = false;
  slow.nodes[10].dv = {0, -.374606593415912, .927183854566787};
  const auto converged = slow.project({.282358605414629, 0, -.14257025718688965});
  assert(converged && converged->error < 1e-8);
  assert(std::abs(converged->u - .76022194) < 1e-6 && std::abs(converged->v - .38529678) < 1e-6);
  // True Hessian derivatives remain correct through boundary continuation.
  for (double u : {-.2, .13, .37, .82, 1.2})
    for (double v : {-.1, .16, .41, .87, 1.1}) {
      const double h = 1e-5;
      const auto value = manual.evaluate(u, v);
      near(value.duu,
           warp_scale(warp_add(manual.evaluate(u + h, v).du, manual.evaluate(u - h, v).du, -1),
                      .5 / h),
           1e-7);
      near(value.dvv,
           warp_scale(warp_add(manual.evaluate(u, v + h).dv, manual.evaluate(u, v - h).dv, -1),
                      .5 / h),
           1e-7);
      near(value.duv,
           warp_scale(warp_add(manual.evaluate(u, v + h).du, manual.evaluate(u, v - h).du, -1),
                      .5 / h),
           1e-7);
    }
  // Near the rotated tangent, tangent-plane iteration oscillated even after64
  // steps. The positive-definite distance Hessian reaches the surface directly.
  const auto curved = slow.project(
      {.14563985541462898, -1.0173929254619907e-8, -.13475775718688965});
  assert(curved && curved->error < 1e-8);
  assert(std::abs(curved->u - .65078136) < 1e-6 && std::abs(curved->v - .39241054) < 1e-6);
  std::cout
      << "Warp tensor patch: plane/continuation, bent normal projection, exact automatic/manual split position+derivatives, reset, degenerate and transactional invalid move passed\n";
}
