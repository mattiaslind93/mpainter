// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialLayeringIO.hh"
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>
using namespace bts;
int main()
{
  MaterialLayering v;
  auto json = material_layering_json(v);
  assert(material_layering_json(read_material_layering(json)) == json);
  assert(!read_material_layering(QJsonValue()).enabled);
  v.enabled = true;
  for (int i = 0; i < 3; ++i) {
    v.mask_slots[i] = i;
    v.mask_roots[i] = QUuid::createUuid();
  }
  v.main_selection = QUuid::createUuid();
  v.active_mask = 1;
  v.materials[0].name = "Independent tiling input";
  v.materials[0].tiling = {2, 3};
  v.materials[0].offset = {.1, -.2};
  v.materials[0].rotation = 90;
  const auto uv = layered_material_uv(v.materials[0], {.25, .5});
  assert(std::abs(uv[0] + 1.4) < 1e-12 && std::abs(uv[1] - .3) < 1e-12);
  auto texture = std::make_shared<FillTexture>();
  texture->asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  texture->hash = std::string(64, 'a');
  texture->name = "Original generic tile";
  texture->width = texture->height = 2;
  texture->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      4, FillTexture::Pixel{.1, .2, .3, 1});
  v.materials[0].textures[0] = texture;
  json = material_layering_json(v);
  const auto restored = read_material_layering(json);
  assert(material_layering_json(restored) == json && !restored.materials[0].textures[0]->pixels);
  assert(v.materials[0].textures[0]->pixels->size() == 4);
  for (int which = 0; which < 4; ++which) {
    std::array<float, 3> mask{};
    if (which)
      mask[which - 1] = 1;
    const auto w = layered_material_weights(mask);
    for (int c = 0; c < 4; ++c)
      assert(w[c] == float(c == which));
  }
  std::mt19937 random(149);
  for (int i = 0; i < 1000; ++i) {
    std::array<float, 3> mask;
    for (auto &m : mask)
      m = float(random() % 10001) / 10000;
    const auto weights = layered_material_weights(mask);
    assert(std::abs(std::accumulate(weights.begin(), weights.end(), 0.f) - 1) < 1e-6);
    for (auto w : weights)
      assert(w >= 0 && w <= 1);
  }
  assert(layered_material_weights({-1, 2, 0}) == layered_material_weights({0, 1, 0}));
  const auto rejects = [](const QJsonObject &data) {
    bool rejected = false;
    try {
      read_material_layering(data);
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  };
  auto bad = json;
  auto stacks = bad["opacity_stacks"].toArray();
  auto s = stacks[1].toObject();
  s["channel"] = 0;
  stacks[1] = s;
  bad["opacity_stacks"] = stacks;
  rejects(bad);
  bad = json;
  bad["active_mask"] = 3;
  rejects(bad);
  bad = json;
  bad["enabled"] = false;
  rejects(bad);
  bad = json;
  bad["version"] = 2;
  rejects(bad);
  auto invalid = v;
  invalid.mask_roots[0] = {};
  bool rejected = false;
  try {
    invalid.validate();
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  invalid = v;
  auto raw = std::make_shared<FillTexture>(*texture);
  raw->raw = true;
  invalid.materials[0].textures[0] = raw;
  rejected = false;
  try {
    invalid.validate();
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  invalid = v;
  invalid.materials[0].values.base_color[3] = 2;
  rejected = false;
  try {
    invalid.validate();
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::cout
      << "Shader-time material weights, independent generic UV transforms, shader-defined stacks and strict resource-reference codec PASS\n";
}
