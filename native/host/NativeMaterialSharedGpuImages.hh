#pragma once
#include <memory>
namespace blender {struct Image;struct ImageGPUTextures;namespace gpu {class Texture;}}
namespace bts {
struct NativeMaterialGraph;
struct NativeTiledStoragePlan;
enum class NativeTiledRepresentation;
// GPU owner only. The cache keeps weak owners; each Image runtime retains its
// own reference to the same allocation. No graph/Main lifetime cycles.
bool reuse_native_gpu_image(const std::shared_ptr<NativeMaterialGraph> &,blender::Image *);
void remember_native_gpu_image(const std::shared_ptr<NativeMaterialGraph> &,blender::Image *);
// Retained TILED source path. All callbacks below require the GPU owner; the
// metadata planner/state machine itself is independently CPU-testable.
bool reuse_native_tiled_gpu_image(const std::shared_ptr<NativeMaterialGraph> &,blender::Image *,const std::shared_ptr<const NativeTiledStoragePlan> &);
void publish_native_tiled_gpu_image(const std::shared_ptr<NativeMaterialGraph> &,blender::Image *,const std::shared_ptr<const NativeTiledStoragePlan> &,blender::gpu::Texture *&primary,blender::gpu::Texture *&mapping);
blender::ImageGPUTextures retained_native_tiled_gpu_image(const std::shared_ptr<NativeMaterialGraph> &,blender::Image *,NativeTiledRepresentation);
const NativeTiledStoragePlan &retained_native_tiled_gpu_plan(const std::shared_ptr<NativeMaterialGraph> &,blender::Image *,NativeTiledRepresentation);
}
