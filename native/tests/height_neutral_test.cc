// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushSettings.hh"
#include "BumpPrecision.hh"
#include "HeightNeutralResourceUnit.hh"
#include "SurfacePathIO.hh"
#include "TextureDocument.hh"
#include <cassert>
#include <cstdio>
using namespace bts;
int main()
{
  verify_height_neutral_resource_coordinates();
  {
    TextureSet old;
    old.bump_origin = 0;
    old.base_values.bump = .0001f;
    old.precision[4] = ChannelPrecision::L16F;
    const float display = .5f + float(qfloat16(.0001f));
    assert(precision_bump_output(old, display) == display && precision_bump_base(old) == display);
  }
  SurfacePath path;
  path.id = "c0a80001-1234-4000-8000-000000000001";
  path.name = "Neutral coordinates";
  path.points = {{{0, 0, 0}, {0, 0, 1}, {.25, .5}, 1}, {{1, 0, 0}, {0, 0, 1}, {.75, .5}, 1}};
  auto stored = surface_path_in_origin(path, 0);
  assert(stored.brush.values.bump == 0 && stored.bump_origin == 0);
  assert(surface_path_in_origin(stored, .5f).brush.values.bump == .5f);
  assert(read_surface_path(surface_path_json(stored))->bump_origin == 0);
  auto old_path = surface_path_json(stored);
  old_path["version"] = 5;
  old_path.remove("bump_origin");
  assert(read_surface_path(old_path)->brush.values.bump == 0 &&
         read_surface_path(old_path)->bump_origin == 0);
  assert(MaterialValues{}.bump == .5f && new_base_values().bump == .5f &&
         BrushSettings{}.values.bump == .5f);
  assert(DisplacementSettings{}.midlevel == 0);
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Neutral";
  material.uniform_editable = true;
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "Neutral"}};
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  TextureDocument document(options, imported, scene);
  auto &set = document.set(1);
  assert(set.bump_origin == .5f);
  assert(TextureDocument::composite(set).material.bump == .5f);
  assert(TextureDocument::displacement_value(set, 64, 64) == 0);
  TextureDocument::add_fill(set);
  assert(set.layers.back().values.bump == .5f);
  assert(TextureDocument::composite(set).material.bump == .5f);
  TextureDocument::add_displacement_channel(set);
  for (float mid : {0.f, .5f}) {
    set.displacement.midlevel = mid;
    set.layers.back().displacement.enabled = false;
    assert(TextureDocument::displacement_value(set, 64, 64) == mid);
    set.layers.back().displacement.enabled = true;
    set.layers.back().displacement.value = mid;
    assert(TextureDocument::displacement_value(set, 64, 64) == mid);
  }
  const std::array<TextureSetBinding, 1> bindings{{{set.id, 1, set.name}}};
  auto json = document.to_json();
  auto modern = TextureDocument::from_json(json, bindings);
  assert(modern.set(1).bump_origin == .5f && modern.set(1).displacement.midlevel == .5f);
  // Real previous schemas have no origin field. Preserve signed authoring,
  // nonlinear operations and page/history identities; shift only presentation.
  auto legacy = document;
  auto &old = legacy.set(1);
  old.bump_origin = 0;
  old.base_values.bump = 0;
  old.layers.front().values.bump = 0;
  old.layers.back().values.bump = .2f;
  auto raw = legacy.to_json();
  raw["schema"] = 75;
  auto sets = raw["texture_sets"].toArray();
  auto metadata = sets[0].toObject();
  metadata.remove("bump_origin");
  sets[0] = metadata;
  raw["texture_sets"] = sets;
  auto opened = TextureDocument::from_json(raw, bindings);
  assert(opened.set(1).bump_origin == 0);
  assert(opened.set(1).layers.back().values.bump == .2f);
  assert(std::abs(TextureDocument::composite(opened.set(1)).material.bump - .7f) < 1e-6);
  auto &working = opened.set(1);
  TextureDocument::add_fill(working);
  assert(working.layers.back().values.bump == 0 &&
         working.bump_display(working.layers.back().values.bump) == .5f);
  working.layers.back().values.bump = working.bump_storage(.8f);
  assert(std::abs(TextureDocument::composite(working).material.bump - .8f) < 1e-6);
  auto again = TextureDocument::from_json(opened.to_json(), bindings);
  assert(again.set(1).bump_origin == 0 &&
         again.set(1).bump_display(again.set(1).layers.back().values.bump) ==
             working.bump_display(working.layers.back().values.bump));
  auto invalid = document.to_json();
  sets = invalid["texture_sets"].toArray();
  metadata = sets[0].toObject();
  metadata["bump_origin"] = .25;
  sets[0] = metadata;
  invalid["texture_sets"] = sets;
  bool rejected = false;
  try {
    TextureDocument::from_json(invalid, bindings);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  std::puts(
      "Fixed Bump .5, Displacement 0/.5, schema76 legacy signed-zero coordinate and normalized editing/output PASS");
}
