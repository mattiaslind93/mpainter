#pragma once
#include <QJsonArray>
#include <atomic>
#include <span>
#include <memory>
namespace blender {struct bNodeTree;}
namespace bts {
struct NativeMaterialGraph;
struct NativeMaterialImageReference;
// Worker only, before any GPU program refers to the detached graph. Each source
// Image ID is decoded once; all derived channel trees keep that same ID.
QJsonArray prepare_native_material_images(NativeMaterialGraph &,const std::atomic_bool &,
 std::span<blender::bNodeTree *const> extra_roots={},bool ordinary_channels=true);
// Optional Normal-only image failure never discards independent Color/PBR.
void prepare_native_world_normal_images(const std::shared_ptr<NativeMaterialGraph> &,const std::atomic_bool &);
void prepare_native_world_normal_images(const std::shared_ptr<NativeMaterialGraph> &,const std::atomic_bool &,std::span<const NativeMaterialImageReference> archive_references);
// Fence prepared TILED pixels against the actual immutable archive closure.
void validate_native_material_tiled_sources(const NativeMaterialGraph &,std::span<const NativeMaterialImageReference>,const std::atomic_bool &);
}
