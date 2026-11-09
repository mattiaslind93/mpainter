#pragma once
#include <QString>
#include "NativeWorldNormalSurface.hh"
#include <atomic>
#include <memory>
namespace blender {
struct Main;
struct Material;
}  // namespace blender
namespace bts {
struct NativeMaterialGraph;
struct NativeMaterialDescription;
// Source interface only: no AOV copies, image decode or source changes.
NativeMaterialDescription describe_native_material_source(blender::Material &,
                                                          const std::atomic_bool &);
// Worker-owned original source, before runtime copies, adapters and AOVs.
// Reading preserves the archive's encoded working space. Only a new detached
// variant may edit this source; existing graph/library owners are never lent.
struct DetachedNativeMaterial {
  using MainOwner = std::unique_ptr<blender::Main, void (*)(blender::Main *)>;
  MainOwner main{nullptr, nullptr};
  blender::Material *material = nullptr;
};
DetachedNativeMaterial read_native_material_source(const QString &archive,
                                                   const QString &material,
                                                   const std::atomic_bool &);
std::shared_ptr<NativeMaterialGraph> finish_native_material_source(
    DetachedNativeMaterial,
    const std::atomic_bool &,
    const NativeMaterialDescription *retained_source = nullptr,
    NativeWorldNormalSurfaceContract = NativeWorldNormalSurfaceContract::SourceAuthored);
}  // namespace bts
