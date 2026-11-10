// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkewArchive.hh"
#include "SkewJSON.hh"
#include "BakeQuality.hh"
#include "LayerEditDiff.hh"
#include <QDataStream>
#include <QIODevice>
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Skew";
  material.uniform_editable = true;
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "Skew", {1001, 1002}}};
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  TextureDocument doc(options, imported, scene);
  auto &set = doc.set(1);
  const std::array bindings{TextureSetBinding{set.id, 1, set.name}};
  const auto rejects = [](auto &&operation) {
    bool bad = false;
    try {
      operation();
    }
    catch (const std::exception &) {
      bad = true;
    }
    assert(bad);
  };
  auto empty = TextureDocument::from_json(doc.to_json(), bindings);
  read_skew_archive(empty, write_skew_archive(doc));
  assert(empty.to_json() == doc.to_json());
  PaintPixels::Stamp stamp{{.5, .5}, {.24, 0}, {0, .24}};
  stamp.channels = {false, false, true, false, false};
  stamp.values.roughness = .8;
  stamp.flow = .5;
  stamp.hardness = 1;
  const auto ordinary = set.layers.front().pixels.hash(0);
  set.skew.pixels.stamp(128, stamp, set.tiles);
  stamp.center = {1.5, .5};
  set.skew.pixels.stamp(128, stamp, set.tiles);
  set.skew.settings.edge_contrast = .37;
  set.skew.settings.vector_density = 24;
  assert(set.skew.sample(1001, .5, .5) > .39 && set.skew.sample(1002, .5, .5) > .39);
  assert(set.layers.front().pixels.hash(0) == ordinary);
  const auto identity = doc.edit_state();
  const auto hashing = PaintPixels::hash_bytes();
  assert(doc.edit_state() == identity && PaintPixels::hash_bytes() == hashing);
  auto previous = doc;
  stamp.center = {.5, .5};
  stamp.values.roughness = 0;
  set.skew.pixels.stamp(128, stamp, set.tiles);
  assert(doc.edit_state() != identity &&
         previous.set(1).skew.sample(1001, .5, .5) > set.skew.sample(1001, .5, .5));
  assert(layer_edit_compatible(edit_set_state(previous.edit_state(), set.id),
                               edit_set_state(doc.edit_state(), set.id)));
  auto recipe = with_bake_quality(QJsonObject{{"auto_role", 3}, {"rays", 16}},
                                  set.id,
                                  3,
                                  {{"rays", 128}, {"diffusion", true}});
  doc.set_bake_configuration(recipe);
  auto invalid = recipe;
  invalid["map_options"] = QJsonObject{{"invalid", QJsonObject{}}};
  rejects([&] { doc.set_bake_configuration(invalid); });
  assert(doc.bake_configuration() == recipe);
  const auto metadata = doc.to_json();
  assert(metadata["schema"] == texture_document_schema);
  const auto bytes = write_skew_archive(doc);
  auto loaded = TextureDocument::from_json(metadata, bindings);
  assert(!loaded.set(1).skew.loaded);
  rejects([&] { loaded.set(1).skew.sample(1001, .5, .5); });
  rejects([&] { write_skew_archive(loaded); });
  read_skew_archive(loaded, bytes);
  assert(loaded.to_json() == metadata && loaded.set(1).skew.loaded);
  for (int tile : set.tiles)
    assert(loaded.set(1).skew.pixels.tile(tile).hash(2) == set.skew.pixels.tile(tile).hash(2));
  assert(loaded.set(1).layers.front().pixels.hash(0) == ordinary);
  auto legacy = metadata;
  legacy["schema"] = 56;
  auto old_sets = legacy["texture_sets"].toArray();
  auto old_set = old_sets[0].toObject();
  old_set.remove("skew_correction");
  old_sets[0] = old_set;
  legacy["texture_sets"] = old_sets;
  auto migrated = TextureDocument::from_json(legacy, bindings);
  assert(migrated.set(1).skew.loaded && migrated.set(1).skew.pixels.empty() &&
         migrated.set(1).skew.resolution == 128);
  for (const auto &key : {"edge_distance", "vector_density", "resolution"}) {
    auto value = skew_field_json(set.skew);
    value[key] = -1;
    rejects([&] { read_skew_field_json(value, set.tiles); });
  }
  auto outside = skew_field_json(set.skew);
  auto tiles = outside["tiles"].toArray();
  auto tile = tiles[0].toObject();
  tile["number"] = 1003;
  tiles[0] = tile;
  outside["tiles"] = tiles;
  rejects([&] { read_skew_field_json(outside, set.tiles); });
  auto bad = bytes;
  bad.data()[bad.size() - 1] ^= 1;
  rejects([&] { read_skew_archive(loaded, bad); });
  assert(loaded.to_json() == metadata);
  rejects([&] { read_skew_archive(loaded, bytes.first(bytes.size() - 1)); });
  rejects([&] { read_skew_archive(loaded, bytes + "x"); });
  assert(loaded.to_json() == metadata);
  QByteArray forged;
  QDataStream stream(&forged, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_8);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  stream << quint32(0x4254534b) << quint32(1) << quint32(1) << set.id << qint32(128) << quint32(2)
         << qint32(1001) << quint32(0xffffffff);
  rejects([&] { read_skew_archive(loaded, forged); });
  std::cout
      << "Skew authoring COW/independence/2UDIM/root57/legacy56/exact sparse archive/checksum/bounded malformed reads passed\n";
}
