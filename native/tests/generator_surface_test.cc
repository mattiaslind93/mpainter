// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshGenerator.hh"
#include "GpuLayerEffects.hh"
#include "SmartMaskThumbnailPacket.hh"
#include <cassert>
#include <cstdio>
#include <set>
int main()
{
  using namespace bts;
  ThumbnailMetadata preceding;
  preceding.ticket = 41;
  assert(!smart_mask_thumbnail_packet_ready(true, &preceding, 42));
  assert(!smart_mask_thumbnail_packet_ready(false, &preceding, 42));
  assert(!smart_mask_thumbnail_packet_ready(false, nullptr, 42));
  preceding.ticket = 42;
  assert(!smart_mask_thumbnail_packet_ready(true, &preceding, 42));
  assert(smart_mask_thumbnail_packet_ready(false, &preceding, 42));
  MeshGeneratorSettings legacy;
  legacy.generator = 7;
  legacy.grunge_scale = 11;
  legacy.grunge_seed = 41;
  const auto legacy_json = mesh_generator_json(legacy);
  assert(!legacy_json.contains("grunge_projection") && !legacy_json.contains("surface_fallback"));
  const auto old = read_mesh_generator(legacy_json);
  assert(!old.triplanar_grunge && !old.surface_fallback);
  assert(*generated_mask(legacy, nullptr, .2, .2, 1001) !=
         *generated_mask(legacy, nullptr, .8, .2, 1001));
  FillSurfaceAtlas atlas;
  atlas.resolution = atlas.height = 32;
  atlas.extent = 2;
  auto &points = atlas.tiles[1001];
  points.resize(32 * 32);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x) {
      auto &p = points[y * 32 + x];
      p.valid = true;
      p.position = {x / 16.f - 1, y / 16.f - 1, .2f + float(x + y) / 128};
      p.normal = {.2f + x / 64.f, .4f, .9f - y / 64.f};
    }
  points[6 * 32 + 25] = points[6 * 32 + 6];  // Different UV island address, same 3D point/frame.
  auto spatial = legacy;
  spatial.triplanar_grunge = true;
  spatial.surface_fallback = true;
  const auto encoded = mesh_generator_json(spatial);
  const auto decoded = read_mesh_generator(encoded);
  assert(decoded.triplanar_grunge && decoded.surface_fallback && decoded.grunge_seed == 41);
  const auto a = generated_mask(spatial, nullptr, .2, .2, 1001, nullptr, &atlas);
  const auto b = generated_mask(spatial, nullptr, .8, .2, 1001, nullptr, &atlas);
  const auto c = generated_mask(spatial, nullptr, .5, .5, 1001, nullptr, &atlas);
  assert(a && b && c && *a == *b && std::abs(*a - *c) > .005f);
  assert(!generated_mask(spatial, nullptr, .2, .2, 1001));
  AnchorSampleContext context;
  context.projection = &atlas;
  assert(generated_mask(spatial, nullptr, .2, .2, 1001, &context) == a);
  for (int kind : {8, 9}) {
    MeshGeneratorSettings e;
    e.generator = kind;
    e.grunge_amount = .4f;
    assert(!generated_mask(e, nullptr, .2, .2, 1001, nullptr, &atlas));
    e.triplanar_grunge = true;
    e.surface_fallback = true;
    float low = 1, high = 0;
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        const auto value = generated_mask(
            e, nullptr, (x + .5) / 32, (y + .5) / 32, 1001, nullptr, &atlas);
        assert(value && std::isfinite(*value));
        low = std::min(low, *value);
        high = std::max(high, *value);
      }
    assert(high - low > .02f);
    std::printf("BN_GENERATOR kind=%d fallback_min=%.9g fallback_max=%.9g\n", kind, low, high);
  }
  // Missing and installed-but-uncovered optional inputs use the same explicit
  // provisional surface policy. A covered baked texel replaces that proxy.
  for (int kind : {8, 9}) {
    MeshGeneratorSettings e;
    e.generator = kind;
    e.triplanar_grunge = e.surface_fallback = true;
    e.position_influence = .4f;
    e.normal_influence = .3f;
    e.thickness_influence = .6f;
    e.grunge_amount = .2f;
    MeshMaps maps;
    const auto absent = generated_mask(e, &maps, .5, .5, 1001, nullptr, &atlas);
    assert(absent);
    for (int role : {1, 3, 4, 5, 6}) {
      std::vector<float> rgba(32 * 32 * 4, .75f);
      for (int i = 0; i < 32 * 32; ++i)
        rgba[i * 4 + 3] = 0;
      auto pixels = std::make_shared<const MeshMapPixels>(32, std::move(rgba));
      MeshMap map;
      map.tiles[1001] = {32, pixels->hash, pixels};
      maps[role] = std::move(map);
    }
    const auto uncovered = generated_mask(e, &maps, .5, .5, 1001, nullptr, &atlas);
    assert(uncovered && *uncovered == *absent);
    std::array<bool, 6> needed{};
    gpu_need_generator_maps(needed, e);
    assert(needed[1] && needed[4] && needed[5]);
    needed.fill(false);
    gpu_need_generator_row_maps(needed, gpu_generator_row(e, maps));
    assert(needed[1] && needed[4] && needed[5]);
    e.input_anchors[0].source = QUuid::createUuid();
    assert(!generated_mask(
        e, &maps, .5, .5, 1001, nullptr, &atlas));  // Invalid explicit source is never replaced.
  }
  for (const char *key : {"grunge_projection", "surface_fallback"}) {
    auto bad = encoded;
    bad[key] = 42;
    bool rejected = false;
    try {
      read_mesh_generator(bad);
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  auto bad = encoded;
  bad["grunge_projection"] = "unknown";
  bool rejected = false;
  try {
    read_mesh_generator(bad);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  bad = encoded;
  bad["native_grunge"] = QJsonArray{0, 8, 0, 0, 1, 1, 0};
  rejected = false;
  try {
    read_mesh_generator(bad);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  points[6 * 32 + 6].normal = {0, 0, 0};
  assert(!generated_mask(spatial, nullptr, .2, .2, 1001, nullptr, &atlas));
  // Painter coverage detail cannot create paint on neutral planar faces.
  {
    MeshMaps maps;
    const auto map = [&](int role, float left, float right) {
      std::vector<float> data(32 * 32 * 4);
      for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
          const int i = (y * 32 + x) * 4;
          data[i] = data[i + 1] = data[i + 2] = x < 16 ? left : right;
          data[i + 3] = 1;
        }
      auto pixels = std::make_shared<const MeshMapPixels>(32, std::move(data));
      MeshMap m;
      m.tiles[1001] = {32, pixels->hash, pixels};
      maps[role] = std::move(m);
    };
    map(3, 1, 1);
    map(4, .5f, .95f);
    MeshGeneratorSettings e;
    e.generator = 8;
    e.response = 1;
    e.grunge_amount = 1;
    e.grunge_scale = 7;
    e.contrast = 2;
    e.bias = .3f;
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 16; ++x)
        assert(*generated_mask(e, &maps, (x + .5) / 32, (y + .5) / 32, 1001) == 0);
    const auto low = *generated_mask(e, &maps, .8, .5, 1001);
    e.bias += .2f;
    const auto high = *generated_mask(e, &maps, .8, .5, 1001);
    assert(high >= low);
    e.invert = true;
    assert(*generated_mask(e, &maps, .2, .5, 1001) == 1);
    e.invert = false;
    e.response = 2;
    e.axis = 1;
    const auto saved = read_mesh_generator(mesh_generator_json(e));
    assert(saved.response == 2 && saved.axis == 1);
    assert(gpu_generator_row(e, maps)[0][3] == 2);
    std::puts(
        "Painter response: exact black flats at full grunge, monotonic Level, invert and CPU/GPU parameter codec PASS");
  }
  // Imported texture input survives on a flat plane; legacy erosion does not.
  {
    MeshGeneratorSettings e;
    e.generator = 8;
    e.response = 4;
    e.grunge_amount = 1;
    e.grunge_scale = 7;
    e.surface_fallback = true;
    e.triplanar_grunge = true;
    const auto saved = read_mesh_generator(mesh_generator_json(e));
    assert(saved.response == 4);
    MeshMaps none;
    assert(gpu_generator_row(e, none)[0][3] == 4);
    const auto v = generated_mask(e, nullptr, .8, .5, 1001, nullptr, &atlas);
    assert(v && *v > 0 && *v < 1);
    auto higher = e;
    higher.bias = .1f;
    assert(*generated_mask(higher, nullptr, .8, .5, 1001, nullptr, &atlas) >= *v);
  }
  // Leak gaps must be dry, and neighboring samples must change more slowly
  // down a wall than across it. This catches blobs and an artificial gray floor.
  {
    int dry = 0;
    double across = 0, down = 0;
    float high = 0;
    constexpr int side = 128;
    for (int y = 0; y < side - 1; ++y)
      for (int x = 0; x < side - 1; ++x) {
        const float u = float(x) / side, v = float(y) / side;
        const float value = native_grunge(u, v, 3, 41, 3);
        assert(std::isfinite(value) && value >= 0 && value <= 1);
        dry += value == 0;
        high = std::max(high, value);
        across += std::abs(value - native_grunge(u + 1.f / side, v, 3, 41, 3));
        down += std::abs(value - native_grunge(u, v + 1.f / side, 3, 41, 3));
      }
    assert(dry > 0 && high > .25f && across > down * 3);
  }
  // A remote ridge must not be replicated onto a neutral face by a wide band.
  {
    MeshMaps maps;
    constexpr int side = 64;
    for (int role : {3, 4}) {
      std::vector<float> data(side * side * 4);
      for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
          const auto at = (y * side + x) * 4;
          data[at] = data[at + 1] = data[at + 2] = role == 3 ? 1.f : x == 48 ? .75f : .5f;
          data[at + 3] = 1;
        }
      auto pixels = std::make_shared<const MeshMapPixels>(side, std::move(data));
      MeshMap map;
      map.tiles[1001] = {side, pixels->hash, pixels};
      maps[role] = std::move(map);
    }
    MeshGeneratorSettings edge;
    edge.generator = 8;
    edge.response = 1;
    edge.map_filter = 1;
    edge.contrast = 5;
    edge.bias = -.15f;
    edge.curvature_radius = 38.8f;
    assert(*generated_mask(edge, &maps, 9.7 / side, 20.5 / side, 1001) == 0);
    const auto ridge = generated_mask(edge, &maps, 48.5 / side, 20.5 / side, 1001);
    assert(ridge && *ridge > .9f);
    edge.curvature_radius = 2.5f;
    assert(generated_mask(edge, &maps, 48.5 / side, 20.5 / side, 1001) == ridge);
    edge.response = 0;
    assert(*generated_mask(edge, &maps, 48.5 / side, 20.5 / side, 1001) < .4f);
  }
  std::puts(
      "Painter regressions: dry continuous leaks, local curvature bands, convex ridge support PASS");
  std::puts(
      "generator surface: legacy UV/strict defaults, exact optional-key codec, triplanar seam invariance and spatial fallback 7/8/9 PASS");
}
