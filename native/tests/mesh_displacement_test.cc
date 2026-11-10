// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshDisplacementSample.hh"
#include <array>
#include <cassert>
#include <limits>
#include <cstdio>
int main()
{
  const std::array<float, 16> image{-.2f, 0, 0, 1, .8f, 0, 0, 1, 1.2f, 0, 0, 1, .4f, 0, 0, 1};
  const auto close = [](float a, float b) { assert(std::abs(a - b) < 1e-6f); };
  close(bts::sample_mesh_displacement(image, 2, 2, .25, .25, .5, false), -.2);
  close(bts::sample_mesh_displacement(image, 2, 2, .5, .5, .5, false), .55);
  close(bts::sample_mesh_displacement(image, 2, 2, 0, .25, .5, false), -.2);
  close(bts::sample_mesh_displacement(image, 2, 2, 1, .25, .5, false), .8);
  for (float u : {-1.f, 0.f, 1.f, 2.f})
    close(bts::sample_mesh_displacement(image, 2, 2, u, .25, .5, true), .3);
  const std::array<float, 8> associated{-.1f, 0, 0, .5f, .8f, 0, 0, 1};
  close(bts::sample_mesh_displacement(associated, 2, 1, .5, .5, .5, false), .475);
  const std::array<float, 4> empty{};
  close(bts::sample_mesh_displacement(empty, 1, 1, .5, .5, .7, true), .7);
  const std::array<float, 4> hdr{1, 0, 0, .5};
  close(bts::sample_mesh_displacement(hdr, 1, 1, .5, .5, .5, true), 1.25);
  for (int test = 0; test < 3; ++test) {
    bool rejected = false;
    try {
      bts::sample_mesh_displacement(image,
                                    test == 0 ? 0 : 2,
                                    2,
                                    test == 1 ? std::numeric_limits<float>::quiet_NaN() : .5,
                                    .5,
                                    test == 2 ? std::numeric_limits<float>::infinity() : .5,
                                    false);
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  std::puts(
      "Signed/HDR bilinear displacement, premultiplied coverage, Midlevel fallback, repeat/clamp and invalid inputs PASS");
}
