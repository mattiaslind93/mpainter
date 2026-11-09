// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MeshBaker.hh"
#include "BlenderHost.hh"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QScopeGuard>
namespace bts {
inline QJsonObject verify_production_bake_gpu(BlenderHost &host)
{
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  SceneSnapshot source;
  MaterialSummary material;
  material.session_id = 1;
  source.materials.push_back(material);
  source.minimum = {-1, -1, -.05f};
  source.maximum = {1, 1, 1};
  source.has_bounds = true;
  for (float z : {-.05f, 1.f}) {
    UvFaceTriangle triangle;
    triangle.positions = {{{-.2f, -.2f, z}, {.2f, -.2f, z}, {0, .2f, z}}};
    source.uv_triangles.push_back(triangle);
  }
  auto atlas = std::make_shared<FillSurfaceAtlas>();
  atlas->resolution = atlas->height = 64;
  auto &points = atlas->tiles[1001];
  points.resize(64 * 64);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      auto &point = points[y * 64 + x];
      point.valid = true;
      point.position = {float(x + .5f) / 64 - .5f, float(y + .5f) / 64 - .5f, 0};
    }
  const std::vector<MeshBakeInput> inputs{{1, "GPU mailbox proof", {}, atlas}};
  MeshBakeSettings settings;
  settings.resolution = settings.height = 64;
  settings.use_project_mesh = true;
  settings.dilation = 0;
  settings.rays = 16;
  settings.enabled.fill(false);
  settings.enabled[3] = settings.enabled[6] = true;
  const auto wait = [&](MeshBakeJob &job) {
    QElapsedTimer timer;
    timer.start();
    while (!job.finished.load(std::memory_order_acquire)) {
      job.process_gpu();
      require(timer.elapsed() < 30000, "Production bake mailbox timed out");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(job.error.empty(), job.error.c_str());
  };
  MeshBakeJob reference(source, inputs, settings);
  wait(reference);
  require(reference.cpu_ray_maps == 2 && reference.gpu_maps == 0, "CPU baseline used a GPU");
  const auto dispatch = [&](std::uint64_t available) {
    return [&, available](const std::function<void()> &work) {
      host.with_gpu([&] {
        const auto old_free = texture_free_vram.exchange(available);
        const auto old_valid = texture_vram_query_valid.exchange(true);
        const auto restore = qScopeGuard([&] {
          texture_free_vram = old_free;
          texture_vram_query_valid = old_valid;
        });
        work();
      });
    };
  };
  settings.with_gpu = dispatch(16ULL * 1024 * 1024);
  MeshBakeJob gpu(source, inputs, settings);
  wait(gpu);
  require(gpu.gpu_maps == 2 && gpu.cpu_ray_maps == 0 && !gpu.cpu_fallback,
          "Production GPU map path unexpectedly fell back");
  double maximum = 0;
  for (int role : {3, 6}) {
    const auto &a = reference.result.front().maps[role]->tiles.at(1001).pixels->rgba,
               &b = gpu.result.front().maps[role]->tiles.at(1001).pixels->rgba;
    require(a.size() == b.size(), "GPU map dimensions changed");
    for (std::size_t i = 0; i < a.size(); ++i)
      maximum = std::max(maximum, std::abs(double(a[i]) - b[i]));
  }
  require(maximum < 2e-5, "Production GPU AO/thickness differs from CPU including origin epsilon");
  settings.with_gpu = dispatch(0);
  MeshBakeJob fallback(source, inputs, settings);
  wait(fallback);
  require(fallback.cpu_ray_maps == 2 && fallback.gpu_maps == 0 && fallback.cpu_fallback,
          "Zero free VRAM did not continue on CPU");
  for (int role : {3, 6}) {
    const auto &a = reference.result.front().maps[role]->tiles.at(1001).pixels->rgba,
               &b = fallback.result.front().maps[role]->tiles.at(1001).pixels->rgba;
    require(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()),
            "CPU fallback changed the baseline image");
  }
  settings.with_gpu = dispatch(16ULL * 1024 * 1024);
  {
    MeshBakeJob cancelled(source, inputs, settings);
    QElapsedTimer timer;
    timer.start();
    while (!cancelled.gpu_pending() && !cancelled.finished.load(std::memory_order_acquire)) {
      require(timer.elapsed() < 30000, "GPU request did not reach owner mailbox");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(cancelled.gpu_pending(), "Cancellation fixture had no queued request");
    cancelled.cancel();
    while (!cancelled.finished.load(std::memory_order_acquire)) {
      require(timer.elapsed() < 30000, "Cancelled GPU mailbox failed to wake worker");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(cancelled.gpu_maps == 0 && !cancelled.error.empty(),
            "Cancelled request ran or published maps");
  }
  return {{"production_MeshBakeJob", true},
          {"resolution", 64},
          {"gpu_maps", 2},
          {"cpu_max_error", maximum},
          {"zero_VRAM_CPU_fallback_bit_exact", true},
          {"pending_GPU_cancel_join_safe", true}};
}
}  // namespace bts
