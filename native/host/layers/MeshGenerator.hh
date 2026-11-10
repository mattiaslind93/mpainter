// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MeshMaps.hh"
#include "NativeGrunge.hh"
#include "AnchorReference.hh"
#include "FillProjection.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
namespace bts {
struct MeshGeneratorSettings {
  std::array<AnchorReference, 2>
      input_anchors{};  // Primary map; Dirt uses Curvature as its second input.
  int generator = 0, axis = 2, curve = 0, map_filter = 0;
  int response = 0;  // 0 legacy, 1 support-preserving, 2 gravity leaks, 3 top moss, 4
                     // independently composed source inputs.
  float contrast = 1, bias = 0, dirt_curvature = .5f, curvature_radius = 0;
  // Native semantic approximations used by imported named generators.
  float grunge_amount = 0, grunge_scale = 8, ao_influence = 1, curvature_influence = 1;
  float position_influence = 0, normal_influence = 0, thickness_influence = 0;
  int grunge_seed = 0, grunge_pattern = 0;
  bool optional_inputs = false;  // Imported approximation may await live mesh maps.
  bool triplanar_grunge = false, surface_fallback = false;  // Legacy files stay UV/strict.
  std::array<float, 3> light_direction{0, 0, 1};
  bool invert = false;
};
inline bool generator_has_anchors(const MeshGeneratorSettings &e)
{
  return std::any_of(e.input_anchors.begin(), e.input_anchors.end(), [](const auto &ref) {
    return !ref.source.isNull();
  });
}
inline int generator_input_count(int generator)
{
  return generator == 5 || generator == 8 || generator == 9 ? 2 :
         generator == 7                                     ? 0 :
         generator > 0                                      ? 1 :
                                                              0;
}
inline int generator_input_role(int generator, int index)
{
  return index == 1                       ? (generator == 8 ? 3 : 4) :
         generator == 8                   ? 4 :
         generator == 9                   ? 3 :
         generator == 1                   ? 5 :
         generator == 2                   ? 4 :
         generator == 3 || generator == 5 ? 3 :
         generator == 4                   ? 6 :
         generator == 6                   ? 1 :
                                            -1;
}
inline std::array<float, 4> sample_id_map(const MeshMap *map, double u, double v, int tile)
{
  if (!map)
    return {};
  const auto found = map->tiles.find(tile);
  if (found == map->tiles.end() || !found->second.pixels)
    return {};
  const auto &pixels = *found->second.pixels;
  const int x = std::clamp(int(u * pixels.resolution), 0, pixels.resolution - 1),
            y = std::clamp(int(v * pixels.height), 0, pixels.height - 1);
  std::array<float, 4> rgba;
  for (int c = 0; c < 4; ++c)
    rgba[c] = pixels.rgba[(std::size_t(y) * pixels.resolution + x) * 4 + c];
  if (rgba[3] > 1e-8f)
    for (int c = 0; c < 3; ++c)
      rgba[c] /= rgba[3];
  return rgba;
}
inline constexpr std::array<const char *, 10> mask_generator_names{"No generator selected",
                                                                   "World Height",
                                                                   "Curvature",
                                                                   "Ambient Occlusion",
                                                                   "Thickness",
                                                                   "Dirt",
                                                                   "Light (World Normal)",
                                                                   "Native Grunge",
                                                                   "Native Edge Wear",
                                                                   "Native Cavity Dirt"};
inline int mask_generator_role(int generator)
{
  return generator == 1                                     ? 5 :
         generator == 2 || generator == 8                   ? 4 :
         generator == 3 || generator == 5 || generator == 9 ? 3 :
         generator == 4                                     ? 6 :
         generator == 6                                     ? 1 :
                                                              -1;
}
inline unsigned mask_generator_inputs(int generator)
{
  const int role = mask_generator_role(generator);
  return role < 0 ?
             0u :
             (1u << role) |
                 (generator == 5 || generator == 9 ? 1u << 4 :
                  generator == 8                   ? 1u << 3 :
                                                     0u) |
                 (generator == 8 || generator == 9 ? (1u << 1) | (1u << 5) | (1u << 6) : 0u);
}
inline bool mask_generator_maps_present(const MeshMaps &maps, int generator)
{
  const auto required = mask_generator_inputs(generator);
  if (generator == 7)
    return true;
  if (!required)
    return false;
  for (int role = 0; role < 7; ++role)
    if ((required & (1u << role)) && !maps[role])
      return false;
  return true;
}
inline bool generator_resources_present(const MeshMaps &maps,
                                        const MeshGeneratorSettings &settings)
{
  if (!settings.generator)
    return false;
  for (int slot = 0; slot < generator_input_count(settings.generator); ++slot)
    if (settings.input_anchors[slot].source.isNull() &&
        !maps[generator_input_role(settings.generator, slot)])
      return false;
  return true;
}
inline std::array<float, 4> sample_generator_map(
    const MeshMap *map, double u, double v, int tile, int filtering)
{
  if (!filtering)
    return sample_id_map(map, u, v, tile);
  if (!map)
    return {};
  const auto found = map->tiles.find(tile);
  if (found == map->tiles.end() || !found->second.pixels)
    return {};
  const auto &pixels = *found->second.pixels;
  const double x = u * pixels.resolution - .5, y = v * pixels.height - .5;
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  const auto at = [&](int x, int y, int c) {
    return pixels.rgba[(std::size_t(std::clamp(y, 0, pixels.height - 1)) * pixels.resolution +
                        std::clamp(x, 0, pixels.resolution - 1)) *
                           4 +
                       c];
  };
  std::array<float, 4> result;
  for (int c = 0; c < 4; ++c)
    result[c] = std::lerp(std::lerp(at(ix, iy, c), at(ix + 1, iy, c), float(x - ix)),
                          std::lerp(at(ix, iy + 1, c), at(ix + 1, iy + 1, c), float(x - ix)),
                          float(y - iy));
  if (result[3] > 1e-8f)
    for (int c = 0; c < 3; ++c)
      result[c] /= result[3];
  return result;
}
struct GeneratorSurface {
  std::array<float, 3> position{}, normal{};
  bool valid = false;
};
inline GeneratorSurface generator_surface(
    const FillSurfaceAtlas *atlas, const MeshMaps *maps, double u, double v, int tile)
{
  GeneratorSurface result;
  if (atlas && atlas->resolution > 0 && atlas->extent > 1e-20) {
    const auto found = atlas->tiles.find(tile);
    if (found != atlas->tiles.end()) {
      const int rows = atlas->height ? atlas->height : atlas->resolution;
      const int x = std::clamp(int(u * atlas->resolution), 0, atlas->resolution - 1),
                y = std::clamp(int(v * rows), 0, rows - 1);
      const auto index = std::size_t(y) * atlas->resolution + x;
      if (index < found->second.size()) {
        const auto &point = found->second[index];
        if (point.valid && std::hypot(point.normal[0], point.normal[1], point.normal[2]) > 1e-12f)
        {
          for (int c = 0; c < 3; ++c) {
            result.position[c] = float((point.position[c] - atlas->center[c]) / atlas->extent);
            result.normal[c] = point.normal[c];
          }
          result.valid = true;
        }
      }
    }
  }
  // A baked Position map already uses the normalized object bounds frame.
  if (!result.valid && maps && (*maps)[5] && (*maps)[1]) {
    const auto p = sample_generator_map(&*(*maps)[5], u, v, tile, 1),
               n = sample_generator_map(&*(*maps)[1], u, v, tile, 1);
    if (p[3] > 1e-8f && n[3] > 1e-8f) {
      for (int c = 0; c < 3; ++c) {
        result.position[c] = p[c] - .5f;
        result.normal[c] = n[c] * 2 - 1;
      }
      result.valid = std::hypot(result.normal[0], result.normal[1], result.normal[2]) > 1e-12f;
    }
  }
  return result;
}
// Explicit provisional surface proxies, not baked occlusion/curvature claims.
// They make missing-map presets useful while the visible Bake action remains.
inline std::array<float, 4> generator_surface_map(const GeneratorSurface &s, int role)
{
  if (!s.valid)
    return {};
  const float length = std::hypot(s.normal[0], s.normal[1], s.normal[2]);
  if (length <= 1e-12f)
    return {};
  std::array<float, 3> n;
  for (int c = 0; c < 3; ++c)
    n[c] = s.normal[c] / length;
  if (role == 5)
    return {std::clamp(s.position[0] + .5f, 0.f, 1.f),
            std::clamp(s.position[1] + .5f, 0.f, 1.f),
            std::clamp(s.position[2] + .5f, 0.f, 1.f),
            1};
  if (role == 1)
    return {.5f + .5f * n[0], .5f + .5f * n[1], .5f + .5f * n[2], 1};
  const float up = std::clamp(.5f + .5f * n[2], 0.f, 1.f),
              height = std::clamp(s.position[2] + .5f, 0.f, 1.f);
  const float value = role == 3 ? .15f + .3f * up + .15f * height :
                      role == 4 ? .5f + .65f * (1 - std::abs(n[2])) :
                      role == 6 ? .25f + .5f * height :
                                  .5f;
  return {value, value, value, 1};
}
inline std::optional<float> generated_mask(const MeshGeneratorSettings &effect,
                                           const MeshMaps *maps,
                                           double u,
                                           double v,
                                           int tile,
                                           const AnchorSampleContext *context = nullptr,
                                           const FillSurfaceAtlas *atlas = nullptr)
{
  const auto shape = [&](float input) {
    float value = std::clamp((input - .5f + (effect.response == 4 ? effect.bias : 0.f)) *
                                     effect.contrast +
                                 .5f + (effect.response == 4 ? 0.f : effect.bias),
                             0.f,
                             1.f);
    if (effect.invert)
      value = 1 - value;
    if (effect.curve == 1)
      value = value * value * (3 - 2 * value);
    else if (effect.curve == 2)
      value *= value;
    return value;
  };
  const auto surface = effect.triplanar_grunge || effect.surface_fallback || effect.response == 2 ?
                           generator_surface(atlas   ? atlas :
                                             context ? context->projection :
                                                       nullptr,
                                             maps,
                                             u,
                                             v,
                                             tile) :
                           GeneratorSurface{};
  const bool noise_required = effect.generator == 7 || effect.grunge_amount > 0;
  if (noise_required && effect.triplanar_grunge && !surface.valid)
    return {};
  const float frequency = effect.grunge_scale *
                          (effect.response && effect.grunge_pattern != 3 ? 8.f : 1.f);
  const float noise = !noise_required         ? .5f :
                      effect.triplanar_grunge ? native_grunge_triplanar(surface.position,
                                                                        surface.normal,
                                                                        frequency,
                                                                        effect.grunge_seed,
                                                                        effect.grunge_pattern) :
                                                native_grunge(float(u),
                                                              float(v),
                                                              frequency,
                                                              effect.grunge_seed,
                                                              effect.grunge_pattern);
  if (effect.generator == 7)
    return shape(noise);
  const int role = mask_generator_role(effect.generator);
  if (role < 0)
    return {};
  const auto sample_input = [&](int slot) -> std::optional<std::array<float, 4>> {
    const auto &ref = effect.input_anchors[slot];
    if (!ref.source.isNull()) {
      if (!context || !context->resolve)
        return {};
      const auto raw = context->resolve(*context, ref);
      if (!raw.valid)
        return {};
      auto sample = anchor_components(raw, ref);
      for (int c = 0; c < 3; ++c)
        sample[c] *= sample[3];
      return sample;
    }
    const int input_role = generator_input_role(effect.generator, slot);
    if (maps && (*maps)[input_role]) {
      const auto value = sample_generator_map(
          &*(*maps)[input_role], u, v, tile, effect.map_filter);
      if (value[3] > 1e-8f || !effect.surface_fallback)
        return value;
    }
    if (effect.surface_fallback && surface.valid)
      return generator_surface_map(surface, input_role);
    return {};
  };
  const auto primary = sample_input(0);
  if (!primary)
    return {};
  std::optional<std::array<float, 4>> secondary;
  if (effect.generator == 5 || effect.generator == 8 || effect.generator == 9) {
    secondary = sample_input(1);
    if (!secondary)
      return {};
  }
  auto sample = *primary;
  if (effect.generator == 8 && effect.curvature_radius > 0 && maps && (*maps)[4] &&
      effect.input_anchors[0].source.isNull())
  {
    const auto found = (*maps)[4]->tiles.find(tile);
    if (found != (*maps)[4]->tiles.end() && found->second.pixels) {
      const auto &pixels = *found->second.pixels;
      const float radius = std::min(effect.curvature_radius, 2.5f);
      for (const auto &offset :
           std::array<std::array<int, 2>, 8>{
               {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}})
      {
        const auto nearby = sample_generator_map(&*(*maps)[4],
                                                 u + offset[0] * radius / pixels.resolution,
                                                 v + offset[1] * radius / pixels.height,
                                                 tile,
                                                 effect.map_filter);
        if (nearby[3] > 1e-8f)
          sample[0] = std::max(sample[0], nearby[0]);
      }
    }
  }
  if (sample[3] <= 1e-8f)
    return 0.f;
  float input = sample[effect.generator == 1 ? effect.axis : 0];
  if (effect.generator == 8) {
    if ((*secondary)[3] <= 1e-8f)
      return 0.f;
    float convex = std::max(0.f, (sample[0] - .5f) * 2.f);
    if (effect.response != 0 && convex > 0.f)
      convex = std::min(1.f, std::pow(convex, .6f) * 1.32f);
    input = convex * effect.curvature_influence;
    if (effect.response > 0 && effect.response < 4 && effect.grunge_pattern == 2 &&
        effect.ao_influence < .75f)
      input = std::max(input, noise * .46f);
    input *= std::lerp(1.f, std::clamp((*secondary)[0], 0.f, 1.f), effect.ao_influence);
  }
  else if (effect.generator == 9) {
    if ((*secondary)[3] <= 1e-8f)
      return 0.f;
    float occ = 1.f - std::clamp(sample[0], 0.f, 1.f);
    if (effect.response != 0 && occ > 0.f)
      occ = std::pow(occ, .72f);
    float conc = std::max(0.f, (.5f - (*secondary)[0]) * 2.f);
    if (effect.response != 0 && conc > 0.f)
      conc = std::pow(conc, .72f);
    input = std::clamp(occ * effect.ao_influence + conc * effect.curvature_influence, 0.f, 1.f);
  }
  else if (effect.generator == 5) {
    if ((*secondary)[3] <= 1e-8f)
      return 0.f;
    input = (1 - std::clamp(sample[0], 0.f, 1.f)) *
            (1 - effect.dirt_curvature * std::clamp((*secondary)[0], 0.f, 1.f));
  }
  else if (effect.generator == 6) {
    std::array<float, 3> normal;
    float square = 0, light_square = 0, dot = 0;
    for (int c = 0; c < 3; ++c) {
      normal[c] = sample[c] * 2 - 1;
      square += normal[c] * normal[c];
      light_square += effect.light_direction[c] * effect.light_direction[c];
      dot += normal[c] * effect.light_direction[c];
    }
    input = square > 1e-12f && light_square > 1e-12f ?
                std::max(0.f, dot / std::sqrt(square * light_square)) :
                0;
  }
  if (effect.response == 3) {
    const auto normal = maps && (*maps)[1] ?
                            sample_generator_map(&*(*maps)[1], u, v, tile, effect.map_filter) :
                            generator_surface_map(surface, 1);
    const float up = normal[3] > 1e-8f ?
                         std::clamp((normal[1] * 2.f - 1.f) * .75f + .18f, 0.f, 1.f) :
                         0.f;
    input = std::clamp(std::lerp(input, up, effect.position_influence) + .12f * up, 0.f, 1.f);
  }
  if (effect.generator == 8 || effect.generator == 9) {
    for (const auto &[role, weight] :
         std::array<std::pair<int, float>, 3>{{{5, effect.position_influence},
                                               {1, effect.normal_influence},
                                               {6, effect.thickness_influence}}})
      if (weight > 0 && !(effect.response == 3 && role == 5)) {
        const auto value = [&] {
          if (maps && (*maps)[role]) {
            const auto value = sample_generator_map(
                &*(*maps)[role], u, v, tile, effect.map_filter);
            if (value[3] > 1e-8f || !effect.surface_fallback)
              return value;
          }
          return effect.surface_fallback ? generator_surface_map(surface, role) :
                                           std::array<float, 4>{};
        }();
        if (value[3] > 1e-8f) {
          const float factor = role == 6 ?
                                   1 - std::clamp(value[0], 0.f, 1.f) :
                               role == 1 && effect.response ?
                                   std::max(0.f, value[effect.axis] * 2 - 1) :
                                   std::clamp(value[effect.response ? effect.axis : 2], 0.f, 1.f);
          if (effect.response == 4 && role == 5)
            input = std::lerp(input, factor, weight);
          else
            input *= std::lerp(1.f, factor, weight);
        }
      }
  }
  if (effect.response == 2) {
    if (!surface.valid)
      return {};
    // Gravity is world Y: horizontal projections share the same downward axis.
    const float a = native_grunge(surface.position[0] + .5f,
                                  .5f - surface.position[1],
                                  effect.grunge_scale,
                                  effect.grunge_seed,
                                  3);
    const float b = native_grunge(surface.position[2] + .5f,
                                  .5f - surface.position[1],
                                  effect.grunge_scale,
                                  effect.grunge_seed,
                                  3);
    const float nx = std::abs(surface.normal[0]), nz = std::abs(surface.normal[2]);
    const float streak = nx + nz > 1e-8f ? (a * nz + b * nx) / (nx + nz) : a;
    const float vertical = 1 - std::clamp(std::abs(surface.normal[1]), 0.f, 1.f);
    const float drip = std::clamp(streak * 1.45f * std::sqrt(vertical) *
                                      std::lerp(.65f, 1.f, std::clamp(input, 0.f, 1.f)),
                                  0.f,
                                  1.f);
    input = std::max(input * std::lerp(1.f, 2 * noise, effect.grunge_amount), drip);
  }
  else if (effect.response == 4 && effect.grunge_amount > 0) {
    // A source texture is an independent input. Multiplying by curvature would
    // erase all authored surface grunge on flat, unoccluded regions.
    input = std::clamp(std::lerp(input, noise, effect.grunge_amount), 0.f, 1.f);
  }
  else if (effect.grunge_amount > 0)
    input = effect.response ?
                std::clamp(input * std::lerp(1.f, 2 * noise, effect.grunge_amount), 0.f, 1.f) :
                std::clamp(input + (noise - .5f) * effect.grunge_amount, 0.f, 1.f);
  // Detail erodes existing support; it cannot paint neutral planar regions.
  if (effect.response > 0 && effect.response < 4 && input <= 0 &&
      (effect.generator == 8 || effect.generator == 9))
    return effect.invert ? 1.f : 0.f;
  return shape(input);
}

inline void write_generator_anchors(QJsonObject &o, const MeshGeneratorSettings &e)
{
  if (!generator_has_anchors(e))
    return;
  QJsonArray inputs;
  for (const auto &ref : e.input_anchors)
    inputs.append(ref.source.isNull() ? QJsonValue(QJsonValue::Null) :
                                        QJsonValue(anchor_reference_json(ref)));
  o["input_anchors"] = inputs;
}
inline QJsonObject mesh_generator_json(const MeshGeneratorSettings &e)
{
  QJsonObject o = {{"generator", e.generator},
                   {"axis", e.axis},
                   {"curve", e.curve},
                   {"map_filter", e.map_filter},
                   {"contrast", e.contrast},
                   {"bias", e.bias},
                   {"invert", e.invert},
                   {"dirt_curvature", e.dirt_curvature},
                   {"light_direction",
                    QJsonArray{e.light_direction[0], e.light_direction[1], e.light_direction[2]}}};
  o["optional_inputs"] = e.optional_inputs;
  if (e.response)
    o["response"] = e.response;
  if (e.curvature_radius)
    o["curvature_radius"] = e.curvature_radius;
  o["native_grunge"] = QJsonArray{e.grunge_amount,
                                  e.grunge_scale,
                                  e.grunge_seed,
                                  e.grunge_pattern,
                                  e.ao_influence,
                                  e.curvature_influence,
                                  e.position_influence,
                                  e.normal_influence,
                                  e.thickness_influence};
  if (e.triplanar_grunge)
    o["grunge_projection"] = "triplanar";
  if (e.surface_fallback)
    o["surface_fallback"] = true;
  write_generator_anchors(o, e);
  return o;
}
inline MeshGeneratorSettings read_mesh_generator(const QJsonObject &o)
{
  const auto fail = []() { throw std::invalid_argument("Invalid mesh generator settings"); };
  const auto number = [&](const char *key, double low, double high) {
    const auto v = o[key];
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < low || v.toDouble() > high)
      fail();
    return float(v.toDouble());
  };
  MeshGeneratorSettings e;
  if (o.contains("curvature_radius"))
    e.curvature_radius = number("curvature_radius", 0, 64);
  if (o.contains("response")) {
    const auto v = number("response", 0, 4);
    if (v != std::floor(v))
      fail();
    e.response = int(v);
  }
  if (o.contains("input_anchors")) {
    const auto inputs = o["input_anchors"];
    if (!inputs.isArray() || inputs.toArray().size() != 2)
      fail();
    for (int slot = 0; slot < 2; ++slot) {
      const auto value = inputs.toArray()[slot];
      if (!value.isNull()) {
        e.input_anchors[slot] = read_anchor_reference(value);
        if (e.input_anchors[slot].source.isNull())
          fail();
      }
    }
  }
  const auto generator = number("generator", 0, 9), axis = number("axis", 0, 2);
  if (generator != std::floor(generator) || axis != std::floor(axis) || !o["invert"].isBool())
    fail();
  e.generator = int(generator);
  e.axis = int(axis);
  e.invert = o["invert"].toBool();
  e.contrast = number("contrast", 0, 8);
  e.bias = number("bias", -1, 1);
  if (o.contains("optional_inputs")) {
    if (!o["optional_inputs"].isBool())
      fail();
    e.optional_inputs = o["optional_inputs"].toBool();
  }
  if (o.contains("grunge_projection")) {
    const auto value = o["grunge_projection"];
    if (!value.isString() || (value.toString() != "uv" && value.toString() != "triplanar"))
      fail();
    e.triplanar_grunge = value.toString() == "triplanar";
  }
  if (o.contains("surface_fallback")) {
    if (!o["surface_fallback"].isBool())
      fail();
    e.surface_fallback = o["surface_fallback"].toBool();
  }
  if (o.contains("native_grunge")) {
    const auto a = o["native_grunge"].toArray();
    if (a.size() != 6 && a.size() != 9)
      fail();
    const auto value = [&](int i, float lo, float hi) {
      if (!a[i].isDouble() || !std::isfinite(a[i].toDouble()) || a[i].toDouble() < lo ||
          a[i].toDouble() > hi)
        fail();
      return float(a[i].toDouble());
    };
    e.grunge_amount = value(0, 0, 1);
    e.grunge_scale = value(1, .01f, 4096);
    e.grunge_seed = int(value(2, 0, 65535));
    e.grunge_pattern = int(value(3, 0, 3));
    if (e.grunge_seed != a[2].toDouble() || e.grunge_pattern != a[3].toDouble())
      fail();
    e.ao_influence = value(4, 0, 1);
    e.curvature_influence = value(5, 0, 1);
    if (a.size() == 9) {
      e.position_influence = value(6, 0, 1);
      e.normal_influence = value(7, 0, 1);
      e.thickness_influence = value(8, 0, 1);
    }
  }
  if (o.contains("dirt_curvature"))
    e.dirt_curvature = number("dirt_curvature", 0, 1);
  if (o.contains("light_direction")) {
    const auto v = o["light_direction"];
    if (!v.isArray() || v.toArray().size() != 3)
      fail();
    for (int c = 0; c < 3; ++c) {
      const auto value = v.toArray()[c];
      if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > 1)
        fail();
      e.light_direction[c] = float(value.toDouble());
    }
  }
  for (const char *key : {"curve", "map_filter"})
    if (o.contains(key)) {
      const auto value = number(key, 0, QString(key) == "curve" ? 2 : 1);
      if (value != std::floor(value))
        fail();
      if (QString(key) == "curve")
        e.curve = int(value);
      else
        e.map_filter = int(value);
    }
  return e;
}
}  // namespace bts
