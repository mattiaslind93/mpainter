// SPDX-License-Identifier: GPL-3.0-or-later
#include "DisplayPostEffects.hh"
#include <cassert>
#include <limits>
int main()
{
  using namespace bts;
  DisplayPostSettings value;
  assert(!value.enabled && !value.vignette && !value.active() && value.vignette_strength == .2f);
  value.enabled = true;
  assert(!value.active());
  value.vignette = true;
  assert(value.active());
  value.vignette_strength = 1;
  assert(DisplayPostSettings::from_json(value.to_json()) == value);
  value.enabled = false;
  assert(!value.active() && value.vignette_strength == 1 && value.vignette);
  assert(DisplayPostSettings::from_json({}) == DisplayPostSettings{});
  for (float invalid : {-1.f, 2.f, std::numeric_limits<float>::quiet_NaN()}) {
    auto bad = value;
    bad.vignette_strength = invalid;
    bool failed = false;
    try {
      bad.to_json();
    }
    catch (...) {
      failed = true;
    }
    assert(failed);
  }
  DisplayPostSettings neutral;
  neutral.enabled = neutral.color_correction = neutral.tone_mapping = true;
  assert(!neutral.active());
  neutral.edit("exposure", 2);
  assert(neutral.active() && neutral.tone_active() && !neutral.color_active());
  neutral.edit("tone_reset", 1);
  assert(!neutral.active());
  auto legacy = DisplayPostSettings::from_json(
      {{"version", 1}, {"enabled", true}, {"vignette", true}, {"vignette_strength", .7}});
  assert(legacy.vignette && legacy.enabled && !legacy.color_correction && !legacy.tone_mapping &&
         legacy.vignette_strength == .7f);
  legacy.edit("color_correction", 1);
  legacy.edit("saturation", 0);
  legacy.edit("color_reset", 1);
  assert(legacy.color_correction && legacy.saturation == 1 && legacy.vignette_strength == .7f);
  const auto before = legacy;
  bool rejected = false;
  try {
    legacy.edit("gamma", 0);
  }
  catch (...) {
    rejected = true;
  }
  assert(rejected && legacy == before);
  legacy.edit("tone_function", 2);
  assert(DisplayPostSettings::from_json(legacy.to_json()) == legacy);
  auto older = legacy.to_json();
  older["version"] = 2;
  for (const auto *key : {"dof",
                          "glare",
                          "glare_shape",
                          "focus_distance",
                          "aperture",
                          "glare_luminance",
                          "glare_threshold",
                          "glare_remap",
                          "lens",
                          "lens_power",
                          "lens_fov",
                          "lens_roundness",
                          "lens_smoothness"})
    older.remove(key);
  auto v2 = DisplayPostSettings::from_json(older);
  assert(!v2.dof && !v2.glare && v2.saturation == legacy.saturation &&
         v2.tone_function == legacy.tone_function);
  v2.edit("dof", 1);
  assert(!v2.dof_active());
  v2.edit("aperture", 2);
  assert(v2.dof_active());
  v2.edit("dof_reset", 1);
  assert(!v2.dof_active() && v2.dof);
  v2.edit("glare", 1);
  assert(v2.glare_active());
  v2.edit("glare_luminance", 0);
  assert(!v2.glare_active());
  v2.edit("glare_shape", 2);
  assert(DisplayPostSettings::from_json(v2.to_json()) == v2);
  auto v3 = value.to_json();
  v3["version"] = 3;
  for (auto key : {"lens", "lens_power", "lens_fov", "lens_roundness", "lens_smoothness"})
    v3.remove(key);
  assert(!DisplayPostSettings::from_json(v3).lens);
  v2.edit("lens", 1);
  v2.edit("lens_power", 1.5);
  assert(v2.projection().enabled && v2.lens_active());
  assert(DisplayPostSettings::from_json(v2.to_json()) == v2);
  v2.edit("lens_reset", 1);
  assert(v2.lens_power == 1);
  auto corrupt = value.to_json();
  corrupt["vignette"] = 1;
  bool failed = false;
  try {
    DisplayPostSettings::from_json(corrupt);
  }
  catch (...) {
    failed = true;
  }
  assert(failed);
}
