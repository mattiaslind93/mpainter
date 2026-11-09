// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TexturePadding.hh"
#include "TextureMemory.hh"
#include "Performance.hh"
#include <QThreadPool>
#include <atomic>
#include <map>
#include <memory>
#include <string>
namespace bts {
// Geometry only: never evaluate a layer, touch Main, or allocate a GPU object.
// The promise is shared, so cancelling/replacing a request never waits on the UI.
struct ProjectionGeometryJob {
  using Sizes = std::map<int, std::pair<int, int>>;
  using Atlases = std::map<int, std::shared_ptr<const FillSurfaceAtlas>>;
  std::shared_ptr<TexturePadding> source;
  Sizes sizes;
  Atlases result;
  std::map<int, std::shared_ptr<const GeometryOwnershipTile>> ownership;
  std::map<std::uint32_t, std::string> names;
  std::string error;
  std::atomic_bool ready = false, cancelled = false;
  static QThreadPool &pool()
  {
    static QThreadPool pool;
    return pool;
  }
  static std::shared_ptr<ProjectionGeometryJob> start(std::shared_ptr<TexturePadding> source,
                                                      Sizes sizes)
  {
    auto job = std::make_shared<ProjectionGeometryJob>();
    job->source = std::move(source);
    job->sizes = std::move(sizes);
    pool().setMaxThreadCount(2);
    pool().start([job] {
      performance::Scope timing("projection_geometry_worker");
      try {
        if (job->cancelled.load(std::memory_order_relaxed)) {
          job->ready.store(true, std::memory_order_release);
          return;
        }
        std::uint64_t texels = 0, largest = 0;
        for (const auto &[tile, size] : job->sizes) {
          const auto count = std::uint64_t(size.first) * size.second;
          texels += count;
          largest = std::max(largest, count);
        }
        // Atlas storage plus conservative raster scratch. Budget follows available
        // RAM rather than rejecting a 4K tile at an arbitrary fixed byte limit.
        const auto required = texels * (sizeof(FillSurfacePoint) + sizeof(std::uint32_t)) +
                              largest * 64,
                   budget = texture_available_ram_bytes();

        auto padding = job->source->clone();
        job->names = padding->mesh_names();
        for (const auto &[tile, size] : job->sizes) {
          if (job->cancelled.load(std::memory_order_relaxed))
            break;
          job->result[tile] = padding->projection_geometry(size.first, {tile}, false, size.second);
          job->ownership[tile] = padding->mesh_ownership(size.first, tile, size.second);
        }
      }
      catch (const std::exception &error) {
        job->error = error.what();
      }
      job->ready.store(true, std::memory_order_release);
    });
    return job;
  }
};
}  // namespace bts
