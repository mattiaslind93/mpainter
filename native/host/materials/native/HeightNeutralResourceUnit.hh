// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "FillSpatialJSON.hh"
#include <cassert>
namespace bts {
inline void verify_height_neutral_resource_coordinates()
{
  TextureSet set;
  set.bump_origin = 0;
  set.base_values.bump = 0;
  auto &layer = set.layers.front();
  layer.kind = LayerKind::Fill;
  layer.values.bump = 0;
  auto texture = std::make_shared<FillTexture>();
  texture->width = texture->height = 1;
  texture->raw = true;
  texture->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
      1, FillTexture::Pixel{.5f, .5f, .5f, 0});
  fill_channel_texture(layer.fill, 4) = texture;
  // A retained old zero-based resource is untouched; an explicitly newly
  // assigned middle-gray resource gets a per-binding conversion, not a copy.
  assert(TextureDocument::composite(set, 64, 64).material.bump == 1);
  layer.fill.bump_bias = -set.bump_offset();
  assert(TextureDocument::composite(set, 64, 64).material.bump == .5f);
  assert(texture->pixels->front()[0] == .5f);
  const auto descriptor = fill_spatial_json(layer.fill);
  FillProjection restored;
  read_fill_spatial(restored, descriptor);
  assert(restored.bump_bias == -.5f);
  auto old = descriptor;
  old.remove("bump_bias");
  read_fill_spatial(restored, old);
  assert(restored.bump_bias == 0);
  auto invalid = descriptor;
  invalid["bump_bias"] = .25;
  bool rejected = false;
  try {
    read_fill_spatial(restored, invalid);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
  set.bump_origin = .5f;
  set.base_values.bump = .5f;
  layer.values.bump = .5f;
  layer.fill.bump_bias = 0;
  assert(TextureDocument::composite(set, 64, 64).material.bump == .5f);
}
}  // namespace bts
