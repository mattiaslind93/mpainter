// SPDX-License-Identifier: GPL-3.0-or-later
#include "LazyMouse.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main()
{
  bts::LazyMouse mouse;
  mouse.reset({200, 900});
  assert((mouse.follow({650, 900}, 800) == bts::LazyMouse::Point{200, 900}));
  mouse.reset({200, 900});
  assert((mouse.follow({650, 900}, 80) == bts::LazyMouse::Point{570, 900}));
  assert((mouse.follow({650, 900}, 80) == bts::LazyMouse::Point{570, 900}));
  auto p = mouse.follow({650, 1000}, 80);
  assert(std::abs(std::hypot(650 - p[0], 1000 - p[1]) - 80) < 1e-10);
  mouse.reset({0, 0});
  for (int i = 0; i < 100; ++i) {
    auto q = mouse.follow({i % 2 ? 10. : -10., 0}, 20);
    assert(q[0] == 0 && q[1] == 0);
  }
  assert((mouse.follow({100, 50}, 0) == bts::LazyMouse::Point{100, 50}));
  bool rejected = false;
  try {
    mouse.follow({0, 0}, -1);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::puts(
      "lazy mouse: large dead zone, exact 80px lag, no release catch-up, taut corner, jitter rejection and zero radius PASS");
}
