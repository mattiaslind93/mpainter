// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "FillTextureJSON.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  auto image = std::make_shared<FillTexture>();
  image->asset = "c36ae466-dbad-43fc-b48e-b58e42b06011";
  image->hash = std::string(64, 'a');
  image->name = "Two colors";
  image->width = image->height = 2;
  image->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      std::vector<FillTexture::Pixel>{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, .5}, {0, 0, 0, 0}});
  image->validate();
  assert(image->sample(.25, .25) == FillTexture::Pixel({1, 0, 0, 1}));
  assert(image->sample(1.25, -.75) == image->sample(.25, .25));
  const auto transparent = image->sample(.75, .75);
  assert(transparent[3] == 0);
  const auto blended = image->sample(.5, .75);
  assert(blended[2] == 1 && blended[3] == .25f);
  const auto unmasked = image->sample(.5, .75, true, false);
  assert(unmasked[2] == .5f && unmasked[3] == 1);
  FillProjection mapping;
  mapping.texture = image;
  validate_fill_projection(mapping);
  assert(fill_texture_uv_sample(mapping, 31, 31, 128)[0] > .98);
  mapping.offset = {.5, 0};
  assert(fill_texture_uv_sample(mapping, 31, 31, 128)[1] > .98);
  mapping.offset = {0, 0};
  FillSurfacePoint surface;
  surface.valid = true;
  surface.position = {-.25, -.25, 0};
  surface.normal = {0, 0, 1};
  assert(fill_texture_triplanar_sample(mapping, surface, 1)[0] == 1);
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Fill";
  material.uniform_editable = true;
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "Fill"}};
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  TextureDocument document(options, imported, scene);
  auto &set = document.set(1);
  TextureDocument::add_fill(set);
  auto &fill = TextureDocument::layer(set, set.selected_layer);
  fill.fill = mapping;
  assert(TextureDocument::has_pixels(set));
  auto color = TextureDocument::composite(set, 31, 31).material.base_color;
  assert(color[0] > .98 && color[1] < .02);
  const auto partial = TextureDocument::composite(set, 31, 95);
  assert(partial.coverage[0] > .48 && partial.coverage[0] < .52);
  auto atlas = std::make_shared<FillSurfaceAtlas>();
  atlas->resolution = atlas->height = 8;
  atlas->extent = 1;
  atlas->tiles[1001] = std::vector<FillSurfacePoint>(64, surface);
  set.projection_geometry = atlas;
  set.projection_tiles[1001] = atlas;
  fill.fill.mode = 1;
  assert(TextureDocument::composite(set, 31, 31).material.base_color[0] > .99);
  fill.fill.mode = 0;
  const auto root = document.to_json();
  assert(root["schema"] == bts::texture_document_schema);
  const std::array bindings{TextureSetBinding{set.id, 1, "Fill"}};
  auto restored = TextureDocument::from_json(root, bindings);
  auto &loaded = TextureDocument::layer(restored.set(1), restored.set(1).selected_layer);
  assert(loaded.fill.texture && !loaded.fill.texture->pixels);
  assert(fill_texture_json(loaded.fill.texture) == fill_texture_json(image));
  loaded.fill.texture = image;
  assert(TextureDocument::composite(restored.set(1), 31, 31).material ==
         TextureDocument::composite(set, 31, 31).material);
  auto legacy = root;
  legacy["schema"] = 47;
  auto old = TextureDocument::from_json(legacy, bindings);
  assert(!old.set(1).layers.back().fill.texture);
  mapping.texture.reset();
  mapping.atom = true;
  assert(atom_pattern(mapping, .5, .5) == 1);
  std::cout
      << "Fill texture repeat, associated-alpha bilinear, UV transforms, triplanar, color coverage and descriptor/legacy roundtrip passed\n";
}
