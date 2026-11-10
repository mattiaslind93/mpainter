// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExportPacking.hh"
#include <cassert>
#include <iostream>
int main()
{
  bts::MaterialValues value;
  value.roughness = .2f;
  value.metallic = .8f;
  value.ambient_occlusion = .7f;
  value.bump = -.1f;
  bts::PackedOutput output;
  assert((bts::packed_rgb(output, value, .4f) == std::array<float, 3>{.4f, .2f, .8f}));
  output.source = {
      bts::PackedSource::Metallic, bts::PackedSource::MixedAo, bts::PackedSource::Roughness};
  assert((bts::packed_rgb(output, value, .4f) == std::array<float, 3>{.8f, .4f, .2f}));
  output.source = {
      bts::PackedSource::AuthoredAo, bts::PackedSource::Bump, bts::PackedSource::Zero};
  assert((bts::packed_rgb(output, value, .4f) == std::array<float, 3>{.7f, -.1f, 0}));
  assert(bts::packed_scalar(bts::PackedSource::Displacement, value, .1f, .8f) == .8f);
  assert(bts::packed_scalar(bts::PackedSource::One, value, 0) == 1);
  bool rejected = false;
  try {
    bts::packed_scalar(bts::PackedSource(100), value, 1);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout << "Independent packed scalar sources, signed data and neutral slots passed\n";
}
