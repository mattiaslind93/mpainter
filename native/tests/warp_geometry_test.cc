// SPDX-License-Identifier: GPL-3.0-or-later
#include "WarpGeometry.hh"
#include <cassert>
#include <iostream>
using namespace bts;
bool near(const WarpVector &a, const WarpVector &b, double epsilon = 1e-9)
{
  for (int c = 0; c < 3; ++c)
    if (std::abs(a[c] - b[c]) > epsilon)
      return false;
  return true;
}
int main()
{
  WarpGrid authored;
  WarpFitFrame frame;
  const auto original = authored.nodes;
  // Analytic curved sheet y = .3*x^2 + .1*z^2. Rays point along +Y.
  WarpSurfaceRay sheet =
      [](const auto &origin, const auto &direction, double maximum) -> std::optional<WarpVector> {
    if (std::abs(direction[1]) < 1e-12)
      return {};
    const double y = .3 * origin[0] * origin[0] + .1 * origin[2] * origin[2],
                 distance = (y - origin[1]) / direction[1];
    if (distance < 0 || distance > maximum)
      return {};
    return warp_add(origin, direction, distance);
  };
  auto fitted = fit_warp_geometry(authored, frame, 3, sheet);
  assert(fitted.hits == 9 && fitted.misses == 0);
  for (int i = 0; i < 9; ++i) {
    const auto p = fitted.grid->nodes[i].position;
    assert(std::abs(p[1] - (.3 * p[0] * p[0] + .1 * p[2] * p[2])) < 1e-9);
    assert(near(authored.nodes[i].position, original[i].position));
  }
  authored.at(1, 1).position[0] += .12;
  authored.at(1, 1).position[1] += .07;
  authored.align_tangents();
  auto deformed = fit_warp_geometry(authored, frame, 3, sheet);
  const auto p = deformed.grid->at(1, 1).position;
  assert(std::abs(p[0] - .12) < 1e-9 && std::abs(p[1] - (.3 * .12 * .12 + .07)) < 1e-9);
  frame.origin = {.4, 0, .2};
  auto moved = fit_warp_geometry(authored, frame, 3, sheet);
  assert(!near(moved.grid->at(0, 0).position, deformed.grid->at(0, 0).position));
  const auto world = frame.world(moved.grid->at(1, 1).position);
  assert(std::abs(world[1] - (.3 * world[0] * world[0] + .1 * world[2] * world[2] + .07)) < 1e-9);
  auto edited = *moved.grid;
  edited.at(0, 0).position[0] += .08;
  edited.at(0, 0).position[1] += .03;
  edited.align_tangents();
  auto rebased = rebase_warp_edit(authored, *moved.grid, edited);
  assert(std::abs(rebased->at(0, 0).position[0] - authored.at(0, 0).position[0] - .08) < 1e-9 &&
         std::abs(rebased->at(0, 0).position[1] - authored.at(0, 0).position[1] - .03) < 1e-9);
  auto split = *moved.grid;
  split.split(true, .25);
  auto split_author = rebase_warp_edit(authored, *moved.grid, split);
  assert(split_author->width() == 4);
  for (double v : {.1, .5, .9})
    for (double u : {.1, .25, .8})
      assert(near(split_author->evaluate(u, v).position, authored.evaluate(u, v).position, 1e-8));
  authored.automatic_tangents = false;
  authored.at(1, 1).du[1] += .2;
  auto manual = fit_warp_geometry(authored, frame, 3, sheet);
  auto auto_author = authored;
  auto_author.automatic_tangents = true;
  auto_author.align_tangents();
  auto smooth = fit_warp_geometry(auto_author, frame, 3, sheet);
  assert(std::abs(manual.grid->at(1, 1).du[1] - smooth.grid->at(1, 1).du[1] - .2) < 1e-8);
  auto miss = fit_warp_geometry(
      authored, frame, 3, [](const auto &, const auto &, double) -> std::optional<WarpVector> {
        return {};
      });
  assert(miss.hits == 0 && miss.misses == 9);
  for (int i = 0; i < 9; ++i)
    assert(near(miss.grid->nodes[i].position, authored.nodes[i].position) &&
           near(miss.grid->nodes[i].du, authored.nodes[i].du));
  auto author_pointer = std::make_shared<const WarpGrid>(authored);
  auto on = change_warp_geometry_mode(author_pointer, {}, {}, true);
  assert(on.settings->enabled);
  auto off = change_warp_geometry_mode(on.grid, on.settings, manual.grid, false);
  assert(!off.settings->enabled && off.grid == manual.grid &&
         off.settings->authored_reference == author_pointer);
  auto manual_off = std::make_shared<WarpGrid>(*off.grid);
  manual_off->at(1, 1).position[0] += .04;
  auto resumed = change_warp_geometry_mode(manual_off, off.settings, {}, true);
  assert(resumed.settings->enabled && !resumed.settings->authored_reference);
  assert(std::abs(resumed.grid->at(1, 1).position[0] - authored.at(1, 1).position[0] - .04) <
         1e-8);
  auto unchanged = change_warp_geometry_mode(off.grid, off.settings, {}, false);
  assert(unchanged.grid == off.grid && unchanged.settings == off.settings);
  const double c = std::cos(.4), s = std::sin(.4);
  frame.axes = {WarpVector{2 * c, 2 * s, 0}, WarpVector{-3 * s, 3 * c, 0}, WarpVector{0, 0, 4}};
  frame.validate();
  const WarpVector point{.2, .3, -.7};
  assert(near(frame.local(frame.world(point)), point));
  for (auto &axis : frame.axes)
    for (double &v : axis)
      v *= 1e-10;
  frame.validate();
  assert(near(frame.local(frame.world(point)), point, 1e-5));
  frame.axes[2] = frame.axes[1];
  bool invalid = false;
  try {
    frame.validate();
  }
  catch (const std::invalid_argument &) {
    invalid = true;
  }
  assert(invalid);
  std::cout
      << "Warp geometry curved fit, authored offsets, movement, fitted-edit rebasing/split, custom tangents, misses and affine scale PASS\n";
}
