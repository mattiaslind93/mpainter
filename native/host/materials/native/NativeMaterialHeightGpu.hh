// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
namespace bts {
class BlenderHost;
class GpuLayerPreview;
// Owner-only ready candidate/transaction. Cancellation and failed publication
// retire it under the active host context. It never owns a project BFD/Main.
class NativeMaterialHeightGpu {
 public:
  ~NativeMaterialHeightGpu();
  NativeMaterialHeightGpu(const NativeMaterialHeightGpu &) = delete;
  bool ready() const noexcept
  {
    return ready_;
  }
  void commit() noexcept;  // Only inside the noexcept accepted document commit.
 private:
  friend class BlenderHost;
  friend class GpuLayerPreview;
  explicit NativeMaterialHeightGpu(BlenderHost &);
  struct Impl;
  BlenderHost *host_;
  std::unique_ptr<Impl> impl_;
  bool gpu_owned_ = false, ready_ = false;
};
}  // namespace bts
