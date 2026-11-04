#pragma once
#include "MeshBaker.hh"
#include "SkewGeometry.hh"
#include "TexturePadding.hh"
namespace bts {
struct AutoBakeOptions {
  MeshBakeSettings maps;
  BakeRayOptions ray;
};
// Serial worker only. Sources are immutable original meshes, never live Main/evaluated IDs.
class AutoBakeEngine {
 public:
  AutoBakeEngine(std::shared_ptr<const SceneSnapshot> low,
                 std::shared_ptr<const SceneSnapshot> high,
                 std::shared_ptr<const SceneSnapshot> cage={});
  ~AutoBakeEngine();
  std::shared_ptr<const SkewGeometry> geometry() const;
  MeshMaps bake(std::uint32_t material,const std::string &name,const MaterialValues &base,
                const std::set<int> &tiles,const SkewField &,const AutoBakeOptions &,
                const std::atomic<bool> &cancel);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
