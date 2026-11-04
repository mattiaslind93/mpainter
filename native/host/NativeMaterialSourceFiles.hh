#pragma once
#include "AssetCatalog.hh"
#include "NativeMaterialBundle.hh"
#include <atomic>
namespace blender {struct Main;}
namespace bts {
// Worker-only materialization of verified immutable originals, never a format
// conversion. Source bytes and external SHA references are unchanged.
QString materialize_native_material_archive(const StudioAsset &,const std::atomic_bool &);
QString materialize_native_material_image(const NativeMaterialImageReference &,const std::vector<StudioAsset> &,const std::atomic_bool &);
void bind_native_material_source_images(blender::Main &,const NativeMaterialBundle &,const std::vector<StudioAsset> &,const std::atomic_bool &);
}
