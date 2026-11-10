// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include <cassert>
#include <iostream>
int main()
{
  using namespace bts;
  MaskEffect e;
  const std::array<float, 4> red{1, 0, 0, 1}, green{0, 1, 0, 1};
  assert(color_selection(e, red) == 0);
  e.colors = {{1, 0, 0}};
  assert(color_selection(e, red) == 1 && color_selection(e, green) == 0);
  e.colors.push_back({0, 1, 0});
  assert(color_selection(e, green) == 1);
  e.colors.pop_back();
  e.output = .25;
  assert(color_selection(e, red) == .25);
  e.output = 1;
  e.tolerance = .5;
  e.hardness = 0;
  const float soft = color_selection(e, {.5, 0, 0, 1});
  assert(soft > 0 && soft < 1);
  e.hardness = 1;
  assert(color_selection(e, {.5, 0, 0, 1}) == 1);
  e.tolerance = 0;
  assert(color_selection(e, red) == 1 && color_selection(e, {.999, 0, 0, 1}) == 0);
  e.use_id_map = false;
  assert(color_selection(e, red) == 0);
  e.use_id_map = true;
  std::vector<float> rgba(32 * 32 * 4);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x) {
      auto i = (y * 32 + x) * 4;
      rgba[i] = x < 16;
      rgba[i + 1] = x >= 16;
      rgba[i + 3] = 1;
    }
  auto data = std::make_shared<const MeshMapPixels>(32, std::move(rgba));
  MeshMap map;
  map.name = "Two ID colors";
  map.tiles[1001] = {32, data->hash, data};
  LayerMask mask;
  mask.background = 0;
  mask.effects = {e};
  assert(mask.value(0, 0, 1001, 128, 64, &map) == 1 &&
         mask.value(127, 0, 1001, 128, 64, &map) == 0);
  assert(mask.value(0, 0, 1002, 128, 64, &map) == 0);
  mask.effects[0].enabled = false;
  assert(mask.value(0, 0, 1001, 128, 64, &map) == 0);
  SceneSnapshot scene;
  MaterialSummary m;
  m.session_id = 1;
  m.name = "ID";
  m.uniform_editable = true;
  scene.materials = {m};
  ImportSummary imported;
  imported.materials = {{1, "ID"}};
  TextureDocument doc({}, imported, scene);
  auto &set = doc.set(1);
  set.precision[0] = ChannelPrecision::RGB32F;
  set.mesh_maps[2] = map;
  TextureDocument::add_fill(set);
  auto &black = TextureDocument::layer(set, set.selected_layer);
  black.values.base_color = {0, 0, 0, 1};
  TextureDocument::add_fill(set);
  auto &white = TextureDocument::layer(set, set.selected_layer);
  white.values.base_color = {1, 1, 1, 1};
  white.mask = mask;
  white.mask->effects[0].enabled = true;
  set.selected_mask = true;
  set.selected_effect = e.id;
  assert(TextureDocument::evaluate(set, 0, 0).material.base_color[0] == 1 &&
         TextureDocument::evaluate(set, set.width() - 1, 0).material.base_color[0] == 0);
  white.mask->enabled = false;
  assert(TextureDocument::evaluate(set, set.width() - 1, 0).material.base_color[0] == 1);
  white.mask->enabled = true;
  std::vector<TextureSetBinding> bindings{{set.id, 1, set.name}};
  assert(TextureDocument::has_pixels(set));
  assert(TextureDocument::from_json(doc.to_json(), bindings).to_json() == doc.to_json());
  auto invalid = mask_effects_json(mask.effects);
  auto value = invalid[0].toObject();
  value["tolerance"] = -1;
  invalid[0] = value;
  bool rejected = false;
  try {
    read_mask_effects(invalid);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout
      << "ID multi-color membership, empty/absent/zero-alpha maps, output/hardness/tolerance, rectangular sampling, mask bypass and strict editable metadata passed\n";
}
