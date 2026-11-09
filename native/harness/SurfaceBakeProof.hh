// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BlenderHost.hh"
#include "MeshBaker.hh"
#include "TexturePadding.hh"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <chrono>
#include <numbers>
#include <thread>
namespace bts {
// Small analytic checks exercise the production baker rather than an oracle copy.
inline void surface_bake_regressions()
{
  const auto require = [](bool ok, const char *message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  SceneSnapshot scene;
  scene.materials.resize(1);
  scene.materials[0].session_id = 1;
  scene.minimum = {0, 0, 0};
  scene.maximum = {1, 1, 0};
  constexpr int grid = 16;
  for (int y = 0; y < grid; ++y)
    for (int x = 0; x < grid; ++x)
      for (int h = 0; h < 2; ++h) {
        UvFaceTriangle t;
        t.has_normals = true;
        const std::array<std::array<int, 2>, 3> points =
            h == 0 ? std::array<std::array<int, 2>, 3>{{{x, y}, {x + 1, y}, {x + 1, y + 1}}} :
                     std::array<std::array<int, 2>, 3>{{{x, y}, {x + 1, y + 1}, {x, y + 1}}};
        for (int k = 0; k < 3; ++k) {
          const auto [a, b] = points[k];
          t.positions[k] = {float(a) / grid, float(b) / grid, 0};
          t.corners[k] = {float(a) / grid, float(b) / grid};
          t.normals[k] = {0, 0, 1};
          t.vertex_ids[k] = (std::uint64_t(1) << 32) | (b * (grid + 1) + a);
        }
        scene.uv_triangles.push_back(t);
      }
  TexturePadding geometry(scene, 1);
  auto atlas = geometry.projection_geometry(32, {1001}, false);
  MeshBakeSettings options;
  options.resolution = options.height = 32;
  options.use_project_mesh = true;
  options.dilation = 0;
  options.rays = 64;
  options.enabled.fill(false);
  options.enabled[3] = options.enabled[4] = options.enabled[6] = true;
  options.curvature_radius = .15f;
  const auto run = [&](const MeshBakeSettings &o) {
    MeshBakeJob job(scene, {{1, "plane", {}, atlas}}, o);
    while (!job.finished.load(std::memory_order_acquire))
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (!job.error.empty())
      throw std::runtime_error(job.error);
    return std::move(job.result.front().maps);
  };
  const auto flat = run(options);
  for (int role : {3, 4, 6}) {
    const auto &p = flat[role]->tiles.at(1001).pixels->rgba;
    for (std::size_t i = 0; i < p.size(); i += 4)
      if (p[i + 3])
        require(std::abs(p[i] - (role == 4 ? .5f : 1.f)) < 1e-6f,
                "Plane must have neutral curvature and no self-occlusion");
  }
  options.normal_detail = 1;
  auto &normal = options.normal_maps[1];
  normal.width = normal.height = 32;
  auto &pixels = normal.tiles[1001];
  pixels.resize(1024);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x)
      pixels[y * 32 + x] = normalized_normal(
          {static_cast<float>(.5f * std::sin(2 * std::numbers::pi * (x + .5f) / 32)), 0, 1});
  const auto detailed = run(options);
  require(detailed[4]->tiles.at(1001).hash == flat[4]->tiles.at(1001).hash,
          "Pointiness must ignore normal-map shading");
  options.normal_detail = 0;
  options.displacement[1].strength = -.2f;
  options.displacement[1].width = options.displacement[1].rows = 2;
  options.displacement[1].samples[1001] = {1, 1, 1, 1};
  const auto moved = prepare_bake_surface(scene, options);
  for (const auto &tri : moved.uv_triangles)
    for (const auto &v : tri.positions)
      require(std::abs(v[2] + .1f) < 1e-6f, "Signed displacement must affect traced geometry");
  require(scene.maximum[2] == 0, "Surface preparation must preserve the source snapshot");
  std::printf(
      "BTS_SURFACE_REGRESSION plane_visibility=1 normal_map_independent=1 negative_displacement=1 immutable_source=1\n");
}
// Small, headless production-path fixture. Raw RGBA32F is retained for comparisons.
inline int surface_bake_proof(BlenderHost &host, const QString &directory)
{
  QDir().mkpath(directory);
  QJsonArray results;
  if (qEnvironmentVariableIsSet("BTS_SURFACE_REGRESSION"))
    surface_bake_regressions();
  QOpenGLContext context;
  QOffscreenSurface surface;
  std::unique_ptr<QOpenGLFramebufferObject> target;
  struct Close {
    BlenderHost &host;
    bool active = false;
    ~Close()
    {
      if (active)
        host.shutdown();
    }
  } close{host};
  if (qEnvironmentVariableIsSet("BTS_SURFACE_GPU")) {
    context.setFormat(QSurfaceFormat::defaultFormat());
    if (!context.create())
      throw std::runtime_error("Cannot create surface proof GL context");
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid() || !context.makeCurrent(&surface))
      throw std::runtime_error("Cannot activate offscreen surface proof");
    target = std::make_unique<QOpenGLFramebufferObject>(
        32, 32, QOpenGLFramebufferObject::CombinedDepthStencil);
    close.active = true;
    host.initialize_gpu(context, surface, target->handle());
  }
  const auto root = qEnvironmentVariable("BTS_SURFACE_FIXTURES");
  for (const QString name : {"displaced", "shaderball"}) {
    auto scene = host.load_bake_source((root + "/" + name + ".obj").toStdString());
    // The fixtures have one texture set; preserve objects and world coordinates.
    for (auto &tri : scene.uv_triangles)
      tri.material = 0;
    const auto uid = scene.materials.front().session_id;
    MeshBakeSettings settings;
    settings.resolution = settings.height = 256;
    settings.use_project_mesh = true;
    settings.dilation = 4;
    settings.rays = 256;
    settings.enabled.fill(false);
    if (qEnvironmentVariableIsSet("BTS_SURFACE_GPU"))
      settings.with_gpu = [&](const std::function<void()> &work) { host.with_gpu(work); };
    settings.enabled[3] = settings.enabled[4] = settings.enabled[6] = true;
    if (name == "displaced") {
      BakeDisplacement field;
      field.width = field.rows = 128;
      field.subdivision = 1;
      field.strength = .18f;
      auto &samples = field.samples[1001];
      samples.resize(128 * 128);
      for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
          const double u = double(x) / 127, v = double(y) / 127;
          samples[y * 128 + x] = float(.5 + .45 * std::sin(8 * std::numbers::pi * u) *
                                                std::pow(std::sin(std::numbers::pi * v), 2) *
                                                std::sin(6 * std::numbers::pi * v));
        }
      settings.displacement[uid] = std::move(field);
    }
    if (qEnvironmentVariableIsSet("BTS_SURFACE_EXPORT")) {
      const auto evaluated = prepare_bake_surface(scene, settings);
      QFile obj(root + "/" + name + "-evaluated.obj");
      if (!obj.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot export evaluated fixture");
      QByteArray text;
      for (const auto &tri : evaluated.uv_triangles)
        for (int k = 0; k < 3; ++k) {
          const auto &p = tri.positions[k];
          text += "v " + QByteArray::number(p[0], 'g', 9) + " " +
                  QByteArray::number(p[1], 'g', 9) + " " + QByteArray::number(p[2], 'g', 9) + "\n";
        }
      for (const auto &tri : evaluated.uv_triangles)
        for (const auto &uv : tri.corners)
          text += "vt " + QByteArray::number(uv[0], 'g', 9) + " " +
                  QByteArray::number(uv[1], 'g', 9) + "\n";
      // Shared vertex indices are necessary for Cycles Pointiness adjacency.
      std::map<std::uint64_t, int> first;
      int index = 1;
      for (const auto &tri : evaluated.uv_triangles)
        for (auto id : tri.vertex_ids) {
          first.try_emplace(id, index);
          ++index;
        }
      index = 1;
      for (const auto &tri : evaluated.uv_triangles) {
        text += "f";
        for (auto id : tri.vertex_ids) {
          text += " " + QByteArray::number(first.at(id)) + "/" + QByteArray::number(index++);
        }
        text += "\n";
      }
      obj.write(text);
    }
    TexturePadding geometry(scene, uid);
    auto atlas = geometry.projection_geometry(256, {1001}, true);
    MeshBakeJob job(scene, {{uid, name.toStdString(), {}, atlas}}, settings);
    while (!job.finished.load(std::memory_order_acquire)) {
      job.process_gpu();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!job.error.empty())
      throw std::runtime_error(job.error);
    if (settings.with_gpu && job.gpu_maps != 3)
      throw std::runtime_error("Surface proof did not use GPU for AO, Thickness and Pointiness");
    for (int role : {3, 4, 6}) {
      const auto &pixels = job.result.front().maps[role]->tiles.at(1001).pixels->rgba;
      QFile file(directory + "/" + name + "-" + QString::number(role) + ".rgba");
      if (!file.open(QIODevice::WriteOnly) ||
          file.write(reinterpret_cast<const char *>(pixels.data()),
                     qint64(pixels.size() * sizeof(float))) !=
              qint64(pixels.size() * sizeof(float)))
        throw std::runtime_error("Cannot write surface proof");
      double low = 1e30, high = -1e30, sum = 0;
      int count = 0;
      for (std::size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i + 3] > 0) {
          if (!std::isfinite(pixels[i]))
            throw std::runtime_error("Nonfinite bake");
          low = std::min(low, double(pixels[i]));
          high = std::max(high, double(pixels[i]));
          sum += pixels[i];
          ++count;
        }
      results.append(QJsonObject{{"mesh", name},
                                 {"role", role},
                                 {"minimum", low},
                                 {"maximum", high},
                                 {"mean", sum / std::max(1, count)},
                                 {"covered", count},
                                 {"triangles", qint64(scene.uv_triangles.size())}});
    }
  }
  QFile file(directory + "/metrics.json");
  if (!file.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot write surface metrics");
  file.write(QJsonDocument(results).toJson());
  return 0;
}
}  // namespace bts
