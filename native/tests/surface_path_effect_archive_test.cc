// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeArchive.hh"
#include "SurfacePathIO.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "EffectPaths";
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "EffectPaths"}};
  TextureDocument doc({}, imported, scene);
  auto &set = doc.set(1);
  set.texture_resolution = 128;
  TextureDocument::add_paint_effect(set);
  auto *effect = TextureDocument::paint_effect(set);
  assert(effect);
  effect->resolution = 128;
  auto path = std::make_shared<SurfacePath>();
  path->id = "c0a80001-1234-4000-8000-000000000001";
  path->points = {{{-.5, 0, 0}, {0, 0, 1}, {.25, .5}, 1}, {{.5, 0, 0}, {0, 0, 1}, {.75, .5}, 1}};
  path->symmetry.enabled = true;
  path->symmetry.offset = .3;
  path->symmetry.intersection = false;
  path->brush.mask_value = .25;
  StrokeRaster ink;
  PaintPixels::Stamp stamp{{.5, .5}, {.1, 0}, {0, .1}};
  stamp.channels = {true, false, false, false, false};
  stamp.values.base_color = {.8, .2, .1, 1};
  ink.paint.stamp(128, stamp);
  effect->strokes = append_surface_path({}, {}, ink, path, 128);
  effect->pixels = replay_strokes(*effect->strokes, 128).paint;
  StrokeRaster user;
  stamp.channels = {false, false, true, false, false};
  stamp.values.roughness = .7;
  user.paint.stamp(128, stamp);
  effect->user_strokes[0] = append_surface_path({}, {}, user, path, 128);
  effect->users[0] = replay_strokes(*effect->user_strokes[0], 128).paint;
  const auto json = doc.to_json();
  const std::array<TextureSetBinding, 1> bindings{{{set.id, 1, "EffectPaths"}}};
  auto restored = TextureDocument::from_json(json, bindings);
  read_stroke_archive(restored, write_stroke_archive(doc));
  const auto *loaded = TextureDocument::paint_effect(restored.set(1));
  assert(loaded && loaded->strokes && loaded->user_strokes[0] && loaded->strokes->loaded &&
         loaded->user_strokes[0]->loaded);
  assert(surface_path_json(*loaded->strokes->operations[0]->path) == surface_path_json(*path));
  assert(restored.to_json() == json && loaded->pixels.hash(0) == effect->pixels.hash(0) &&
         loaded->users[0].hash(2) == effect->users[0].hash(2));
  limit_stroke_history(restored, 0);
  const auto spill = wait_history_spill();
  assert(spill && spill->error.empty() && TextureDocument::paint_effect(restored.set(1))->strokes);
  std::cout
      << "Paint-effect path and user-channel BTP1 archive, exact profile/symmetry/gray metadata, packed rasters and pinned memory budget passed\n";
}
