// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialImageUpload.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialResourcePreview.hh"
#include <QElapsedTimer>
#include <QScopeGuard>
#include <atomic>
#include <array>
#include "Performance.hh"
namespace bts {
// GPU-owner state machine, called once per frame. Artist activation waits for
// ready; pixels and programs are complete before a layer can use the resource.
class NativeMaterialGpuPreparation {
 public:
  NativeMaterialGpuPreparation(NativeMaterialGpuLibrary &library,
                               std::string resource,
                               std::string hash,
                               std::shared_ptr<NativeMaterialGraph> graph)
      : library_(library),
        resource_(std::move(resource)),
        hash_(std::move(hash)),
        graph_(std::move(graph))
  {
    if (resource_.empty() || hash_.empty() || !graph_)
      throw std::invalid_argument("Invalid native material preparation");
    static std::atomic_uint64_t serial{0};
    pending_ = resource_ + "/pending-" + std::to_string(++serial);
    upload_ = std::make_unique<NativeMaterialImageUpload>(graph_);
    if (graph_->world_normal_tree && graph_->world_normal_error.isEmpty()) {
      std::array<blender::bNodeTree *, 1> world{graph_->world_normal_tree};
      world_upload_ = std::make_unique<NativeMaterialImageUpload>(graph_, world, false);
    }
    for (const auto &channel : graph_->channels)
      if (channel.tree)
        channels_.push_back(channel.channel);
  }
  ~NativeMaterialGpuPreparation()
  {
    if (installed_ && !ready_)
      library_.erase(pending_);
  }
  bool step(std::size_t packet_bytes = 4ULL * 1024 * 1024)
  {
    QElapsedTimer clock;
    clock.start();
    const auto cpu = performance::thread_ms();
    const auto phase = status().toUtf8();
    const auto record = qScopeGuard([&] {
      const auto ms = clock.nsecsElapsed() / 1e6;
      maximum_ms_ = std::max(maximum_ms_, ms);
      if (qEnvironmentVariableIsSet("BTS_NATIVE_PREPARATION_TRACE"))
        std::printf("BTS_NATIVE_PREPARE_PHASE phase=\"%s\" wall_ms=%.6f cpu_ms=%.6f\n",
                    phase.constData(),
                    ms,
                    performance::thread_ms() - cpu);
    });
    if (ready_)
      return true;
    if (upload_) {
      if (!upload_->step(packet_bytes))
        return false;
      images_ = upload_->progress();
      upload_.reset();
      return false;
    }
    if (world_upload_) {
      try {
        if (!world_upload_->step(packet_bytes))
          return false;
        world_upload_.reset();
      }
      catch (const std::exception &error) {
        world_upload_.reset();
        graph_->world_normal_error = QString::fromUtf8(error.what());
      }
      return false;
    }
    if (!installed_) {
      library_.install(pending_, hash_, graph_);
      installed_ = true;
      return false;
    }
    if (next_ < channels_.size()) {
      library_.prepare(pending_, channels_[next_++]);
      if (next_ < channels_.size())
        return false;
    }
    if (!world_prepared_) {
      library_.prepare_world_normal(pending_);
      world_prepared_ = true;
      return false;
    }
    if (graph_->generate_resource_preview) {
      if (!preview_)
        preview_ = std::make_unique<NativeMaterialResourcePreview>();
      if (!preview_->step(library_, pending_, graph_->description))
        return false;
      generated_preview_ = preview_->image();
      preview_.reset();
    }
    library_.alias(pending_, resource_);
    library_.erase(pending_);
    ready_ = true;
    return true;
  }
  const QImage &generated_preview() const
  {
    return generated_preview_;
  }
  bool ready() const
  {
    return ready_;
  }
  double maximum_step_ms() const
  {
    return maximum_ms_;
  }
  NativeMaterialImageUpload::Progress images() const
  {
    return upload_ ? upload_->progress() : world_upload_ ? world_upload_->progress() : images_;
  }
  QString status() const
  {
    if (upload_) {
      const auto p = upload_->progress();
      if (!p.total)
        return "Preparing procedural material";
      if (p.rows <= 0)
        return QString("Uploading %1/%2 — %3").arg(p.completed + 1).arg(p.total).arg(p.image);
      return QString("Uploading %1/%2 — %3 (%4/%5 rows)")
          .arg(p.completed + 1)
          .arg(p.total)
          .arg(p.image)
          .arg(p.uploaded_rows)
          .arg(p.rows);
    }
    if (world_upload_)
      return "Preparing Bump shading";
    if (preview_)
      return "Preparing native material preview";
    return ready_ ? QString("Material ready") :
                    QString("Preparing GPU channels %1/%2").arg(next_).arg(channels_.size());
  }

 private:
  NativeMaterialGpuLibrary &library_;
  std::string resource_, hash_, pending_;
  std::shared_ptr<NativeMaterialGraph> graph_;
  std::unique_ptr<NativeMaterialResourcePreview> preview_;
  QImage generated_preview_;
  std::unique_ptr<NativeMaterialImageUpload> upload_, world_upload_;
  NativeMaterialImageUpload::Progress images_;
  std::vector<int> channels_;
  std::size_t next_ = 0;
  bool installed_ = false, ready_ = false, world_prepared_ = false;
  double maximum_ms_ = 0;
};
}  // namespace bts
