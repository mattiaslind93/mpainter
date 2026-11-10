// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialProjection.hh"
#include "TextureDocument.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  FillProjection p;
  p.mode = 4;
  p.filtering = 2;
  validate_fill_projection(p);
  FillSurfaceAtlas atlas;
  atlas.extent = 2;
  FillSurfacePoint s;
  s.valid = true;
  s.position = {0, -1, 0};
  s.normal = {0, -1, 0};
  assert(fill_cylindrical_uv({0, -.5, 0}, 360) == (std::array<double, 2>{.5, .5}));
  assert(fill_cylindrical_uv({0, 0, 0}, 360) == (std::array<double, 2>{.5, .5}));
  assert(fill_cylindrical_uv({0, -.5, .5}, 360)[1] == 1);
  assert(fill_cylindrical_uv({-1e-8, 1, 0}, 360)[0] < 1e-7 &&
         fill_cylindrical_uv({1e-8, 1, 0}, 360)[0] > 1 - 1e-7);
  auto image = std::make_shared<FillTexture>();
  image->asset = "c0a80001-1234-4000-8000-000000000001";
  image->hash = std::string(64, '1');
  image->name = "Cylinder directions";
  image->width = 4;
  image->height = 1;
  image->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      std::vector<FillTexture::Pixel>{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}});
  p.texture = image;
  assert(fill_spatial_sample(p, s, atlas)[2] == 1);
  p.rotation3[2] = 90;
  assert(fill_spatial_sample(p, s, atlas)[1] == 1);
  p.rotation3 = {};
  p.backface_culling = true;
  assert(fill_spatial_sample(p, s, atlas)[3] == 1);
  s.normal = {0, 0, 1};
  assert(fill_spatial_sample(p, s, atlas)[3] == 0);
  s.normal = {0, 1, 0};
  assert(fill_spatial_sample(p, s, atlas)[3] == 0);
  s.normal = {0, -1, 0};
  p.backface_culling = false;
  p.shape_crop = true;
  s.position = {0, -2, 0};
  assert(fill_spatial_sample(p, s, atlas)[3] == 0);
  s.position = {0, -1, 2};
  assert(fill_spatial_sample(p, s, atlas)[3] == 0);
  s.position = {0, -1, 0};
  p.depth_hardness = 0;
  p.depth_culling = true;
  assert(fill_spatial_sample(p, s, atlas)[3] == 1);
  p.cylinder_angle = 180;
  s.position = {0, 1, 0};
  assert(fill_spatial_sample(p, s, atlas)[3] == 0);
  p.shape_crop = false;
  p.wrap = 1;
  assert(fill_spatial_sample(p, s, atlas)[3] == 0);
  p.wrap = 0;
  assert(fill_spatial_sample(p, s, atlas)[3] == 1);
  p.filtering = 0;
  s.position = {0, 0, 0};
  s.footprint = .01;
  for (float v : fill_spatial_sample(p, s, atlas))
    assert(std::isfinite(v));
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Cylinder";
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "Cylinder"}};
  TextureDocument doc({}, imported, scene);
  auto &set = doc.set(1);
  TextureDocument::set_resolution(set, 128);
  TextureDocument::add_fill(set);
  auto &layer = TextureDocument::layer(set, set.selected_layer);
  layer.fill.mode = 4;
  layer.fill.cylinder_angle = 180;
  layer.fill.backface_culling = true;
  auto geometry = std::make_shared<FillSurfaceAtlas>();
  geometry->resolution = geometry->height = 128;
  geometry->extent = 2;
  geometry->tiles[1001].resize(128 * 128);
  for (auto &point : geometry->tiles[1001]) {
    point.valid = true;
    point.position = {0, -1, 0};
    point.normal = {0, 0, 1};
  }
  set.projection_geometry = geometry;
  TextureDocument::add_user_channel(set, 0);
  layer.users[0].enabled = true;
  layer.users[0].value = .8;
  TextureDocument::add_displacement_channel(set);
  layer.displacement.enabled = true;
  layer.displacement.value = -.2;
  assert(TextureDocument::composite(set, 64, 64).coverage[0] == 0 &&
         TextureDocument::composite_user(set, 0, 64, 64)[1] == 0 &&
         TextureDocument::composite_displacement(set, 64, 64)[1] == 0);
  geometry->tiles[1001][64 * 128 + 64].normal = {0, -1, 0};
  assert(TextureDocument::composite(set, 64, 64).coverage[0] == 1 &&
         TextureDocument::composite_displacement(set, 64, 64)[0] == -.2f);
  const auto json = doc.to_json();
  const std::array bindings{TextureSetBinding{set.id, 1, "Cylinder"}};
  auto loaded = TextureDocument::from_json(json, bindings);
  const auto &restored = TextureDocument::layer(loaded.set(1), loaded.set(1).selected_layer).fill;
  assert(restored.mode == 4 && restored.cylinder_angle == 180 && restored.backface_culling);
  auto legacy = json;
  legacy["schema"] = 52;
  auto sets = legacy["texture_sets"].toArray();
  auto object = sets[0].toObject();
  auto layers = object["layers"].toArray();
  for (int i = 0; i < layers.size(); ++i) {
    auto item = layers[i].toObject();
    auto fill = item["fill_projection"].toObject();
    fill["mode"] = 3;
    auto options = fill["projection_options"].toObject();
    options.remove("cylinder_angle");
    fill["projection_options"] = options;
    item["fill_projection"] = fill;
    layers[i] = item;
  }
  object["layers"] = layers;
  sets[0] = object;
  legacy["texture_sets"] = sets;
  auto older = TextureDocument::from_json(legacy, bindings);
  assert(TextureDocument::layer(older.set(1), older.set(1).selected_layer).fill.cylinder_angle ==
         360);
  std::cout
      << "Cylinder seam/caps/center/angle, radial culling, crop, wrapping, transformed texture, signed auxiliary coverage and schema53/52 passed\n";
}
