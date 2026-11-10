// SPDX-License-Identifier: GPL-3.0-or-later
#include "TemporaryInk.hh"
#include <cassert>
#include <cstdio>
int main()
{
  bts::MaterialValues primary;
  primary.base_color = {1, 0, 0, 1};
  const std::array<float, 4> secondary{0, 0, 1, 1};
  assert(bts::temporary_ink(primary, secondary, false).base_color == primary.base_color);
  assert(bts::temporary_ink(primary, secondary, true).base_color == secondary);
  assert(primary.base_color[0] == 1 && primary.base_color[2] == 0);
  assert(bts::default_mask_ink(0) == 1 && bts::default_mask_ink(1) == 0);
  for (float background : {0.f, 1.f}) {
    const auto ink = bts::default_mask_ink(background);
    assert(bts::temporary_mask_ink(ink, false) == 1 - background);
    assert(bts::temporary_mask_ink(ink, true) == background);
  }
  assert(bts::temporary_mask_ink(.25f, true) == .75f);
  std::puts("Temporary ink: immutable primary/secondary and black/white mask inversion PASS");
}
