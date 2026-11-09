#pragma once
#include "NativeFragmentGeometry.hh"
#include <QThreadPool>
#include <atomic>
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <algorithm>
namespace bts {
// Immutable mesh preparation only. Cancellation never waits on the UI thread;
// GPU uploads are performed later by the single current owner.
struct NativeWorldNormalGeometryJob {
  std::atomic_bool cancelled = false, ready = false;
  std::shared_ptr<const NativeFragmentGeometry> geometry;
  std::string error;
  static QThreadPool &pool()
  {
    static QThreadPool value;
    return value;
  }
  static std::mutex &mutex()
  {
    static std::mutex value;
    return value;
  }
  static std::vector<std::weak_ptr<NativeWorldNormalGeometryJob>> &jobs()
  {
    static std::vector<std::weak_ptr<NativeWorldNormalGeometryJob>> value;
    return value;
  }
  // Host shutdown only. Replaced jobs remain tracked without retaining a Main;
  // cancel/drain before Blender global teardown, never inside an artist edit.
  static void shutdown()
  {
    {
      std::lock_guard lock(mutex());
      for (auto &entry : jobs())
        if (auto job = entry.lock())
          job->cancelled = true;
    }
    pool().waitForDone();
    std::lock_guard lock(mutex());
    jobs().clear();
  }
  static std::shared_ptr<NativeWorldNormalGeometryJob> start(
      std::shared_ptr<const NativeFragmentGeometrySource> source)
  {
    auto job = std::make_shared<NativeWorldNormalGeometryJob>();
    {
      std::lock_guard lock(mutex());
      std::erase_if(jobs(), [](const auto &entry) { return entry.expired(); });
      jobs().push_back(job);
    }
    pool().setMaxThreadCount(2);
    pool().start([job, source = std::move(source)] {
      // The layer atlas stores tangent perturbations, which can share UVs just
      // like painted normal maps. The former absolute-world overlap veto would
      // discard the entire material even for a subpixel UV seam overlap.
      try {
        job->geometry = prepare_native_fragment_geometry(source, job->cancelled);
      }
      catch (const std::exception &e) {
        job->error = e.what();
      }
      job->ready.store(true, std::memory_order_release);
    });
    return job;
  }
};
}  // namespace bts
