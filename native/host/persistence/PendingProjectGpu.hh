#pragma once
#include "NativeMaterialHeightProject.hh"
#include <memory>

namespace bts {
class BlenderHost;
class MaterialInspection;
class NativeMaterialHeightGpu;
struct PendingProject;
// Stable owner-only staging. Destroy/drain before the viewport/native host is
// shut down; cleanup activates its owner context before releasing any program.
// It owns no BFD: ProjectOpenResult destroys this FIRST, then PendingProject.
class PendingProjectGpu {
 public:
  ~PendingProjectGpu();
  PendingProjectGpu(const PendingProjectGpu &) = delete;
  bool ready() const
  {
    return ready_;
  }

 private:
  friend class BlenderHost;
  explicit PendingProjectGpu(BlenderHost &);
  BlenderHost *host_;
  const PendingProject *pending_ = nullptr;
  std::unique_ptr<MaterialInspection> inspection_;
  std::vector<NativeHeightProjectSource> sources_;
  std::unique_ptr<NativeMaterialHeightGpu> height_work_;
  std::size_t source_ = 0, height_ = 0, probe_ = 0;
  bool queued_ = false, gpu_owned_ = false, ready_ = false, layer_sources_ready_ = false;
};
}  // namespace bts
