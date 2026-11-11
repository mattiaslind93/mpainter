// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureSetResidency.hh"
#include "Theme.hh"
#include "MaskEffectContext.hh"
#include "NativeMaterialGraph.hh"
#include "ImportedShaderCache.hh"
#include "NativeFragmentGeometry.hh"
#include "NativeMaterialDependencies.hh"
#include "NativeMaterialPreviewImages.hh"
#include "NativeMaterialPreview.hh"
#include "GPU_texture.hh"
#include "Performance.hh"
#include "SceneTriangleOverlay.hh"
#include "NativeMaterialGpuPrototype.hh"
#include "NativeMaterialContext.hh"
#include "NativeMaterialResource.hh"
#include "NativeMaterialDependencies.hh"
#include <QScopeGuard>
#include "UvGpuOverlay.hh"
#include <QFileInfo>
#include "BlendMaterial.hh"
#include "OpenPbrImportPolicy.hh"
#include "BKE_idprop.hh"
#include "NOD_defaults.hh"
#include <QJsonDocument>
#include "OpenPbrShader.hh"
#include "MaterialPresetReadback.hh"
#include "MaterialLayerParameterReadback.hh"
#include "MaterialLayerParameterSurface.hh"
#include "MaterialLayerParameterIO.hh"
#include "MaskPresetReadback.hh"
#include "ViewportResidency.hh"
#include "WarpProjection.hh"
#include <ctime>
#include "DisplayPostEffects.hh"
#include "SubsurfacePreviewIO.hh"
#include "QtSharedContext.hh"
#include "BlenderHost.hh"
#include "CyclesCudaTest.hh"
#include "FileBrowser.hh"
#include "TextureExtent.hh"
#include "TextureMemory.hh"
#include "DisplacementPreview.hh"
#include "PropertyLatency.hh"
#include "GpuPerformance.hh"
#include "GpuLayerPreview.hh"
#include "GPU_shader.hh"
#include "StrokeReproject.hh"
#include "ResourceIO.hh"
#include "SymmetryIO.hh"
#include "TextureDocument.hh"
#include "ProjectIO.hh"
#include "PendingProjectGpu.hh"
#include "NativeMaterialHeightGpuPrivate.hh"
#include "GpuProjectReadback.hh"
#include <future>
#include <thread>
#include "BLO_readfile.hh"
#include "SurfaceBrush.hh"
#include "NewProjectProfile.hh"
#include "BKE_world.h"
#include "DNA_world_types.h"
#include "EnvironmentResource.hh"
#include "BKE_studiolight.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <vector>

#ifdef __APPLE__
#include "MetalContextOwner.hh"
#include "MetalRhiViewport.hh"
#include <QPainter>
#endif
#include <epoxy/gl.h>
#include "QtGpuSurface.hh"
#include "GpuContextOwner.hh"
#include <QCoreApplication>
#include <QJsonArray>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QThread>
#include <QTimer>
#include <QStandardPaths>

#include "GHOST_Context.hh"
#include "BKE_context.hh"
#include "MEM_guardedalloc.h"
#include "BLI_colorspace.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_geom.h"
#include "IMB_colormanagement.hh"
#include "RNA_access.hh"
#include "opencolorio/OCIO_matrix.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BKE_layer.hh"
#include "DNA_layer_types.h"
#include "BKE_lib_id.hh"
#include "BKE_lib_remap.hh"
#include "BLI_set.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_scene.hh"
#include "BKE_wm_runtime.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "GPU_material.hh"
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_userdef_types.h"
#include "DNA_view3d_types.h"
#include "DRW_engine.hh"
#include "ED_screen.hh"
#include "ED_view3d_offscreen.hh"
#include "GPU_context.hh"
#include "GPU_framebuffer.hh"
#include "GPU_init_exit.hh"
#include "GPU_capabilities.hh"
#include "GPU_platform.hh"
#include "GPU_state.hh"
#include "GPU_viewport.hh"
#include "GPU_viewport_offscreen_cache.hh"
#include "WM_api.hh"
#include "RE_engine.h"
#include "RE_pipeline.h"
#include "gl_context.hh"

int BTS_blender_init(int argc, const char **argv, blender::bContext **r_context);

namespace blender {
bool BPY_run_string_exec(bContext *, const char *[], const char *);
void BTS_file_adopt(bContext *, BlendFileData *);
void BTS_appdir_documents_path_set(const char *);
}  // namespace blender
namespace blender {
void WM_init_gpu_hosted_shader_workers(GHOST_IContext *);
}
#include "GPU_batch.hh"
#include "GPU_vertex_buffer.hh"
#include "intern/draw_cache.hh"

namespace bts {
using namespace blender;

#include "BlenderHostImpl.inc"

BlenderHost::BlenderHost(const std::string &executable, const std::string &project)
    : impl_(std::make_unique<Impl>())
{
  auto &s = *impl_;
  std::vector<std::string> args = {executable,
                                   "--background",
                                   "--factory-startup",
                                   "--disable-autoexec",
                                   "--gpu-backend",
#ifdef __APPLE__
                                   "metal"
#else
                                   "opengl"
#endif
  };
  if (qEnvironmentVariableIntValue("BTS_HOST_THREADS") > 0) {
    args.push_back("--threads");
    args.push_back(std::to_string(std::clamp(qEnvironmentVariableIntValue("BTS_HOST_THREADS"), 1, 6)));
  }
  if (!project.empty()) {
    args.push_back(project);
  }
  std::vector<const char *> argv;
  for (const auto &arg : args) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);
  BTS_appdir_documents_path_set(
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toUtf8().constData());
  if (BTS_blender_init(int(args.size()), argv.data(), &s.context) != 0 || !s.context) {
    throw std::runtime_error("Blender initialization failed");
  }
  s.attach_context();
  s.factory_workspace_color_pending = project.empty();
  STRNCPY(s.scene->r.engine, "BLENDER_EEVEE");
  DEG_id_tag_update(&s.scene->id, ID_RECALC_SYNC_TO_EVAL);
  s.view->shading.type = OB_MATERIAL;
  if (project.empty())
    s.view->shading.flag |= V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
  s.view->flag2 |= V3D_HIDE_OVERLAYS;
  s.depsgraph = BKE_scene_ensure_depsgraph(s.main, s.scene, s.view_layer);
  BKE_scene_graph_evaluated_ensure(s.depsgraph, s.main);
  std::printf("BTS_SCENE_READY pid=%lld engine=%s native_window=none\n",
              static_cast<long long>(QCoreApplication::applicationPid()),
              s.scene->r.engine);
  std::fflush(stdout);
}
#include "BlenderHostMaterials.inc"
#include "BlenderHostProject.inc"
#include "BlenderHostState.inc"
#include "BlenderHostDraw.inc"
#include "BlenderHostScene.inc"
#include "BlenderHostTextures.inc"
void BlenderHost::shutdown()
{
  auto &s = *impl_;
  s.require_owner();
  s.program_poll_stopping = true;
  s.program_poll.stop();
  if (s.phase == Impl::Phase::Closed) {
    return;
  }
  shutdown_browser_previews();  // Native IMB/BLO workers must finish before WM_exit_ex.
  if (s.native_preview) {
    s.native_preview->shutdown();
    s.native_preview.reset();
  }
  for (auto &job : s.asset_scan_jobs)
    job.cancel->request_stop();
  for (auto &job : s.asset_scan_jobs)
    job.result.wait();
  s.asset_scan_jobs.clear();
  for (auto &job : s.material_prepare_jobs)
    job.cancel->store(true);
  for (auto &job : s.material_prepare_jobs)
    job.result.wait();
  s.material_prepare_jobs.clear();
  for (auto &job : s.material_graph_jobs)
    job.cancel->store(true);
  for (auto &job : s.material_graph_jobs)
    job.result.wait();
  s.material_graph_jobs.clear();
  for (auto &job : s.material_import_jobs)
    job.cancel->store(true);
  for (auto &job : s.material_import_jobs)
    job.result.wait();
  s.material_import_jobs.clear();
  while (save_project_pending()) {
    poll_save_project();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (s.mask_preset) {
    s.enter_gpu();
    s.mask_preset.reset();
    DRW_gpu_context_disable_ex(false);
  }
  if (s.material_preset) {
    s.enter_gpu();
    s.material_preset.reset();
    DRW_gpu_context_disable_ex(false);
  }
  if (s.parameter_export) {
    s.enter_gpu();
    s.parameter_export->cancel();
    s.parameter_export.reset();
    DRW_gpu_context_disable_ex(false);
  }
  // Source Image aliases release their GPU references before their Main dies.
  // Uninitialized/CPU-only inspections have no GPU ownership to retire.
  if (s.phase == Impl::Phase::GpuReady) {
    s.enter_gpu();
    try {
      s.inspection.reset(true);
      DRW_gpu_context_disable_ex(false);
    }
    catch (...) {
      DRW_gpu_context_disable_ex(false);
      throw;
    }
  }
  else
    s.inspection.reset(true);  // Join private CPU jobs before native globals die.
  if (s.main)
    s.displacement_preview.reset(*s.main);
  if (s.phase == Impl::Phase::GpuReady) {
    s.enter_gpu();
    GpuLayerPreview::collect_retired(
        true);  // Terminal cancellation before prepared-info/GPU teardown.
    GpuProjectReadback::free_program();
    s.performance.free();
    s.free_targets();
    GPU_finish();
    DRW_gpu_context_disable_ex(false);
  }
#ifdef __APPLE__
  s.metal_rhi.reset();
  s.metal_frame = {};
#endif
  WM_exit_ex(s.context, true, false);
  // WM_exit/GPU_exit joins compiler workers first. Their disposal never waits
  // on this GUI thread; retire the released Qt surfaces before dropping root.
  drain_shared_shader_contexts();
  s.context = nullptr;
  s.phase = Impl::Phase::Closed;
  install_shared_ghost_context(nullptr);
  s.ghost.reset();
  std::printf("BTS_SHUTDOWN clean pid=%lld\n",
              static_cast<long long>(QCoreApplication::applicationPid()));
  std::fflush(stdout);
}

std::string BlenderHost::renderer() const
{
  return impl_->renderer;
}

}  // namespace bts

std::vector<bts::StudioAsset> bts::BlenderHost::project_resources() const
{
  impl_->require_owner();
  return read_project_assets(*impl_->scene);
}
