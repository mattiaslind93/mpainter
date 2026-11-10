// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureSetResidency.hh"
#include "MaskEffectContext.hh"
#include "NativeWorldNormalObjectContext.hh"
#include "NativeMaterialBakedAoContext.hh"
#include "NativeMaterialFragmentImages.hh"
#include "SpatialOracleScope.hh"
#include "BumpPrecision.hh"
#include "PaintRecording.hh"
#include "MaterialLayerShader.hh"
#include "MaterialLayerParameterIO.hh"
#include "MaterialLayerParameterSurface.hh"
#include "MaterialLayerParameterReadback.hh"
#include "MaterialLayeringSources.hh"
#include "MaterialLayerUdim.hh"
#include "GPU_capabilities.hh"
#include "GPU_context.hh"
#include "OpenPbrShader.hh"
#include "MaterialLayeringStacks.hh"
#include "BKE_image_residency.hh"
#include "Performance.hh"
#include "UniformChannels.hh"
#include "GpuLayerPreview.hh"
#include "GpuTextureProbe.hh"
#include "GpuLayerChannel.hh"
#include "BLI_math_half.hh"
#include "GPU_texture.hh"
#include "GPU_state.hh"
#include "MEM_guardedalloc.h"
#include <set>
#include <utility>
#include "NativeCompositeJob.hh"
#include "CompositeMemory.hh"
#include "CompositeStatus.hh"
#include <future>
#include <deque>
#include <chrono>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QThreadPool>
#include "MaterialInspection.hh"
#include "TextureExtent.hh"
#include "NativeGpuImages.hh"
#include "NativeCyclesImages.hh"
#include "NativeCyclesPublication.hh"
#include "NativeMaterialParameters.hh"
#include "NativeMaterialCoat.hh"
#include "NativeMaterialHeightResource.hh"
#include "NativeMaterialProjectionParameters.hh"
#include "NativeMaterialGpuProgram.hh"
#include "GpuProjectReadback.hh"
#include "TextureMemory.hh"
#include "BLI_task.hh"
#include "NormalMix.hh"
#include "HeightNormal.hh"
#include "AmbientOcclusion.hh"
#include <bit>
#include "TexturePadding.hh"
#include "ProjectionGeometryJob.hh"
#include "NativeWorldNormalGeometryJob.hh"
#include "NativeMaterialFragmentGpuCompiler.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeWorldNormalPlan.hh"
#include "NativeWorldNormalImages.hh"
#include "NativeWorldNormalShader.hh"
#include "MarginGeometryJob.hh"
#include "SceneSnapshot.hh"
#include "TextureDocument.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "BKE_lib_id.hh"
#include "DNA_image_types.h"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "BKE_main.hh"
#include "BKE_global.hh"
#include "BKE_idprop.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DEG_depsgraph.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
namespace bts {
using namespace blender;
namespace {
#include "MaterialInspectionImages.inc"
}  // namespace
#include "MaterialInspectionImpl.inc"
#include "MaterialInspectionScene.inc"
#include "MaterialInspectionUpdate.inc"
#include "MaterialInspectionDiagnostics.inc"
#include "MaterialInspectionPixels.inc"
#include "MaterialInspectionNative.inc"
#include "MaterialInspectionSources.inc"
bool MaterialInspection::cycles_images_pending() const
{
  return impl_->cycles_entry_waiting || impl_->cycles.pending();
}
std::string_view MaterialInspection::cycles_images_error() const
{
  if (impl_->cycles_preparation_failed)
    return impl_->cycles_preparation_error.empty() ?
               std::string_view("Required Cycles GPU image preparation failed") :
               std::string_view(impl_->cycles_preparation_error);
  return impl_->cycles.error();
}

void MaterialInspection::finalize_gpu_images(Main &main, bool allow_gpu_compute)
{
  // One native image per frame; never perform a release-time loop over the set.
  if (allow_gpu_compute) {
    performance::Scope timing("gpu_mipmap_finalize_step");
    impl_->gpu_mips_pending = BKE_image_gpu_finalize_mipmap_step(&main) > 0;
  }
  else
    impl_->gpu_mips_pending = true;
}
bool MaterialInspection::poll_composites(Main &, bool wait)
{
  performance::Scope timing("poll_composites");
  impl_->collect_projection_geometry();
  if (wait && !impl_->failures.empty())
    throw std::runtime_error("GPU layer update failed: " + impl_->failures.begin()->second);
  property_pipeline_pending = updates_pending();
  return !impl_->gpu_stack_queued.empty() || !impl_->gpu_queued.empty() ||
         std::any_of(impl_->bindings.begin(), impl_->bindings.end(), [](const auto &entry) {
           const auto &b = entry.second;
           return bool(b.world_geometry_job) ||
                  (b.world_set && b.world_tile < b.world_tiles.size());
         });
}

}  // namespace bts
