// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuiltinSmartMasks.hh"
#include "TextureDocument.hh"
#include <string_view>
namespace bts::smart_mask_probe {
inline constexpr std::array<const char *, 8> names{"Rust",
                                                   "Edge Wear",
                                                   "Crease Dirt",
                                                   "Moss",
                                                   "Ground Dirt",
                                                   "Scratches",
                                                   "Leaks & Stains",
                                                   "Surface Dust"};
inline const StudioAsset &asset(const char *name)
{
  for (const auto &a : builtin_smart_mask_assets())
    if (a.name == name)
      return a;
  throw std::runtime_error("Missing standard mask generator");
}
inline void map(TextureSet &set, int role, float value)
{
  constexpr int side = 32;
  std::array<float, 4> pixel{value, value, value, 1};
  if (role == 5)
    pixel = {.5f, value, value, 1};
  if (role == 1)
    pixel = {.5f,
             .5f + .5f * std::sqrt(std::max(0.f, 1 - (value * 2 - 1) * (value * 2 - 1))),
             value,
             1};
  std::vector<float> values(side * side * 4);
  for (int i = 0; i < side * side; ++i)
    std::copy(pixel.begin(), pixel.end(), values.begin() + i * 4);
  auto pixels = std::make_shared<const MeshMapPixels>(side, std::move(values));
  MeshMap source;
  source.name = "Causal " + std::string(mesh_map_names[role]);
  source.tiles[1001] = {side, pixels->hash, pixels, side};
  set.mesh_maps[role] = std::move(source);
}
inline void profile(TextureSet &set, int generator)
{
  set.texture_resolution = set.texture_height = 128;
  auto atlas = std::make_shared<FillSurfaceAtlas>();
  atlas->resolution = atlas->height = 32;
  atlas->extent = 2;
  auto &points = atlas->tiles[1001];
  points.resize(32 * 32);
  for (auto &p : points) {
    p.valid = true;
    p.position = {.1f, .2f, .3f};
    p.normal = {.2f, .9f, .38f};
  }
  set.projection_geometry = std::move(atlas);
  map(set, 1, .6f);
  map(set, 3, generator == 8 ? .9f : .35f);
  map(set, 4, generator == 8 ? .9f : .2f);
  map(set, 5, .5f);
  map(set, 6, .5f);
}
struct Influence {
  int role;
  float stronger, weaker;
  const char *label;
};
inline std::vector<Influence> influences(const char *name)
{
  const std::string_view n(name);
  if (n == "Leaks & Stains")
    return {{3, .1f, .9f, "AO cavities"},
            {4, .1f, .9f, "concave curvature"},
            {5, .9f, .1f, "upper Y position"}};
  if (n == "Rust")
    return {{3, .1f, .9f, "AO cavities"},
            {4, .1f, .9f, "concave curvature"},
            {5, .1f, .9f, "bottom position"}};
  if (n == "Moss")
    return {{3, .1f, .9f, "AO cavities"}, {4, .1f, .9f, "concave curvature"}};
  if (n == "Crease Dirt")
    return {{3, .1f, .9f, "AO cavities"},
            {4, .1f, .9f, "concave curvature"},
            {6, .1f, .9f, "thin creases"}};
  if (n == "Edge Wear")
    return {{4, .95f, .05f, "convex curvature"},
            {3, .95f, .15f, "exposed AO"},
            {5, .9f, .1f, "upper position modulation"}};
  if (n == "Scratches")
    return {{4, .95f, .05f, "convex curvature"}, {3, .95f, .15f, "exposed AO"}};
  if (n == "Ground Dirt")
    return {
        {5, .1f, .9f, "bottom position"}, {3, .1f, .9f, "AO cavities"}, {1, 1, 0, "world normal"}};
  return {{1, 1, 0, "upward world normal"}, {3, .1f, .9f, "AO cavities"}};
}
inline LayerMask causal_mask(const LayerMask &mask)
{
  // Keep the supplied bitmap, projection, 3D point and scale fixed. Reduce only
  // its editable opacity and use identity Levels to avoid saturated endpoints
  // hiding a directional map response. Default recipes get a separate witness.
  auto result = mask;
  for (auto &e : result.effects) {
    if (e.kind == MaskEffectKind::Fill)
      e.opacity = .2f;
    if (e.kind == MaskEffectKind::Levels)
      e.levels = LevelsEffect{};
  }
  return result;
}
}  // namespace bts::smart_mask_probe
