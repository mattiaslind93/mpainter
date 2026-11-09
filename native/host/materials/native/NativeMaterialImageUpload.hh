// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <QString>
#include <span>
namespace blender {
struct bNodeTree;
}
namespace bts {
struct NativeMaterialGraph;
// GPU-owner only. Source images were decoded on the worker. Publish the source
// resource only after all bounded transfers finish; never show partial images.
class NativeMaterialImageUpload {
 public:
  explicit NativeMaterialImageUpload(std::shared_ptr<NativeMaterialGraph>,
                                     std::span<blender::bNodeTree *const> extra_roots = {},
                                     bool ordinary_channels = true);
  ~NativeMaterialImageUpload();
  NativeMaterialImageUpload(const NativeMaterialImageUpload &) = delete;
  NativeMaterialImageUpload &operator=(const NativeMaterialImageUpload &) = delete;
  // At most one allocation, transfer packet or mip generation per call.
  bool step(std::size_t packet_bytes = 4ULL * 1024 * 1024);
  struct Progress {
    QString image;
    std::size_t completed = 0, total = 0, allocations = 0, shared_images = 0;
    int uploaded_rows = 0, rows = 0;
    // Conservative RGBA32F packet accounting, not measured driver DMA bytes.
    std::uint64_t transfer_budget_bytes = 0;
    double maximum_step_ms = 0;
  };
  Progress progress() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
