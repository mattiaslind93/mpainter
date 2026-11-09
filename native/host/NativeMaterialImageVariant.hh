#pragma once
#include "NativeMaterialImageVariantMetadata.hh"
#include "NativeMaterialResource.hh"
#include "NativeMaterialParameters.hh"

namespace bts {
enum class NativeImageSourceSpace { Unknown, OriginalDeclared, Named };
struct NativeImageReplacementPolicy {
  NativeImageSourceSpace source_space = NativeImageSourceSpace::Unknown;
  QString named_space;
  // The new replacement always requests native Float32 GPU storage. Original
  // images retain their own precision settings; no inherited half clipping.
};
struct PreparedNativeImageVariant {
  PreparedNativeMaterialResource prepared;
  // An updated source interface carrying this layer's existing editable values
  // and ramps. It is validated against the new layout before any adoption.
  NativeMaterialDescription instance;
  // Encoded bytes remain COW/shared. Caller must atomically retain this source
  // and its ordinary image dependencies for Reset and source-free packed Open.
  StudioAsset original;
  NativeImageVariantProvenance provenance;
};
struct NativeImageBindingStatus {
  NativeImageBinding binding;
  QJsonArray active_channels;
  QString reason;
  bool available() const
  {
    return reason.isEmpty() && !active_channels.isEmpty();
  }
};
struct NativeImageBindingAvailability {
  NativeImageBindingInventory inventory;
  std::vector<NativeImageBindingStatus> bindings;
};
// Worker-only exact-edge probes. Does not decode images, archive a variant or
// create GPU state. The UI coordinator coalesces/caches the result per source.
NativeImageBindingAvailability native_material_image_availability(const StudioAsset &,
                                                                  const std::atomic_bool &);
// Strict source-layout match; copy only authored material input state. Shared
// by replacement and Reset-to-original, including Mapping after its ABI merge.
NativeMaterialDescription retain_native_image_instance(const NativeMaterialDescription &,
                                                       const NativeMaterialDescription &);
// Worker-only; returns detached candidates, never publishes catalog/layer/GPU
// state. All encoded texture sources in the snapshot are immutable originals.
PreparedNativeImageVariant prepare_native_image_variant(const StudioAsset &base,
                                                        const std::vector<StudioAsset> &resources,
                                                        const NativeMaterialDescription &instance,
                                                        const NativeImageBinding &binding,
                                                        const StudioAsset &replacement,
                                                        const NativeImageReplacementPolicy &,
                                                        const std::atomic_bool &);
// Empty for raw/v1 sources; strict typed v2 parsing otherwise.
std::optional<NativeImageVariantProvenance> native_image_variant_provenance(const StudioAsset &);
// Catalog/packing must retain this ORIGINAL asset, whose existing external
// image references in turn preserve its full ordinary dependency closure.
std::vector<QUuid> native_image_variant_dependencies(const StudioAsset &);
}  // namespace bts
