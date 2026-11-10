// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeArchive.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
int main()
{
  using namespace bts;
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "History";
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "History"}};
  TextureDocument doc({}, imported, scene);
  auto &set = doc.set(1);
  auto &layer = set.layers[0];
  StrokeRaster blank, ink, after;
  PaintPixels::Stamp stamp{{.5, .5}, {.1, 0}, {0, .1}};
  stamp.channels = {true, false, false, false, false};
  stamp.values.base_color = {1, 0, 0, 1};
  ink.paint.stamp(1024, stamp);
  after.paint.over_stroke(blank.paint, ink.paint, .5, {0, 0, 1024, 1024});
  layer.pixels = after.paint;
  layer.strokes = append_stroke({}, blank, after, ink, true, false, .5);
  const auto json = doc.to_json();
  const std::array<TextureSetBinding, 1> bindings{{{set.id, 1, "History"}}};
  auto restored = TextureDocument::from_json(json, bindings);
  restored.set(1).layers[0].pixels = layer.pixels;
  const auto archive = write_stroke_archive(doc);
  read_stroke_archive(restored, archive);
  assert(restored.to_json() == json && restored.set(1).layers[0].strokes->operations[0]->spatial);
  auto bad = archive;
  bad[bad.size() / 2] ^= char(0xff);
  bool rejected = false;
  try {
    read_stroke_archive(restored, bad);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  const auto flat = restored.set(1).layers[0].pixels.hash(0);
  assert(limit_stroke_history(restored, 0));
  const auto spilled = wait_history_spill();
  assert(spilled && spilled->error.empty() && spilled->written > 0 &&
         spilled->resident_after <= spilled->resident_before);
  assert(restored.set(1).layers[0].strokes && restored.set(1).layers[0].pixels.hash(0) == flat);
  assert(replay_strokes(*restored.set(1).layers[0].strokes, 1024).paint.hash(0) == flat);
  std::cout << "Sparse stroke archive, source provenance and corruption rejection passed\n";
}
