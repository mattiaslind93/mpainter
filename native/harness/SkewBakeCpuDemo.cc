// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkewBakeCpuDemo.hh"
#include "TexturePadding.hh"
#include "SkewBakeEngine.hh"
#include "AutoBakeEngine.hh"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
namespace bts {
int run_skew_bake_cpu_demo(const QString &directory)
{
  const auto require = [](bool test, const char *message) {
    if (!test)
      throw std::runtime_error(message);
  };
  SceneSnapshot low, high;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "SkewFixture";
  low.materials = {material};
  high.materials = {material};
  low.has_bounds = high.has_bounds = true;
  low.minimum = {-1, -1, 0};
  low.maximum = {1, 1, 1};
  high.minimum = {-1.2, -1.2, 0};
  high.maximum = {1.2, 1.2, .1};
  const auto triangle = [](SceneSnapshot &scene,
                           std::array<std::array<float, 3>, 3> points,
                           std::array<std::uint64_t, 3> ids,
                           std::array<std::array<float, 2>, 3> uv) {
    UvFaceTriangle t;
    t.positions = points;
    t.vertex_ids = ids;
    t.corners = uv;
    t.triangle_id = scene.uv_triangles.size();
    scene.uv_triangles.push_back(t);
  };
  triangle(low, {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}}}, {1, 2, 3}, {{{0, 0}, {1, 0}, {1, 1}}});
  triangle(low, {{{-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}}, {1, 3, 4}, {{{0, 0}, {1, 1}, {0, 1}}});
  // Connected wall changes the averaged cage normal on the floor; its UVs are in another tile.
  triangle(low, {{{1, -1, 0}, {1, -1, 1}, {1, 1, 1}}}, {2, 5, 6}, {{{1, 0}, {2, 0}, {2, 1}}});
  triangle(low, {{{1, -1, 0}, {1, 1, 1}, {1, 1, 0}}}, {2, 6, 3}, {{{1, 0}, {2, 1}, {1, 1}}});
  constexpr int strips = 80;
  for (int j = 0; j < strips; ++j) {
    const float a = -1.2f + 2.4f * j / strips, b = -1.2f + 2.4f * (j + 1) / strips,
                za = .06f + .025f * std::sin(a * 40), zb = .06f + .025f * std::sin(b * 40);
    const std::uint64_t k = j * 2 + 1;
    triangle(high,
             {{{a, -1.2f, za}, {b, -1.2f, zb}, {b, 1.2f, zb}}},
             {k, k + 2, k + 3},
             {{{0, 0}, {1, 0}, {1, 1}}});
    triangle(high,
             {{{a, -1.2f, za}, {b, 1.2f, zb}, {a, 1.2f, za}}},
             {k, k + 3, k + 1},
             {{{0, 0}, {1, 1}, {0, 1}}});
  }
  const auto bake = [&](int mode) {
    TexturePadding geometry(low, 1);
    BakeRayOptions ray;
    ray.relative = false;
    ray.frontal = .2;
    ray.rear = .2;
    ray.average_normals = true;
    if (mode >= 0)
      ray.skew_weight = [mode](int, double u, double) {
        return mode == 0 ? 0.f : mode == 1 ? 1.f : u > .5 ? 1.f : 0.f;
      };
    return geometry.project_source(low, high, nullptr, ray, 64, 64, {1001}, false)->tiles.at(1001);
  };
  const auto baseline = bake(-1), zero = bake(0), full = bake(1), painted = bake(2);
  double error = 0, change = 0, unpainted = 0;
  int changed = 0, covered = 0;
  for (std::size_t i = 0; i < baseline.size(); ++i) {
    require(baseline[i].valid == zero[i].valid, "Zero skew changed native ray coverage");
    if (!baseline[i].valid || !full[i].valid || !painted[i].valid)
      continue;
    ++covered;
    for (int c = 0; c < 3; ++c) {
      error = std::max(error,
                       std::abs(double(baseline[i].baked_normal[c] - zero[i].baked_normal[c])));
      change = std::max(change,
                        std::abs(double(baseline[i].baked_normal[c] - full[i].baked_normal[c])));
      if (i % 64 < 32)
        unpainted = std::max(
            unpainted, std::abs(double(baseline[i].baked_normal[c] - painted[i].baked_normal[c])));
    }
    changed += baseline[i].baked_normal != painted[i].baked_normal;
  }
  require(covered > 3000, "Native skew fixture has insufficient high-to-low coverage");
  require(error == 0 && unpainted == 0, "Unpainted skew texels changed native baking");
  require(change > .05 && changed > 100,
          "Painted skew failed to redirect actual high-poly normal sampling");
  TexturePadding invalid(low, 1);
  BakeRayOptions ray;
  ray.average_normals = false;
  ray.skew_weight = [](int, double, double) { return 1.f; };
  bool rejected = false;
  try {
    invalid.project_source(low, high, nullptr, ray, 32, 32, {1001}, false);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Skew accepted incompatible cage normals");

  for (auto &t : low.uv_triangles)
    t.boundary_edges = 7;
  const auto low_source = std::make_shared<const SceneSnapshot>(low),
             high_source = std::make_shared<const SceneSnapshot>(high);
  SkewBakeEngine engine(low_source, high_source);
  SkewField field;
  field.resolution = 128;
  field.settings.edge_protection = false;
  SkewBakeOptions options;
  options.resolution = options.height = 64;
  options.dilation = 0;
  options.ray.relative = false;
  options.ray.frontal = options.ray.rear = .2;
  std::atomic<bool> cancel = false;
  const auto initial = engine.bake(1, field, {1001}, options, {}, cancel);
  std::vector<float> correction(128 * 128 * 4, 0);
  for (int y = 0; y < 128; ++y)
    for (int x = 64; x < 128; ++x) {
      correction[(y * 128 + x) * 4] = 1;
      correction[(y * 128 + x) * 4 + 3] = 1;
    }
  field.pixels.assign_channel(128, 2, correction);
  const auto partial = engine.bake(1, field, {1001}, options, {{1001, {32, 0, 64, 64}}}, cancel);
  SkewBakeEngine reference(low_source, high_source);
  const auto full_reference = reference.bake(1, field, {1001}, options, {}, cancel);
  require(partial.tiles.at(1001).hash == full_reference.tiles.at(1001).hash,
          "Partial skew rebake differs from full native reference");
  require(initial.tiles.at(1001).hash != partial.tiles.at(1001).hash,
          "Skew engine did not change baked Normal");
  field.settings.edge_protection = true;
  require(engine.geometry()->edges() > 0, "Hard-edge BVH is empty");
  require(engine.geometry()->weight(field, 1001, .75, .5, {1, 0, 0}, 0) == 0,
          "Geometry edge protection did not suppress correction");
  require(engine.geometry()->weight(field, 1001, .75, .5, {0, 0, 0}, 0) == 1,
          "Geometry protection affected interior");
  require(engine.geometry()->distance({0, 0, 0}, 99) == std::numeric_limits<double>::infinity(),
          "Edge query crossed objects");
  TexturePadding vector_geometry(low, 1);
  const auto vectors = vector_geometry.skew_vectors(16, {1001});
  require(vectors.size() > 200, "Native UV vector grid has insufficient coverage");
  require(std::any_of(vectors.begin(),
                      vectors.end(),
                      [](const auto &v) { return v.average != v.geometric; }),
          "Skew vectors lost averaged normals");
  cancel = true;
  require(engine.bake(1, field, {1001}, options, {}, cancel).tiles.empty(),
          "Cancelled skew bake produced maps");
  for (std::size_t i = 0; i < low.uv_triangles.size(); ++i)
    require(low_source->uv_triangles[i].positions == low.uv_triangles[i].positions &&
                low_source->uv_triangles[i].corners == low.uv_triangles[i].corners &&
                low_source->uv_triangles[i].vertex_ids == low.uv_triangles[i].vertex_ids,
            "Skew modified original mesh/UV data");

  // The general worker preserves native Normal output while producing the other map roles.
  cancel = false;
  field.settings.edge_protection = false;
  AutoBakeOptions automatic;
  automatic.maps.resolution = 64;
  automatic.maps.height = 64;
  automatic.maps.dilation = 0;
  automatic.maps.rays = 8;
  automatic.maps.use_project_mesh = false;
  automatic.ray = options.ray;
  AutoBakeEngine general(low_source, high_source);
  const auto maps = general.bake(
      1, "SkewFixture", MaterialValues{}, {1001}, field, automatic, cancel);
  for (int role = 0; role < 7; ++role)
    require(maps[role] && maps[role]->tiles.contains(1001),
            "General automatic bake omitted a selected map");
  require(maps[0]->tiles.at(1001).hash == full_reference.tiles.at(1001).hash,
          "Automatic Normal differs from native skew reference");
  const auto repeat = general.bake(
      1, "SkewFixture", MaterialValues{}, {1001}, field, automatic, cancel);
  for (int role = 0; role < 7; ++role)
    require(repeat[role]->tiles.at(1001).hash == maps[role]->tiles.at(1001).hash,
            "Cached general bake changed pixels");
  automatic.maps.use_project_mesh = true;
  automatic.maps.enabled.fill(false);
  automatic.maps.enabled[2] = true;
  MaterialValues id;
  id.base_color = {.2f, .4f, .6f, 1};
  const auto ids = general.bake(1, "SkewFixture", id, {1001}, field, automatic, cancel);
  require(ids[2] && !ids[0] && !ids[3], "Automatic single-map bake changed other roles");
  const auto &color = ids[2]->tiles.at(1001).pixels->rgba;
  require(std::abs(color[0] - .2f) < 1e-6 && std::abs(color[1] - .4f) < 1e-6 &&
              std::abs(color[2] - .6f) < 1e-6 && color[3] == 1,
          "Project mesh ID differs from source material");
  cancel = true;
  require(!general.bake(1, "SkewFixture", id, {1001}, field, automatic, cancel)[2],
          "Cancelled automatic bake produced a map");
  std::printf(
      "BTS_AUTO_BAKE_ENGINE roles=7 normal_reference_exact=1 cached_repeat_exact=1 project_id_exact=1 single_role=1 cancel=1\n");
  std::printf(
      "BTS_SKEW_ENGINE partial_full_exact=1 source_cache=1 hard_edges=%zu native_vectors=%zu original_uv_unchanged=1\n",
      engine.geometry()->edges(),
      vectors.size());
  QDir().mkpath(directory);
  QFile file(QDir(directory).filePath("skew-bake-cpu.json"));
  require(file.open(QIODevice::WriteOnly), "Skew CPU proof unavailable");
  file.write(QJsonDocument(QJsonObject{{"covered", covered},
                                       {"painted_changed", changed},
                                       {"zero_max_error", error},
                                       {"unpainted_max_error", unpainted},
                                       {"full_max_change", change},
                                       {"native_blender_ray_projection", true},
                                       {"gpu_required", false}})
                 .toJson());
  std::printf(
      "BTS_SKEW_CPU covered=%d changed=%d zero_error=%g unpainted_error=%g normal_change=%g\n",
      covered,
      changed,
      error,
      unpainted,
      change);
  return 0;
}
}  // namespace bts
