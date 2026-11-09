#pragma once
#include "TextureDocument.hh"
#include "TexturePadding.hh"
#include "SkewGeometry.hh"
#include <atomic>
namespace bts {
struct SkewBakeOptions {
  int resolution = 512, height = 512, sampling = 1, dilation = 32;
  bool diffusion = false;
  BakeRayOptions ray;
};
// One serial worker owns this engine. Sources are immutable snapshots of original
// meshes; neither modifiers nor live Main/GPU state enter automatic rebaking.
class SkewBakeEngine {
 public:
  SkewBakeEngine(std::shared_ptr<const SceneSnapshot> low,
                 std::shared_ptr<const SceneSnapshot> high);
  ~SkewBakeEngine();
  MeshMap bake(std::uint32_t material,
               const SkewField &,
               const std::set<int> &,
               const SkewBakeOptions &,
               const std::map<int, PaintPixels::Bounds> &,
               const std::atomic<bool> &cancel);
  std::shared_ptr<const SkewGeometry> geometry() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
