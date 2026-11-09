#pragma once
#include "MeshBaker.hh"
#include "TexturePadding.hh"
#include "MEM_guardedalloc.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <thread>
namespace bts {
inline int rebake_proof(const QString &directory)
{
  if (qEnvironmentVariableIsSet("BTS_REBAKE_DEGENERATE"))
    MEM_set_memory_debug();
  SceneSnapshot scene;
  scene.materials.resize(1);
  scene.materials[0].session_id = 1;
  scene.minimum = {0, 0, 0};
  scene.maximum = {1, 1, 0};
  UvFaceTriangle tri;
  tri.positions = {{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
  tri.corners = {{{0, 0}, {1, 0}, {0, 1}}};
  tri.normals = {{{.2f, 0, 1}, {.2f, 0, 1}, {.2f, 0, 1}}};
  tri.has_normals = true;
  tri.vertex_ids = {1, 2, 3};
  scene.uv_triangles.push_back(tri);
  if (qEnvironmentVariableIsSet("BTS_REBAKE_DEGENERATE")) {
    for (int i = 0; i < 32; ++i) {
      const float x = float(i + 1) / 64;
      tri.positions = {{{x, 0, 0}, {x, 0, 0}, {x + .01f, 0, 0}}};
      tri.vertex_ids = {
          std::uint64_t(4 + i * 3), std::uint64_t(5 + i * 3), std::uint64_t(6 + i * 3)};
      scene.uv_triangles.push_back(tri);
    }
  }
  TexturePadding geometry(scene, 1);
  QJsonArray results;
  MeshMaps retained;
  std::map<int, std::array<std::string, 7>> hashes;
  for (int size : {64, 256, 1024, 256, 64, 1024, 64}) {
    std::printf("BTS_REBAKE_BEGIN size=%d\n", size);
    std::fflush(stdout);
    MeshBakeSettings settings;
    settings.resolution = settings.height = size;
    settings.use_project_mesh = true;
    settings.rays = 4;
    settings.dilation = 2;
    auto atlas = geometry.projection_geometry(size, {1001});
    const auto expected_normal = normalized_normal({.2f, 0, 1});
    for (const auto &point : atlas->tiles.at(1001))
      if (point.valid && !qEnvironmentVariableIsSet("BTS_REBAKE_DEGENERATE"))
        for (int c = 0; c < 3; ++c)
          if (std::abs(point.normal[c] - expected_normal[c]) > 1e-3f)
            throw std::runtime_error("Bake shading normals were not preserved");
    MeshBakeJob job(scene, {{1, "rebake", {}, atlas}}, settings);
    while (!job.finished.load(std::memory_order_acquire))
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (!job.error.empty())
      throw std::runtime_error(job.error);
    retained = std::move(job.result.front().maps);
    for (int role = 0; role < 7; ++role) {
      const auto &plane = retained.at(role)->tiles.at(1001);
      const auto &pixels = plane.pixels->rgba;
      if (plane.resolution != size || pixels.size() != std::size_t(size) * size * 4)
        throw std::runtime_error("Rebake retained wrong dimensions");
      std::size_t covered = 0;
      for (std::size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i + 3]) {
          ++covered;
          for (int c = 0; c < 4; ++c)
            if (!std::isfinite(pixels[i + c]))
              throw std::runtime_error("Nonfinite rebake pixel");
          if ((role == 3 || role == 6) && std::abs(pixels[i] - 1) > 1e-6)
            throw std::runtime_error("Plane visibility changed on rebake");
          if (role == 4 && std::abs(pixels[i] - .5f) > 1e-6)
            throw std::runtime_error("Plane curvature changed on rebake");
        }
      if (covered < std::size_t(size) * size / 3)
        throw std::runtime_error("Empty rebake map");
      auto &previous = hashes[size][role];
      if (!previous.empty() && previous != plane.hash)
        throw std::runtime_error("Rebake is not deterministic");
      previous = plane.hash;
    }
    results.append(
        QJsonObject{{"resolution", size}, {"maps", 7}, {"finite", true}, {"deterministic", true}});
    std::printf("BTS_REBAKE_PASS size=%d maps=7\n", size);
    std::fflush(stdout);
  }
  QDir().mkpath(directory);
  QFile file(directory + "/rebake.json");
  if (!file.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot write rebake results");
  file.write(QJsonDocument(results).toJson());
  return 0;
}
}  // namespace bts
