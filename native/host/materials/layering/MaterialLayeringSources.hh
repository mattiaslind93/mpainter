#pragma once
#include "MaterialLayering.hh"
#include <memory>
#include <optional>
#include <span>
namespace blender {
struct Main;
struct Image;
struct ImBuf;
}  // namespace blender
namespace bts {
class GpuLayerPreview;
struct MaterialLayerImages : std::array<std::array<blender::Image *, 6>, 4> {
  // Sequence members are ordinary shared 2D resources, never an array copy.
  std::array<std::array<std::map<int, blender::Image *>, 6>, 4> members;
};
// Owned READ-ONLY CPU header with shared immutable decoded pixels; no copy/decode
// or GPU reference. Caller releases with IMB_freeImBuf. Used by the source adapter
// and its real ImBuf ownership regression.
blender::ImBuf *material_layer_source_pixel_view(const FillTexture &);
// Per-inspection adapters, not a second GPU cache. Image IDs stay Main-owned;
// immutable pixels share FillTexture storage and GPU allocations stay in the
// existing bounded source pool. All mutating methods require the GPU owner.
class MaterialLayeringSources {
 public:
  MaterialLayeringSources();
  ~MaterialLayeringSources();
  MaterialLayeringSources(const MaterialLayeringSources &) = delete;
  MaterialLayeringSources &operator=(const MaterialLayeringSources &) = delete;
  // Incremental upload, followed by metadata-only Image/ImBuf publication. No
  // caller may change graph bindings until this returns true for its model.
  bool prepare(blender::Main &, GpuLayerPreview &, std::span<const MaterialLayering *const>);
  std::optional<MaterialLayerImages> images(const MaterialLayering &) const;
  // Call after all live graphs have been rebound. Keeps old Image IDs/CPU
  // storage valid for a still-retiring Cycles loader, releases only GPU aliases.
  void retain_only(std::span<const MaterialLayering *const>);
  void reset();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
