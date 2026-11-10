// SPDX-License-Identifier: GPL-3.0-or-later
#include "FragmentTangents.hh"
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
  using namespace bts;
  UvFaceTriangle triangle;
  triangle.positions = {{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
  triangle.corners = {{{0, 0}, {1, 0}, {0, 1}}};
  const auto raw = uv_tangent_frame(triangle);
  const std::array<float, 3> smooth{.4f, 0, std::sqrt(.84f)};
  const auto frame = fragment_tangent_frame(raw, smooth);
  auto neutral = fragment_world_to_tangent(frame, smooth);
  assert(std::abs(neutral[0]) < 1e-6 && std::abs(neutral[1]) < 1e-6 &&
         std::abs(neutral[2] - 1) < 1e-6);
  assert(std::abs(frame[0][0] - std::sqrt(.84f)) < 1e-6 && std::abs(frame[0][2] + .4f) < 1e-6);
  for (auto &uv : triangle.corners)
    uv[0] = 1 - uv[0];
  const auto mirror = fragment_tangent_frame(uv_tangent_frame(triangle), smooth);
  for (int c = 0; c < 3; ++c) {
    assert(std::abs(mirror[0][c] + frame[0][c]) < 1e-6);
    assert(std::abs(mirror[1][c] - frame[1][c]) < 1e-6);
  }
  assert(fragment_tangent_frame(raw, {0, 0, 0}) == raw);
  triangle.positions[1][0] = -1;
  triangle.corners = {{{0, 0}, {1, 0}, {0, 1}}};
  const auto reflected = fragment_tangent_frame(uv_tangent_frame(triangle), {0, 0, 1});
  assert(reflected[0][0] == -1 && reflected[1][1] == 1 && reflected[2][2] == 1);
  std::cout
      << "Per-fragment UV basis: smooth-normal orthogonality, neutral world normal, mirrored UV handedness and degenerate fallback passed\n";
}
