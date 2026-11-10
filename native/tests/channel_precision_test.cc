// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelPrecision.hh"
#include "TextureDocument.hh"
#include <QJsonArray>
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
  using namespace bts;
  const std::array<float, 3> color{.123456f, .456789f, .789123f};
  const auto eight = precision_rgb(color, ChannelPrecision::RGB8),
             sixteen = precision_rgb(color, ChannelPrecision::RGB16),
             half = precision_rgb(color, ChannelPrecision::RGB16F);
  assert(precision_rgb(color, ChannelPrecision::RGB32F) == color);
  double error8 = 0, error16 = 0;
  for (int c = 0; c < 3; ++c) {
    assert(std::abs(eight[c] * 255 - std::round(eight[c] * 255)) < 1e-5);
    error8 += std::abs(eight[c] - color[c]);
    error16 += std::abs(sixteen[c] - color[c]);
    assert(std::abs(half[c] - color[c]) < .0003f);
  }
  assert(error16 < error8 / 100);
  for (auto format : {ChannelPrecision::L8,
                      ChannelPrecision::L16,
                      ChannelPrecision::L16F,
                      ChannelPrecision::L32F})
  {
    const auto scalar = precision_rgb(color, format);
    assert(scalar[0] == scalar[1] && scalar[1] == scalar[2]);
  }
  const auto srgb = precision_rgb({.5f, .5f, .5f}, ChannelPrecision::SRGB8);
  assert(std::abs(srgb[0] - .50288646f) < 1e-6 &&
         srgb[0] != precision_rgb({.5, .5, .5}, ChannelPrecision::RGB8)[0]);
  assert(precision_rgb({-2, 2, .5}, ChannelPrecision::RGB8) ==
         (std::array<float, 3>{0, 1, 128.f / 255}));
  assert(precision_rgb({-2, 2, 1.0001f}, ChannelPrecision::RGB16F) ==
         (std::array<float, 3>{-2, 2, 1}));
  assert(precision_rgb({1e8f, -1e8f, 0}, ChannelPrecision::RGB16F) ==
         (std::array<float, 3>{65504, -65504, 0}));
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Precision";
  material.uniform_editable = true;
  scene.materials = {material};
  ImportSummary imported;
  imported.materials = {{1, "Precision"}};
  TextureDocument doc({}, imported, scene);
  auto &set = doc.set(1);
  std::vector<TextureSetBinding> bindings{{set.id, 1, set.name}};
  for (int format = 0; format < 9; ++format) {
    set.precision.fill(ChannelPrecision(format));
    const auto data = doc.to_json();
    auto loaded = TextureDocument::from_json(data, bindings);
    assert(loaded.to_json() == data && loaded.set(1).precision == set.precision);
  }
  auto legacy = doc.to_json();
  legacy["schema"] = 24;
  assert(TextureDocument::from_json(legacy, bindings).set(1).precision ==
         default_channel_precision);
  auto corrupt = doc.to_json();
  auto sets = corrupt["texture_sets"].toArray();
  auto entry = sets[0].toObject();
  entry["precision"] = QJsonArray{9, 0, 0, 0, 0, 0};
  sets[0] = entry;
  corrupt["texture_sets"] = sets;
  bool rejected = false;
  try {
    TextureDocument::from_json(corrupt, bindings);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  std::cout
      << "Nine channel formats: UNORM quantization, sRGB transfer, half HDR/clipping, luminance, exact float32, schema and migration passed\n";
}
