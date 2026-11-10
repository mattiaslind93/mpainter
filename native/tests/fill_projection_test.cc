// SPDX-License-Identifier: GPL-3.0-or-later
#include "FillProjection.hh"
#include "TextureDocument.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <QJsonArray>
#include <cassert>
#include <cstdio>
int main()
{
  bts::FillProjection p;
  for (double rotation : {0., 30., -90., 720.})
    for (double scale : {.2, 1., 4., -2.}) {
      p.rotation = rotation;
      p.scale = {scale, scale};
      p.offset = {.1, -.3};
      bts::validate_fill_projection(p);
      for (double u : {0., .4, 1.})
        for (double v : {0., .7, 1.}) {
          const auto frame = bts::fill_uv_frame(p, u, v),
                     uv = bts::fill_uv_map(p, frame[0], frame[1]);
          assert(std::abs(uv[0] - u) < 1e-10 && std::abs(uv[1] - v) < 1e-10);
        }
    }
  p = {};
  p.atom = true;
  assert(bts::atom_pattern(p, .5, .5) == 1);
  p.invert = true;
  assert(bts::atom_pattern(p, .5, .5) == 0);
  p.invert = false;
  {
    auto tri = p;
    tri.mode = 1;
    bts::FillSurfacePoint point;
    point.valid = true;
    point.position = {.13, .21, .32};
    point.normal = {0, 0, 1};
    const auto uv = bts::fill_uv_map(tri, .63, .71);
    assert(bts::fill_triplanar_sample(tri, point, 1) == bts::atom_pattern(tri, uv[0], uv[1]));
    const auto original = bts::fill_triplanar_sample(tri, point, 1);
    tri.offset = {.1, .2};
    assert(bts::fill_triplanar_sample(tri, point, 1) == original);
    tri.offset3 = {.2, 0, 0};
    auto shifted = point;
    shifted.position[0] -= .2;
    tri.offset3 = {0, 0, 0};
    const auto expected = bts::fill_triplanar_sample(tri, shifted, 1);
    tri.offset3 = {.2, 0, 0};
    assert(bts::fill_triplanar_sample(tri, point, 1) == expected);
    tri.scale3 = {0, 1, 1};
    bool rejected = false;
    try {
      bts::validate_fill_projection(tri);
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  bts::SceneSnapshot scene;
  bts::MaterialSummary material;
  material.session_id = 1;
  material.name = "Fill";
  material.uniform_editable = true;
  scene.materials.push_back(material);
  bts::ImportSummary imported;
  imported.materials = {{1, "Fill"}};
  bts::NewProjectOptions settings;
  settings.resolution = bts::DocumentResolution(128);
  bts::TextureDocument document(settings, imported, scene);
  auto &set = document.set(1);
  bts::TextureDocument::add_fill(set);
  auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
  fill.fill = p;
  fill.values.base_color = {1, 1, 1, 1};
  fill.values.roughness = .37;
  double difference = 0;
  for (int y = 0; y < 128; y += 3)
    for (int x = 0; x < 128; x += 3) {
      const auto c = bts::TextureDocument::composite(set, x, y);
      assert(c.material.base_color[0] == bts::fill_uv_sample(p, x, y, 128));
      assert(std::abs(c.material.roughness - .37) < 1e-6);
      auto repeated = p;
      repeated.scale = {4, 4};
      difference += std::abs(bts::fill_uv_sample(p, x, y, 128) -
                             bts::fill_uv_sample(repeated, x, y, 128));
    }
  assert(difference > 50 && bts::TextureDocument::has_pixels(set) && fill.pixels.empty());
  fill.fill.scale = {4, 4};
  fill.fill.rotation = 30;
  fill.fill.offset = {.1, 0};
  const auto json = document.to_json();
  const std::array<bts::TextureSetBinding, 1> bindings{{{set.id, 1, "Fill"}}};
  auto restored = bts::TextureDocument::from_json(json, bindings);
  assert(restored.to_json() == json && restored.set(1).texture_resolution == 128);
  auto invalid = json;
  auto sets = invalid["texture_sets"].toArray();
  auto s = sets[0].toObject();
  auto layers = s["layers"].toArray();
  auto layer = layers[1].toObject();
  auto mapping = layer["fill_projection"].toObject();
  mapping["scale"] = QJsonArray{0, 0};
  layer["fill_projection"] = mapping;
  layers[1] = layer;
  s["layers"] = layers;
  sets[0] = s;
  invalid["texture_sets"] = sets;
  bool rejected = false;
  try {
    bts::TextureDocument::from_json(invalid, bindings);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  auto legacy = json;
  legacy["schema"] = 7;
  auto older = bts::TextureDocument::from_json(legacy, bindings);
  assert(!older.set(1).layers[1].fill.atom);
  std::puts(
      "fill UV: inverse frame, linked scale/rotation/offset, procedural density, channel isolation, zero authored pages, schema9 roundtrip / triplanar normal projection / separate UV and world offsets, invalid scale rejection and schema7 defaults PASS");
}
