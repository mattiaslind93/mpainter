// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "StrokeArchive.hh"
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
  using namespace bts;
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Cube";
  material.uniform_editable = true;
  scene.materials = {material};
  ImportSummary summary;
  summary.materials = {{1, "Cube"}};
  TextureDocument doc({}, summary, scene);
  auto &set = doc.set(1);
  TextureDocument::set_resolution(set, 128);
  const auto before = TextureDocument::output(set);
  TextureDocument::add_user_channel(set, 7);
  TextureDocument::rename_user_channel(set, 7, "BTS coverage");
  assert(set.users[7].precision == ChannelPrecision::L32F && set.users[7].present);
  auto &layer = TextureDocument::layer(set, set.selected_layer);
  auto &custom = layer.users[7];
  PaintPixels::Stamp stamp{{.5, .5}, {.1, 0}, {0, .1}};
  stamp.channels = {false, false, true, false, false};
  stamp.values.roughness = .4;
  custom.author_resolution = 128;
  custom.pixels.stamp(128, stamp);
  custom.strokes = append_stroke({}, {}, {custom.pixels, {}}, {custom.pixels, {}}, true, false, 1);
  assert(std::abs(TextureDocument::composite_user(set, 7, 64, 64)[0] - .4) < 1e-6);
  assert(TextureDocument::composite_user(set, 7, 0, 0)[1] == 0 &&
         TextureDocument::output(set) == before);
  const auto hash = custom.pixels.hash(2);
  set.users[7].present = false;
  assert(TextureDocument::composite_user(set, 7, 64, 64) == (std::array<float, 2>{}));
  TextureDocument::add_user_channel(set, 7);
  assert(custom.pixels.hash(2) == hash && set.users[7].name == "BTS coverage");
  const auto metadata = doc.to_json();
  std::vector<TextureSetBinding> bindings{{set.id, 1, set.name}};
  auto loaded = TextureDocument::from_json(metadata, bindings);
  auto &restored = loaded.set(1).layers.front().users[7];
  restored.pixels = custom.pixels;
  read_stroke_archive(loaded, write_stroke_archive(doc));
  assert(loaded.to_json() == metadata && restored.strokes->matches({restored.pixels, {}}));
  auto clone = loaded;
  clone.set(1).users[7].precision = ChannelPrecision::SRGB8;
  assert(std::abs(TextureDocument::composite_user(clone.set(1), 7, 64, 64)[0] - .4) > .0001);
  assert(custom.pixels.hash(2) == hash);  // Scattering has a persistent semantic identity, a
                                          // distinct L8 default, and no PBR effect.
  auto scatter_doc = doc;
  auto &scatter = scatter_doc.set(1);
  const auto original = TextureDocument::output(scatter);
  TextureDocument::add_scattering_channel(scatter);
  assert(scatter.users[0].scattering && scatter.users[0].name == "Scattering" &&
         scatter.users[0].precision == ChannelPrecision::L8);
  assert(TextureDocument::output(scatter) == original && scatter.users[7].name == "BTS coverage");
  assert(TextureDocument::layer(scatter, scatter.selected_layer).users[0].value == 0);
  scatter.users[0].precision = ChannelPrecision::L16F;
  scatter.users[0].present = false;
  TextureDocument::add_scattering_channel(scatter);
  assert(scatter.users[0].present && scatter.users[0].precision == ChannelPrecision::L16F);
  bool rejected = false;
  try {
    TextureDocument::rename_user_channel(scatter, 0, "renamed");
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  const auto encoded = custom_channels_json(scatter.users);
  assert(read_custom_channels(encoded)[0].scattering);
  auto bad = encoded;
  auto duplicate = bad[1].toObject();
  duplicate["scattering"] = true;
  duplicate["allocated"] = true;
  duplicate["name"] = "Scattering";
  bad[1] = duplicate;
  rejected = false;
  try {
    read_custom_channels(bad);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  auto legacy = custom_channels_json(set.users);
  for (int i = 0; i < legacy.size(); ++i) {
    auto value = legacy[i].toObject();
    value.remove("scattering");
    legacy[i] = value;
  }
  assert(!read_custom_channels(legacy)[0].scattering);
  auto metadata_scatter = scatter_doc.to_json();
  auto restored_scatter = TextureDocument::from_json(metadata_scatter, bindings);
  assert(restored_scatter.set(1).users[0].scattering &&
         custom_channels_json(restored_scatter.set(1).users) == encoded);
  {
    auto labels = set.users;
    labels[1].scattering = false;
    labels[1].allocated = labels[1].present = true;
    labels[1].name = std::string(20000, 'c');
    assert(read_custom_channels(custom_channels_json(labels))[1].name == labels[1].name);
  }
  std::cout
      << "Custom scalar coverage, independent material output, rename/remove/re-add, precision, metadata and spatial archive passed\n";
}
