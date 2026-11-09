#pragma once
#include "GpuMarginPlan.hh"
#include "TexturePadding.hh"
#include "TextureMemory.hh"
#include "Performance.hh"
#include <QThreadPool>
#include <atomic>
#include <map>
#include <memory>
#include <string>
namespace bts {
struct MarginGeometryJob {
  using Sizes = std::map<int, std::pair<int, int>>;
  using Plans = std::map<int, std::shared_ptr<const GpuMarginPlan>>;
  std::shared_ptr<TexturePadding> source;
  Sizes sizes;
  Plans extend, adjacent;
  std::string error;
  std::atomic_bool ready = false, cancelled = false;
  static QThreadPool &pool()
  {
    static QThreadPool pool;
    return pool;
  }
  static std::shared_ptr<MarginGeometryJob> start(std::shared_ptr<TexturePadding> source,
                                                  Sizes sizes)
  {
    auto job = std::make_shared<MarginGeometryJob>();
    job->source = std::move(source);
    job->sizes = std::move(sizes);
    pool().setMaxThreadCount(1);
    pool().start([job] {
      performance::Scope timing("margin_geometry_worker");
      try {
        if (!job->cancelled.load(std::memory_order_relaxed)) {
          auto padding = job->source->clone();
          for (const auto &[tile, size] : job->sizes) {
            if (job->cancelled.load(std::memory_order_relaxed))
              break;
            // Raster, dependency arrays and worst-case sparse instructions for this
            // tile. Completed plans are already reflected in available RAM.
            const auto required = std::uint64_t(size.first) * size.second * 160,
                       budget = texture_available_ram_bytes();

            job->extend[tile] = padding->gpu_margin_plan(size.first, size.second, tile, false);
            job->adjacent[tile] = padding->gpu_margin_plan(size.first, size.second, tile, true);
          }
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
