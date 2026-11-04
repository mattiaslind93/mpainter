#include "TextureSetResidency.hh"
#include "Theme.hh"
#include "MaskEffectContext.hh"
#include "NativeMaterialGraph.hh"
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

#include <epoxy/gl.h>
#include "QtGpuSurface.hh"
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

namespace blender { bool BPY_run_string_exec(bContext *,const char *[],const char *); void BTS_file_adopt(bContext *,BlendFileData *); void BTS_appdir_documents_path_set(const char *); }
namespace blender {void WM_init_gpu_hosted_shader_workers(GHOST_IContext *);}
#include "GPU_batch.hh"
#include "GPU_vertex_buffer.hh"
#include "intern/draw_cache.hh"

namespace bts {
using namespace blender;

class QtGhostContext final : public GHOST_Context {
 public:
  explicit QtGhostContext(QtGpuSurface &surface) : GHOST_Context({}), surface_(surface) {}

  GHOST_TSuccess activateDrawingContext() override
  {
    performance::Scope timing("qt_context_activate");
    surface_.makeCurrent();
    if (QOpenGLContext::currentContext() != surface_.context()) {
      return GHOST_kFailure;
    }
    active_context_ = this;
    return GHOST_kSuccess;
  }

  GHOST_TSuccess releaseDrawingContext() override
  {
    performance::Scope timing("qt_context_release");
    active_context_ = nullptr;
    // Blender borrows this UI context; Qt remains its owner. Unbinding here
    // forces GLX/driver flush and synchronization before the immediate Qt
    // overlay. Independent worker contexts still release in QtSharedContext.
    if(qEnvironmentVariableIsSet("BTS_RELEASE_BORROWED_CONTEXT"))surface_.doneCurrent();
    return GHOST_kSuccess;
  }

  GHOST_TSuccess initializeDrawingContext() override { return activateDrawingContext(); }
  GHOST_TSuccess releaseNativeHandles() override { return GHOST_kSuccess; }
  GHOST_TSuccess swapBufferAcquire() override { return GHOST_kSuccess; }
  GHOST_TSuccess swapBufferRelease() override { return GHOST_kSuccess; }
  unsigned int getDefaultFramebuffer() override { return surface_.defaultFramebufferObject(); }

 private:
  QtGpuSurface &surface_;
};

struct BlenderHost::Impl {
  struct RenderTarget {
    GPUOffScreen *offscreen = nullptr;
    GPUViewport *viewport = nullptr;
    std::uint64_t logged_allocations=0;
    std::uint64_t generation=0;

    void free()
    {
      logged_allocations=0;
      if (viewport) {
        GPU_viewport_free(viewport);
        viewport = nullptr;
      }
      if (offscreen) {
        GPU_offscreen_free(offscreen);
        offscreen = nullptr;
      }
    }

    void ensure_size(int width, int height)
    {
      if (offscreen && GPU_offscreen_width(offscreen) == width &&
          GPU_offscreen_height(offscreen) == height)
      {
        return;
      }
      // A normal viewport resize retains its engine/shader cache. Only the
      // caller-owned presentation target changes; Blender resizes its buffers.
      if(offscreen){GPU_offscreen_free(offscreen);offscreen=nullptr;}
      char error[256] = {};
      offscreen = GPU_offscreen_create(width, height, true, gpu::TextureFormat::UNORM_8_8_8_8,
                                       GPU_TEXTURE_USAGE_SHADER_READ, true, error);
      if (!offscreen) {
        throw std::runtime_error(std::string("EEVEE target allocation failed: ") + error);
      }
      if(!viewport){viewport=GPU_viewport_create();GPU_viewport_offscreen_cache_set(viewport,true);}
      ++generation;
    }
  };

  enum class Phase { SceneReady, GpuReady, Closed };
  Phase phase = Phase::SceneReady;
  bool factory_workspace_color_pending = false;
  QThread *owner = QThread::currentThread();
  bContext *context = nullptr;
  Main *main = nullptr;
  struct MaterialImportJob{std::shared_ptr<std::atomic_bool> cancel;std::shared_future<PreparedBlendMaterial> result;};
  std::vector<MaterialImportJob> material_import_jobs;
  struct MaterialGraphJob{std::shared_ptr<std::atomic_bool> cancel;std::shared_future<NativeMaterialDescription> result;};
  std::vector<MaterialGraphJob> material_graph_jobs;
  struct MaterialPrepareJob{std::shared_ptr<std::atomic_bool> cancel;std::shared_future<std::shared_ptr<NativeMaterialGraph>> result;};
  std::vector<MaterialPrepareJob> material_prepare_jobs;
  std::unique_ptr<NativeMaterialPreview> native_preview;
  struct AssetScanJob{std::shared_ptr<std::stop_source> cancel;std::shared_future<BlendAssetLibrary> result;};
  std::vector<AssetScanJob> asset_scan_jobs;
  std::optional<std::pair<std::array<float,16>,std::array<float,16>>> native_view_matrices;
  Scene *scene = nullptr;
  ViewLayer *view_layer = nullptr;
  View3D *view = nullptr;
  ARegion *region = nullptr;
  Depsgraph *depsgraph = nullptr;
  std::unique_ptr<MaskPresetReadback> mask_preset;
  std::unique_ptr<MaterialPresetReadback> material_preset;
  std::unique_ptr<MaterialParameterExport> parameter_export;
  std::unique_ptr<ProjectSaveSnapshot> save_snapshot;
  std::unique_ptr<GpuProjectReadback> save_readback;
  std::future<ProjectFileIdentity> save_writer;
  std::optional<ProjectFileIdentity> save_expected;
  ProjectFileIdentity saved_identity;
  std::string save_path;
  std::function<void()> save_metadata;
  QJsonObject last_tangent_shader_state;
  LensProjection lens;
  std::unique_ptr<QtGpuSurface> surface;
  std::unique_ptr<QtGhostContext> ghost;
  RenderTarget scene_target;
  RenderTarget uv_target;
  EeveeCompletionSnapshot last_eevee_completion;
  const bool surface_graph_test=qEnvironmentVariableIsSet("BTS_PD149_SURFACE_GRAPH_TEST");
  const bool surface_cold_control=qEnvironmentVariable("BTS_PD149_SURFACE_COLD_CONTROL")==QStringLiteral("1");
  SurfaceGraphSnapshot last_surface_graph;
  std::uint64_t host_draw_serial=0;
  UvPreview uv_preview;
  UvGpuOverlay uv_overlay;
  SceneTriangleOverlay triangle_overlay;
  SceneSnapshot triangle_scene;
  std::map<std::uint32_t,std::pair<const gpu::VertBuf *,std::array<float,16>>> triangle_geometry;
  std::map<std::uint32_t,const gpu::VertBuf *> wire_vbos;std::uint64_t wire_cache_changes=0;
  UvPreviewSummary uv_summary;
  std::optional<std::uint32_t> selected_material;
  bool uv_dirty = true,uv_prime=true,texture_dirty=false,texture_interacting=false;
  MaterialInspection inspection;
  DisplacementPreview displacement_preview;
  std::shared_ptr<SurfaceBrushCache> surface_cache=make_surface_brush_cache();
  SceneSnapshot source_snapshot(){return read_scene_snapshot(context,view,false,&displacement_preview.source_meshes());}
  InspectionMode inspection_mode = InspectionMode::Material;
  bool warp_bounds_ready=false;WarpVector warp_center{};double warp_extent=1;std::uint64_t warp_generation=1;
  std::vector<std::shared_ptr<const WarpGeometryCache>> warp_caches;
  ViewportResidency residency;
  void reset_warp_geometry(){reset_surface_brush_cache(*surface_cache);residency.reset();warp_bounds_ready=false;warp_caches.clear();++warp_generation;}
  bool drawing = false;
  std::string renderer;
  GpuPerformance performance;

  // Owns its receiver context; destruction disconnects this capture before any
  // inspection/native state dies. Only submitted programs are polled here.
  QTimer program_poll;
  bool program_poll_failed=false,program_poll_stopping=false;
  Impl(){
    program_poll.setSingleShot(true);
    program_poll.setInterval(16);
    QObject::connect(&program_poll,&QTimer::timeout,&program_poll,[this]{
      performance::Scope callback_timing("gpu_stack_pair_poll_callback");
      if(program_poll_stopping||phase!=Phase::GpuReady)return;
      if(drawing){arm_program_poll();return;}
      try{
        // This is a CPU-only compiler query. Do not restore GL state while
        // any shared compiler worker is active; unrelated jobs delay this poll.
        if(GPU_shader_compiler_has_pending_work()){arm_program_poll();return;}
        enter_gpu();
        const auto release=qScopeGuard([]{performance::Scope release_timing("gpu_stack_pair_poll_release");DRW_gpu_context_disable_ex(false);});
        performance::Scope timing("gpu_stack_pair_poll");
        inspection.poll_gpu_programs();
      }catch(const std::exception &error){
        program_poll_failed=true;
        std::fprintf(stderr,"BTS_GPU_STACK_POLL_FAILED %s\n",error.what());
      }catch(...){
        program_poll_failed=true;
        std::fprintf(stderr,"BTS_GPU_STACK_POLL_FAILED unknown exception\n");
      }
      arm_program_poll();
    });
  }
  ~Impl(){program_poll.stop();QObject::disconnect(&program_poll,nullptr,&program_poll,nullptr);}
  void arm_program_poll(){
    if(!program_poll_stopping&&!program_poll_failed&&phase==Phase::GpuReady&&inspection.gpu_programs_pending()){
      if(!program_poll.isActive())program_poll.start();
    }else program_poll.stop();
  }

  void attach_context()
  {
    auto &s=*this;s.reset_warp_geometry();s.view=nullptr;s.region=nullptr;
  auto *wm = CTX_wm_manager(s.context);
  if (!wm || wm->windows.is_empty()) {
    throw std::runtime_error("Blender startup has no native scene context");
  }
  for (wmWindow &window : wm->windows) {
    if (window.runtime->ghostwin) {
      throw std::runtime_error("Hosted startup unexpectedly created a Blender system window");
    }
  }
  CTX_wm_window_set(s.context, static_cast<wmWindow *>(wm->windows.first));
  auto *screen = CTX_wm_screen(s.context);
  for (ScrArea &area : screen->areabase) {
    if (area.spacetype != SPACE_VIEW3D) {
      continue;
    }
    ED_area_and_region_types_init(&area);
    for (ARegion &region : area.regionbase) {
      if (region.regiontype == RGN_TYPE_WINDOW && region.regiondata) {
        CTX_wm_area_set(s.context, &area);
        CTX_wm_region_set(s.context, &region);
        s.region = &region;
        s.view = CTX_wm_view3d(s.context);
        break;
      }
    }
    if (s.view) {
      break;
    }
  }
  if (!s.view || !s.region) {
    throw std::runtime_error("Blender startup has no usable View3D region");
  }
  // The environment supplies illumination/reflections only, including reopened projects.
  s.view->shading.studiolight_background = 0;
  const QColor backdrop(bts::theme::Viewport), edge(bts::theme::ViewportEdge);
  for (bTheme &theme : U.themes) {
    theme.space_view3d.background_type = TH_BACKGROUND_GRADIENT_RADIAL;
    const unsigned char color[4] = {static_cast<unsigned char>(backdrop.red()),
      static_cast<unsigned char>(backdrop.green()), static_cast<unsigned char>(backdrop.blue()), 255};
    std::copy_n(color, 4, theme.space_view3d.back);
    const unsigned char edge_color[4] = {static_cast<unsigned char>(edge.red()),
      static_cast<unsigned char>(edge.green()), static_cast<unsigned char>(edge.blue()), 255};
    std::copy_n(edge_color, 4, theme.space_view3d.back_grad);
  }
  s.main = CTX_data_main(s.context);
  s.scene = CTX_data_scene(s.context);
  NewProjectProfile::restore_environment_orientation(*s.scene,*s.view);
  s.lens=display_post_settings(*s.scene).projection();
  s.view_layer = CTX_data_view_layer(s.context);
  }

  bool preview_force_cpu=false;
  bool preview_cpu_fallback=false;
  QString preview_device="CPU", preview_device_message;
  void sync_preview_background(){
    // Cycles' Studio Light shader uses World.color for camera rays when HDR
    // opacity is zero. Convert the UI gray back through the active view so it
    // stays neutral; illumination and glossy rays still sample the Studio HDR.
    if(!scene->world)scene->world=BKE_world_add(main,"mPainter Environment");
    const QColor gray(bts::theme::Viewport);
    float rgb[3]={float(gray.redF()),float(gray.greenF()),float(gray.blueF())};
    auto inverse=ColormanageProcessor::display_processor_new(
        &scene->view_settings,&scene->display_settings,DISPLAY_SPACE_DRAW,true);
    inverse.apply_v3(rgb);
    scene->world->horr=rgb[0];scene->world->horg=rgb[1];scene->world->horb=rgb[2];
    DEG_id_tag_update_ex(main,&scene->world->id,ID_RECALC_SHADING|ID_RECALC_SYNC_TO_EVAL);
  }
  void configure_preview(bool enabled,bool force_cpu=false,bool probe=false){
    auto &s=*this;s.free_targets();
    if(enabled){
      s.sync_preview_background();
      if(!RE_engines_find("CYCLES"))throw std::runtime_error("Cycles is not registered");
      if(!BPY_run_string_exec(s.context,nullptr,(std::string("force_cpu = ")+(force_cpu?"True":"False")+"\nprobe_defaults = "+(probe?"True":"False")+"\n"+R"PY(import bpy, os, cycles, _cycles
# Blender starts without its own window; Qt supplies a real graphical viewport.
_cycles.init(os.path.dirname(cycles.__file__), os.path.dirname(os.path.abspath(bpy.utils.user_resource('CONFIG', path=''))), False)
p = bpy.context.preferences.addons['cycles'].preferences
devices = []
backend = 'CPU'
device_errors = []
explicit_cpu = force_cpu or os.environ.get('BTS_CYCLES_DEVICE') == 'CPU'
if not explicit_cpu:
    for candidate in ('OPTIX', 'CUDA'):
        try:
            p.compute_device_type = candidate
            devices = [d for d in p.get_devices_for_type(candidate) if d.type == candidate]
            if devices:
                backend = candidate
                break
        except Exception as exc:
            device_errors.append(candidate + ': ' + str(exc))
for d in p.devices:
    d.use = d in devices
s = bpy.context.scene.cycles
s.device = 'GPU' if devices else 'CPU'
bpy.context.scene['_bts_cycles_backend'] = backend
bpy.context.scene['_bts_cycles_device_message'] = (
    ('CPU selected explicitly.' if explicit_cpu
     else 'GPU unavailable; using CPU. ' + ('; '.join(device_errors) or 'No compatible GPU was detected.'))
    if not devices else backend + ': ' + ', '.join(d.name for d in devices))
if probe_defaults:
    s.preview_samples = 32
    s.use_preview_denoising = False
elif not s.is_property_set('preview_samples'):
    s.preview_samples = 128
print('BTS_CYCLES_DEVICE', s.device, [d.name for d in devices], flush=True)
print('BTS_CYCLES_BACKEND', backend, flush=True)
print('BTS_CYCLES_INPUT', bpy.context.scene.name, [(o.name, o.type, o.hide_render) for o in bpy.context.scene.objects], s.film_exposure, s.max_bounces, flush=True)
)PY").c_str()))throw std::runtime_error("Cycles preview initialization failed");
      STRNCPY(s.scene->r.engine,"CYCLES");s.view->shading.flag&=~(V3D_SHADING_SCENE_WORLD_RENDER|V3D_SHADING_SCENE_LIGHTS_RENDER);DEG_id_tag_update_ex(s.main,&s.scene->id,ID_RECALC_SYNC_TO_EVAL);DEG_relations_tag_update(s.main);std::fprintf(stderr,"BTS_CYCLES_MODE same_pid=%lld\n",static_cast<long long>(QCoreApplication::applicationPid()));
      s.view->shading.type=OB_RENDER;s.preview_force_cpu=force_cpu;
      auto root=RNA_id_pointer_create(&s.scene->id);auto settings=RNA_pointer_get(&root,"cycles");s.preview_device=RNA_enum_get(&settings,"device")==0?"CPU":"CUDA";s.preview_cpu_fallback=s.preview_device=="CPU"&&!force_cpu&&qEnvironmentVariable("BTS_CYCLES_DEVICE")!="CPU";
      const auto *backend=IDP_GetPropertyFromGroup_null(s.scene->id.properties,"_bts_cycles_backend");
      if(backend&&backend->type==IDP_STRING)s.preview_device=QString::fromUtf8(IDP_string_get(backend));
      const auto *message=IDP_GetPropertyFromGroup_null(s.scene->id.properties,"_bts_cycles_device_message");
      s.preview_device_message=message&&message->type==IDP_STRING?QString::fromUtf8(IDP_string_get(message)):QString{};
    }else{STRNCPY(s.scene->r.engine,"BLENDER_EEVEE");s.view->shading.type=OB_MATERIAL;DEG_id_tag_update_ex(s.main,&s.scene->id,ID_RECALC_SYNC_TO_EVAL);DEG_relations_tag_update(s.main);}
  }

  void require_owner() const
  {
    if (QThread::currentThread() != owner) {
      throw std::logic_error("Blender state accessed outside its owner thread");
    }
  }

  void tag_preview(){if(scene&&STREQ(scene->r.engine,"CYCLES"))DEG_id_tag_update_ex(main,&scene->id,ID_RECALC_SYNC_TO_EVAL);}
  void enter_gpu()
  {
    {performance::Scope timing("gpu_owner_context_enable");DRW_gpu_context_enable_ex(false);}
    {performance::Scope timing("gpu_owner_external_state_restore");gpu::GLContext::get()->restore_external_state();}
    {performance::Scope timing("gpu_owner_framebuffer_restore");GPU_framebuffer_restore();}
  }

  void free_targets()
  {
    last_eevee_completion={};
    if(surface_graph_test)last_surface_graph={};
    if(view&&region&&region->regiondata){auto *rv=static_cast<RegionView3D *>(region->regiondata);if(rv->view_render){RE_FreeViewRender(rv->view_render);rv->view_render=nullptr;}}
    triangle_overlay.free();triangle_scene={};triangle_geometry.clear();
    scene_target.free();
    uv_target.free();
    uv_overlay.free();wire_vbos.clear();
    uv_preview.clear();
    uv_summary = {};
    uv_dirty = true;uv_prime=true;
  }
};

BlenderHost::BlenderHost(const std::string &executable, const std::string &project)
    : impl_(std::make_unique<Impl>())
{
  auto &s = *impl_;
  std::vector<std::string> args = {
      executable, "--background", "--factory-startup", "--disable-autoexec", "--gpu-backend", "opengl"};
  if (!project.empty()) {
    args.push_back(project);
  }
  std::vector<const char *> argv;
  for (const auto &arg : args) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);
  BTS_appdir_documents_path_set(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toUtf8().constData());
  if (BTS_blender_init(int(args.size()), argv.data(), &s.context) != 0 || !s.context) {
    throw std::runtime_error("Blender initialization failed");
  }
  s.attach_context();
  s.factory_workspace_color_pending = project.empty();
  STRNCPY(s.scene->r.engine, "BLENDER_EEVEE");
  DEG_id_tag_update(&s.scene->id, ID_RECALC_SYNC_TO_EVAL);
  s.view->shading.type = OB_MATERIAL;
  if(project.empty())s.view->shading.flag|=V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
  s.view->flag2 |= V3D_HIDE_OVERLAYS;
  s.depsgraph = BKE_scene_ensure_depsgraph(s.main, s.scene, s.view_layer);
  BKE_scene_graph_evaluated_ensure(s.depsgraph, s.main);
  std::printf("BTS_SCENE_READY pid=%lld engine=%s native_window=none\n",
              static_cast<long long>(QCoreApplication::applicationPid()), s.scene->r.engine);
  std::fflush(stdout);
}

void BlenderHost::initialize_empty_workspace_working_space(){
 auto &s=*impl_;s.require_owner();
 if(s.phase!=Impl::Phase::SceneReady||!s.factory_workspace_color_pending||!s.main||s.main->filepath[0]||s.surface||s.ghost||s.drawing||
    !s.material_import_jobs.empty()||!s.material_graph_jobs.empty()||!s.material_prepare_jobs.empty()||!s.asset_scan_jobs.empty()||s.native_preview||
    s.save_snapshot||s.save_readback||s.save_writer.valid()||s.material_preset||s.mask_preset||s.parameter_export)
   throw std::logic_error("Empty workspace color initialization requires an unused factory startup before GPU or worker activity");
 NewProjectProfile::initialize_working_space(*s.main);
 s.factory_workspace_color_pending=false;
}

void BlenderHost::retire_layer_channel(const TextureDocument &document,const TextureSet &set,const TextureLayer &previous,int channel){auto &s=*impl_;s.require_owner();s.enter_gpu();try{s.inspection.retire_layer_channel(document,set,previous,channel);DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}}
std::string BlenderHost::rename_material(std::uint32_t uid,const std::string &name){auto &s=*impl_;s.require_owner();for(Material &material:s.main->materials)if(material.id.session_uid==uid){if(name.empty()||name.find('\0')!=std::string::npos)throw std::invalid_argument("Material name must be nonempty text without NUL");if(name.size()>sizeof(material.id.name)-3)throw std::length_error("Material name exceeds Blender ID name storage ("+std::to_string(sizeof(material.id.name)-3)+" UTF-8 bytes)");BKE_id_rename(*s.main,material.id,name);return material.id.name+2;}throw std::invalid_argument("Material no longer exists");}
std::shared_future<BlendAssetLibrary> BlenderHost::inspect_asset_library(const QString &root,std::shared_ptr<std::stop_source> cancel,std::function<void(const QString &)> progress){
 auto &s=*impl_;s.require_owner();if(!cancel)throw std::invalid_argument("Missing Asset Library cancellation");std::erase_if(s.asset_scan_jobs,[](const auto &job){return job.result.wait_for(std::chrono::seconds(0))==std::future_status::ready;});auto result=std::async(std::launch::async,[root,cancel,progress=std::move(progress)]{return inspect_blend_asset_library(root,cancel->get_token(),progress);}).share();s.asset_scan_jobs.push_back({std::move(cancel),result});return result;
}
void BlenderHost::publish_native_material_preview(const QUuid &resource,const QString &hash){auto &s=*impl_;s.require_owner();s.inspection.publish_native_material_preview(resource.toString(QUuid::WithoutBraces).toStdString(),hash.toStdString());}
void BlenderHost::prepare_native_material_gpu(const QUuid &resource,const QString &content,std::shared_ptr<NativeMaterialGraph> graph){
 auto &s=*impl_;s.require_owner();if(resource.isNull()||!graph)throw std::invalid_argument("Native material project resource is invalid");s.enter_gpu();try{s.inspection.prepare_native_material(resource.toString(QUuid::WithoutBraces).toStdString(),content.toStdString(),std::move(graph));s.inspection.set_native_fragment_geometry_provider([&s](NativeFragmentGeometryRequest request,const std::atomic_bool &cancel){return capture_native_fragment_geometry(s.context,s.view,s.displacement_preview.source_meshes(),std::move(request),cancel);});s.inspection.set_native_fragment_object_provider([&s](std::uint32_t uid){NativeMaterialGpuContext context;bind_native_material_objects(*s.main,s.depsgraph,uid,context,false);return context.objects;});s.inspection.set_native_context_provider([&s](const TextureSet &set,const TextureLayer &,int,const NativeMaterialDependencies &dependencies,NativeMaterialGpuContext &context){if(dependencies.object)bind_native_material_objects(*s.main,s.depsgraph,set.material_session_uid,context);if(dependencies.view){if(!s.native_view_matrices)throw std::runtime_error("Native material view is not ready");bind_native_material_view(s.native_view_matrices->first,s.native_view_matrices->second,context);}});DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}
}
void BlenderHost::cancel_native_material_preparation(const QUuid &resource){auto &s=*impl_;s.require_owner();s.enter_gpu();try{s.inspection.cancel_native_preparation(resource.toString(QUuid::WithoutBraces).toStdString());DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}}
void BlenderHost::discard_native_material_candidate(const QUuid &resource){auto &s=*impl_;s.require_owner();if(resource.isNull())throw std::invalid_argument("Native candidate UUID is missing");s.enter_gpu();try{s.inspection.discard_native_candidate(resource.toString(QUuid::WithoutBraces).toStdString());DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}}
void BlenderHost::alias_native_material(const QUuid &source,const QUuid &destination){auto &s=*impl_;s.require_owner();if(source.isNull()||destination.isNull())throw std::invalid_argument("Native material alias identity is invalid");s.enter_gpu();try{s.inspection.alias_native_material(source.toString(QUuid::WithoutBraces).toStdString(),destination.toString(QUuid::WithoutBraces).toStdString());DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}}
bool BlenderHost::has_native_material(const QUuid &resource)const{impl_->require_owner();return impl_->inspection.has_native_material(resource.toString(QUuid::WithoutBraces).toStdString());}
void BlenderHost::validate_material_layering(const TextureSet &set)const{
 auto &s=*impl_;s.require_owner();
 if(!s.main||s.main!=G_MAIN)throw std::runtime_error("Material layering requires the live owner project");
 s.inspection.validate_material_layering(set);
}
void BlenderHost::validate_material_parameter_import(const TextureSet &set)const{
 auto &s=*impl_;s.require_owner();
 if(!s.main||s.main!=G_MAIN)throw std::runtime_error("Parameter Import requires the live owner project");
 if(set.material_layering.allocated())throw std::invalid_argument("Import parameters into a material without allocated four-material stacks");
 require_material_parameter_main(set);s.inspection.validate_material_layering(set);
 for(const Material &material:s.main->materials)if(material.id.session_uid==set.material_session_uid){require_material_parameter_surface(*s.main,material);return;}
 throw std::runtime_error("Parameter Import material no longer belongs to the live project");
}
void BlenderHost::validate_native_mapping_edit(const TextureSet &before,const TextureSet &after,NativeMappingEditOrigin origin)const{impl_->require_owner();impl_->inspection.validate_native_mapping_edit(before,after,origin);}
void BlenderHost::validate_native_material_target(const QUuid &resource,const TextureSet &set)const{
 auto &s=*impl_;s.require_owner();if(!has_native_material(resource))throw std::invalid_argument("Native material is still preparing");const auto key=resource.toString(QUuid::WithoutBraces).toStdString();
 bool matched=false,object=false,ao=false;
 for(const auto &layer:evaluation_layers(set))if(layer.fill.native_material&&layer.fill.material_resource==key){const auto requirements=s.inspection.native_requirements(key,*layer.fill.native_material);matched=true;object|=requirements.object;ao|=requirements.ao;}
 if(!matched){object=s.inspection.native_material_uses_object(key);ao=s.inspection.native_material_uses_ao(key);}
 if(object){NativeMaterialGpuContext context;bind_native_material_objects(*s.main,s.depsgraph,set.material_session_uid,context);}
 if(ao)for(int tile:set.tiles)if(!set.mesh_maps[3]||!set.mesh_maps[3]->tiles.contains(tile)||!set.mesh_maps[3]->tiles.at(tile).pixels)throw std::invalid_argument("Native material requires a baked AO map for every UV tile — Bake Mesh Maps…");
}

NativeMaterialPreparationStatus BlenderHost::native_material_status(const QUuid &resource)const{impl_->require_owner();return impl_->inspection.native_preparation_status(resource.toString(QUuid::WithoutBraces).toStdString());}
void BlenderHost::install_native_material(const QUuid &resource,const QString &content,std::shared_ptr<NativeMaterialGraph> graph){
 auto &s=*impl_;s.require_owner();if(resource.isNull()||!graph)throw std::invalid_argument("Native material project resource is invalid");s.enter_gpu();try{s.inspection.install_native_material(resource.toString(QUuid::WithoutBraces).toStdString(),content.toStdString(),std::move(graph));s.inspection.set_native_fragment_geometry_provider([&s](NativeFragmentGeometryRequest request,const std::atomic_bool &cancel){return capture_native_fragment_geometry(s.context,s.view,s.displacement_preview.source_meshes(),std::move(request),cancel);});s.inspection.set_native_fragment_object_provider([&s](std::uint32_t uid){NativeMaterialGpuContext context;bind_native_material_objects(*s.main,s.depsgraph,uid,context,false);return context.objects;});s.inspection.set_native_context_provider([&s](const TextureSet &set,const TextureLayer &,int,const NativeMaterialDependencies &dependencies,NativeMaterialGpuContext &context){if(dependencies.object)bind_native_material_objects(*s.main,s.depsgraph,set.material_session_uid,context);if(dependencies.view){if(!s.native_view_matrices)throw std::runtime_error("Native material view is not ready");bind_native_material_view(s.native_view_matrices->first,s.native_view_matrices->second,context);}});DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}
}
std::shared_future<NativeMaterialPreviewResult> BlenderHost::preview_native_material(const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancel){
 auto &s=*impl_;s.require_owner();if(s.phase!=Impl::Phase::GpuReady)throw std::logic_error("Native preview requires an initialized GPU viewport");
 if(!s.native_preview)s.native_preview=std::make_unique<NativeMaterialPreview>(*this,[&s](const QString &file,const QString &material,std::shared_ptr<std::atomic_bool> cancellation){const auto colorspace=s.main->colorspace;return std::async(std::launch::async,[file,material,colorspace,cancellation]{std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);reference->colorspace=colorspace;auto graph=prepare_native_material_graph(*reference,file,material,*cancellation);
   const auto channel=std::find_if(graph->channels.begin(),graph->channels.end(),[](const auto &c){return c.channel==0;});
   if(channel==graph->channels.end()||!channel->tree)throw std::runtime_error("Native Base Color channel is unavailable");
   const auto dependencies=native_material_dependencies(*channel->tree,graph->description.parameters.size());
   if(dependencies.surface||dependencies.object||dependencies.view||dependencies.ao)throw std::runtime_error("Base Color preview unavailable: source requires object, surface, view or baked AO context");
   // This private adapter carries only Base Color. Both decoder and uploader
   // traverse its AOV roots, so unrelated channels never force image loading.
   // Preserve the encoded material and its authored capability metadata.
   std::erase_if(graph->channels,[](const auto &c){return c.channel!=0;});
   prepare_native_material_preview_images(*graph,*cancellation);return graph;}).share();});
 return s.native_preview->request(path,name,std::move(cancel));
}
void BlenderHost::cancel_native_material_preview(){auto &s=*impl_;s.require_owner();if(s.native_preview)s.native_preview->cancel();}
bool BlenderHost::native_material_preview_pending()const{impl_->require_owner();return impl_->native_preview&&impl_->native_preview->pending();}
std::shared_future<std::shared_ptr<NativeMaterialGraph>> BlenderHost::prepare_material_resource(const StudioAsset &asset,std::shared_ptr<std::atomic_bool> cancel,std::vector<StudioAsset> textures){
 auto &s=*impl_;s.require_owner();if(!cancel)throw std::invalid_argument("Missing native source hydration cancellation");std::erase_if(s.material_prepare_jobs,[](const auto &job){return job.result.wait_for(std::chrono::seconds(0))==std::future_status::ready;});const auto colorspace=s.main->colorspace;
 auto result=std::async(std::launch::async,[asset,colorspace,cancel,textures=std::move(textures)]{std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);reference->colorspace=colorspace;return hydrate_native_material_resource(*reference,asset,*cancel,textures);}).share();s.material_prepare_jobs.push_back({std::move(cancel),result});return result;
}
std::shared_future<std::shared_ptr<NativeMaterialGraph>> BlenderHost::prepare_material_graph(const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancel){
 auto &s=*impl_;s.require_owner();if(!cancel)throw std::invalid_argument("Missing material preparation cancellation");std::erase_if(s.material_prepare_jobs,[](const auto &job){return job.result.wait_for(std::chrono::seconds(0))==std::future_status::ready;});const auto colorspace=s.main->colorspace;
 auto result=std::async(std::launch::async,[path,name,colorspace,cancel]{std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);reference->colorspace=colorspace;return prepare_native_material_graph(*reference,path,name,*cancel);}).share();s.material_prepare_jobs.push_back({std::move(cancel),result});return result;
}
std::shared_future<NativeMaterialDescription> BlenderHost::inspect_material_graph(const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancellation){
 auto &s=*impl_;s.require_owner();if(!cancellation)throw std::invalid_argument("Missing material inspection cancellation");
 std::erase_if(s.material_graph_jobs,[](const auto &job){return job.result.wait_for(std::chrono::seconds(0))==std::future_status::ready;});const auto colorspace=s.main->colorspace;
 auto result=std::async(std::launch::async,[path,name,colorspace,cancellation]{std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);reference->colorspace=colorspace;return inspect_blend_material_graph(*reference,path,name,*cancellation);}).share();s.material_graph_jobs.push_back({std::move(cancellation),result});return result;
}
std::shared_future<PreparedBlendMaterial> BlenderHost::import_native_material(const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancel){
 auto &s=*impl_;s.require_owner();if(!cancel)throw std::invalid_argument("Missing native material import cancellation");std::erase_if(s.material_import_jobs,[](const auto &job){return job.result.wait_for(std::chrono::seconds(0))==std::future_status::ready;});const auto colorspace=s.main->colorspace;
 auto result=std::async(std::launch::async,[path,name,colorspace,cancel]{std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);reference->colorspace=colorspace;auto prepared=prepare_native_material_resource(*reference,path,name,*cancel);PreparedBlendMaterial result;result.material=std::move(prepared.asset);result.native_graph=std::move(prepared.graph);result.textures=std::move(prepared.textures);return result;}).share();s.material_import_jobs.push_back({std::move(cancel),result});return result;
}
std::shared_future<PreparedBlendMaterial> BlenderHost::import_blend_material(const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancellation,bool preview_only){
 auto &s=*impl_;s.require_owner();if(!cancellation)throw std::invalid_argument("Missing material-import cancellation");
 std::erase_if(s.material_import_jobs,[](const auto &job){return job.result.wait_for(std::chrono::seconds(0))==std::future_status::ready;});
 const auto colorspace=s.main->colorspace;
 auto result=std::async(std::launch::async,[path,name,colorspace,cancellation,preview_only]{std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);reference->colorspace=colorspace;return bts::import_blend_material(*reference,path,name,*cancellation,preview_only);}).share();
 s.material_import_jobs.push_back({std::move(cancellation),result});return result;
}
MeshReplacement BlenderHost::prepare_reimport(const TextureDocument &document,const std::string &path,bool preserve_world,std::optional<bool> import_cameras,std::optional<AutoUnwrapOptions> auto_unwrap,std::optional<UsdImportOptions> usd,std::optional<UnitSettings> units,std::optional<BlendMeshSelection> blend){auto &s=*impl_;s.require_owner();if(s.phase!=Impl::Phase::GpuReady||s.drawing)throw std::logic_error("Reimport requires an idle viewport");auto result=prepare_mesh_replacement(*s.main,document,path,import_cameras.value_or(document.settings().import_cameras),auto_unwrap.value_or(document.settings().auto_unwrap),usd.value_or(document.settings().usd),units.value_or(document.settings().units),blend.value_or(document.settings().blend));reproject_spatial_strokes(*result.authored,s.source_snapshot(),result.source,result.materials,preserve_world);return result;}
void BlenderHost::rebind_texture_sets(const TextureDocument &document){document.resolve_instances();auto &s=*impl_;s.require_owner();s.reset_warp_geometry();const auto geometry=scene_snapshot();for(const auto &set:document.texture_sets())if(!set.disabled)s.inspection.set_padding_geometry(*s.main,set.material_session_uid,geometry);for(const auto &set:document.texture_sets())apply_texture_set(set,int(document.settings().resolution));s.uv_dirty=true;}
void BlenderHost::commit_reimport(TextureDocument &document,MeshReplacement &replacement){
  auto &s=*impl_;s.require_owner();s.reset_warp_geometry();if(s.phase!=Impl::Phase::GpuReady||s.drawing)throw std::logic_error("Reimport requires an idle viewport");
  auto candidate=replacement.authored?std::move(*replacement.authored):document;candidate.replace_mesh_source(replacement.summary.source_path);s.enter_gpu();try{s.free_targets();s.displacement_preview.reset(*s.main);adopt_mesh_replacement(*s.main,*s.scene,replacement);DEG_relations_tag_update(s.main);BKE_view_layer_synced_ensure(*s.main,s.scene,s.view_layer);BKE_scene_set_background(s.main,s.scene);s.depsgraph=BKE_scene_ensure_depsgraph(s.main,s.scene,s.view_layer);DEG_make_active(s.depsgraph);BKE_scene_graph_evaluated_ensure(s.depsgraph,s.main);const auto geometry=s.source_snapshot();for(const auto &description:candidate.texture_sets()){auto &set=candidate.set(description.material_session_uid);if(set.disabled)continue;s.inspection.set_padding_geometry(*s.main,set.material_session_uid,geometry);}document=std::move(candidate);s.uv_dirty=true;DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}
  for(const auto &set:document.texture_sets())apply_texture_set(set,int(document.settings().resolution));
  std::printf("BTS_MESH_REIMPORTED pid=%lld objects=%zu triangles=%zu\n",static_cast<long long>(QCoreApplication::applicationPid()),replacement.summary.objects,replacement.summary.triangles);std::fflush(stdout);
}

std::vector<std::string> BlenderHost::apply_imported_shader_profiles(TextureDocument &document){
 auto &s=*impl_;s.require_owner();auto library=document.shader_instances();int applied=0;std::vector<std::string> notes;
 for(blender::Material &material:s.main->materials){auto *properties=IDP_GetProperties(&material.id);auto *property=properties?IDP_GetPropertyFromGroup(properties,"bts_imported_openpbr"):nullptr;if(!property||property->type!=IDP_STRING)continue;const auto metadata=QJsonDocument::fromJson(QByteArray(IDP_string_get(property))).object();if(!metadata["activate"].toBool())continue;
  const auto found=std::find_if(document.texture_sets().begin(),document.texture_sets().end(),[&](const auto &set){return set.material_session_uid==material.id.session_uid;});if(found==document.texture_sets().end()||!found->uniform_editable)continue;
  try{const auto constants=openpbr_import_constants(OpenPbrParameters::from_json(metadata["parameters"].toObject()));ShaderParameters parameters;parameters.openpbr=constants.shader;
  auto match=std::find_if(library.instances.begin(),library.instances.end(),[&](const auto &entry){return entry.second.parameters==parameters;});QUuid id;
  if(match!=library.instances.end())id=match->first;else{id=library.create(library.default_instance,QString("Imported OpenPBR %1").arg(++applied));library.parameters(id,parameters);}library.assign(found->id,id);notes.push_back("Imported OpenPBR compatibility profile: "+found->name);}catch(const std::exception &error){notes.push_back(std::string("OpenPBR profile deferred: ")+error.what());}
 }
 document.set_shader_instances(std::move(library));return notes;
}

ImportSummary BlenderHost::new_project(const std::string &mesh_path, const std::string &hdr_path,const NewProjectOptions *options)
{
  auto &s = *impl_;
  s.require_owner();s.reset_warp_geometry();
  if (s.phase != Impl::Phase::GpuReady || s.drawing) {
    throw std::logic_error("New requires an idle initialized viewport");
  }
  auto profile = NewProjectProfile::prepare(hdr_path, *s.scene, *s.view);
  auto pending = PendingMeshProject::from_scene(*s.main, mesh_path,options?options->import_cameras:true,{},options?options->usd:UsdImportOptions{},options?options->blend:BlendMeshSelection{});
  pending->prepare_uvs(options?options->auto_unwrap:AutoUnwrapOptions{});
  const ImportSummary summary = pending->summary();
  if(options&&options->uv_workflow==UvWorkflow::PreserveTiles)for(const auto &material:summary.materials) {
    if(!material.udim_valid||material.tiles.empty())
      throw std::runtime_error("UV Tile projects require valid 1001–2000 UVs");

  }
  Set<ID *> retired;
  ID *id;
  FOREACH_MAIN_ID_BEGIN (s.main, id) {
    if (!ELEM(GS(id->name), ID_WM, ID_SCR, ID_WS)) retired.add(id);
  }
  FOREACH_MAIN_ID_END;
  s.enter_gpu();
  try {
    s.free_targets();
    s.inspection.reset();s.displacement_preview.reset(*s.main);s.last_tangent_shader_state={};
    s.inspection_mode = InspectionMode::Material;
    Scene *previous = s.scene;
    s.scene = &pending->merge_into(*s.main);
    for (wmWindowManager &manager : s.main->wm) {
      for (wmWindow &window : manager.windows) {
        ED_screen_scene_change(s.context, &window, s.scene, false);
      }
    }
    CTX_data_scene_set(s.context, s.scene);
    s.view_layer = CTX_data_view_layer(s.context);
    BKE_scene_free_depsgraph_hash(previous);
    s.depsgraph = nullptr;
    BKE_id_multi_delete(s.main, retired, {.extra_remapping_flags = ID_REMAP_FORCE_UI_POINTERS});
    // Keep original material labels after the old project releases its names.
    for(Object &object:s.main->objects)if(summary.camera_names.contains(object.id.session_uid))BKE_id_rename(*s.main,object.id,summary.camera_names.at(object.id.session_uid));
    for (Material &material : s.main->materials) {
      for (const auto &imported : summary.materials) {
        if (material.id.session_uid == imported.session_uid) {
          BKE_id_rename(*s.main, material.id, imported.name);
          break;
        }
      }
    }
    s.selected_material.reset();
    s.view->local_collections_uid = 0;
    s.view->flag2 |= V3D_HIDE_OVERLAYS;
    profile->apply(*s.main, *s.scene, *s.view);
    BKE_view_layer_synced_ensure(*s.main, s.scene, s.view_layer);
    BKE_scene_set_background(s.main, s.scene);
    s.depsgraph = BKE_scene_ensure_depsgraph(s.main, s.scene, s.view_layer);
    DEG_make_active(s.depsgraph);
    BKE_scene_graph_evaluated_ensure(s.depsgraph, s.main);
    s.main->filepath[0] = '\0';
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
  std::printf("BTS_NEW_PROJECT pid=%lld objects=%zu materials=%zu triangles=%zu working_space=%s\n",
              static_cast<long long>(QCoreApplication::applicationPid()), summary.objects,
              summary.materials.size(), summary.triangles, s.main->colorspace.scene_linear_name);
  std::fflush(stdout);
  return summary;
}

void BlenderHost::begin_mask_preset(const LayerMask &mask){
 auto &s=*impl_;s.require_owner();if(s.mask_preset)throw std::logic_error("A mask generator is already being saved");s.mask_preset=std::make_unique<MaskPresetReadback>(mask);
}
std::optional<QByteArray> BlenderHost::poll_mask_preset(){
 auto &s=*impl_;s.require_owner();if(!s.mask_preset)return {};s.enter_gpu();
 try{auto result=s.mask_preset->poll(s.inspection);if(result)s.mask_preset.reset();DRW_gpu_context_disable_ex(false);return result;}catch(...){s.mask_preset.reset();DRW_gpu_context_disable_ex(false);throw;}
}
void BlenderHost::begin_material_preset(const TextureSet &set,const QUuid &folder){
 auto &s=*impl_;s.require_owner();if(s.material_preset)throw std::logic_error("A material preset is already being saved");s.material_preset=std::make_unique<MaterialPresetReadback>(set,folder);
}
void BlenderHost::begin_material_parameter_export(const TextureSet &set,const std::vector<StudioAsset> &resources,const QString &directory){
 auto &s=*impl_;s.require_owner();if(s.phase!=Impl::Phase::GpuReady||s.drawing||s.parameter_export||save_project_pending()||s.material_preset||s.mask_preset)throw std::logic_error("Another capture/export is still finishing");if(s.inspection.updates_pending())throw std::logic_error("Wait for the selected set's GPU changes before exporting parameters");
 std::array<double,9> matrix;for(int row=0;row<3;++row)for(int column=0;column<3;++column)matrix[row*3+column]=s.main->colorspace.scene_linear_to_xyz[column][row];
 auto job=std::make_unique<MaterialParameterExport>(set,resources,directory,QString::fromUtf8(s.main->colorspace.scene_linear_name),matrix);
 s.enter_gpu();try{job->capture(s.inspection,set);s.parameter_export=std::move(job);DRW_gpu_context_disable_ex(false);}catch(...){job.reset();DRW_gpu_context_disable_ex(false);throw;}
}
bool BlenderHost::material_parameter_export_pending()const{impl_->require_owner();return bool(impl_->parameter_export);}
void BlenderHost::cancel_material_parameter_export(){impl_->require_owner();if(impl_->parameter_export)impl_->parameter_export->cancel();}
bool BlenderHost::poll_material_parameter_export(bool current){
 auto &s=*impl_;s.require_owner();if(!s.parameter_export)return true;if(!current)s.parameter_export->cancel();if(s.phase!=Impl::Phase::GpuReady)return false;s.enter_gpu();try{const bool ready=s.parameter_export->poll(current);if(ready)s.parameter_export.reset();DRW_gpu_context_disable_ex(false);return ready;}catch(...){s.parameter_export.reset();DRW_gpu_context_disable_ex(false);throw;}
}
std::optional<QByteArray> BlenderHost::poll_material_preset(){
 auto &s=*impl_;s.require_owner();if(!s.material_preset)return {};s.enter_gpu();
 try{auto result=s.material_preset->poll(s.inspection);if(result)s.material_preset.reset();DRW_gpu_context_disable_ex(false);return result;}catch(...){s.material_preset.reset();DRW_gpu_context_disable_ex(false);throw;}
}

void BlenderHost::begin_save_project(const TextureDocument &document,const std::string &path,const std::vector<StudioAsset> &resources,std::function<void()> persist_metadata,std::optional<ProjectFileIdentity> expected)
{
 auto &s=*impl_;s.require_owner();performance::Scope timing("save_snapshot_request");
 if(s.phase!=Impl::Phase::GpuReady||s.drawing||save_project_pending())throw std::logic_error("Another project save is in progress");
 if(s.inspection.updates_pending())throw std::logic_error("Project GPU changes are still being published");
 auto snapshot=std::make_unique<ProjectSaveSnapshot>(*s.main,*s.scene,*s.view,document,resources);
 s.enter_gpu();try{
  auto readback=std::make_unique<GpuProjectReadback>();s.inspection.capture_project_images(*readback);
  s.save_snapshot=std::move(snapshot);s.save_readback=std::move(readback);s.save_path=path;s.save_metadata=std::move(persist_metadata);s.save_expected=expected.value_or(ProjectFileIdentity::read(QString::fromStdString(path)));
  DRW_gpu_context_disable_ex(false);
 }catch(...){DRW_gpu_context_disable_ex(false);throw;}
}
ProjectFileIdentity BlenderHost::saved_project_identity()const{impl_->require_owner();return impl_->saved_identity;}
bool BlenderHost::save_project_pending()const{impl_->require_owner();return bool(impl_->save_readback)||impl_->save_writer.valid();}
bool BlenderHost::poll_save_project()
{
 auto &s=*impl_;s.require_owner();
 if(s.save_snapshot){
  bool ready=false;s.enter_gpu();try{const auto before=s.save_readback->images().size();const bool pixels_ready=s.save_readback->poll();const bool sources_ready=s.save_snapshot->capture_authored(s.inspection,*s.save_readback);ready=pixels_ready&&sources_ready&&before==s.save_readback->images().size();DRW_gpu_context_disable_ex(false);}catch(...){s.save_readback.reset();s.save_snapshot.reset();DRW_gpu_context_disable_ex(false);throw;}
  if(!ready)return false;
  s.save_writer=std::async(std::launch::async,[snapshot=std::move(s.save_snapshot),readback=s.save_readback.get(),path=s.save_path,expected=s.save_expected,metadata=std::move(s.save_metadata)]()mutable{
    auto owned=std::move(snapshot);struct RetireCpu{GpuProjectReadback *readback;~RetireCpu(){readback->release_cpu();}} retire{readback};
    const auto identity=owned->write(path,*readback,expected);owned.reset();readback->release_cpu();if(metadata){performance::Scope timing("save_metadata_io");metadata();}return identity;
  });
 }
 if(s.save_writer.valid()){
  if(s.save_writer.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
  std::exception_ptr failure;try{s.saved_identity=s.save_writer.get();}catch(...){failure=std::current_exception();}
  s.enter_gpu();s.save_readback.reset();DRW_gpu_context_disable_ex(false);
  if(failure)std::rethrow_exception(failure);
  std::printf("BTS_PROJECT_SAVED path=%s pid=%lld background=1 cpu_composite=0\n",s.save_path.c_str(),static_cast<long long>(QCoreApplication::applicationPid()));std::fflush(stdout);
 }
 return true;
}
void BlenderHost::save_project(const TextureDocument &document,const std::string &path,const std::vector<StudioAsset> &resources,bool recovery_copy,std::optional<ProjectFileIdentity> expected)
{
 // Synchronous diagnostic/export bridge. Workspace actions use the asynchronous
 // API above; this bridge retains deterministic CLI/demo round trips.
 auto &s=*impl_;const auto started=performance::Clock::now();
 while(s.inspection.updates_pending()){
  s.enter_gpu();try{s.inspection.upload_staging_images(true,false);s.inspection.finalize_gpu_images(*s.main);DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}
  if(performance::ms(started)>30000)throw std::runtime_error("Diagnostic GPU save publication timed out");
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 s.inspection.poll_composites(*s.main,true);
 begin_save_project(document,path,resources,{},expected);
 while(!poll_save_project())std::this_thread::sleep_for(std::chrono::milliseconds(1));
 if(!recovery_copy)STRNCPY(impl_->main->filepath,path.c_str());
}
void BlenderHost::prepare_native_height(const QUuid &resource,NativeMaterialHeightResource value){
 impl_->require_owner();if(resource.isNull())throw std::invalid_argument("Native Height source identity is empty");
 with_gpu([&]{impl_->inspection.prepare_native_height(resource.toString(QUuid::WithoutBraces).toStdString(),std::move(value));});
}
std::unique_ptr<NativeMaterialHeightGpu> BlenderHost::stage_native_height_gpu(const QUuid &resource,NativeMaterialHeightResource value){
 impl_->require_owner();if(resource.isNull()||!project_gpu_available())throw std::logic_error("Native Height preparation requires an idle viewport and source identity");
 auto work=std::unique_ptr<NativeMaterialHeightGpu>(new NativeMaterialHeightGpu(*this));work->impl_->key=resource.toString(QUuid::WithoutBraces).toStdString();
 with_gpu([&]{work->gpu_owned_=true;impl_->inspection.stage_native_height(*work,std::move(value));});return work;
}
bool BlenderHost::step_native_height_gpu(NativeMaterialHeightGpu &work){
 impl_->require_owner();if(work.host_!=this||!work.impl_||!project_gpu_available())throw std::logic_error("Native Height candidate belongs to a different or unavailable owner");
 bool ready=false;with_gpu([&]{ready=impl_->inspection.step_native_height(work);});return ready;
}
void BlenderHost::install_native_height_gpu(NativeMaterialHeightGpu &work){
 impl_->require_owner();if(work.host_!=this||!work.impl_||!project_gpu_available())throw std::logic_error("Native Height candidate belongs to a different or unavailable owner");
 with_gpu([&]{impl_->inspection.install_native_height(work);});
}
void BlenderHost::probe_native_height(const TextureSet &set,const QUuid &id){
 impl_->require_owner();const auto &layer=TextureDocument::layer(const_cast<TextureSet &>(set),id);if(!layer.fill.native_material)throw std::invalid_argument("Native Height target is not a native Fill");require_native_height_origin(layer.fill.native_material->height_policy,set.bump_origin);
 const auto requirements=impl_->inspection.native_requirements(layer.fill.material_resource,*layer.fill.native_material);NativeMaterialGpuContext context;if(requirements.object)bind_native_material_objects(*impl_->main,impl_->depsgraph,set.material_session_uid,context);context.tile=*set.tiles.begin();
 with_gpu([&]{impl_->inspection.probe_native_height(layer.fill.material_resource,*layer.fill.native_material,std::move(context));});
}
void BlenderHost::discard_native_height_gpu(NativeMaterialHeightGpu &work){
 impl_->require_owner();if(work.host_!=this)throw std::logic_error("Native Height candidate belongs to a different owner");
 if(work.gpu_owned_)with_gpu([&]{work.impl_.reset();});else work.impl_.reset();work.host_=nullptr;
}
bool BlenderHost::set_native_height_policy(TextureSet &set,const QUuid &layer,NativeHeightPolicy policy)const{
 impl_->require_owner();if(impl_->phase!=Impl::Phase::GpuReady||impl_->drawing)throw std::logic_error("Native Height policy requires an idle viewport");
 return impl_->inspection.set_native_height_policy(set,layer,policy);
}
namespace {
void require_height_open_color_context(const blender::Main &live,const blender::Main &pending){
 const auto recorded=[](const blender::Main &main){
  const auto &name=main.colorspace.scene_linear_name;const auto length=strnlen(name,sizeof(name));if(length==sizeof(name))throw std::invalid_argument("Native Height working-space name is not terminated");
  NativeHeightColorContext result;result.name=QString::fromUtf8(name,int(length));for(int c=0;c<3;++c)for(int r=0;r<3;++r)result.scene_linear_to_xyz[c*3+r]=main.colorspace.scene_linear_to_xyz[c][r];return result;
 };
 NativeHeightColorContext active;active.name=QString::fromUtf8(blender::IMB_colormanagement_working_space_get());const auto matrix=blender::IMB_colormanagement_get_scene_linear_to_xyz();for(int c=0;c<3;++c)for(int r=0;r<3;++r)active.scene_linear_to_xyz[c*3+r]=matrix[c][r];
 require_native_height_color_context(recorded(live),recorded(pending),active);
}
}
std::unique_ptr<PendingProjectGpu> BlenderHost::begin_project_gpu(PendingProject &pending){
 auto &s=*impl_;s.require_owner();pending.validate();
 const bool raw=native_height_project(*pending.document);if(!raw&&!pending.layer_sources)return {};
 if(s.phase!=Impl::Phase::GpuReady||s.drawing||save_project_pending())throw std::logic_error("Source Open requires an idle viewport with no pending save");
 if(raw)require_height_open_color_context(*s.main,*pending.file->main);
 if(!pending.validated_inspection||(raw&&pending.native_sources.empty()))throw std::logic_error("Source Open has no validated worker state");
 if(pending.layer_sources)pending.validated_inspection->validate_material_layer_sources(*pending.document);
 auto *wm=static_cast<wmWindowManager *>(pending.file->main->wm.first);
 if(!wm||wm->windows.is_empty())throw std::invalid_argument("Staged source project has no recorded native window context");
 for(const auto &window:wm->windows)if(!window.runtime||window.runtime->ghostwin)throw std::invalid_argument("Staged source project cannot adopt a native system window");
 const auto *window=static_cast<const wmWindow *>(wm->windows.first);auto *screen=WM_window_get_active_screen(window);
 if(!screen||window->scene!=pending.file->curscene||BKE_view_layer_find(window->scene,window->view_layer_name)!=pending.file->cur_view_layer)throw std::invalid_argument("Staged source project window/scene/view-layer context does not match its validated file context");
 bool viewport=false;for(const auto &area:screen->areabase)if(area.spacetype==SPACE_VIEW3D)for(const auto &region:area.regionbase)if(region.regiontype==RGN_TYPE_WINDOW&&region.regiondata)viewport=true;
 if(!viewport)throw std::invalid_argument("Staged source project first native window has no usable View3D region");
 auto stage=std::unique_ptr<PendingProjectGpu>(new PendingProjectGpu(*this));stage->pending_=&pending;
 stage->inspection_=std::move(pending.validated_inspection);stage->sources_=std::move(pending.native_sources);
 // Capture only stable Host Impl. Pending/staging pointers never enter the
 // long-lived context provider which will run after the accepted Main switch.
 stage->inspection_->set_native_fragment_geometry_provider([&s](NativeFragmentGeometryRequest request,const std::atomic_bool &cancel){return capture_native_fragment_geometry(s.context,s.view,s.displacement_preview.source_meshes(),std::move(request),cancel);});stage->inspection_->set_native_fragment_object_provider([&s](std::uint32_t uid){NativeMaterialGpuContext context;bind_native_material_objects(*s.main,s.depsgraph,uid,context,false);return context.objects;});
 stage->inspection_->set_native_context_provider([&s](const TextureSet &set,const TextureLayer &,int,const NativeMaterialDependencies &requirements,NativeMaterialGpuContext &context){
  if(requirements.object)bind_native_material_objects(*s.main,s.depsgraph,set.material_session_uid,context);
  if(requirements.view){if(!s.native_view_matrices)throw std::runtime_error("Native material view is not ready");bind_native_material_view(s.native_view_matrices->first,s.native_view_matrices->second,context);}
 });
 return stage;
}
bool BlenderHost::step_project_gpu(PendingProject &pending,PendingProjectGpu &stage){
 auto &s=*impl_;s.require_owner();pending.validate();
 if(stage.host_!=this||stage.pending_!=&pending||!stage.inspection_)throw std::logic_error("Pending native GPU project belongs to a different owner");
 if(pending.layer_sources)stage.inspection_->validate_material_layer_sources(*pending.document,stage.layer_sources_ready_);
 if(stage.ready_)return true;
 // Release a completed wrapper outside the active with_gpu scope. Its owner
 // cleanup activates the context itself, so do not nest context entry/exit.
 if(stage.height_work_&&stage.height_work_->ready())stage.height_work_.reset();
 with_gpu([&]{
  stage.gpu_owned_=true;auto &inspection=*stage.inspection_;
  if(pending.layer_sources&&!stage.layer_sources_ready_){stage.layer_sources_ready_=inspection.prepare_material_layer_sources();return;}
  if(!native_height_project(*pending.document)){stage.ready_=true;return;}
  if(stage.source_<stage.sources_.size()){
   auto &source=stage.sources_[stage.source_];const auto key=source.id.toString(QUuid::WithoutBraces).toStdString();
   if(!stage.queued_){inspection.prepare_native_material(key,source.hash.toStdString(),source.graph);stage.queued_=true;}
   inspection.step_native_preparations();const auto status=inspection.native_preparation_status(key);
   if(!status.error.isEmpty())throw std::runtime_error(status.error.toStdString());
   if(status.ready){++stage.source_;stage.queued_=false;}
   else if(!status.pending)throw std::runtime_error("Pending native source GPU preparation disappeared");
   return;
  }
  if(stage.height_<stage.sources_.size()){
   auto &source=stage.sources_[stage.height_];
   if(!source.height&&!stage.height_work_){++stage.height_;return;}
   if(!stage.height_work_){
    stage.height_work_=std::unique_ptr<NativeMaterialHeightGpu>(new NativeMaterialHeightGpu(*this));auto &work=*stage.height_work_;work.impl_->key=source.id.toString(QUuid::WithoutBraces).toStdString();work.gpu_owned_=true;inspection.stage_native_height(work,std::move(*source.height));source.height.reset();return;
   }
   auto &work=*stage.height_work_;if(!inspection.step_native_height(work))return;inspection.install_native_height(work);work.commit();++stage.height_;return;
  }
  for(const auto &set:pending.document->texture_sets())if(!set.disabled&&set.uniform_editable)for(const auto &layer:evaluation_layers(set))if(layer.fill.native_material){
   const auto requirements=inspection.native_requirements(layer.fill.material_resource,*layer.fill.native_material);
   if(requirements.object){const auto found=pending.native_object_contexts.find(set.material_session_uid);if(found==pending.native_object_contexts.end()||!found->second.objects||!found->second.objects->size())throw std::invalid_argument("Pending native Height project has no validated object context");}
   if(requirements.ao)for(int tile:set.tiles)if(!set.mesh_maps[3]||!set.mesh_maps[3]->tiles.contains(tile)||!set.mesh_maps[3]->tiles.at(tile).pixels)throw std::invalid_argument("Pending native source requires a baked AO map for every UV tile");
  }
  std::size_t probe=0;
  for(const auto &set:pending.document->texture_sets())if(!set.disabled&&set.uniform_editable)for(const auto &layer:evaluation_layers(set))if(layer.fill.native_material&&layer.fill.native_material->height_policy==NativeHeightPolicy::RawAuthoredFloatV1){
   if(probe++!=stage.probe_)continue;
   NativeMaterialGpuContext context;const auto found=pending.native_object_contexts.find(set.material_session_uid);if(found!=pending.native_object_contexts.end())context=found->second;
   context.tile=*set.tiles.begin();inspection.probe_native_height(layer.fill.material_resource,*layer.fill.native_material,std::move(context));++stage.probe_;return;
  }
  stage.ready_=true;
 });
 return stage.ready_;
}
bool BlenderHost::project_gpu_available()const{impl_->require_owner();return impl_->phase==Impl::Phase::GpuReady&&!impl_->drawing;}
void BlenderHost::discard_project_gpu(PendingProjectGpu &stage){
 impl_->require_owner();if(stage.host_!=this)throw std::logic_error("Pending native GPU project belongs to a different owner");
 stage.height_work_.reset(); // Wrapper owns its own context boundary.
 const auto release=[&]{stage.inspection_.reset();stage.sources_.clear();};
 if(stage.gpu_owned_)with_gpu([&]{release();GpuLayerPreview::collect_retired();});else release();stage.host_=nullptr;
}
TextureDocument BlenderHost::open_project(const std::string &path,std::function<void(const std::vector<StudioAsset> &)> validate_resources)
{
  auto &s=*impl_;s.require_owner();
  if(s.phase!=Impl::Phase::GpuReady||s.drawing)throw std::logic_error("Open requires an idle viewport");
  // Batch bridge only. Workspace/Python Open use the nonblocking owner job.
  if(QThread::currentThread()->loopLevel()>0)throw std::logic_error("Synchronous project Open is batch-only; use asynchronous Open in the workspace");
  auto reader=std::async(std::launch::async,[path]{return PendingProject::prepare(path);});
  auto pending=reader.get();pending->validate(); // Ready batch result; detached cleanup stays on the owner.
  if(validate_resources)validate_resources(pending->resources);
  auto gpu=begin_project_gpu(*pending);
  // Batch-only bridge: no GUI event loop is running and no interactive owner
  // waits on a future. Use the same admission/ready state as asynchronous Open.
  while(gpu&&!step_project_gpu(*pending,*gpu))std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return adopt_project(std::move(pending),path,std::move(gpu));
}
TextureDocument BlenderHost::adopt_project(std::unique_ptr<PendingProject> pending,const std::string &path)
{
 return adopt_project(std::move(pending),path,{});
}
void BlenderHost::validate_project_adoption(const PendingProject &pending,const PendingProjectGpu *gpu)const
{
 const auto &s=*impl_;s.require_owner();
 if(s.phase!=Impl::Phase::GpuReady||s.drawing||save_project_pending())throw std::logic_error("Open requires an idle viewport with no pending save");
 pending.validate();
 const bool raw=native_height_project(*pending.document);const bool staged=raw||pending.layer_sources;
 if(staged&&(!gpu||gpu->host_!=this||gpu->pending_!=&pending||!gpu->ready_||!gpu->inspection_))throw std::logic_error("Source Open requires complete isolated owner GPU staging before adoption");
 if(!staged&&gpu)throw std::logic_error("Ordinary project cannot adopt an unrelated source GPU stage");
 if(pending.layer_sources)gpu->inspection_->validate_material_layer_sources(*pending.document,true);
 if(raw)require_height_open_color_context(*s.main,*pending.file->main);
}
TextureDocument BlenderHost::adopt_project(std::unique_ptr<PendingProject> pending,const std::string &path,std::unique_ptr<PendingProjectGpu> gpu)
{
  // Parameter destruction order is unspecified. This guard always releases
  // the GPU side before the pending BFD on success and every rejection path.
  struct GpuBeforeFile{std::unique_ptr<PendingProjectGpu> &stage;~GpuBeforeFile(){stage.reset();}} release{gpu};
  auto &s=*impl_;s.require_owner();
  if(!pending)throw std::invalid_argument("Cannot adopt an empty project");
  validate_project_adoption(*pending,gpu.get());
  const bool staged=native_height_project(*pending->document)||pending->layer_sources;
  // Every new policy/context/source/program failure precedes this accepted
  // native adoption boundary. This is not a general rollback guarantee for
  // Blender's existing file setup, after-setup versioning or allocation errors.
  pending->activate_environment(); // Global StudioLight registration is owner-only.
  s.enter_gpu();
  try {
    s.free_targets();s.inspection.reset();s.displacement_preview.reset(*s.main);s.last_tangent_shader_state={};s.selected_material.reset();s.inspection_mode=InspectionMode::Material;
    // reset joins old readers; drain retired GPU owners while their original
    // Main is still alive. No queue/reference-bound preparation is moved.
    if(staged)GpuLayerPreview::collect_retired();
    auto *file=pending->file;pending->file=nullptr;BTS_file_adopt(s.context,file);
    s.attach_context();STRNCPY(s.scene->r.engine,"BLENDER_EEVEE");s.view->shading.type=OB_MATERIAL;s.view->flag2|=V3D_HIDE_OVERLAYS;
    if(staged)s.inspection.swap_prepared(*gpu->inspection_); // noexcept stable-pimpl transfer.
    else s.inspection.restore(*s.main,*pending->document);
    s.depsgraph=BKE_scene_ensure_depsgraph(s.main,s.scene,s.view_layer);DEG_make_active(s.depsgraph);
    BKE_scene_graph_evaluated_ensure(s.depsgraph,s.main);
    DRW_gpu_context_disable_ex(false);
  }
  catch(...){DRW_gpu_context_disable_ex(false);throw;}
  for(const auto &set:pending->document->texture_sets())if(!set.disabled&&set.uniform_editable){
    prepare_paint_texture(set);
    if(!preview_layer_edit(set,complete_composite_channels,false))apply_texture_set(set,int(pending->document->settings().resolution));
  }
  std::printf("BTS_PROJECT_OPENED path=%s pid=%lld\n",path.c_str(),static_cast<long long>(QCoreApplication::applicationPid()));std::fflush(stdout);
  return std::move(*pending->document);
}

SymmetrySettings BlenderHost::symmetry_settings() const {impl_->require_owner();return read_project_symmetry(*impl_->scene);}
void BlenderHost::set_symmetry(const SymmetrySettings &settings) {impl_->require_owner();write_project_symmetry(*impl_->scene,settings);}

QJsonObject BlenderHost::project_profile() const
{
  auto &s = *impl_;
  s.require_owner();
  auto profile=NewProjectProfile::inspect(*s.main, *s.scene, *s.view);profile["texture_residency"]=s.residency.snapshot();return profile;
}

std::vector<std::string> BlenderHost::display_transforms() const
{
  auto &s = *impl_;
  s.require_owner();
  auto scene = RNA_id_pointer_create(&s.scene->id);
  auto view = RNA_pointer_get(&scene, "view_settings");
  auto *property = RNA_struct_find_property(&view, "view_transform");
  const EnumPropertyItem *items;
  bool owned;
  RNA_property_enum_items(s.context, &view, property, &items, nullptr, &owned);
  std::vector<std::string> result;
  for (auto *item = items; item && item->identifier; ++item)
    if (item->identifier[0]) result.emplace_back(item->identifier);
  if (owned) MEM_delete(items);
  return result;
}

void BlenderHost::set_inspection(InspectionMode mode,std::span<const InspectionMaterial> materials)
{
  auto &s=*impl_;
  s.require_owner();
  if (s.drawing || s.phase==Impl::Phase::Closed) throw std::logic_error("Inspection change outside idle session");
  s.uv_dirty |= s.inspection.update(*s.main,mode,materials);
  s.inspection_mode=mode;
}
void BlenderHost::set_material_layering_view(std::uint32_t uid,bool enabled,int active_mask){
 auto &s=*impl_;s.require_owner();
 if(s.drawing||s.phase==Impl::Phase::Closed)throw std::logic_error("Material layering view requires an idle viewport");
 s.uv_preview.set_material_layering_view(uid,enabled,active_mask);
}
void BlenderHost::set_geometry_visibility(const TextureSet *set){auto &s=*impl_;s.require_owner();if(s.drawing||s.phase==Impl::Phase::Closed)throw std::logic_error("Geometry visibility requires an idle viewport");s.uv_dirty|=s.inspection.set_geometry_visibility(*s.main,set);}
bool BlenderHost::geometry_visibility_enabled(std::uint32_t uid) const{return impl_->inspection.geometry_visibility_enabled(uid);}
std::size_t BlenderHost::geometry_visibility_image_count() const{return impl_->inspection.geometry_visibility_image_count();}
InspectionMode BlenderHost::inspection_mode() const { return impl_->inspection_mode; }

std::string BlenderHost::shader_failures() const{auto &s=*impl_;s.require_owner();if(!s.main||!s.depsgraph)return {};std::string result;for(Material &material:s.main->materials){auto *evaluated=DEG_get_evaluated(s.depsgraph,&material);bool failed=false;for(Material *owner:std::set<Material *>{&material,evaluated}){if(!owner)continue;for(LinkData &link:owner->gpumaterial)failed|=GPU_material_status(static_cast<GPUMaterial *>(link.data))==GPU_MAT_FAILED;}if(failed){if(!result.empty())result+=", ";result+=material.id.name+2;}}return result;}
QJsonObject BlenderHost::tangent_shader_state() const{auto &s=*impl_;s.require_owner();QJsonObject result;for(Material &material:s.main->materials)if(material.nodetree){auto *node=bke::node_find_node_by_name(*material.nodetree,"__BTS_PaintNormal");if(!node)continue;int success=0,failed=0;auto *evaluated=DEG_get_evaluated(s.depsgraph,&material);for(Material *owner:std::set<Material *>{&material,evaluated}){if(!owner)continue;for(LinkData &link:owner->gpumaterial){const auto status=GPU_material_status(static_cast<GPUMaterial *>(link.data));success+=status==GPU_MAT_SUCCESS;failed+=status==GPU_MAT_FAILED;}}const auto name=QString::fromUtf8(material.id.name+2);const auto cached=s.last_tangent_shader_state[name].toObject();if(success+failed==0&&cached["fragment"].isBool()&&cached["fragment"].toBool()==bool(node->custom1&1)){success=cached["success"].toInt();failed=cached["failed"].toInt();}result[name]=QJsonObject{{"fragment",bool(node->custom1&1)},{"success",success},{"failed",failed}};}return result;}
LensProjection BlenderHost::lens_projection()const{impl_->require_owner();return impl_->lens;}
void BlenderHost::set_display_post_effects(const QJsonObject &json){auto &s=*impl_;s.require_owner();if(s.drawing||s.phase==Impl::Phase::Closed)throw std::logic_error("Display effects require an idle viewport");set_display_post_settings(*s.main,*s.scene,*s.view,DisplayPostSettings::from_json(json));s.lens=display_post_settings(*s.scene).projection();}
void BlenderHost::edit_display_post_effect(const QString &key,double value){auto &s=*impl_;s.require_owner();auto state=display_post_settings(*s.scene);state.edit(key,value);set_display_post_effects(state.to_json());}
void BlenderHost::set_subsurface_preview(const QJsonObject &json){auto &s=*impl_;s.require_owner();if(s.drawing||s.phase==Impl::Phase::Closed)throw std::logic_error("Subsurface preview requires an idle viewport");const auto value=SubsurfacePreview::from_json(json);bts::set_subsurface_preview(*s.main,*s.scene,value);s.inspection.set_subsurface_enabled(*s.main,value.enabled);s.tag_preview();s.texture_dirty=true;}
void BlenderHost::edit_subsurface_preview(const QString &key,double value){impl_->require_owner();if(impl_->phase==Impl::Phase::Closed)throw std::logic_error("Subsurface preview is closed");auto next=subsurface_preview(*impl_->scene);if(!std::isfinite(value))throw std::invalid_argument("Invalid subsurface preview edit");if(key=="enabled")next.enabled=value!=0;else if(key=="samples"){if(value!=std::floor(value))throw std::invalid_argument("Use an integer subsurface sample count");next.samples=int(value);}else if(key=="reset")next={};else throw std::invalid_argument("Unknown subsurface preview property");set_subsurface_preview(next.to_json());}
void BlenderHost::set_render_preview(bool enabled,bool force_cpu){auto &s=*impl_;s.require_owner();if(s.drawing||s.phase!=Impl::Phase::GpuReady)throw std::logic_error("Renderer switch requires an idle GPU viewport");s.enter_gpu();try{s.configure_preview(enabled,force_cpu);DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}}
void BlenderHost::set_render_quality(int samples,bool denoise){auto &s=*impl_;s.require_owner();if(s.drawing||samples<1||samples>65536)throw std::invalid_argument("Invalid Cycles sample count");auto root=RNA_id_pointer_create(&s.scene->id);auto settings=RNA_pointer_get(&root,"cycles");const bool decrease=samples<RNA_int_get(&settings,"preview_samples");RNA_int_set(&settings,"preview_samples",samples);RNA_boolean_set(&settings,"use_preview_denoising",denoise);RNA_property_update(s.context,&settings,RNA_struct_find_property(&settings,"preview_samples"));RNA_property_update(s.context,&settings,RNA_struct_find_property(&settings,"use_preview_denoising"));DEG_id_tag_update_ex(s.main,&s.scene->id,ID_RECALC_PARAMETERS|ID_RECALC_SYNC_TO_EVAL);if(decrease&&STREQ(s.scene->r.engine,"CYCLES")){/* Cycles only increases a running viewport sample budget. Restart for a lower limit. */s.enter_gpu();s.free_targets();DRW_gpu_context_disable_ex(false);}}
void BlenderHost::diagnostic_cycles_cuda_render(const QString &directory) {
 auto &s=*impl_;s.require_owner();
 if(s.phase!=Impl::Phase::SceneReady)throw std::logic_error("CUDA diagnostic requires factory startup before viewport initialization");
 // Python snapshots os.environ at initialization; pass the path directly as
 // a JSON-escaped string literal instead of changing the process environment.
 QByteArray code="output_directory = ";
 code+=QJsonDocument(QJsonArray{QFileInfo(directory).absoluteFilePath()}).toJson(QJsonDocument::Compact);
 code+="[0]\n";code+=cycles_cuda_test_python;
 if(!BPY_run_string_exec(s.context,nullptr,code.constData()))throw std::runtime_error("Cycles CUDA test failed; see result.json and the CUDA error above");
}
QJsonObject BlenderHost::render_state()const {
 auto &s=*impl_;s.require_owner();
 if(s.phase==Impl::Phase::Closed)return {{"active",false},{"available",false},{"status","Closed"}};
 const bool active=STREQ(s.scene->r.engine,"CYCLES");
 RenderEngine *engine=nullptr;
 if(s.region&&s.region->regiondata){auto *rv=static_cast<RegionView3D *>(s.region->regiondata);if(rv->view_render)engine=RE_view_engine_get(rv->view_render);}
 QString error;
 if(active&&engine){
  if(engine->re){const auto *result=RE_AcquireResultRead(engine->re);if(result&&result->error)error=QString::fromUtf8(result->error);RE_ReleaseResult(engine->re);}
  const auto text=QString::fromUtf8(engine->text);
  if(error.isEmpty()&&text.startsWith("Cycles error: "))error=text.mid(14);
 }
 auto root=RNA_id_pointer_create(&s.scene->id);auto settings=RNA_pointer_get(&root,"cycles");
 const auto mirror_error=active?s.inspection.cycles_images_error():std::string_view{};
 const bool mirror_paused=!mirror_error.empty();
 QString message=engine?QString::fromUtf8(engine->text):active?QString("Initializing Cycles…"):QString("EEVEE");
 if(active&&s.inspection.cycles_images_pending())message="Preparing live material images…";
 if(mirror_paused)message=QString("Live image error: %1. Switch to Painting, free resources if needed, then return to Rendering to retry.").arg(QString::fromUtf8(mirror_error.data(),mirror_error.size()));
 if(!error.isEmpty())message="Cycles / "+s.preview_device+" error: "+error+". Select CPU to retry, or F9 to return to painting.";
 return {{"cpu_fallback",active&&s.preview_cpu_fallback},{"device_message",active?s.preview_device_message:QString{}},
  {"error",error},{"image_mirror_paused",mirror_paused},{"active",active},
  {"view_clip_start",double(s.view->clip_start)},{"view_clip_end",double(s.view->clip_end)},
  {"available",RE_engines_find("CYCLES")!=nullptr},{"device",active?s.preview_device:QString("EEVEE")},
  {"status",message},{"samples",RNA_int_get(&settings,"preview_samples")},{"denoise",RNA_boolean_get(&settings,"use_preview_denoising")}};
}

QJsonObject BlenderHost::environment_state() const
{
  auto &s=*impl_;s.require_owner();
  return {{"subsurface",subsurface_preview(*s.scene).to_json()},{"post_depth_enabled",bool(s.view_layer->passflag&SCE_PASS_DEPTH)},{"post_effects",display_post_settings(*s.scene).to_json()},{"resource",s.view->shading.lookdev_light},{"rotation",double(s.view->shading.studiolight_rot_z)*180/3.14159265358979323846},
    {"exposure",std::log2(std::max(double(s.view->shading.studiolight_intensity),1./1024))},
    {"opacity",s.view->shading.studiolight_background*100},{"blur",s.view->shading.studiolight_blur*100},
    {"alignment",(s.view->shading.flag&V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION)?"Local":"World"},
    {"lighting_follows_view",bool(s.view->shading.flag&V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION)}};
}
void BlenderHost::set_environment_resource(const std::string &path)
{
  auto &s=*impl_;s.require_owner();
  if(s.drawing||s.phase!=Impl::Phase::GpuReady)throw std::logic_error("Environment change requires an idle viewport");
  auto *light=register_environment_file(QString::fromStdString(path));
  STRNCPY(s.view->shading.lookdev_light,light->name);s.tag_preview();
}
void BlenderHost::set_environment_alignment(bool world)
{
  auto &s=*impl_;s.require_owner();
  if(s.drawing||s.phase!=Impl::Phase::GpuReady)throw std::logic_error("Environment change requires an idle viewport");
  if(world)s.view->shading.flag&=~V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
  else s.view->shading.flag|=V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
  s.tag_preview();
}
void BlenderHost::set_environment_rotation(double degrees)
{
  auto &s=*impl_;s.require_owner();
  if(s.drawing||s.phase!=Impl::Phase::GpuReady)throw std::logic_error("Environment change requires an idle viewport");
  if(!std::isfinite(degrees)||degrees < -180||degrees>180)throw std::invalid_argument("Environment rotation outside -180..180 degrees");
  s.view->shading.studiolight_rot_z=float(degrees*3.14159265358979323846/180);s.tag_preview();
}

void BlenderHost::set_environment_appearance(const std::string &field,double value)
{
  auto &s=*impl_;s.require_owner();
  if(s.drawing||s.phase!=Impl::Phase::GpuReady)throw std::logic_error("Environment change requires an idle viewport");
  if(!std::isfinite(value))throw std::invalid_argument("Nonfinite environment value");
  if(field=="exposure"&&value>=-10&&value<=10)s.view->shading.studiolight_intensity=float(std::exp2(value));
  else if(field=="opacity"&&value>=0&&value<=100)s.view->shading.studiolight_background=0; // Camera background is always the UI theme.
  else if(field=="blur"&&value>=0&&value<=100)s.view->shading.studiolight_blur=float(value/100);
  else throw std::invalid_argument("Unsupported environment control or value");
  s.tag_preview();
}

void BlenderHost::set_display_transform(const std::string &name)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed) throw std::logic_error("Display change outside idle session");
  auto scene = RNA_id_pointer_create(&s.scene->id);
  auto view = RNA_pointer_get(&scene, "view_settings");
  auto *property = RNA_struct_find_property(&view, "view_transform");
  int value;
  if (!RNA_property_enum_value(s.context, &view, property, name.c_str(), &value))
    throw std::invalid_argument("The native OCIO config does not offer this display transform");
  RNA_property_enum_set(&view, property, value);
  if(STREQ(s.scene->r.engine,"CYCLES"))s.sync_preview_background();
  DEG_id_tag_update_ex(s.main, &s.scene->id, ID_RECALC_SYNC_TO_EVAL);
  s.uv_dirty = true;
}

void BlenderHost::display_rgba(std::span<float> rgba) const
{
  auto &s=*impl_;s.require_owner();
  if(!s.scene||rgba.size()%4)throw std::invalid_argument("Invalid display preview buffer");if(rgba.size()/4>std::uint64_t(std::numeric_limits<int>::max()))throw std::length_error("Native display processor pixel indexing exceeds signed int");
  auto processor=ColormanageProcessor::display_processor_new(&s.scene->view_settings,&s.scene->display_settings);
  processor.apply(rgba.data(),int(rgba.size()/4),1,4,false);
}

QJsonObject BlenderHost::color_state() const
{
  auto &s = *impl_;
  s.require_owner();
  QJsonArray matrix;
  for (int row = 0; row < 3; ++row) {
    QJsonArray values;
    for (int column = 0; column < 3; ++column) values.append(s.main->colorspace.scene_linear_to_xyz[column][row]);
    matrix.append(values);
  }
  return {{"working_space", s.main->colorspace.scene_linear_name},
          {"active_working_space", IMB_colormanagement_working_space_get()},
          {"scene_linear_to_xyz", matrix},
          {"native_acescg", math::is_equal(s.main->colorspace.scene_linear_to_xyz, ocio::ACESCG_TO_XYZ, 1e-5f) &&
                             math::is_equal(colorspace::scene_linear_to_xyz, ocio::ACESCG_TO_XYZ, 1e-5f)},
          {"display", s.scene->display_settings.display_device},
          {"view_transform", s.scene->view_settings.view_transform},
          {"look", s.scene->view_settings.look}, {"exposure", s.scene->view_settings.exposure},
          {"gamma", s.scene->view_settings.gamma}};
}

BlenderHost::~BlenderHost()
{
  shutdown();
}

void BlenderHost::release_bake_scratch(){with_gpu([&]{
 GPU_finish();int before_total=0,before_free=0;if(GPU_mem_stats_supported())GPU_mem_stats_get(&before_total,&before_free);
 const auto reclaimed=impl_->inspection.release_bake_scratch();GPU_finish();int total=0,free=0;if(GPU_mem_stats_supported())GPU_mem_stats_get(&total,&free);
 texture_vram_query_valid=total>0&&free>=0&&free<=total;texture_free_vram=texture_vram_query_valid?std::uint64_t(free)*1024:0;++texture_vram_query_generation;
 std::printf("BTS_BAKE_VRAM total_kib=%d free_before_kib=%d free_after_kib=%d reclaimed_cache_bytes=%llu\n",total,before_free,free,(unsigned long long)reclaimed);
});}
void BlenderHost::with_gpu(const std::function<void()> &fn){auto &s=*impl_;s.require_owner();if(s.phase!=Impl::Phase::GpuReady||s.drawing)throw std::logic_error("GPU bake rays require an idle viewport");s.enter_gpu();try{fn();DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}}
void BlenderHost::initialize_gpu(QOpenGLWidget &surface){impl_->surface=std::make_unique<QtGpuSurface>(surface);initialize_gpu_surface();}
void BlenderHost::initialize_gpu(QOpenGLContext &context,QSurface &surface,unsigned framebuffer){impl_->surface=std::make_unique<QtGpuSurface>(context,surface,framebuffer);initialize_gpu_surface();}
void BlenderHost::initialize_gpu(QOpenGLContext &context,QSurface &surface,std::function<unsigned()> framebuffer){impl_->require_owner();if(impl_->phase!=Impl::Phase::SceneReady)throw std::logic_error("GPU initialization requires scene-ready host");impl_->surface=std::make_unique<QtGpuSurface>(context,surface,std::move(framebuffer));initialize_gpu_surface();}
void BlenderHost::initialize_gpu_surface()
{
  if(!qEnvironmentVariableIsSet("BTS_GPU_STORAGE_STAGING"))qputenv("BTS_GPU_STORAGE_STAGING","1");
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::SceneReady) {
    throw std::logic_error("GPU initialization requires the initialized scene");
  }
  if (GPU_is_init()) {
    throw std::logic_error("Blender initialized its GPU before the Qt host supplied a context");
  }
  const bool shader_workers=qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS")==1;
  install_shared_ghost_context(s.surface->context(),shader_workers);
  s.ghost = std::make_unique<QtGhostContext>(*s.surface);
  U.shader_compilation_method = USER_SHADER_COMPILE_THREAD;
  if (s.ghost->activateDrawingContext() != GHOST_kSuccess) {
    throw std::runtime_error("Could not activate Qt's OpenGL context");
  }
  GPU_backend_type_selection_set(GPU_BACKEND_OPENGL);
  if(shader_workers)WM_init_gpu_hosted_shader_workers(s.ghost.get());
  else WM_init_gpu_hosted(s.ghost.get());
  s.phase = Impl::Phase::GpuReady;
  s.renderer = GPU_platform_renderer();
  s.surface->makeCurrent();
  if(qEnvironmentVariableIntValue("BTS_GL_DEBUG")==1){
    glEnable(GL_DEBUG_OUTPUT);glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback([](GLenum,GLenum type,GLuint id,GLenum,GLsizei,const GLchar *message,const void *){
      if(type==GL_DEBUG_TYPE_ERROR)std::fprintf(stderr,"BTS_GL_ERROR id=%u message=%s\n",id,message);
    },nullptr);
  }
  // Texture-backed programs need Blender's draw-manager owner, not just Qt's
  // current GL context (which sufficed for the shader-only readback warmup).
  s.enter_gpu();
  try{
    // Native source images use these two float RGBA formats. Material browsing
    // must not discover their lazy mip-compute programs during an owner tick.
    // A real 2x2 -> 1x1 dispatch warms both the program and driver pipeline;
    // there is no readback, finish, reduced mip quality or scene mutation.
    for(const auto format:{gpu::TextureFormat::SFLOAT_16_16_16_16,gpu::TextureFormat::SFLOAT_32_32_32_32}){
      const auto begin=std::chrono::steady_clock::now();const auto cpu=performance::thread_ms();
      auto *texture=GPU_texture_create_2d("Native image mip startup",2,2,2,format,GPU_TEXTURE_USAGE_SHADER_READ|GPU_TEXTURE_USAGE_SHADER_WRITE,nullptr);
      if(texture){const float zero[4]{};GPU_texture_clear(texture,GPU_DATA_FLOAT,zero);GPU_texture_update_mipmap_chain(texture);GPU_texture_free(texture);}
      const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
      std::printf("BTS_NATIVE_MIP_STARTUP format=%s shader_write=1 route=builtin-compute available=%d wall_ms=%.3f cpu_ms=%.3f\n",GPU_texture_format_name(format),bool(texture),elapsed,performance::thread_ms()-cpu);
    }
    s.inspection.initialize_gpu_programs();GpuProjectReadback::warmup();if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_GPU_TEST"))verify_native_material_gpu_prototype(*s.main);DRW_gpu_context_disable_ex(false);}
  catch(...){DRW_gpu_context_disable_ex(false);throw;}
  s.arm_program_poll();
  s.surface->makeCurrent();
  std::printf("BTS_GPU_READY pid=%lld renderer=%s qt=%s shader_workers=%d presentation=GPU_blit\n",
              static_cast<long long>(QCoreApplication::applicationPid()), s.renderer.c_str(), qVersion(),
              shader_workers&&!GPU_use_main_context_workaround()?GPU_max_parallel_compilations():0);
  std::fflush(stdout);
}

double BlenderHost::draw(std::span<const ViewFrame> frames, int width, int height,bool defer_material_render)
{
  performance::Scope draw_timing("draw");
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing || width < 1 || height < 1 ||
      frames.empty() || frames.size() > 2)
  {
    throw std::logic_error("Invalid or reentrant native draw");
  }
  s.view->shading.studiolight_background = 0;
  // BKE_scene_graph_update_tagged clears these flags. Latch before any
  // residency snapshot can ensure evaluation and clear the same notification.
  bool native_owner_revision=DEG_id_type_updated(s.depsgraph,ID_OB)||DEG_id_type_updated(s.depsgraph,ID_ME);
  bool native_geometry_revision=native_material_geometry_updated(*s.main,s.depsgraph);
  if(s.inspection.poll_composites(*s.main)){s.displacement_preview.sync_contents(*s.main,*s.depsgraph,[&](auto uid){return s.inspection.displacement_content(uid);});s.texture_dirty=true;s.uv_preview.tag_images_updated();}
  const bool image_changed=s.texture_dirty;s.texture_dirty=false;
  bool has_scene = false, has_uv = false;
  for (const ViewFrame &frame : frames) {
    const ViewPane &pane = frame.pane;
    if (pane.x < 0 || pane.y < 0 || pane.width < 1 || pane.height < 1 ||
        pane.width > width || pane.height > height ||
        pane.x > width - pane.width || pane.y > height - pane.height ||
        !std::all_of(frame.view.begin(), frame.view.end(), [](float v) { return std::isfinite(v); }) ||
        !std::all_of(frame.projection.begin(), frame.projection.end(),
                     [](float v) { return std::isfinite(v); }))
    {
      throw std::invalid_argument("Invalid native viewport rectangle or matrices");
    }
    bool &present = pane.kind == ViewKind::Scene ? has_scene : has_uv;
    if (present) {
      throw std::invalid_argument("A native view can appear only once in a frame");
    }
    present = true;
  }
  s.drawing = true;
  s.last_eevee_completion={};
  s.last_eevee_completion.host_draw=++s.host_draw_serial;
  if(s.surface_graph_test){s.last_surface_graph={};s.last_surface_graph.status=SurfaceGraphStatus::NoSceneDraw;s.last_surface_graph.host_draw=s.host_draw_serial;}
  const auto start = std::chrono::steady_clock::now();
  s.enter_gpu();
  s.performance.begin();
  bool gpu_active = true;
  try {
    for(const auto &frame:frames)if(frame.pane.kind==ViewKind::Scene){const auto matrices=std::pair{frame.view,frame.projection};const bool changed=s.native_view_matrices!=matrices;s.native_view_matrices=matrices;if(changed)s.inspection.refresh_native_view();break;}
    if(qEnvironmentVariableIsSet("BTS_CYCLES_PREVIEW_PROBE")&&s.selected_material&&!STREQ(s.scene->r.engine,"CYCLES"))s.configure_preview(true,false,true);
    if(STREQ(s.scene->r.engine,"CYCLES")){
      s.view->shading.type=OB_RENDER;
      for(const auto &frame:frames)if(frame.pane.kind==ViewKind::Scene){
        auto *rv=static_cast<RegionView3D *>(s.region->regiondata);const auto &p=frame.projection;
        copy_m4_m4(rv->viewmat,reinterpret_cast<const float (*)[4]>(frame.view.data()));invert_m4_m4(rv->viewinv,rv->viewmat);copy_m4_m4(rv->winmat,reinterpret_cast<const float (*)[4]>(p.data()));
        const float aspect=float(frame.pane.width)/frame.pane.height;
        if(std::abs(p[15])<.5f){rv->persp=RV3D_PERSP;s.view->lens=36.f*p[5]/std::max(1.f,aspect);s.view->clip_start=frame.clipping[0]>0?frame.clipping[0]:p[14]/(p[10]-1);s.view->clip_end=frame.clipping[1]>0?frame.clipping[1]:p[14]/(p[10]+1);}
        else{rv->persp=RV3D_ORTHO;s.view->lens=36;rv->dist=std::max(1.f,aspect)/p[5];s.view->clip_start=frame.clipping[0]>0?frame.clipping[0]:.01;s.view->clip_end=frame.clipping[1]>0?2*frame.clipping[1]:4.f/std::abs(p[10]);}
      }
    }
    {performance::Scope prepare_timing("depsgraph_upload");const auto mip_before=gpu::host_metrics::counters[gpu::host_metrics::Mipmap].ns.load();
    std::vector<ResidencyView> residency_views;for(const auto &frame:frames)residency_views.push_back({frame.view,frame.projection,frame.pane.width,frame.pane.height,frame.pane.kind==ViewKind::Uv});
    s.scene->r.anisotropic_filter=16;U.anisotropic_filter=16;
    {performance::Scope timing("depsgraph_residency");s.residency.update(*s.main,residency_views,s.selected_material,[&]{return s.source_snapshot();},s.inspection.updates_pending());}
    const auto property_gpu_start=bts::PropertyClock::now();{performance::Scope timing("depsgraph_staging_upload");s.inspection.upload_staging_images(s.performance.compute_ready(),s.texture_interacting);}
    if(s.inspection.take_displacement_updates()){s.displacement_preview.sync_contents(*s.main,*s.depsgraph,[&](auto uid){return s.inspection.displacement_content(uid);});s.texture_dirty=true;s.uv_preview.tag_images_updated();}
    if(!s.inspection.sync_cycles_images(*s.main,STREQ(s.scene->r.engine,"CYCLES")))defer_material_render=true;
    for(const auto &frame:frames)if(frame.pane.kind==ViewKind::Scene)prepare_display_post_effects(*s.main,*s.scene,*s.view,frame.pane.width,frame.pane.height);
    s.inspection.set_subsurface_enabled(*s.main,subsurface_preview(*s.scene).enabled);
    native_owner_revision|=DEG_id_type_updated(s.depsgraph,ID_OB)||DEG_id_type_updated(s.depsgraph,ID_ME);
    native_geometry_revision|=native_material_geometry_updated(*s.main,s.depsgraph);
    {performance::Scope timing("depsgraph_update_tagged");BKE_scene_graph_update_tagged(s.depsgraph, s.main);}
    if(native_owner_revision||native_geometry_revision)s.inspection.refresh_native_world_objects(native_owner_revision,native_geometry_revision);
    // EEVEE can retain a material pass across frames. Consume queued image regions
    // explicitly on its GPU owner before drawing, without retagging the node tree.
    {performance::Scope timing("depsgraph_image_partial_upload");std::set<Image *> images;
    for(Material &material:s.main->materials){
      auto *evaluated=DEG_get_evaluated(s.depsgraph,&material);
      for(Material *owner:std::set<Material *>{&material,evaluated})if(owner){
        for(LinkData &link:owner->gpumaterial){
          const auto textures=GPU_material_textures(static_cast<GPUMaterial *>(link.data));
          for(auto *texture=static_cast<GPUMaterialTexture *>(textures.first);texture;texture=texture->next){
            if(texture->ima&&STRPREFIX(texture->ima->id.name+2,"BTS ")&&images.insert(texture->ima).second)
              BKE_image_get_gpu_material_texture_try(texture->ima,texture->iuser_available?&texture->iuser:nullptr,texture->tiled_mapping_name[0]);
          }
        }
      }
    }
    }
    s.inspection.finalize_gpu_images(*s.main,s.performance.compute_ready());
    if(performance::enabled()){const auto elapsed=gpu::host_metrics::counters[gpu::host_metrics::Mipmap].ns.load()-mip_before;if(elapsed)performance::record({performance::flow(),"image_mipmap_generate",elapsed/1e6,elapsed/1e6,0,true});}
    if(bts::property_waiting.event)bts::property_waiting.gpu_ms+=bts::property_ms(property_gpu_start);
    }
    const bool prime_uv=!defer_material_render&&s.uv_prime&&!has_uv&&s.selected_material.has_value();
    if(s.uv_preview.shader_rebuild_needed())s.uv_dirty=true;
    if (!defer_material_render&&(has_uv||prime_uv) && s.uv_dirty) {
      // Keep the engine cache and retain predecessor IDs through the new draw.
      // New/Open still free targets before destroying any source Main.
      s.uv_preview.retire();
      s.uv_summary = {};
      if (s.selected_material) {
        s.uv_summary = s.uv_preview.rebuild(
            *s.main, *s.scene, *s.depsgraph, *s.view, *s.selected_material, s.inspection_mode != InspectionMode::Material,&s.displacement_preview.source_meshes());
      }
      s.uv_dirty = false;
      std::printf("BTS_UV_PREVIEW pid=%lld material=%u triangles=%zu objects=%zu\n",
                  static_cast<long long>(QCoreApplication::applicationPid()),
                  s.selected_material.value_or(0), s.uv_summary.uv_triangles,
                  s.uv_summary.mesh_objects);
      std::fflush(stdout);
    }
    if(prime_uv&&s.uv_summary){
      performance::Scope timing("uv_shader_startup_warmup");
      const auto &size=frames.front().pane;
      s.uv_target.ensure_size(size.width,size.height);GPU_offscreen_bind(s.uv_target.offscreen,true);
      float view[4][4],projection[4][4];unit_m4(view);view[3][2]=-5;
      orthographic_m4(projection,s.uv_summary.minimum_uv[0]-.5f,s.uv_summary.maximum_uv[0]+.5f,s.uv_summary.minimum_uv[1]-.5f,s.uv_summary.maximum_uv[1]+.5f,.01f,100.f);
      s.uv_preview.draw(size.width,size.height,view,projection,s.uv_target.offscreen,s.uv_target.viewport,*s.view,int(s.inspection_mode)<=0);
      GPU_offscreen_unbind(s.uv_target.offscreen,true);
    }
    if(!defer_material_render&&(has_uv||prime_uv))s.uv_prime=false;
    std::vector<GLint> source_fbos;
    source_fbos.reserve(frames.size());
    for (const ViewFrame &frame : frames) {
      const ViewPane &pane = frame.pane;
      auto &target = pane.kind == ViewKind::Scene ? s.scene_target : s.uv_target;
      target.ensure_size(pane.width, pane.height);
      if(pane.kind==ViewKind::Scene){
        s.last_eevee_completion.target_generation=target.generation;
        s.last_eevee_completion.width=pane.width;
        s.last_eevee_completion.height=pane.height;
        s.last_eevee_completion.status=defer_material_render?EeveeCompletionStatus::Deferred:EeveeCompletionStatus::OtherBackend;
      }
      GPU_viewport_tag_update(target.viewport);
      GPU_offscreen_bind(target.offscreen, true);
      performance::Scope pane_timing(pane.kind==ViewKind::Scene?"eevee_scene_draw":"uv_scene_draw");
      // A new image-bound material has a temporary USD fallback. Keep its tree
      // intact, but do not compile it immediately before replacing it. Uploads
      // and GPU preparation above continue while the worker decodes resources.
      if(defer_material_render){GPU_framebuffer_clear_color(GPU_framebuffer_active_get(),{0.10,0.10,0.10,1.0});}
      else if (pane.kind == ViewKind::Scene) {
        const auto old_flags=s.view->flag;const auto old_flags2=s.view->flag2;const auto old_grid=s.view->gridflag;const auto old_overlay=s.view->overlay;
        const auto restore_overlay=qScopeGuard([&]{s.view->flag=old_flags;s.view->flag2=old_flags2;s.view->gridflag=old_grid;s.view->overlay=old_overlay;});
        if(frame.wireframe){s.view->flag2&=~V3D_HIDE_OVERLAYS;s.view->flag&=~V3D_SELECT_OUTLINE;s.view->gridflag=eView3D_GridFlag(0);s.view->overlay.flag=(frame.wireframe_triangles?eView3DOverlay_Flag(0):V3D_OVERLAY_WIREFRAMES)|V3D_OVERLAY_HIDE_CURSOR|V3D_OVERLAY_HIDE_TEXT|V3D_OVERLAY_HIDE_BONES|V3D_OVERLAY_HIDE_MOTION_PATHS|V3D_OVERLAY_HIDE_OBJECT_XTRAS|V3D_OVERLAY_HIDE_OBJECT_ORIGINS;s.view->overlay.wireframe_threshold=1;s.view->overlay.wireframe_opacity=.65f;}

        auto *region_view=static_cast<RegionView3D *>(s.region->regiondata);
        struct PaintingHint{RegionView3D &view;decltype(RegionView3D::rflag) before;~PaintingHint(){view.rflag=before;}} hint{*region_view,region_view->rflag};
        // Native paint_stroke.cc uses this flag to reset EEVEE sampling without
        // rebuilding a scene or material whose nodes and geometry did not change.
        if(image_changed)region_view->rflag|=RV3D_PAINTING;
        ED_view3d_draw_offscreen(s.depsgraph, s.scene, STREQ(s.scene->r.engine,"CYCLES")?OB_RENDER:OB_MATERIAL, s.view, s.region, STREQ(s.scene->r.engine,"CYCLES")?s.context:nullptr,
                                pane.width, pane.height,
                                reinterpret_cast<const float (*)[4]>(frame.view.data()),
                                reinterpret_cast<const float (*)[4]>(frame.projection.data()),
                                false, true, "", int(s.inspection_mode)<=0, !STREQ(s.scene->r.engine,"CYCLES"), target.offscreen, target.viewport);
        if(STREQ(s.scene->r.engine,"BLENDER_EEVEE")){
          auto snapshot=detail::capture_eevee_completion(*target.viewport);
          snapshot.host_draw=s.host_draw_serial;
          snapshot.target_generation=target.generation;
          snapshot.width=pane.width;snapshot.height=pane.height;
          s.last_eevee_completion=snapshot;
          if(s.surface_graph_test){
            s.last_surface_graph=detail::capture_surface_graph(*s.main,*s.depsgraph,*s.view_layer,*s.view,s.selected_material.value_or(0));
            s.last_surface_graph.host_draw=s.host_draw_serial;
            s.last_surface_graph.target_generation=target.generation;
          }
        }
        if(frame.wireframe&&qEnvironmentVariableIsSet("BTS_OVERLAY_PROFILE")){
          for(const Base &base:*BKE_view_layer_object_bases_get(s.view_layer)){if(base.object->type!=OB_MESH||!BKE_base_is_visible(s.view,&base))continue;auto *object=DEG_get_evaluated(s.depsgraph,base.object);auto *batch=blender::draw::DRW_cache_mesh_face_wireframe_get(object);if(batch&&batch->verts[0]){auto &prior=s.wire_vbos[base.object->id.session_uid];if(prior!=batch->verts[0]){prior=batch->verts[0];++s.wire_cache_changes;}}}
        }
        s.last_tangent_shader_state=tangent_shader_state();
        if(qEnvironmentVariableIsSet("BTS_CYCLES_PREVIEW_PROBE")){static int count=0;if(++count%60==0){auto *engine=region_view->view_render?RE_view_engine_get(region_view->view_render):nullptr;std::fprintf(stderr,"BTS_CYCLES_FRAME engine=%s info=%s\n",engine?engine->type->idname:"none",engine?engine->text:"");}}
        if(qEnvironmentVariableIsSet("BTS_DISPLAY_POST_DEBUG")){static int post_frame=0;if(++post_frame%60==0){auto *evaluated=DEG_get_evaluated_scene(s.depsgraph);auto *tree=evaluated->compositing_node_group;float factor=-1;if(tree){auto *mix=bke::node_find_node_by_name(*tree,"__BTS_DisplayVignette");auto *socket=mix?bke::node_find_enabled_input_socket(*mix,"Factor"):nullptr;if(socket)factor=static_cast<bNodeSocketValueFloat *>(socket->default_value)->value;}std::fprintf(stderr,"BTS_POST_GRAPH original=%d evaluated=%d shading=%d mode=%d factor=%g\n",s.scene->compositing_node_group!=nullptr,tree!=nullptr,int(s.view->shading.type),int(s.view->shading.use_compositor),factor);}}

      }
      else if (!s.uv_preview.draw(pane.width, pane.height,
                                  reinterpret_cast<const float (*)[4]>(frame.view.data()),
                                  reinterpret_cast<const float (*)[4]>(frame.projection.data()),
                                  target.offscreen, target.viewport, *s.view, int(s.inspection_mode)<=0))
      {
        GPU_framebuffer_clear_color(GPU_framebuffer_active_get(), {0.10, 0.10, 0.10, 1.0});
      }
      if(performance::enabled()){
        const auto counters=GPU_viewport_offscreen_cache_stats(target.viewport);
        if(counters.reuses==1||counters.allocations!=target.logged_allocations)std::printf("BTS_OFFSCREEN_CACHE kind=%s allocations=%llu reuses=%llu width=%d height=%d\n",pane.kind==ViewKind::Scene?"scene":"uv",(unsigned long long)counters.allocations,(unsigned long long)counters.reuses,pane.width,pane.height);
        target.logged_allocations=counters.allocations;
      }
      GPU_offscreen_bind(target.offscreen, false);
      if(pane.kind==ViewKind::Uv&&!defer_material_render)s.uv_overlay.draw(s.uv_summary,frame);
      if(pane.kind==ViewKind::Scene&&frame.wireframe&&frame.wireframe_triangles&&!defer_material_render){
        // ED_view3d_draw_offscreen unbinds the viewport and clears its borrowed
        // depth pointer. The offscreen owns that same rendered depth texture.
        gpu::FrameBuffer *framebuffer=nullptr;gpu::Texture *color=nullptr,*depth=nullptr;
        GPU_offscreen_viewport_data_get(target.offscreen,&framebuffer,&color,&depth);
        std::map<std::uint32_t,std::pair<const gpu::VertBuf *,std::array<float,16>>> geometry;
        for(const Base &base:*BKE_view_layer_object_bases_get(s.view_layer)){
          if(base.object->type!=OB_MESH||!BKE_base_is_visible(s.view,&base))continue;
          auto *object=DEG_get_evaluated(s.depsgraph,base.object);auto *batch=blender::draw::DRW_cache_mesh_face_wireframe_get(object);
          std::array<float,16> transform;std::copy_n(&object->object_to_world()[0][0],16,transform.data());
          geometry[base.object->id.session_uid]={batch?batch->verts[0]:nullptr,transform};
        }
        if(geometry!=s.triangle_geometry||s.triangle_scene.uv_triangles.empty()){
          // Match rendered/evaluated geometry, including preview subdivision;
          // original source meshes remain the authority for picking/export/UV.
          s.triangle_scene=read_scene_snapshot(s.context,s.view);s.triangle_geometry=std::move(geometry);
        }
        auto triangle_frame=frame;triangle_frame.wire_source=&s.triangle_scene;
        s.triangle_overlay.draw(triangle_frame,depth,color);
      }
      GLint source_fbo = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &source_fbo);
      source_fbos.push_back(source_fbo);
      GPU_offscreen_unbind(target.offscreen, true);
    }
    s.residency.finish_frame();
    s.inspection.resume_gpu_channels();
    s.inspection.collect_retired_visibility_images(*s.main);
    s.performance.end();s.performance.submitted();
    DRW_gpu_context_disable_ex(false);
    gpu_active = false;
    s.surface->makeCurrent();
    const GLuint target_fbo = s.surface->defaultFramebufferObject();
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    // Offscreen drawing leaves the final pane's viewport active. Blits ignore it,
    // but Qt's painter may retain its cached full-surface viewport across native
    // painting. Restore actual pixels before returning to the overlay renderer.
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    for (std::size_t index = 0; index < frames.size(); ++index) {
      const ViewPane &pane = frames[index].pane;
      glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(source_fbos[index]));
      glReadBuffer(GL_COLOR_ATTACHMENT0);
      const int bottom = height - pane.y - pane.height;
      glBlitFramebuffer(0, 0, pane.width, pane.height,
                        pane.x, bottom, pane.x + pane.width, bottom + pane.height,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    // Qt's glyph cache uploads QImage rows with the default four-byte alignment.
    // Blender uses one-byte alignment, which otherwise corrupts narrow glyphs.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if(qEnvironmentVariableIsSet("BTS_OVERLAY_PROBE")){
      static int count=0;if(++count<=8){GLint viewport[4],fbo;glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);std::fprintf(stderr,"BTS_OVERLAY_GL viewport=%d,%d,%d,%d fbo=%d expected=%u cull=%d depth=%d stencil=%d blend=%d contours=%zu\n",viewport[0],viewport[1],viewport[2],viewport[3],fbo,target_fbo,int(glIsEnabled(GL_CULL_FACE)),int(glIsEnabled(GL_DEPTH_TEST)),int(glIsEnabled(GL_STENCIL_TEST)),int(glIsEnabled(GL_BLEND)),s.uv_summary.contours?s.uv_summary.contours->size():0);}
    }
    s.drawing = false;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  catch (...) {
    s.last_eevee_completion={};
    s.last_eevee_completion.host_draw=s.host_draw_serial;
    s.last_eevee_completion.status=EeveeCompletionStatus::DrawFailed;
    if(s.surface_graph_test){s.last_surface_graph={};s.last_surface_graph.status=SurfaceGraphStatus::DrawFailed;s.last_surface_graph.host_draw=s.host_draw_serial;}
    if (gpu_active) {
      s.performance.end();
      GPU_framebuffer_restore();
      DRW_gpu_context_disable_ex(false);
    }
    s.surface->makeCurrent();
    s.drawing = false;
    throw;
  }
}

EeveeCompletionSnapshot BlenderHost::eevee_completion_snapshot()const
{
  impl_->require_owner();
  return impl_->last_eevee_completion;
}

SurfaceGraphSnapshot BlenderHost::surface_graph_snapshot()const
{
  impl_->require_owner();
  return impl_->last_surface_graph;
}

std::uint64_t BlenderHost::diagnostic_recreate_scene_viewport()
{
  auto &s=*impl_;
  s.require_owner();
  if(!s.surface_cold_control||!s.surface_graph_test||s.phase!=Impl::Phase::GpuReady||
     s.drawing||!s.scene||!STREQ(s.scene->r.engine,"BLENDER_EEVEE")||
     !s.scene_target.offscreen||!s.scene_target.viewport)
    throw std::logic_error("Cold Scene viewport diagnostic requires explicit idle live EEVEE target");
  if(s.scene_target.generation==std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("Scene target generation exhausted");
  std::uint64_t generation=0;
  with_gpu([&]{
    // Construct first: allocation failure leaves the current target untouched.
    std::unique_ptr<GPUViewport,decltype(&GPU_viewport_free)> fresh(GPU_viewport_create(),GPU_viewport_free);
    if(!fresh)throw std::runtime_error("Cannot allocate cold diagnostic viewport");
    GPU_viewport_offscreen_cache_set(fresh.get(),true);
    auto *previous=s.scene_target.viewport;
    s.scene_target.viewport=fresh.release();
    generation=++s.scene_target.generation;
    // These are unavailable observations until the NEXT actual Scene draw.
    // Keep its prior draw serial as the actor's phase floor, not a new draw.
    s.last_eevee_completion={};s.last_eevee_completion.host_draw=s.host_draw_serial;
    s.last_eevee_completion.target_generation=generation;
    s.last_surface_graph={};s.last_surface_graph.status=SurfaceGraphStatus::NoSceneDraw;
    s.last_surface_graph.host_draw=s.host_draw_serial;s.last_surface_graph.target_generation=generation;
    GPU_viewport_free(previous);
  });
  return generation;
}

void BlenderHost::set_selected_material(std::optional<std::uint32_t> session_uid)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing) {
    throw std::logic_error("Material selection outside an idle live session");
  }
  if (s.selected_material != session_uid) {
    s.selected_material = session_uid;
    s.uv_dirty = true;
  }
}

std::optional<std::uint32_t> BlenderHost::selected_material() const
{
  impl_->require_owner();
  return impl_->selected_material;
}

UvPreviewSummary BlenderHost::uv_preview_summary() const
{
  impl_->require_owner();
  return impl_->uv_summary;
}

void BlenderHost::set_roughness(float roughness)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing) {
    throw std::logic_error("Material edit outside an idle live session");
  }
  int changed = 0;
  for (Material &material : s.main->materials) {
    if (!material.nodetree) {
      continue;
    }
    auto *node = bke::node_find_node_by_name(*material.nodetree, "Principled BSDF");
    if (!node) {
      continue;
    }
    auto *socket = bke::node_find_enabled_input_socket(*node, "Roughness");
    if (!socket || !socket->default_value) {
      continue;
    }
    static_cast<bNodeSocketValueFloat *>(socket->default_value)->value = std::clamp(roughness, 0.0f, 1.0f);
    BKE_ntree_update_tag_socket_property(material.nodetree, socket);
    BKE_ntree_update_after_single_tree_change(*s.main, *material.nodetree);
    DEG_id_tag_update(&material.id, ID_RECALC_SHADING);
    DEG_id_tag_update(&material.nodetree->id, ID_RECALC_NTREE_OUTPUT);
    ++changed;
  }
  if (!changed) {
    throw std::runtime_error("The scene has no editable Principled material");
  }
  s.uv_dirty = true;
  std::printf("BTS_MATERIAL_EDIT pid=%lld roughness=%.3f materials=%d\n",
              static_cast<long long>(QCoreApplication::applicationPid()), roughness, changed);
  std::fflush(stdout);
}

SceneSnapshot BlenderHost::load_bake_source(const std::string &path,const BakeSourceOptions &options){
  auto &s=*impl_;s.require_owner();if(s.phase==Impl::Phase::Closed||s.drawing)throw std::logic_error("Bake source import outside idle native session");
  bts::performance::Scope timing("bake_source_import");const auto started=std::chrono::steady_clock::now();
  options.validate();UsdImportOptions usd;if(options.frame){usd.explicit_frame=true;usd.frame=*options.frame;}
  auto pending=QFileInfo(QString::fromStdString(path)).suffix().compare("abc",Qt::CaseInsensitive)==0?
    PendingMeshProject::from_alembic(*s.main,path,false,{},options.frame):PendingMeshProject::from_scene(*s.main,path,false,{},usd);
  auto result=pending->snapshot();scale_bake_source(result,options.scale);
  std::printf("BTS_BAKE_SOURCE file=%s objects=%zu triangles=%zu scale=%.6g frame=%s bounds=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g) ms=%.3f\n",qPrintable(QFileInfo(QString::fromStdString(path)).fileName()),result.mesh_objects,result.uv_triangles.size(),options.scale,options.frame?std::to_string(*options.frame).c_str():"first",result.minimum[0],result.minimum[1],result.minimum[2],result.maximum[0],result.maximum[1],result.maximum[2],std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());std::fflush(stdout);return result;
}

bool BlenderHost::mesh_export_displacement_available(const TextureDocument &document)const{const auto &s=*impl_;s.require_owner();for(const auto &set:document.texture_sets())if(!set.disabled&&set.displacement.channel&&set.displacement.enabled&&set.displacement.strength>0&&s.inspection.displacement_content(set.material_session_uid))return true;return false;}
std::shared_ptr<NativeMeshExport> BlenderHost::prepare_mesh_export(const TextureDocument &document,bool displaced,bool recompute_normals){auto &s=*impl_;s.require_owner();if(s.phase!=Impl::Phase::GpuReady||s.drawing)throw std::logic_error("Mesh export requires an idle viewport");if(!displaced)return NativeMeshExport::prepare(*s.main,*s.scene,*s.depsgraph,s.view,s.displacement_preview.source_meshes());std::set<std::uint32_t> active;for(const auto &set:document.texture_sets())if(!set.disabled&&set.displacement.channel&&set.displacement.enabled&&set.displacement.strength>0&&s.inspection.displacement_content(set.material_session_uid))active.insert(set.material_session_uid);const auto geometry=s.source_snapshot();return NativeMeshExport::prepare(*s.main,*s.scene,*s.depsgraph,s.view,s.displacement_preview.source_meshes(),&document,&geometry,active,recompute_normals);}

QJsonObject BlenderHost::overlay_profile()const{auto &s=*impl_;s.require_owner();auto result=s.uv_overlay.stats();const auto triangle=s.triangle_overlay.stats();for(auto i=triangle.begin();i!=triangle.end();++i)result[i.key()]=i.value();result["native_wire_vbos"]=qint64(s.wire_vbos.size());result["native_wire_cache_changes"]=qint64(s.wire_cache_changes);result["uv_contour_edges"]=qint64(s.uv_summary.contours?s.uv_summary.contours->size():0);result["uv_wire_edges"]=qint64(s.uv_summary.wireframe?s.uv_summary.wireframe->size():0);result["uv_triangles"]=qint64(s.uv_summary.uv_triangles);return result;}

SceneSnapshot BlenderHost::scene_snapshot()
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing) {
    throw std::logic_error("Scene inspection outside an idle live session");
  }
  const bool gpu_ready = s.phase == Impl::Phase::GpuReady;
  if (gpu_ready) {
    s.enter_gpu();
  }
  try {
    SceneSnapshot snapshot = s.source_snapshot();
    snapshot.preview_subdivision_levels=s.displacement_preview.material_levels();
    if (gpu_ready) {
      DRW_gpu_context_disable_ex(false);
      s.surface->makeCurrent();
    }
    return snapshot;
  }
  catch (...) {
    if (gpu_ready) {
      DRW_gpu_context_disable_ex(false);
      s.surface->makeCurrent();
    }
    throw;
  }
}

std::optional<SurfaceBrushSample> BlenderHost::surface_stamp(std::uint32_t selected,
    const std::array<float,3> &origin,const std::array<float,3> &direction,float diameter,bool coverage,bool square,const BrushProjection &projection)
{
  auto &s=*impl_;s.require_owner();
  if (s.phase==Impl::Phase::Closed || s.drawing) throw std::logic_error("Surface hit outside idle session");
  return surface_brush_stamp(*s.main,*s.scene,*s.depsgraph,*s.view,selected,origin,direction,diameter,coverage,square,projection,&s.displacement_preview.source_meshes(),s.surface_cache.get());
}

void BlenderHost::prepare_warp_projections(const TextureSet &set)
{
 auto &s=*impl_;s.require_owner();
 const auto needs=[](const TextureLayer &layer){return (layer.kind==LayerKind::Fill&&warp_geometry_enabled(layer.fill))||(layer.mask&&any_mask_effect(layer.mask->effects,[](const auto &effect){return effect.kind==MaskEffectKind::Fill&&effect.anchor_source.isNull()&&warp_geometry_enabled(effect.mapping.projection);}));};
 if(!std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),needs))return;
 if(s.drawing||s.phase==Impl::Phase::Closed)throw std::logic_error("Warp geometry fitting requires an idle scene");
 if(!s.warp_bounds_ready){const auto geometry=s.source_snapshot();s.warp_extent=0;for(int c=0;c<3;++c){s.warp_center[c]=(double(geometry.minimum[c])+geometry.maximum[c])*.5;s.warp_extent=std::max(s.warp_extent,double(geometry.maximum[c]-geometry.minimum[c]));}if(s.warp_extent<1e-6)s.warp_extent=1;s.warp_bounds_ready=true;}
 const auto prepare=[&](const FillProjection &projection){
   if(!warp_geometry_enabled(projection))return;auto p=projection;p.cm_per_native=set.cm_per_native;p.native_per_uv=set.native_per_uv;
   if(warp_fit_pose_matches(p)&&p.warp_fit->generation==s.warp_generation&&p.warp_fit->material==set.material_session_uid&&p.warp_fit->reach==std::max(s.warp_extent*2,std::sqrt(warp_dot(p.warp_fit->frame.axes[1],p.warp_fit->frame.axes[1]))*(p.warp_depth+1)))return;
   const auto author=warp_authored_grid(p);const auto frame=warp_projection_frame(p,s.warp_center,s.warp_extent);
   const double reach=std::max(s.warp_extent*2,std::sqrt(warp_dot(frame.axes[1],frame.axes[1]))*(p.warp_depth+1));
   for(const auto &cache:s.warp_caches)if(cache->generation==s.warp_generation&&cache->material==set.material_session_uid&&cache->authored==author&&cache->frame.origin==frame.origin&&cache->frame.axes==frame.axes&&cache->reach==reach){projection.warp_fit=cache;return;}
   const auto start=std::chrono::steady_clock::now();const auto cpu=std::clock();auto cache=std::make_shared<WarpGeometryCache>();cache->authored=author;cache->frame=frame;cache->center=s.warp_center;cache->extent=s.warp_extent;cache->reach=reach;cache->generation=s.warp_generation;cache->material=set.material_session_uid;cache->offset=p.offset3;cache->rotation=p.rotation3;cache->scale=p.scale3;cache->physical=p.physical;cache->physical_size=fill_physical_size(p);cache->cm_per_native=p.cm_per_native;
   BrushProjection ray_projection;ray_projection.cull=false;ray_projection.symmetry_ray=true;
   cache->fitted=fit_warp_geometry(*author,frame,reach,[&](const WarpVector &origin,const WarpVector &direction,double maximum)->std::optional<WarpVector>{std::array<float,3> from,to;for(int c=0;c<3;++c){from[c]=float(origin[c]);to[c]=float(direction[c]);}const auto hit=surface_brush_stamp(*s.main,*s.scene,*s.depsgraph,*s.view,set.material_session_uid,from,to,.001f,false,false,ray_projection,&s.displacement_preview.source_meshes());if(!hit)return {};WarpVector point;for(int c=0;c<3;++c)point[c]=hit->position[c];const auto delta=warp_add(point,origin,-1);if(warp_dot(delta,direction)>maximum)return {};return point;});
   std::printf("BTS_WARP_FIT_MS material=%u nodes=%zu hits=%d misses=%d wall_ms=%.3f cpu_ms=%.3f\n",set.material_session_uid,author->nodes.size(),cache->fitted.hits,cache->fitted.misses,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),1000.*double(std::clock()-cpu)/CLOCKS_PER_SEC);
   if(s.warp_caches.size()>=128)s.warp_caches.erase(s.warp_caches.begin());s.warp_caches.push_back(cache);projection.warp_fit=std::move(cache);
 };
 // Instance evaluations are shared immutable snapshots. Clone before installing
 // consumer-specific fit caches so existing background jobs retain their grids.
 const auto prepare_layer=[&](const TextureLayer &layer){if(layer.kind==LayerKind::Fill)prepare(layer.fill);if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Fill&&effect.anchor_source.isNull())prepare(effect.mapping.projection);};
 for(const auto &layer:set.layers)if(!layer.instance)prepare_layer(layer);
 if(set.instance_layers){auto instances=std::make_shared<std::vector<TextureLayer>>(*set.instance_layers);for(const auto &layer:*instances)prepare_layer(layer);set.instance_layers=std::move(instances);}
}

void BlenderHost::change_texture_residency(const TextureSet &set){
 auto &s=*impl_;s.require_owner();if(s.drawing)throw std::logic_error("Residency requires an idle viewport");
 s.enter_gpu();try{s.inspection.change_residency(*s.main,set);s.uv_dirty=true;s.texture_dirty=true;s.uv_preview.tag_images_updated();DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}
}
void BlenderHost::apply_texture_set(const TextureSet &set,int resolution,PaintPixels::Bounds bounds,bool interactive)
{
  // All workspace callers share the GPU path, including first-use, resize and
  // reimport. CPU evaluation is reserved for explicit export/diagnostic APIs.
  if(set.disabled||!set.uniform_editable)return;
  auto &s=*impl_;s.require_owner();
  if(s.phase==Impl::Phase::Closed||s.drawing)throw std::logic_error("Paint update outside idle session");
  prepare_paint_texture(set);
  if(!s.inspection.queue_layer_edit(set,complete_composite_channels,!interactive)){
    s.inspection.update_pixels(*s.main,set,set.texture_resolution,{},true,true);
    if(!s.inspection.queue_layer_edit(set,complete_composite_channels,!interactive))throw std::runtime_error("GPU texture-set allocation failed");
  }
  if(set.displacement.allocated)s.displacement_preview.apply(*s.main,*s.depsgraph,set,s.inspection.displacement_content(set.material_session_uid));
  const InspectionMaterial material{set.material_session_uid,set.base_values,{},s.selected_material==set.material_session_uid&&set.selected_mask?&set:nullptr,set.texture_resolution};
  s.uv_dirty|=s.inspection.update(*s.main,s.inspection_mode,std::span(&material,1));
  s.texture_dirty=true;s.uv_preview.tag_images_updated();
}

void BlenderHost::preview_displacement(const TextureSet &set){auto &s=*impl_;s.require_owner();s.inspection.preview_displacement(*s.main,set);s.displacement_preview.apply(*s.main,*s.depsgraph,set,s.inspection.displacement_content(set.material_session_uid));s.texture_dirty=true;s.uv_preview.tag_images_updated();}
QJsonObject BlenderHost::displacement_preview_state(){auto &s=*impl_;s.require_owner();BKE_scene_graph_evaluated_ensure(s.depsgraph,s.main);return s.displacement_preview.state(*s.main,*s.depsgraph);}
bool BlenderHost::preview_layer_edit(const TextureSet &source,CompositeChannels channels,bool finalize){const auto set=resident_texture_view(source);performance::Scope timing("preview_layer_edit");auto &s=*impl_;s.require_owner();prepare_warp_projections(set);if(!s.inspection.queue_layer_edit(set,channels,finalize))return false;if(channels[16])s.displacement_preview.apply(*s.main,*s.depsgraph,set,s.inspection.displacement_content(set.material_session_uid));s.texture_dirty=true;s.uv_preview.tag_images_updated();return true;}
NativeSourceEditBatch BlenderHost::prepare_native_source_edits(std::span<const NativeSourceEdit> edits){
 auto &s=*impl_;s.require_owner();if(s.phase==Impl::Phase::Closed||s.drawing)throw std::logic_error("Native source publication requires an idle owner");
 // No warp, shader/topology, displacement or UV rebinding on this source-only
 // boundary. Normal frame publication handles the resulting image updates.
 auto batch=std::make_shared<NativeSourceEditBatch>(s.inspection.prepare_native_source_edits(edits));
 return NativeSourceEditBatch([&s,batch]()noexcept{batch->commit();s.texture_dirty=true;});
}
QJsonObject BlenderHost::diagnostic_native_source_queues()const{impl_->require_owner();return impl_->inspection.diagnostic_native_source_queues();}
QJsonObject BlenderHost::native_world_normal_status(std::uint32_t uid)const{impl_->require_owner();return impl_->inspection.native_world_normal_status(uid);}
QJsonArray BlenderHost::diagnostic_native_world_normal_samples(std::uint32_t uid,bool edge){auto &s=*impl_;s.require_owner();if(!qEnvironmentVariableIsSet("BTS_NATIVE_WORLD_NORMAL_UI"))throw std::logic_error("Native world probe is diagnostic-only");s.enter_gpu();try{auto result=s.inspection.diagnostic_native_world_normal_samples(uid,edge);DRW_gpu_context_disable_ex(false);return result;}catch(...){DRW_gpu_context_disable_ex(false);throw;}}
void BlenderHost::preview_material_roughness(const TextureSet &set){auto &s=*impl_;s.require_owner();if(!s.inspection.preview_roughness(*s.main,set))s.inspection.queue_composite(set);s.texture_dirty=true;s.uv_preview.tag_images_updated();}
void BlenderHost::set_shader_parameters(const TextureSet &set){auto &s=*impl_;s.require_owner();if(s.drawing||s.phase==Impl::Phase::Closed)throw std::logic_error("Shader change requires an idle viewport");if(set.disabled||!set.uniform_editable)return;if(s.inspection.shader_parameters(*s.main,set)){s.texture_dirty=true;s.uv_dirty=true;}}
QJsonObject BlenderHost::shader_parameter_state()const{auto &s=*impl_;s.require_owner();QJsonObject result;for(Material &material:s.main->materials)if(material.nodetree){QJsonObject parameters;for(auto entry:std::array{std::array<const char *,3>{"normal_intensity","__BTS_PaintNormal","Strength"},std::array<const char *,3>{"ao_intensity","__BTS_AOMaterial","Factor"}}){auto *node=bke::node_find_node_by_name(*material.nodetree,entry[1]);auto *socket=node?bke::node_find_enabled_input_socket(*node,entry[2]):nullptr;if(socket&&socket->default_value)parameters[entry[0]]=static_cast<bNodeSocketValueFloat *>(socket->default_value)->value;}if(auto *weight=bke::node_find_node_by_name(*material.nodetree,"__BTS_SubsurfaceWeight")){auto *scale=bke::node_find_node_by_name(*material.nodetree,"__BTS_SubsurfaceScale");auto *radius=bke::node_find_node_by_name(*material.nodetree,"__BTS_SubsurfaceRadius");auto *surface=native_principled_surface(material);const auto number=[](bNode *node,int index){if(!node)return -1.f;for(auto &socket:node->inputs)if(index--==0)return static_cast<bNodeSocketValueFloat *>(socket.default_value)->value;return -1.f;};parameters["subsurface"]=QJsonObject{{"gate",number(weight,1)},{"scale",number(scale,0)},{"radius",QJsonArray{number(radius,0),number(radius,1),number(radius,2)}},{"method",surface?surface->custom2:-1}};}if(auto *bump=bke::node_find_node_by_name(*material.nodetree,"__BTS_PaintHeight")){auto *distance=bke::node_find_enabled_input_socket(*bump,"Distance");if(distance&&distance->default_value)parameters["bump_distance"]=static_cast<bNodeSocketValueFloat *>(distance->default_value)->value;}parameters["openpbr"]=openpbr_shader_state(material);if(qEnvironmentVariableIsSet("BTS_OPENPBR_IMPORT_DEMO")){auto *properties=IDP_GetProperties(&material.id);auto *source=properties?IDP_GetPropertyFromGroup(properties,"bts_imported_openpbr"):nullptr;if(source&&source->type==IDP_STRING)parameters["imported_source"]=QJsonDocument::fromJson(QByteArray(IDP_string_get(source))).object();}if(qEnvironmentVariableIsSet("BTS_OPENPBR_DEMO"))parameters["openpbr_idempotent"]=s.inspection.openpbr_reapply_is_stable(*s.main,material.id.session_uid);result[QString::fromUtf8(material.id.name+2)]=parameters;}return result;}
void BlenderHost::preview_material_color(const TextureSet &set){auto &s=*impl_;s.require_owner();if(!s.inspection.preview_color(*s.main,set)){CompositeChannels channels{};channels[0]=true;if(!s.inspection.queue_layer_edit(set,channels))s.inspection.queue_composite(set);}s.texture_dirty=true;s.uv_preview.tag_images_updated();}
void BlenderHost::prepare_paint_texture(const TextureSet &source){const auto set=resident_texture_view(source);auto &s=*impl_;s.require_owner();prepare_warp_projections(set);if(s.inspection.shader_parameters(*s.main,set))s.uv_preview.tag_images_updated();if(!s.inspection.has_padding(set.material_session_uid))s.inspection.set_padding_geometry(*s.main,set.material_session_uid,s.source_snapshot());if(!s.inspection.has_pixels(set.material_session_uid)){s.inspection.prepare_pixels(*s.main,set);s.uv_dirty=true;}}
QString BlenderHost::viewport_texture_status()const{
 auto &s=*impl_;s.require_owner();if(s.inspection.gpu_programs_pending())return "  |  Preparing GPU layer and mask shaders…";QStringList parts;const auto images=s.residency.snapshot()["images"].toArray();for(const auto &entry:images){const auto value=entry.toObject();if(!value["shader_bound"].toBool(true))continue;if(s.selected_material&&std::uint32_t(value["material_uid"].toDouble())!=*s.selected_material)continue;auto name=value["image"].toString();if(name.startsWith("BTS Composite ")){name=name.mid(14);name=name.mid(name.indexOf(' ')+1);}else if(name.startsWith("BTS Edit Preview ")){static const QStringList channels{"Base Color","Metallic","Roughness","Normal","Bump","AO","Authored Normal","User0","User1","User2","User3","User4","User5","User6","User7","Mask","Displacement"};name=channels.value(name.section(' ',4,4).toInt(),"Texture")+" (preview)";}else if(name=="BTS Complete Ambient Occlusion")name="AO";else if(name=="BTS Displacement")name="Displacement";else continue;const int size=std::min(value["sampling_cap"].toInt(),std::max(value["width"].toInt(),value["height"].toInt()));parts.append(name+" "+(size?QString::number(size):QString("loading")));}const auto residency=s.residency.snapshot();for(const auto *key:{"warning","memory_warning"})if(!residency[key].toString().isEmpty())parts.append(residency[key].toString());return parts.empty()?QString{}:QString("  |  Textures: ")+parts.join(", ");
}
bool BlenderHost::viewport_redraw_pending()const{
 auto &s=*impl_;s.require_owner();s.arm_program_poll();if(s.inspection.cycles_images_pending()||s.inspection.updates_pending(!s.texture_interacting)||s.residency.snapshot()["refining"].toBool())return true;
 if(STREQ(s.scene->r.engine,"CYCLES")){const auto state=render_state();if(state["image_mirror_paused"].toBool())return false;const auto status=state["status"].toString();return !status.contains("Done",Qt::CaseInsensitive)&&!status.contains("Finished",Qt::CaseInsensitive);}
 return false;
}
void BlenderHost::set_texture_interaction(bool active){impl_->require_owner();impl_->texture_interacting=active;}
void BlenderHost::diagnostic_adjustment_budget(std::optional<std::uint64_t> limit){impl_->require_owner();impl_->inspection.diagnostic_adjustment_budget(limit);}
std::shared_ptr<const AdjustmentPaintSnapshot> BlenderHost::begin_adjustment_source(const TextureSet &set,const QUuid &layer,const QUuid &document){impl_->require_owner();return impl_->inspection.begin_adjustment_source(set,layer,document);}
TextureSet BlenderHost::prepare_layer_capture(const TextureSet &set)const{impl_->require_owner();return impl_->inspection.prepare_layer_capture(set);}
std::uint64_t BlenderHost::request_layer_capture(const TextureSet &set,const LayerCaptureRequest &request){impl_->require_owner();return impl_->inspection.request_layer_capture(set,request);}
std::optional<LayerCaptureResult> BlenderHost::layer_capture_result()const{impl_->require_owner();return impl_->inspection.layer_capture_result();}
std::uint64_t BlenderHost::request_thumbnail(const TextureSet &set,const ThumbnailRequest &request){impl_->require_owner();return impl_->inspection.request_thumbnail(set,request);}
std::optional<ThumbnailMetadata> BlenderHost::thumbnail_result()const{impl_->require_owner();return impl_->inspection.thumbnail_result();}
std::uint64_t BlenderHost::request_material_sample(const TextureSet &set,const MaterialSampleRequest &request){impl_->require_owner();return impl_->inspection.request_material_sample(set,request);}
std::optional<MaterialSample> BlenderHost::material_sample_result()const{impl_->require_owner();return impl_->inspection.material_sample_result();}
std::uint64_t BlenderHost::request_histogram(const TextureSet &set,const HistogramRequest &request){impl_->require_owner();return impl_->inspection.request_histogram(set,request);}
std::optional<HistogramMetadata> BlenderHost::histogram_result(const QUuid &id)const{impl_->require_owner();return impl_->inspection.histogram_result(id);}
bool BlenderHost::prepare_texture_resources(const std::vector<std::shared_ptr<const FillTexture>> &textures){impl_->require_owner();return impl_->inspection.prepare_texture_resources(textures);}
bool BlenderHost::claim_texture_preparation(const QUuid &owner){impl_->require_owner();return impl_->inspection.claim_texture_preparation(owner);}
bool BlenderHost::prepare_texture_resources(const QUuid &owner,const std::vector<std::shared_ptr<const FillTexture>> &textures){impl_->require_owner();return impl_->inspection.prepare_texture_resources(owner,textures);}
bool BlenderHost::cancel_texture_preparation(const QUuid &owner){impl_->require_owner();return impl_->inspection.cancel_texture_preparation(owner);}
std::map<std::uint32_t,QJsonObject> BlenderHost::deferred_imported_materials()const{auto &s=*impl_;s.require_owner();std::map<std::uint32_t,QJsonObject> result;for(Material &material:s.main->materials){auto *properties=IDP_GetProperties(&material.id);auto *source=properties?IDP_GetPropertyFromGroup(properties,"bts_imported_openpbr"):nullptr;if(!source||source->type!=IDP_STRING)continue;const auto metadata=QJsonDocument::fromJson(QByteArray(IDP_string_get(source))).object();if(!metadata["converted"].toBool()&&!metadata["images"].toObject().isEmpty())result.emplace(material.id.session_uid,metadata);}return result;}
void BlenderHost::adopt_imported_material(const TextureSet &set,float bump_distance,const SceneSnapshot &geometry){
 auto &s=*impl_;s.require_owner();if(!std::isfinite(bump_distance)||bump_distance<0)throw std::invalid_argument("Invalid imported Bump Distance");Material *material=nullptr;for(Material &candidate:s.main->materials)if(candidate.id.session_uid==set.material_session_uid){material=&candidate;break;}if(!material)throw std::invalid_argument("Imported material disappeared");auto *properties=IDP_GetProperties(&material->id);auto *source=properties?IDP_GetPropertyFromGroup(properties,"bts_imported_openpbr"):nullptr;if(!source||source->type!=IDP_STRING)throw std::invalid_argument("Imported source metadata is missing");auto metadata=QJsonDocument::fromJson(QByteArray(IDP_string_get(source))).object();auto *previous=material->nodetree;std::vector<Image *> foreign_images;if(previous)for(bNode &node:previous->nodes)if(node.id&&GS(node.id->name)==ID_IM)foreign_images.push_back(reinterpret_cast<Image *>(node.id));
 s.enter_gpu();try{GPU_material_free(&material->gpumaterial);material->nodetree=nullptr;nodes::node_tree_shader_default(s.context,s.main,&material->id);write_material_values(*s.main,*material,set.base_values);s.inspection.set_padding_geometry(*s.main,set.material_session_uid,geometry);s.inspection.prepare_pixels(*s.main,set);auto *bump=bke::node_find_node_by_name(*material->nodetree,"__BTS_PaintHeight");auto *distance=bump?bke::node_find_enabled_input_socket(*bump,"Distance"):nullptr;if(!distance)throw std::runtime_error("Imported Bump node is missing");static_cast<bNodeSocketValueFloat *>(distance->default_value)->value=bump_distance;s.inspection.shader_parameters(*s.main,set);metadata["converted"]=true;metadata["activate"]=true;metadata["reason"]="Imported as GPU fill resources";const auto json=QJsonDocument(metadata).toJson(QJsonDocument::Compact);IDP_FreeFromGroup(properties,source);IDP_AddToGroup(properties,IDP_NewString(json.constData(),"bts_imported_openpbr"));DEG_id_tag_update_ex(s.main,&material->id,ID_RECALC_SHADING|ID_RECALC_SYNC_TO_EVAL);s.texture_dirty=true;s.uv_dirty=true;s.uv_preview.tag_images_updated();DRW_gpu_context_disable_ex(false);}catch(...){s.inspection.discard_binding(set.material_session_uid);if(material->nodetree&&material->nodetree!=previous){bke::node_tree_free_embedded_tree(material->nodetree);MEM_delete(material->nodetree);}material->nodetree=previous;DRW_gpu_context_disable_ex(false);throw;}
 // The previous tree remains available throughout the fallible adoption transaction.
 if(previous){bke::node_tree_free_embedded_tree(previous);MEM_delete(previous);}BKE_main_id_refcount_recompute(s.main,false);for(auto *image:foreign_images)if(image->id.us==0){BKE_image_free_buffers(image);BKE_image_free_packedfiles(image);}
}
bool BlenderHost::prepare_texture_resource(const std::shared_ptr<const FillTexture> &texture){impl_->require_owner();return impl_->inspection.prepare_texture_resource(texture);}
void BlenderHost::cancel_texture_preparation(){impl_->require_owner();impl_->inspection.cancel_texture_preparation();}
bool BlenderHost::texture_updates_pending() const{impl_->require_owner();impl_->arm_program_poll();return impl_->inspection.updates_pending();}
void BlenderHost::flush_texture_updates(){auto &s=*impl_;s.require_owner();
 s.enter_gpu();try{s.inspection.upload_staging_images(true,false);s.inspection.finalize_gpu_images(*s.main);DRW_gpu_context_disable_ex(false);}catch(...){DRW_gpu_context_disable_ex(false);throw;}
 if(s.inspection.poll_composites(*s.main,true)){s.displacement_preview.sync_contents(*s.main,*s.depsgraph,[&](auto uid){return s.inspection.displacement_content(uid);});s.texture_dirty=true;s.uv_preview.tag_images_updated();}}
void BlenderHost::finish_texture_set(const TextureSet &set){auto &s=*impl_;s.require_owner();
 // Release may quantize the working stroke. Publish that final revision to the
 // GPU as well as the export/packing worker, without invalidating the last dab.
 if(!s.inspection.queue_layer_edit(set,complete_composite_channels,true))s.inspection.queue_composite(set);
 s.texture_dirty=true;s.uv_preview.tag_images_updated();
}

std::vector<unsigned char> BlenderHost::texture_coverage(std::uint32_t uid,int tile) const {impl_->require_owner();return impl_->inspection.uv_coverage(uid,tile);}
std::vector<ExportTile> BlenderHost::export_source(const TextureDocument &document,const std::vector<QUuid> &selected){
  auto &s=*impl_;s.require_owner();if(s.phase==Impl::Phase::Closed||s.drawing)throw std::logic_error("Export outside idle session");
  std::uint64_t bytes=0;
  for(const auto &set:document.texture_sets())if(!set.disabled&&std::find(selected.begin(),selected.end(),set.id)!=selected.end())bytes+=set.evaluation_texels()*16*(set.ao_channel||set.mesh_maps[3]?2:1);

  std::map<std::uint32_t,float> bump_distances;for(Material &material:s.main->materials)if(material.nodetree){auto *bump=bke::node_find_node_by_name(*material.nodetree,"__BTS_PaintHeight");if(!bump)bump=bke::node_find_node_by_name(*material.nodetree,"__BTS_PaintBump");auto *distance=bump?bke::node_find_enabled_input_socket(*bump,"Distance"):nullptr;if(distance&&distance->default_value)bump_distances[material.id.session_uid]=static_cast<bNodeSocketValueFloat *>(distance->default_value)->value;}
  std::vector<ExportTile> result;
  for(const auto &set:document.texture_sets())if(!set.disabled&&std::find(selected.begin(),selected.end(),set.id)!=selected.end()){
    prepare_warp_projections(set);
    const int resolution=set.texture_resolution;
    if(!s.inspection.has_padding(set.material_session_uid))s.inspection.set_padding_geometry(*s.main,set.material_session_uid,s.source_snapshot());
    (void)resolution;
    for(int number:set.tiles){ExportTile tile{set,number,set.width(number)};tile.height=set.rows(number);if(const auto distance=bump_distances.find(set.material_session_uid);distance!=bump_distances.end())tile.bump_distance=distance->second;tile.deferred_padding=s.inspection.export_padding(set.material_session_uid);result.push_back(std::move(tile));}
  }
  return result;
}

bool BlenderHost::has_texture_pixels(std::uint32_t uid) const{impl_->require_owner();return impl_->inspection.has_pixels(uid);}
std::vector<float> BlenderHost::texture_pixels(std::uint32_t uid,int channel,int tile) const{impl_->require_owner();return impl_->inspection.copy_pixels(uid,channel,tile);}
std::uint64_t BlenderHost::texture_hash(std::uint32_t uid,int channel,int tile) const {impl_->require_owner();return impl_->inspection.image_hash(uid,channel,tile);}
double BlenderHost::verify_gpu_texture_stack(std::uint32_t uid) const {
 auto &s=*impl_;s.require_owner();s.enter_gpu();try{const auto error=s.inspection.verify_gpu_pixels(uid);DRW_gpu_context_disable_ex(false);return error;}catch(...){DRW_gpu_context_disable_ex(false);throw;}
}
std::vector<std::array<float,4>> BlenderHost::diagnostic_gpu_pixels(std::uint32_t uid,int channel,std::span<const std::array<int,2>> coordinates,int tile)const{
 auto &s=*impl_;s.require_owner();s.enter_gpu();try{auto values=s.inspection.diagnostic_gpu_pixels(uid,channel,coordinates,tile);DRW_gpu_context_disable_ex(false);return values;}catch(...){DRW_gpu_context_disable_ex(false);throw;}
}
std::optional<std::array<float,4>> BlenderHost::diagnostic_gpu_pixel(std::uint32_t uid,int channel,int x,int y,int tile)const{
 auto &s=*impl_;s.require_owner();s.enter_gpu();try{auto value=s.inspection.diagnostic_gpu_pixel(uid,channel,x,y,tile);DRW_gpu_context_disable_ex(false);return value;}catch(...){DRW_gpu_context_disable_ex(false);throw;}
}
std::array<float,4> BlenderHost::sample_texture(std::uint32_t uid,int channel,int x,int y,int tile) const {
  impl_->require_owner();return impl_->inspection.sample_pixel(uid,channel,x,y,tile);
}

void BlenderHost::set_material_values(std::uint32_t session_uid, const MaterialValues &values)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing) throw std::logic_error("Material edit outside idle session");
  for (Material &material : s.main->materials) {
    if (material.id.session_uid == session_uid) {
      write_material_values(*s.main, material, values);
      s.uv_dirty = true;
      std::printf("BTS_MATERIAL_EDIT pid=%lld roughness=%.3f metallic=%.3f materials=1 session_uid=%u\n",
          static_cast<long long>(QCoreApplication::applicationPid()), values.roughness, values.metallic, session_uid);
      std::fflush(stdout);
      return;
    }
  }
  throw std::runtime_error("The material no longer belongs to this project");
}

void BlenderHost::set_material_roughness(std::uint32_t session_uid, float roughness)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing) {
    throw std::logic_error("Material edit outside an idle live session");
  }
  if (session_uid == 0) {
    throw std::invalid_argument("No material is assigned to this texture set.");
  }
  for (Material &material : s.main->materials) {
    if (material.id.session_uid != session_uid) {
      continue;
    }
    write_material_roughness(*s.main, material, roughness);
    s.uv_dirty = true;
    std::printf("BTS_MATERIAL_EDIT pid=%lld roughness=%.3f materials=1 session_uid=%u\n",
                static_cast<long long>(QCoreApplication::applicationPid()),
                std::clamp(roughness, 0.0f, 1.0f), session_uid);
    std::fflush(stdout);
    return;
  }
  throw std::out_of_range("The selected material is no longer in the project.");
}

void BlenderHost::shutdown()
{
  auto &s = *impl_;
  s.require_owner();
  s.program_poll_stopping=true;s.program_poll.stop();
  if (s.phase == Impl::Phase::Closed) {
    return;
  }
  shutdown_browser_previews(); // Native IMB/BLO workers must finish before WM_exit_ex.
  if(s.native_preview){s.native_preview->shutdown();s.native_preview.reset();}
  for(auto &job:s.asset_scan_jobs)job.cancel->request_stop();
  for(auto &job:s.asset_scan_jobs)job.result.wait();
  s.asset_scan_jobs.clear();
  for(auto &job:s.material_prepare_jobs)job.cancel->store(true);
  for(auto &job:s.material_prepare_jobs)job.result.wait();
  s.material_prepare_jobs.clear();
  for(auto &job:s.material_graph_jobs)job.cancel->store(true);
  for(auto &job:s.material_graph_jobs)job.result.wait();
  s.material_graph_jobs.clear();
  for(auto &job:s.material_import_jobs)job.cancel->store(true);
  for(auto &job:s.material_import_jobs)job.result.wait();
  s.material_import_jobs.clear();
  while(save_project_pending()){poll_save_project();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  if(s.mask_preset){s.enter_gpu();s.mask_preset.reset();DRW_gpu_context_disable_ex(false);}
  if(s.material_preset){s.enter_gpu();s.material_preset.reset();DRW_gpu_context_disable_ex(false);}
  if(s.parameter_export){s.enter_gpu();s.parameter_export->cancel();s.parameter_export.reset();DRW_gpu_context_disable_ex(false);}
  // Source Image aliases release their GPU references before their Main dies.
  // Uninitialized/CPU-only inspections have no GPU ownership to retire.
  if(s.phase==Impl::Phase::GpuReady){
    s.enter_gpu();
    try{s.inspection.reset(true);DRW_gpu_context_disable_ex(false);}
    catch(...){DRW_gpu_context_disable_ex(false);throw;}
  }else s.inspection.reset(true); // Join private CPU jobs before native globals die.
  if(s.main)s.displacement_preview.reset(*s.main);
  if (s.phase == Impl::Phase::GpuReady) {
    s.enter_gpu();
    GpuLayerPreview::collect_retired(true); // Terminal cancellation before prepared-info/GPU teardown.
    GpuProjectReadback::free_program();
    s.performance.free();
    s.free_targets();
    GPU_finish();
    DRW_gpu_context_disable_ex(false);
  }
  WM_exit_ex(s.context, true, false);
  // WM_exit/GPU_exit joins compiler workers first. Their disposal never waits
  // on this GUI thread; retire the released Qt surfaces before dropping root.
  drain_shared_shader_contexts();
  s.context = nullptr;
  s.phase = Impl::Phase::Closed;
  install_shared_ghost_context(nullptr);
  s.ghost.reset();
  std::printf("BTS_SHUTDOWN clean pid=%lld\n", static_cast<long long>(QCoreApplication::applicationPid()));
  std::fflush(stdout);
}

std::string BlenderHost::renderer() const
{
  return impl_->renderer;
}

}  // namespace bts

std::vector<bts::StudioAsset> bts::BlenderHost::project_resources() const {impl_->require_owner();return read_project_assets(*impl_->scene);}
