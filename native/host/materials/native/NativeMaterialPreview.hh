// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ThumbnailMetadata.hh"
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <atomic>
#include <future>
#include <functional>
#include <memory>
namespace bts {
class BlenderHost;
struct NativeMaterialGraph;
struct NativeMaterialPreviewResult {
  QImage image;
  QJsonObject timings;
  ThumbnailMetadata linear;  // Small fenced ACEScg packet; never a document readback.
};
// One explicit UV Base Color request at a time. Workers own detached graphs;
// uploads, compilation, dispatch, readback and destruction stay on the GPU owner.
class NativeMaterialPreview {
 public:
  using Worker = std::function<std::shared_future<std::shared_ptr<NativeMaterialGraph>>(
      const QString &, const QString &, std::shared_ptr<std::atomic_bool>)>;
  NativeMaterialPreview(BlenderHost &, Worker);
  ~NativeMaterialPreview();
  std::shared_future<NativeMaterialPreviewResult> request(const QString &,
                                                          const QString &,
                                                          std::shared_ptr<std::atomic_bool>);
  void cancel();
  bool pending() const;
  // Host shutdown only: join a canceled detached worker before native allocators exit.
  void shutdown();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
