// SPDX-License-Identifier: GPL-3.0-or-later
// The Qt GHOST context and struct BlenderHost::Impl: Blender's Main, the scene, the viewport and the queues.
// A section of native/host/BlenderHost.cc, which includes it in place; not a standalone file.
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
    if (qEnvironmentVariableIsSet("BTS_RELEASE_BORROWED_CONTEXT"))
      surface_.doneCurrent();
    return GHOST_kSuccess;
  }

  GHOST_TSuccess initializeDrawingContext() override
  {
    return activateDrawingContext();
  }
  GHOST_TSuccess releaseNativeHandles() override
  {
    return GHOST_kSuccess;
  }
  GHOST_TSuccess swapBufferAcquire() override
  {
    return GHOST_kSuccess;
  }
  GHOST_TSuccess swapBufferRelease() override
  {
    return GHOST_kSuccess;
  }
  unsigned int getDefaultFramebuffer() override
  {
    return surface_.defaultFramebufferObject();
  }

 private:
  QtGpuSurface &surface_;
};

struct BlenderHost::Impl {
  struct RenderTarget {
    GPUOffScreen *offscreen = nullptr;
    GPUViewport *viewport = nullptr;
    std::uint64_t logged_allocations = 0;
    std::uint64_t generation = 0;

    void free()
    {
      logged_allocations = 0;
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
      if (offscreen) {
        GPU_offscreen_free(offscreen);
        offscreen = nullptr;
      }
      char error[256] = {};
      offscreen = GPU_offscreen_create(width,
                                       height,
                                       true,
                                       gpu::TextureFormat::UNORM_8_8_8_8,
                                       GPU_TEXTURE_USAGE_SHADER_READ,
                                       true,
                                       error);
      if (!offscreen) {
        throw std::runtime_error(std::string("EEVEE target allocation failed: ") + error);
      }
      if (!viewport) {
        viewport = GPU_viewport_create();
        GPU_viewport_offscreen_cache_set(viewport, true);
      }
      ++generation;
    }
  };

  enum class Phase { SceneReady, GpuReady, Closed };
  Phase phase = Phase::SceneReady;
  bool factory_workspace_color_pending = false;
  QThread *owner = QThread::currentThread();
  bContext *context = nullptr;
  Main *main = nullptr;
  struct MaterialImportJob {
    std::shared_ptr<std::atomic_bool> cancel;
    std::shared_future<PreparedBlendMaterial> result;
  };
  std::vector<MaterialImportJob> material_import_jobs;
  struct MaterialGraphJob {
    std::shared_ptr<std::atomic_bool> cancel;
    std::shared_future<NativeMaterialDescription> result;
  };
  std::vector<MaterialGraphJob> material_graph_jobs;
  struct MaterialPrepareJob {
    std::shared_ptr<std::atomic_bool> cancel;
    std::shared_future<std::shared_ptr<NativeMaterialGraph>> result;
  };
  std::vector<MaterialPrepareJob> material_prepare_jobs;
  std::unique_ptr<NativeMaterialPreview> native_preview;
  std::map<std::uint32_t, ImportedShaderCache> imported_shader_cache;
  struct AssetScanJob {
    std::shared_ptr<std::stop_source> cancel;
    std::shared_future<BlendAssetLibrary> result;
  };
  std::vector<AssetScanJob> asset_scan_jobs;
  std::optional<std::pair<std::array<float, 16>, std::array<float, 16>>> native_view_matrices;
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
  const bool surface_graph_test = qEnvironmentVariableIsSet("BTS_PD149_SURFACE_GRAPH_TEST");
  const bool surface_cold_control = qEnvironmentVariable("BTS_PD149_SURFACE_COLD_CONTROL") ==
                                    QStringLiteral("1");
  SurfaceGraphSnapshot last_surface_graph;
  std::uint64_t host_draw_serial = 0;
  UvPreview uv_preview;
  UvGpuOverlay uv_overlay;
  SceneTriangleOverlay triangle_overlay;
  SceneSnapshot triangle_scene;
  std::map<std::uint32_t, std::pair<const gpu::VertBuf *, std::array<float, 16>>>
      triangle_geometry;
  std::map<std::uint32_t, const gpu::VertBuf *> wire_vbos;
  std::uint64_t wire_cache_changes = 0;
  UvPreviewSummary uv_summary;
  std::optional<std::uint32_t> selected_material;
  bool uv_dirty = true, uv_prime = true, texture_dirty = false, texture_interacting = false;
  MaterialInspection inspection;
  DisplacementPreview displacement_preview;
  std::shared_ptr<SurfaceBrushCache> surface_cache = make_surface_brush_cache();
  SceneSnapshot source_snapshot()
  {
    return read_scene_snapshot(context, view, false, &displacement_preview.source_meshes());
  }
  InspectionMode inspection_mode = InspectionMode::Material;
  bool warp_bounds_ready = false;
  WarpVector warp_center{};
  double warp_extent = 1;
  std::uint64_t warp_generation = 1;
  std::vector<std::shared_ptr<const WarpGeometryCache>> warp_caches;
  ViewportResidency residency;
  void reset_warp_geometry()
  {
    reset_surface_brush_cache(*surface_cache);
    residency.reset();
    warp_bounds_ready = false;
    warp_caches.clear();
    ++warp_generation;
  }
  bool drawing = false;
  std::string renderer;
  GpuPerformance performance;

  // Owns its receiver context; destruction disconnects this capture before any
  // inspection/native state dies. Only submitted programs are polled here.
  QTimer program_poll;
  bool program_poll_failed = false, program_poll_stopping = false;
  Impl()
  {
    program_poll.setSingleShot(true);
    program_poll.setInterval(16);
    QObject::connect(&program_poll, &QTimer::timeout, &program_poll, [this] {
      performance::Scope callback_timing("gpu_stack_pair_poll_callback");
      if (program_poll_stopping || phase != Phase::GpuReady)
        return;
      if (drawing) {
        arm_program_poll();
        return;
      }
      try {
        // This is a CPU-only compiler query. Do not restore GL state while
        // any shared compiler worker is active; unrelated jobs delay this poll.
        if (GPU_shader_compiler_has_pending_work()) {
          arm_program_poll();
          return;
        }
        enter_gpu();
        const auto release = qScopeGuard([] {
          performance::Scope release_timing("gpu_stack_pair_poll_release");
          DRW_gpu_context_disable_ex(false);
        });
        performance::Scope timing("gpu_stack_pair_poll");
        inspection.poll_gpu_programs();
      }
      catch (const std::exception &error) {
        program_poll_failed = true;
        std::fprintf(stderr, "BTS_GPU_STACK_POLL_FAILED %s\n", error.what());
      }
      catch (...) {
        program_poll_failed = true;
        std::fprintf(stderr, "BTS_GPU_STACK_POLL_FAILED unknown exception\n");
      }
      arm_program_poll();
    });
  }
  ~Impl()
  {
    program_poll.stop();
    QObject::disconnect(&program_poll, nullptr, &program_poll, nullptr);
  }
  void arm_program_poll()
  {
    if (!program_poll_stopping && !program_poll_failed && phase == Phase::GpuReady &&
        inspection.gpu_programs_pending())
    {
      if (!program_poll.isActive())
        program_poll.start();
    }
    else
      program_poll.stop();
  }

  void attach_context()
  {
    auto &s = *this;
    s.reset_warp_geometry();
    s.view = nullptr;
    s.region = nullptr;
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
                                      static_cast<unsigned char>(backdrop.green()),
                                      static_cast<unsigned char>(backdrop.blue()),
                                      255};
      std::copy_n(color, 4, theme.space_view3d.back);
      const unsigned char edge_color[4] = {static_cast<unsigned char>(edge.red()),
                                           static_cast<unsigned char>(edge.green()),
                                           static_cast<unsigned char>(edge.blue()),
                                           255};
      std::copy_n(edge_color, 4, theme.space_view3d.back_grad);
    }
    s.main = CTX_data_main(s.context);
    s.scene = CTX_data_scene(s.context);
    NewProjectProfile::restore_environment_orientation(*s.scene, *s.view);
    s.lens = display_post_settings(*s.scene).projection();
    s.view_layer = CTX_data_view_layer(s.context);
  }

  bool preview_force_cpu = false;
  bool preview_cpu_fallback = false;
  QString preview_device = "CPU", preview_device_message;
  void sync_preview_background()
  {
    // Cycles' Studio Light shader uses World.color for camera rays when HDR
    // opacity is zero. Convert the UI gray back through the active view so it
    // stays neutral; illumination and glossy rays still sample the Studio HDR.
    if (!scene->world)
      scene->world = BKE_world_add(main, "mPainter Environment");
    const QColor gray(bts::theme::Viewport);
    float rgb[3] = {float(gray.redF()), float(gray.greenF()), float(gray.blueF())};
    auto inverse = ColormanageProcessor::display_processor_new(
        &scene->view_settings, &scene->display_settings, DISPLAY_SPACE_DRAW, true);
    inverse.apply_v3(rgb);
    scene->world->horr = rgb[0];
    scene->world->horg = rgb[1];
    scene->world->horb = rgb[2];
    DEG_id_tag_update_ex(main, &scene->world->id, ID_RECALC_SHADING | ID_RECALC_SYNC_TO_EVAL);
  }
  void configure_preview(bool enabled, bool force_cpu = false, bool probe = false)
  {
    auto &s = *this;
    s.free_targets();
    if (enabled) {
      s.sync_preview_background();
      if (!RE_engines_find("CYCLES"))
        throw std::runtime_error("Cycles is not registered");
      if (!BPY_run_string_exec(s.context,
                               nullptr,
                               (std::string("force_cpu = ") + (force_cpu ? "True" : "False") +
                                "\nprobe_defaults = " + (probe ? "True" : "False") + "\n" +
                                R"PY(import bpy, os, cycles, _cycles
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
)PY")
                                   .c_str()))
        throw std::runtime_error("Cycles preview initialization failed");
      STRNCPY(s.scene->r.engine, "CYCLES");
      s.view->shading.flag &= ~(V3D_SHADING_SCENE_WORLD_RENDER | V3D_SHADING_SCENE_LIGHTS_RENDER);
      DEG_id_tag_update_ex(s.main, &s.scene->id, ID_RECALC_SYNC_TO_EVAL);
      DEG_relations_tag_update(s.main);
      std::fprintf(stderr,
                   "BTS_CYCLES_MODE same_pid=%lld\n",
                   static_cast<long long>(QCoreApplication::applicationPid()));
      s.view->shading.type = OB_RENDER;
      s.preview_force_cpu = force_cpu;
      auto root = RNA_id_pointer_create(&s.scene->id);
      auto settings = RNA_pointer_get(&root, "cycles");
      s.preview_device = RNA_enum_get(&settings, "device") == 0 ? "CPU" : "CUDA";
      s.preview_cpu_fallback = s.preview_device == "CPU" && !force_cpu &&
                               qEnvironmentVariable("BTS_CYCLES_DEVICE") != "CPU";
      const auto *backend = IDP_GetPropertyFromGroup_null(s.scene->id.properties,
                                                          "_bts_cycles_backend");
      if (backend && backend->type == IDP_STRING)
        s.preview_device = QString::fromUtf8(IDP_string_get(backend));
      const auto *message = IDP_GetPropertyFromGroup_null(s.scene->id.properties,
                                                          "_bts_cycles_device_message");
      s.preview_device_message = message && message->type == IDP_STRING ?
                                     QString::fromUtf8(IDP_string_get(message)) :
                                     QString{};
    }
    else {
      STRNCPY(s.scene->r.engine, "BLENDER_EEVEE");
      s.view->shading.type = OB_MATERIAL;
      DEG_id_tag_update_ex(s.main, &s.scene->id, ID_RECALC_SYNC_TO_EVAL);
      DEG_relations_tag_update(s.main);
    }
  }

  void require_owner() const
  {
    if (QThread::currentThread() != owner) {
      throw std::logic_error("Blender state accessed outside its owner thread");
    }
  }

  void tag_preview()
  {
    if (scene && STREQ(scene->r.engine, "CYCLES"))
      DEG_id_tag_update_ex(main, &scene->id, ID_RECALC_SYNC_TO_EVAL);
  }
  void enter_gpu()
  {
    {
      performance::Scope timing("gpu_owner_context_enable");
      DRW_gpu_context_enable_ex(false);
    }
    {
      performance::Scope timing("gpu_owner_external_state_restore");
      gpu::GLContext::get()->restore_external_state();
    }
    {
      performance::Scope timing("gpu_owner_framebuffer_restore");
      GPU_framebuffer_restore();
    }
  }

  void free_targets()
  {
    last_eevee_completion = {};
    if (surface_graph_test)
      last_surface_graph = {};
    if (view && region && region->regiondata) {
      auto *rv = static_cast<RegionView3D *>(region->regiondata);
      if (rv->view_render) {
        RE_FreeViewRender(rv->view_render);
        rv->view_render = nullptr;
      }
    }
    triangle_overlay.free();
    triangle_scene = {};
    triangle_geometry.clear();
    scene_target.free();
    uv_target.free();
    uv_overlay.free();
    wire_vbos.clear();
    uv_preview.clear();
    uv_summary = {};
    uv_dirty = true;
    uv_prime = true;
  }
};
