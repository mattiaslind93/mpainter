// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "TextureMemory.hh"
#include <QJsonArray>
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
  using namespace bts;
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "UDIMSurface";
  material.uniform_editable = true;
  scene.materials = {material};
  UvFaceTriangle first, second;
  first.material = second.material = 0;
  first.corners = {{{0, 0}, {1, 0}, {0, 1}}};
  second.corners = {{{1, 0}, {2, 0}, {1, 1}}};
  scene.uv_triangles = {first, second};
  ImportSummary summary;
  summary.materials = {{1, "UDIMSurface"}};
  summary.materials[0].tiles = {1001, 1002};
  NewProjectOptions options;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  TextureDocument doc(options, summary, scene);
  auto &set = doc.set(1);
  assert(set.tiles == (std::set<int>{1001, 1002}));
  auto &layer = TextureDocument::layer(set, set.selected_layer);
  PaintPixels::Stamp stamp{{1.5, .5}, {.1, 0}, {0, .1}};
  stamp.channels = {true, false, false, false, false};
  stamp.values.base_color = {1, 0, 0, 1};
  layer.pixels.stamp(1024, stamp, set.tiles);
  const auto hash = layer.pixels.hash(0);
  TextureDocument::set_tile_resolution(set, 1002, 512);
  assert(set.width(1001) == 1024 && set.rows(1001) == 1024 && set.width(1002) == 512 &&
         set.rows(1002) == 512 && set.evaluation_texels() == 1024ULL * 1024 + 512ULL * 512);
  assert(TextureDocument::composite(set, 256, 256, 1002).material.base_color[0] > .99);
  TextureDocument::set_dimensions(set, 2048, 1024, false);
  assert(set.width(1001) == 2048 && set.rows(1001) == 1024 && set.width(1002) == 512 &&
         set.rows(1002) == 512);
  TextureDocument::set_tile_resolution(set, 1002, 1024);
  assert(set.tile_resolutions.contains(1002));
  TextureDocument::set_tile_resolution(set, 1002, 0);
  assert(!set.tile_resolutions.contains(1002) && set.width(1002) == 2048 &&
         set.rows(1002) == 1024 && layer.pixels.hash(0) == hash);
  TextureDocument::set_tile_resolution(set, 1002, 512);
  std::vector<TextureSetBinding> bindings{{set.id, 1, set.name}};
  auto metadata = doc.to_json();
  auto loaded = TextureDocument::from_json(metadata, bindings);
  loaded.set(1).layers.front().pixels = layer.pixels;
  assert(loaded.to_json() == metadata && loaded.set(1).width(1002) == 512);
  bool rejected = false;
  try {
    TextureDocument::set_tile_resolution(set, 1003, 512);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected && set.tile_resolutions.size() == 1);
  rejected = false;
  try {
    TextureDocument::set_tile_resolution(set, 1002, 300);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected && set.width(1002) == 512);
  if (texture_available_ram_bytes() > 12ULL * 1024 * 1024 * 1024) {
    TextureDocument big(options, summary, scene);
    auto &large = big.set(1);
    TextureDocument::set_dimensions(large, 4096, 4096, true);
    TextureDocument::add_displacement_channel(large);
    TextureDocument::add_user_channel(large, 0);
    assert(large.evaluation_texels() == 2ULL * 4096 * 4096 && large.displacement.channel &&
           large.users[0].present);
    const std::array large_bindings{TextureSetBinding{large.id, 1, large.name}};
    auto roundtrip = TextureDocument::from_json(big.to_json(), large_bindings);
    assert(roundtrip.to_json() == big.to_json());
    TextureDocument::add_fill(large);
    large.layers.back().fill.mode = 1;
    TextureDocument::set_dimensions(large, 4096, 4096, true);
  }
  std::cout
      << "Dynamic RAM admission for 2x4K displacement/custom channels, projection and JSON; Per-tile inherited/explicit resolution, rectangular parent, retained authoring, evaluation sampling and schema roundtrip passed\n";
}
