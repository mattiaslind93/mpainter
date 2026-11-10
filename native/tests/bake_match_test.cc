// SPDX-License-Identifier: GPL-3.0-or-later
#include "BakeMatch.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main()
{
  using bts::bake_names_match;
  for (auto name : {"body_high", "body_high_top", "body_high_1", "body_high_2"})
    assert(bake_names_match("body_low", name));
  for (auto name : {"body-high", "body_top_high"})
    assert(!bake_names_match("body_low", name));
  assert(bake_names_match("Head_low", "Head_high") && !bake_names_match("Head_low", "head_high"));
  for (auto name : {"Leg_high", "Leg_high_top", "Leg_high_high_top"})
    assert(bake_names_match("Leg_low_top", name));
  assert(!bake_names_match("Leg_low_top", "Leg_top_high"));
  assert(bake_names_match("Body_LOW_top", "Body_HIGH_top", "_LOW", "_HIGH"));
  assert(!bake_names_match("Body_low_top", "Body_HIGH_top", "_LOW", "_HIGH"));
  assert(bake_names_match("identical", "identical") && !bake_names_match("other", "identical"));
  assert(bake_names_match("same", "same", "", "") &&
         !bake_names_match("same_low", "same_high", "", ""));
  assert(bts::bake_ignores_backfaces("body_high_ignorebf_top", "_ignorebf") &&
         !bts::bake_ignores_backfaces("body_high_IgnoreBF", "_ignorebf") &&
         !bts::bake_ignores_backfaces("body_high", ""));
  std::puts(
      "bake matching: keyword positions, multiple high meshes, geometry case, custom markers, missing/empty markers and secondary-ray flags PASS");
}
