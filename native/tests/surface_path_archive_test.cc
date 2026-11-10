// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeArchive.hh"
#include "StrokeMetadata.hh"
#include <QUuid>
#include "SurfacePathIO.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Paths";
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "Paths"}};
  TextureDocument doc({}, imported, scene);
  auto &set = doc.set(1);
  set.texture_resolution = 128;
  auto &layer = set.layers[0];
  layer.author_resolution = 128;
  auto path = std::make_shared<SurfacePath>();
  path->id = "c0a80001-1234-4000-8000-000000000001";
  path->name = "Retained path";
  path->seed = 0xf0123456789abcdULL;
  path->points = {{{-.5, 0, 0}, {0, 0, 1}, {.25, .5}, .3}, {{.5, 0, 0}, {0, 0, 1}, {.75, .5}, .8}};
  path->brush.alpha.kind = AlphaKind::DynamicOutline;
  path->brush.alpha.dynamic.cycle = 4;
  path->brush.alpha.dynamic.random_start = true;
  path->brush.alpha.dynamic.width = .2;
  path->brush.stroke_opacity = 60;
  StrokeRaster ink;
  PaintPixels::Stamp stamp{{.5, .5}, {.1, 0}, {0, .1}};
  stamp.channels = {true, false, false, false, false};
  stamp.values.base_color = {1, 0, 0, 1};
  ink.paint.stamp(128, stamp);
  layer.strokes = append_surface_path({}, {}, ink, path, 128);
  layer.pixels = replay_strokes(*layer.strokes, 128).paint;
  const auto json = doc.to_json();
  assert(json["schema"].toInt() == bts::texture_document_schema);
  const std::array<TextureSetBinding, 1> bindings{{{set.id, 1, "Paths"}}};
  auto restored = TextureDocument::from_json(json, bindings);
  restored.set(1).layers[0].pixels = layer.pixels;
  read_stroke_archive(restored, write_stroke_archive(doc));
  const auto &loaded = restored.set(1).layers[0];
  assert(loaded.strokes->operations[0]->path &&
         surface_path_json(*loaded.strokes->operations[0]->path) == surface_path_json(*path));
  assert(restored.to_json() == json && loaded.pixels.hash(0) == layer.pixels.hash(0));
  limit_stroke_history(restored, 0);
  const auto spill = wait_history_spill();
  assert(spill && spill->error.empty() && has_surface_paths(restored.set(1).layers[0].strokes));
  bool rejected = false;
  auto hidden = std::make_shared<SurfacePath>(*path);
  hidden->visible = false;
  auto &target = restored.set(1).layers[0];
  target.strokes = edit_surface_path(
      target.strokes, path->id, hidden, std::make_shared<StrokeRaster>(ink), 128);
  target.pixels = replay_strokes(*target.strokes, 128).paint;
  assert(target.pixels.empty());
  const auto hidden_json = restored.to_json();
  auto reopened = TextureDocument::from_json(hidden_json, bindings);
  read_stroke_archive(reopened, write_stroke_archive(restored));
  assert(reopened.to_json() == hidden_json &&
         !reopened.set(1).layers[0].strokes->operations[0]->path->visible);
  auto malformed = surface_path_json(*path);
  malformed["seed"] = "invalid";
  rejected = false;
  try {
    read_surface_path(malformed);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  const auto history = stroke_history_json(layer.strokes, {layer.pixels, {}}).toObject();
  auto duplicate = history;
  auto paths = history["paths"].toArray();
  paths.append(paths[0]);
  duplicate["paths"] = paths;
  rejected = false;
  try {
    read_stroke_history_json(duplicate);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  auto legacy = surface_path_json(*path);
  legacy.remove("version");
  legacy.remove("closed");
  auto legacy_points = legacy["points"].toArray();
  for (int i = 0; i < legacy_points.size(); ++i) {
    auto point = legacy_points[i].toObject();
    point.remove("curve");
    legacy_points[i] = point;
  }
  legacy["points"] = legacy_points;
  const auto old = read_surface_path(legacy);
  assert(!old->closed && !old->points[0].smooth && !old->points[0].custom &&
         old->points[0].position == path->points[0].position);
  auto curved = std::make_shared<SurfacePath>(*path);
  curved->closed = true;
  curved->points[0].smooth = true;
  curved->points[1].custom = true;
  curved->points[1].tangent_in = {-.2, .1, 0};
  curved->points[1].tangent_out = {.2, -.1, 0};
  assert(surface_path_json(*read_surface_path(surface_path_json(*curved))) ==
         surface_path_json(*curved));
  auto future = surface_path_json(*curved);
  future["version"] = 7;
  rejected = false;
  try {
    read_surface_path(future);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::shared_ptr<const StrokeHistory> many_paths;
  for (int i = 0; i < 140; ++i) {
    auto entry = std::make_shared<SurfacePath>();
    entry->id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    entry->visible = false;
    entry->points = {{{0, 0, 0}, {0, 0, 1}, {0, 0}, 1}, {{1, 0, 0}, {0, 0, 1}, {1, 0}, 1}};
    many_paths = append_surface_path(many_paths, {}, {}, entry, 128);
  }
  const auto metadata = stroke_history_json(many_paths, {});
  assert(read_stroke_history_json(metadata)->declared_paths.size() == 140);
  std::cout
      << "Editable path schema45, exact brush/point/seed archive roundtrip, hidden path retained, memory-limit protection, malformed identity and duplicate-index rejection passed\n";
}
