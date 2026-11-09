// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialSourceLoader.hh"
#include "BlenderHost.hh"
#include "NativeMaterialAsset.hh"
#include <chrono>
namespace bts {
NativeMaterialSourceLoader::NativeMaterialSourceLoader(
    BlenderHost &host,
    QObject *parent,
    std::function<void()> redraw,
    std::function<void(const QString &)> progress)
    : QObject(parent), host_(host), redraw_(std::move(redraw)), progress_(std::move(progress))
{
  timer_.setInterval(16);
  QObject::connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}
NativeMaterialSourceLoader::~NativeMaterialSourceLoader()
{
  cancel_all();
}
void NativeMaterialSourceLoader::ensure(const StudioAsset &asset,
                                        std::vector<StudioAsset> textures,
                                        Ready ready,
                                        Failed failed,
                                        bool require_preview)
{
  if (!is_native_material_asset(asset))
    throw std::invalid_argument("Expected a native Blender material source");
  const bool resident = host_.has_native_material(asset.id);
  if (resident && !require_preview) {
    ready(asset);
    return;
  }
  auto found = jobs_.find(asset.id);
  if (found != jobs_.end()) {
    if (found->second.asset.hash() != asset.hash())
      throw std::invalid_argument("Native source changed during preparation");
    found->second.completions.push_back({std::move(ready), std::move(failed)});
    return;
  }
  Job job;
  job.asset = asset;
  job.completions.push_back({std::move(ready), std::move(failed)});
  if (resident && !host_.native_material_status(asset.id).generated_preview.isNull())
    job.queued = true;
  else
    job.worker = host_.prepare_material_resource(asset, job.cancel, std::move(textures));
  jobs_.emplace(asset.id, std::move(job));
  timer_.start();
  progress_("Loading " + asset.name + "…");
}
void NativeMaterialSourceLoader::prepared(PreparedBlendMaterial prepared,
                                          Ready ready,
                                          Failed failed)
{
  if (!prepared.native_graph || !is_native_material_asset(prepared.material) ||
      prepared.preview_only)
    throw std::invalid_argument("Native import has no prepared source");
  const auto id = prepared.material.id;
  cancel(id);
  Job job;
  job.asset = std::move(prepared.material);
  job.completions.push_back({std::move(ready), std::move(failed)});
  host_.prepare_native_material_gpu(id, job.asset.hash(), std::move(prepared.native_graph));
  job.queued = true;
  jobs_.emplace(id, std::move(job));
  timer_.start();
  redraw_();
}
void NativeMaterialSourceLoader::cancel(const QUuid &id)
{
  const auto found = jobs_.find(id);
  if (found == jobs_.end())
    return;
  found->second.cancel->store(true);
  if (found->second.queued)
    host_.cancel_native_material_preparation(id);
  jobs_.erase(found);
  if (jobs_.empty())
    timer_.stop();
}
void NativeMaterialSourceLoader::cancel_all()
{
  while (!jobs_.empty())
    cancel(jobs_.begin()->first);
}
void NativeMaterialSourceLoader::poll()
{
  struct Finished {
    StudioAsset asset;
    std::vector<Completion> completions;
    QString error;
  };
  std::vector<Finished> finished;
  for (auto it = jobs_.begin(); it != jobs_.end();) {
    auto &job = it->second;
    bool done = false;
    QString error;
    try {
      if (!job.queued && job.worker.valid() &&
          job.worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
      {
        host_.prepare_native_material_gpu(it->first, job.asset.hash(), job.worker.get());
        job.worker = {};
        job.queued = true;
      }
      if (job.queued) {
        const auto status = host_.native_material_status(it->first);
        progress_(job.asset.name + " — " + status.message);
        error = status.error;
        if (!status.pending && !status.ready && error.isEmpty())
          error =
              "Project changed during native GPU preparation; import or select the material again";
        done = status.ready || !error.isEmpty();
        if (status.ready && !status.generated_preview.isNull()) {
          // Bundle/PNG encoding stays on a worker, including legacy Shelf jobs.
          if (!job.preview_archive.valid())
            job.preview_archive = std::async(
                                      std::launch::async,
                                      [asset = job.asset, image = status.generated_preview] {
                                        auto result = asset;
                                        set_native_material_preview(result, image);
                                        return result;
                                      })
                                      .share();
          done = job.preview_archive.wait_for(std::chrono::seconds(0)) ==
                 std::future_status::ready;
          if (done) {
            job.asset = job.preview_archive.get();
            host_.publish_native_material_preview(it->first, job.asset.hash());
          }
        }
        if (!done)
          redraw_();
      }
    }
    catch (const std::exception &exception) {
      error = QString::fromUtf8(exception.what());
      done = true;
    }
    if (done) {
      finished.push_back({std::move(job.asset), std::move(job.completions), error});
      it = jobs_.erase(it);
    }
    else
      ++it;
  }
  if (jobs_.empty())
    timer_.stop();
  // Erase completed jobs first: callbacks may enqueue another source or change
  // the project, and must never invalidate the iteration above.
  for (auto &job : finished)
    for (auto &completion : job.completions) {
      if (!job.error.isEmpty()) {
        if (completion.failed)
          completion.failed(job.error);
        continue;
      }
      try {
        completion.ready(job.asset);
      }
      catch (const std::exception &error) {
        if (completion.failed)
          completion.failed(QString::fromUtf8(error.what()));
      }
    }
}
}  // namespace bts
