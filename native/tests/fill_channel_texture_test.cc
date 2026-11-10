// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "FillTextureJSON.hh"
#include "WarpIO.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
std::shared_ptr<const FillTexture> resource(bool raw = true)
{
  auto texture = std::make_shared<FillTexture>();
  texture->asset = "c0a80001-1234-4000-8000-000000000001";
  texture->hash = std::string(64, '1');
  texture->name = "Raw red pairs";
  texture->raw = raw;
  texture->width = 128;
  texture->height = 1;
  auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(128);
  for (int x = 0; x < 128; ++x)
    (*pixels)[x] = x < 64 ? FillTexture::Pixel{-.25, .8, 1, 1} : FillTexture::Pixel{.75, .1, 1, 1};
  texture->pixels = pixels;
  return texture;
}
int main()
{
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Fill channels";
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "Fill channels"}};
  imported.materials[0].tiles = {1001};
  NewProjectOptions options;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  options.resolution = DocumentResolution::Pixels128;
  TextureDocument document(options, imported, scene);
  auto &set = document.set(1);
  set.channels[3] = true;
  set.precision.fill(ChannelPrecision::RGB32F);
  set.ao_channel = true;
  TextureDocument::add_displacement_channel(set);
  for (int slot = 0; slot < 8; ++slot)
    TextureDocument::add_user_channel(set, slot);
  TextureDocument::add_fill(set);
  auto &layer = TextureDocument::layer(set, set.selected_layer);
  layer.values.roughness = .3;
  layer.values.bump = .2;
  layer.ao_enabled = true;
  layer.displacement.enabled = true;
  layer.displacement.value = .4;
  for (auto &user : layer.users)
    user.enabled = true;
  const auto raw = resource();
  layer.fill.texture = resource(false);
  for (int c = 1; c < 15; ++c)
    fill_channel_texture(layer.fill, c) = raw;
  assert(fill_has_texture(layer.fill));
  for (int c = 1; c < 15; ++c)
    assert(fill_channel_texture(layer.fill, c) == raw && fill_channel_texture(layer.fill, c)->raw);
  const auto left = TextureDocument::composite(set, 32, 32),
             right = TextureDocument::composite(set, 96, 32);
  assert(left.material.roughness == 0 && right.material.roughness == .75f &&
         left.material.metallic == 0 && right.material.metallic == .75f);
  assert(left.material.bump == -.25f && right.material.bump == .75f &&
         right.material.ambient_occlusion == .75f);
  assert(std::abs(right.authored.normal[0] - .36369648f) < 1e-6 &&
         std::abs(right.authored.normal[1] + .58191437f) < 1e-6);
  for (int slot = 0; slot < 8; ++slot) {
    assert(TextureDocument::composite_user(set, slot, 32, 32)[0] == -.25f);
    assert(TextureDocument::composite_user(set, slot, 96, 32)[0] == .75f);
  }
  assert(TextureDocument::composite_displacement(set, 32, 32)[0] == -.25f &&
         TextureDocument::composite_displacement(set, 96, 32)[0] == .75f);
  auto before = document;
  fill_channel_texture(layer.fill, 2).reset();
  assert(TextureDocument::composite(set, 96, 32).material.roughness == .3f &&
         fill_channel_texture(before.set(1).layers.back().fill, 2));
  assert(fill_channel_texture(layer.fill, 1) == raw && layer.fill.texture);
  fill_channel_texture(layer.fill, 2) = raw;
  auto alpha = std::make_shared<FillTexture>(*raw);
  auto alpha_pixels = std::make_shared<std::vector<FillTexture::Pixel>>(128);
  for (int x = 0; x < 128; ++x)
    (*alpha_pixels)[x] = x < 64 ? FillTexture::Pixel{.2, .8, 1, .5} :
                                  FillTexture::Pixel{.8, .1, 1, .5};
  alpha->pixels = alpha_pixels;
  fill_channel_texture(layer.fill, 2) = alpha;
  assert(TextureDocument::composite(set, 96, 32).coverage[2] == 1);
  layer.fill.alpha_mask[2] = true;
  const auto masked = TextureDocument::composite(set, 96, 32);
  assert(masked.coverage[2] == .5f && masked.authored.roughness == .8f);
  fill_channel_texture(layer.fill, 2) = raw;
  auto atlas = std::make_shared<FillSurfaceAtlas>();
  atlas->resolution = atlas->height = 128;
  atlas->extent = 2;
  atlas->tiles[1001].resize(128 * 128);
  for (auto &s : atlas->tiles[1001]) {
    s.valid = true;
    s.position = {.25, -.5, .25};
    s.normal = {0, -1, 0};
  }
  set.projection_geometry = atlas;
  for (int mode = 0; mode <= 5; ++mode) {
    layer.fill.mode = mode;
    layer.fill.offset = {.1, .2};
    layer.fill.scale = {2, 2};
    const auto color = TextureDocument::fill_sample(set, layer, 0, 64, 64),
               data = TextureDocument::fill_sample(set, layer, 2, 64, 64);
    assert(color == data);
    for (int c = 1; c < 15; ++c)
      assert(TextureDocument::fill_sample(set, layer, c, 64, 64) == data);
  }
  layer.fill.mode = 0;
  const auto json = document.to_json();
  assert(json["schema"] == bts::texture_document_schema);
  const std::array bindings{TextureSetBinding{set.id, 1, set.name}};
  auto loaded = TextureDocument::from_json(json, bindings);
  const auto &restored = loaded.set(1).layers.back().fill;
  for (int c = 1; c < 15; ++c)
    assert(fill_channel_texture(restored, c) && fill_channel_texture(restored, c)->raw &&
           !fill_channel_texture(restored, c)->pixels);
  assert(loaded.to_json() == json);
  auto old = json;
  old["schema"] = 53;
  auto sets = old["texture_sets"].toArray();
  auto obj = sets[0].toObject();
  auto layers = obj["layers"].toArray();
  for (int i = 0; i < layers.size(); ++i) {
    auto item = layers[i].toObject();
    auto fill = item["fill_projection"].toObject();
    fill.remove("channel_textures");
    auto texture = fill["texture"].toObject();
    if (!texture.empty()) {
      texture.remove("raw");
      fill["texture"] = texture;
    }
    item["fill_projection"] = fill;
    layers[i] = item;
  }
  obj["layers"] = layers;
  sets[0] = obj;
  old["texture_sets"] = sets;
  auto legacy = TextureDocument::from_json(old, bindings);
  assert(legacy.set(1).layers.back().fill.texture &&
         !legacy.set(1).layers.back().fill.texture->raw);
  for (int c = 1; c < 15; ++c)
    assert(!fill_channel_texture(legacy.set(1).layers.back().fill, c));
  // A genuinely older schema54 document has no Warp object and retains all P bindings.
  auto old54 = json;
  old54["schema"] = 54;
  auto old_sets = old54["texture_sets"].toArray();
  auto old_set = old_sets[0].toObject();
  auto old_layers = old_set["layers"].toArray();
  for (int i = 0; i < old_layers.size(); ++i) {
    auto item = old_layers[i].toObject();
    auto mapping = item["fill_projection"].toObject();
    mapping.remove("warp");
    item["fill_projection"] = mapping;
    old_layers[i] = item;
  }
  old_set["layers"] = old_layers;
  old_sets[0] = old_set;
  old54["texture_sets"] = old_sets;
  auto legacy54 = TextureDocument::from_json(old54, bindings);
  assert(!legacy54.set(1).layers.back().fill.warp && legacy54.set(1).layers.back().fill.mode == 0);
  for (int c = 0; c < 15; ++c)
    assert(fill_channel_texture(legacy54.set(1).layers.back().fill, c));
  layer.fill.mode = 5;
  auto grid = std::make_shared<WarpGrid>();
  grid->move(4, {.12, -.35, .08});
  grid->automatic_tangents = false;
  grid->nodes[4].du = {.8, .15, .3};
  grid->split(true, .3);
  grid->split(false, .7);
  layer.fill.warp = grid;
  layer.fill.warp_depth = .8;
  layer.fill.warp_edit = true;
  const auto warped = document.to_json();
  auto reloaded = TextureDocument::from_json(warped, bindings);
  assert(reloaded.to_json() == warped);
  assert(warp_settings_json(reloaded.set(1).layers.back().fill) == warp_settings_json(layer.fill));
  auto invalid = warped;
  invalid["schema"] = 54;
  bool rejected_warp = false;
  try {
    (void)TextureDocument::from_json(invalid, bindings);
  }
  catch (const std::exception &) {
    rejected_warp = true;
  }
  assert(rejected_warp);
  auto old55 = warped;
  old55["schema"] = 55;
  auto legacy55 = TextureDocument::from_json(old55, bindings);
  assert(legacy55.set(1).layers.back().fill.mode == 5 &&
         warp_settings_json(legacy55.set(1).layers.back().fill) == warp_settings_json(layer.fill));
  set.tiles = {1001, 1002, 1003};
  set.projection_geometry.reset();
  set.projection_tiles.clear();
  layer.fill.mode = 6;
  auto first = std::make_shared<FillTexture>(*raw), second = std::make_shared<FillTexture>(*raw);
  first->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      128, FillTexture::Pixel{.2, .3, .4, 1});
  second->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      128, FillTexture::Pixel{.8, .1, .2, 1});
  auto sequence = std::make_shared<FillTexture>(*first);
  sequence->tiles = std::make_shared<const std::map<int, std::shared_ptr<const FillTexture>>>(
      std::map<int, std::shared_ptr<const FillTexture>>{{1001, first}, {1002, second}});
  fill_channel_texture(layer.fill, 2) = sequence;
  assert(TextureDocument::composite(set, 64, 64, 1001).material.roughness == .2f &&
         TextureDocument::composite(set, 64, 64, 1002).material.roughness == .8f);
  assert(TextureDocument::fill_sample(set, layer, 2, 64, 64, 1003)[3] == 0);
  assert(TextureDocument::fill_sample(set, layer, 1, 64, 64, 1003)[3] == 1);
  layer.fill.offset3 = {2, 3, 4};
  layer.fill.scale = {5, 5};
  assert(TextureDocument::composite(set, 64, 64, 1002).material.roughness == .8f);
  const auto sequenced = document.to_json();
  auto loaded_sequence = TextureDocument::from_json(sequenced, bindings);
  assert(loaded_sequence.to_json() == sequenced &&
         loaded_sequence.set(1).layers.back().fill.mode == 6);
  // Data image alpha never erases signed displacement or its baked margin.
  layer.fill.alpha_mask.fill(false);
  auto zero_alpha = std::make_shared<FillTexture>(*raw);
  zero_alpha->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      128, FillTexture::Pixel{-0.001f, 2, 3, 0});
  auto high = std::make_shared<FillTexture>(*zero_alpha);
  high->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      128, FillTexture::Pixel{2.25f, 2, 3, 0});
  auto signed_tiles = std::make_shared<FillTexture>(*zero_alpha);
  signed_tiles->tiles = std::make_shared<const std::map<int, std::shared_ptr<const FillTexture>>>(
      std::map<int, std::shared_ptr<const FillTexture>>{
          {1001, zero_alpha}, {1002, high}, {1011, zero_alpha}});
  set.tiles.insert(1011);
  fill_channel_texture(layer.fill, 14) = signed_tiles;
  for (int filter = 0; filter < 3; ++filter) {
    layer.fill.filtering = filter;
    for (int tile : {1001, 1002, 1011}) {
      const auto value = TextureDocument::composite_displacement(set, 32, 32, tile);
      assert(value[0] == (tile == 1002 ? 2.25f : -.001f) && value[1] == 1);
    }
  }
  layer.fill.alpha_mask[14] = true;
  assert(TextureDocument::composite_displacement(set, 32, 32, 1002)[1] == 0);
  layer.fill.alpha_mask[14] = false;
  auto policy = document.to_json();
  auto policy_loaded = TextureDocument::from_json(policy, bindings);
  assert(policy_loaded.to_json() == policy);
  auto old63 = policy;
  old63["schema"] = 63;
  auto legacy63 = TextureDocument::from_json(old63, bindings);
  assert(!legacy63.set(1).layers.back().fill.alpha_mask[14]);
  assert(legacy63.set(1).displacement.midlevel == set.displacement.midlevel);
  FillTexture metadata = *signed_tiles;
  metadata.range_valid = true;
  metadata.floating_point = true;
  metadata.minimum = -.001f;
  metadata.maximum = 2.25f;
  DisplacementSettings settings;
  displacement_import_range(settings, metadata);
  assert(settings.midlevel == 0 && settings.strength == 1);
  metadata.minimum = 0;
  metadata.maximum = 1;
  settings = {};
  displacement_import_range(settings, metadata);
  assert(settings.midlevel == .5f);
  metadata.data_units = true;
  displacement_import_range(settings, metadata);
  assert(settings.midlevel == 0);
  metadata.floating_point = false;
  settings = {};
  displacement_import_range(settings, metadata);
  assert(settings.midlevel == .5f);
  auto bad_policy = fill_alpha_mask_json(layer.fill);
  bad_policy[14] = 1;
  bool rejected_alpha = false;
  try {
    read_fill_alpha_mask(layer.fill, bad_policy);
  }
  catch (const std::invalid_argument &) {
    rejected_alpha = true;
  }
  assert(rejected_alpha);
  std::cout
      << "AB signed/raw alpha policy, UDIM tile mapping, normalized map midpoint, metadata defaults and legacy settings passed; All15 fill sources, raw red/signed scalars, RGB normal decode, alpha, shared UV/tri/planar/spherical/cylindrical mapping, uniform restore/COW and schema55/Warp split/legacy54+53 passed\n";
}
