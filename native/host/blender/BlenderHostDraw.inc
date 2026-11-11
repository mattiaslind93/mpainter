// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU surface and draw(): rendering the viewport frames.
// A section of native/host/BlenderHost.cc, which includes it in place; not a standalone file.
void BlenderHost::initialize_gpu_surface()
{
  if (!qEnvironmentVariableIsSet("BTS_GPU_STORAGE_STAGING"))
    qputenv("BTS_GPU_STORAGE_STAGING", "1");
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::SceneReady) {
    throw std::logic_error("GPU initialization requires the initialized scene");
  }
  if (GPU_is_init()) {
    throw std::logic_error("Blender initialized its GPU before the Qt host supplied a context");
  }
  const bool shader_workers = qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS") == 1;
#ifdef __APPLE__
  install_metal_shared_contexts();
#else
  install_shared_ghost_context(s.surface->context(), shader_workers);
  s.ghost = std::make_unique<QtGhostContext>(*s.surface);
#endif
  U.shader_compilation_method = USER_SHADER_COMPILE_THREAD;
  if (s.ghost->ghost_context()->activateDrawingContext() != GHOST_kSuccess) {
    throw std::runtime_error("Could not activate Qt's OpenGL context");
  }
  GPU_backend_type_selection_set(s.ghost->backend());
  if (shader_workers)
    WM_init_gpu_hosted_shader_workers(s.ghost->ghost_context());
  else
    WM_init_gpu_hosted(s.ghost->ghost_context());
  s.phase = Impl::Phase::GpuReady;
  s.renderer = GPU_platform_renderer();
  s.restore_surface();
#ifndef __APPLE__
  if (qEnvironmentVariableIntValue("BTS_GL_DEBUG") == 1) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(
        [](GLenum, GLenum type, GLuint id, GLenum, GLsizei, const GLchar *message, const void *) {
          if (type == GL_DEBUG_TYPE_ERROR)
            std::fprintf(stderr, "BTS_GL_ERROR id=%u message=%s\n", id, message);
        },
        nullptr);
  }
#endif
  // Texture-backed programs need Blender's draw-manager owner, not just Qt's
  // current GL context (which sufficed for the shader-only readback warmup).
  s.enter_gpu();
  try {
    // Native source images use these two float RGBA formats. Material browsing
    // must not discover their lazy mip-compute programs during an owner tick.
    // A real 2x2 -> 1x1 dispatch warms both the program and driver pipeline;
    // there is no readback, finish, reduced mip quality or scene mutation.
    for (const auto format :
         {gpu::TextureFormat::SFLOAT_16_16_16_16, gpu::TextureFormat::SFLOAT_32_32_32_32})
    {
      const auto begin = std::chrono::steady_clock::now();
      const auto cpu = performance::thread_ms();
      auto *texture = GPU_texture_create_2d("Native image mip startup",
                                            2,
                                            2,
                                            2,
                                            format,
                                            GPU_TEXTURE_USAGE_SHADER_READ |
                                                GPU_TEXTURE_USAGE_SHADER_WRITE,
                                            nullptr);
      if (texture) {
        const float zero[4]{};
        GPU_texture_clear(texture, GPU_DATA_FLOAT, zero);
        GPU_texture_update_mipmap_chain(texture);
        GPU_texture_free(texture);
      }
      const double elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - begin)
                                 .count();
      std::printf(
          "BTS_NATIVE_MIP_STARTUP format=%s shader_write=1 route=builtin-compute available=%d wall_ms=%.3f cpu_ms=%.3f\n",
          GPU_texture_format_name(format),
          bool(texture),
          elapsed,
          performance::thread_ms() - cpu);
    }
#ifdef __APPLE__
    s.metal_rhi = std::make_unique<MetalRhiViewport>();
#endif
    s.inspection.initialize_gpu_programs();
    GpuProjectReadback::warmup();
    if (qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_GPU_TEST"))
      verify_native_material_gpu_prototype(*s.main);
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
  s.arm_program_poll();
  s.restore_surface();
  std::printf(
      "BTS_GPU_READY pid=%lld renderer=%s qt=%s shader_workers=%d presentation=backend-owned\n",
      static_cast<long long>(QCoreApplication::applicationPid()),
      s.renderer.c_str(),
      qVersion(),
      shader_workers && !GPU_use_main_context_workaround() ? GPU_max_parallel_compilations() : 0);
  std::fflush(stdout);
}

double BlenderHost::draw(std::span<const ViewFrame> frames,
                         int width,
                         int height,
                         bool defer_material_render)
{
  performance::Scope draw_timing("draw");
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing || width < 1 || height < 1 || frames.empty() ||
      frames.size() > 2)
  {
    throw std::logic_error("Invalid or reentrant native draw");
  }
  s.view->shading.studiolight_background = 0;
  // BKE_scene_graph_update_tagged clears these flags. Latch before any
  // residency snapshot can ensure evaluation and clear the same notification.
  bool native_owner_revision = DEG_id_type_updated(s.depsgraph, ID_OB) ||
                               DEG_id_type_updated(s.depsgraph, ID_ME);
  bool native_geometry_revision = native_material_geometry_updated(*s.main, s.depsgraph);
  if (s.inspection.poll_composites(*s.main)) {
    s.displacement_preview.sync_contents(
        *s.main, *s.depsgraph, [&](auto uid) { return s.inspection.displacement_content(uid); });
    s.texture_dirty = true;
    s.uv_preview.tag_images_updated();
  }
  const bool image_changed = s.texture_dirty;
  s.texture_dirty = false;
  bool has_scene = false, has_uv = false;
  for (const ViewFrame &frame : frames) {
    const ViewPane &pane = frame.pane;
    if (pane.x < 0 || pane.y < 0 || pane.width < 1 || pane.height < 1 || pane.width > width ||
        pane.height > height || pane.x > width - pane.width || pane.y > height - pane.height ||
        !std::all_of(
            frame.view.begin(), frame.view.end(), [](float v) { return std::isfinite(v); }) ||
        !std::all_of(frame.projection.begin(),
                     frame.projection.end(),
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
  s.last_eevee_completion = {};
  s.last_eevee_completion.host_draw = ++s.host_draw_serial;
  if (s.surface_graph_test) {
    s.last_surface_graph = {};
    s.last_surface_graph.status = SurfaceGraphStatus::NoSceneDraw;
    s.last_surface_graph.host_draw = s.host_draw_serial;
  }
  const auto start = std::chrono::steady_clock::now();
  s.enter_gpu();
  s.performance.begin();
  bool gpu_active = true;
  try {
    for (const auto &frame : frames)
      if (frame.pane.kind == ViewKind::Scene) {
        const auto matrices = std::pair{frame.view, frame.projection};
        const bool changed = s.native_view_matrices != matrices;
        s.native_view_matrices = matrices;
        if (changed)
          s.inspection.refresh_native_view();
        break;
      }
    if (qEnvironmentVariableIsSet("BTS_CYCLES_PREVIEW_PROBE") && s.selected_material &&
        !STREQ(s.scene->r.engine, "CYCLES"))
      s.configure_preview(true, false, true);
    if (STREQ(s.scene->r.engine, "CYCLES")) {
      s.view->shading.type = OB_RENDER;
      for (const auto &frame : frames)
        if (frame.pane.kind == ViewKind::Scene) {
          auto *rv = static_cast<RegionView3D *>(s.region->regiondata);
          const auto &p = frame.projection;
          copy_m4_m4(rv->viewmat, reinterpret_cast<const float (*)[4]>(frame.view.data()));
          invert_m4_m4(rv->viewinv, rv->viewmat);
          copy_m4_m4(rv->winmat, reinterpret_cast<const float (*)[4]>(p.data()));
          const float aspect = float(frame.pane.width) / frame.pane.height;
          if (std::abs(p[15]) < .5f) {
            rv->persp = RV3D_PERSP;
            s.view->lens = 36.f * p[5] / std::max(1.f, aspect);
            s.view->clip_start = frame.clipping[0] > 0 ? frame.clipping[0] : p[14] / (p[10] - 1);
            s.view->clip_end = frame.clipping[1] > 0 ? frame.clipping[1] : p[14] / (p[10] + 1);
          }
          else {
            rv->persp = RV3D_ORTHO;
            s.view->lens = 36;
            rv->dist = std::max(1.f, aspect) / p[5];
            s.view->clip_start = frame.clipping[0] > 0 ? frame.clipping[0] : .01;
            s.view->clip_end = frame.clipping[1] > 0 ? 2 * frame.clipping[1] :
                                                       4.f / std::abs(p[10]);
          }
        }
    }
    {
      performance::Scope prepare_timing("depsgraph_upload");
      const auto mip_before = gpu::host_metrics::counters[gpu::host_metrics::Mipmap].ns.load();
      std::vector<ResidencyView> residency_views;
      for (const auto &frame : frames)
        residency_views.push_back({frame.view,
                                   frame.projection,
                                   frame.pane.width,
                                   frame.pane.height,
                                   frame.pane.kind == ViewKind::Uv});
      s.scene->r.anisotropic_filter = 16;
      U.anisotropic_filter = 16;
      {
        performance::Scope timing("depsgraph_residency");
        s.residency.update(
            *s.main,
            residency_views,
            s.selected_material,
            [&] { return s.source_snapshot(); },
            s.inspection.updates_pending());
      }
      const auto property_gpu_start = bts::PropertyClock::now();
      {
        performance::Scope timing("depsgraph_staging_upload");
        s.inspection.upload_staging_images(s.performance.compute_ready(), s.texture_interacting);
      }
      if (s.inspection.take_displacement_updates()) {
        s.displacement_preview.sync_contents(*s.main, *s.depsgraph, [&](auto uid) {
          return s.inspection.displacement_content(uid);
        });
        s.texture_dirty = true;
        s.uv_preview.tag_images_updated();
      }
      if (!s.inspection.sync_cycles_images(*s.main, STREQ(s.scene->r.engine, "CYCLES")))
        defer_material_render = true;
      for (const auto &frame : frames)
        if (frame.pane.kind == ViewKind::Scene)
          prepare_display_post_effects(
              *s.main, *s.scene, *s.view, frame.pane.width, frame.pane.height);
      s.inspection.set_subsurface_enabled(*s.main, subsurface_preview(*s.scene).enabled);
      native_owner_revision |= DEG_id_type_updated(s.depsgraph, ID_OB) ||
                               DEG_id_type_updated(s.depsgraph, ID_ME);
      native_geometry_revision |= native_material_geometry_updated(*s.main, s.depsgraph);
      {
        performance::Scope timing("depsgraph_update_tagged");
        BKE_scene_graph_update_tagged(s.depsgraph, s.main);
      }
      if (native_owner_revision || native_geometry_revision)
        s.inspection.refresh_native_world_objects(native_owner_revision, native_geometry_revision);
      // EEVEE can retain a material pass across frames. Consume queued image regions
      // explicitly on its GPU owner before drawing, without retagging the node tree.
      {
        performance::Scope timing("depsgraph_image_partial_upload");
        std::set<Image *> images;
        for (Material &material : s.main->materials) {
          auto *evaluated = DEG_get_evaluated(s.depsgraph, &material);
          for (Material *owner : std::set<Material *>{&material, evaluated})
            if (owner) {
              for (LinkData &link : owner->gpumaterial) {
                const auto textures = GPU_material_textures(static_cast<GPUMaterial *>(link.data));
                for (auto *texture = static_cast<GPUMaterialTexture *>(textures.first); texture;
                     texture = texture->next)
                {
                  if (texture->ima && STRPREFIX(texture->ima->id.name + 2, "BTS ") &&
                      images.insert(texture->ima).second)
                    BKE_image_get_gpu_material_texture_try(
                        texture->ima,
                        texture->iuser_available ? &texture->iuser : nullptr,
                        texture->tiled_mapping_name[0]);
                }
              }
            }
        }
      }
      s.inspection.finalize_gpu_images(*s.main, s.performance.compute_ready());
      if (performance::enabled()) {
        const auto elapsed = gpu::host_metrics::counters[gpu::host_metrics::Mipmap].ns.load() -
                             mip_before;
        if (elapsed)
          performance::record({performance::flow(),
                               "image_mipmap_generate",
                               elapsed / 1e6,
                               elapsed / 1e6,
                               0,
                               true});
      }
      if (bts::property_waiting.event)
        bts::property_waiting.gpu_ms += bts::property_ms(property_gpu_start);
    }
    const bool prime_uv = !defer_material_render && s.uv_prime && !has_uv &&
                          s.selected_material.has_value();
    if (s.uv_preview.shader_rebuild_needed())
      s.uv_dirty = true;
    if (!defer_material_render && (has_uv || prime_uv) && s.uv_dirty) {
      // Keep the engine cache and retain predecessor IDs through the new draw.
      // New/Open still free targets before destroying any source Main.
      s.uv_preview.retire();
      s.uv_summary = {};
      if (s.selected_material) {
        s.uv_summary = s.uv_preview.rebuild(*s.main,
                                            *s.scene,
                                            *s.depsgraph,
                                            *s.view,
                                            *s.selected_material,
                                            s.inspection_mode != InspectionMode::Material,
                                            &s.displacement_preview.source_meshes());
      }
      s.uv_dirty = false;
      std::printf("BTS_UV_PREVIEW pid=%lld material=%u triangles=%zu objects=%zu\n",
                  static_cast<long long>(QCoreApplication::applicationPid()),
                  s.selected_material.value_or(0),
                  s.uv_summary.uv_triangles,
                  s.uv_summary.mesh_objects);
      std::fflush(stdout);
    }
    if (prime_uv && s.uv_summary) {
      performance::Scope timing("uv_shader_startup_warmup");
      const auto &size = frames.front().pane;
      s.uv_target.ensure_size(size.width, size.height);
      GPU_offscreen_bind(s.uv_target.offscreen, true);
      float view[4][4], projection[4][4];
      unit_m4(view);
      view[3][2] = -5;
      orthographic_m4(projection,
                      s.uv_summary.minimum_uv[0] - .5f,
                      s.uv_summary.maximum_uv[0] + .5f,
                      s.uv_summary.minimum_uv[1] - .5f,
                      s.uv_summary.maximum_uv[1] + .5f,
                      .01f,
                      100.f);
      s.uv_preview.draw(size.width,
                        size.height,
                        view,
                        projection,
                        s.uv_target.offscreen,
                        s.uv_target.viewport,
                        *s.view,
                        int(s.inspection_mode) <= 0);
      GPU_offscreen_unbind(s.uv_target.offscreen, true);
    }
    if (!defer_material_render && (has_uv || prime_uv))
      s.uv_prime = false;
#ifdef __APPLE__
    s.metal_frame = QImage(width, height, QImage::Format_RGBA8888);
    s.metal_frame.fill(QColor::fromRgbF(.10, .10, .11));
    QPainter metal_composite(&s.metal_frame);
#endif
    std::vector<GLint> source_fbos;
    source_fbos.reserve(frames.size());
    for (const ViewFrame &frame : frames) {
      const ViewPane &pane = frame.pane;
      auto &target = pane.kind == ViewKind::Scene ? s.scene_target : s.uv_target;
      target.ensure_size(pane.width, pane.height);
      if (pane.kind == ViewKind::Scene) {
        s.last_eevee_completion.target_generation = target.generation;
        s.last_eevee_completion.width = pane.width;
        s.last_eevee_completion.height = pane.height;
        s.last_eevee_completion.status = defer_material_render ?
                                             EeveeCompletionStatus::Deferred :
                                             EeveeCompletionStatus::OtherBackend;
      }
      GPU_viewport_tag_update(target.viewport);
      GPU_offscreen_bind(target.offscreen, true);
      performance::Scope pane_timing(pane.kind == ViewKind::Scene ? "eevee_scene_draw" :
                                                                    "uv_scene_draw");
      // A new image-bound material has a temporary USD fallback. Keep its tree
      // intact, but do not compile it immediately before replacing it. Uploads
      // and GPU preparation above continue while the worker decodes resources.
      if (defer_material_render) {
        GPU_framebuffer_clear_color(GPU_framebuffer_active_get(), {0.10, 0.10, 0.10, 1.0});
      }
      else if (pane.kind == ViewKind::Scene) {
        const auto old_flags = s.view->flag;
        const auto old_flags2 = s.view->flag2;
        const auto old_grid = s.view->gridflag;
        const auto old_overlay = s.view->overlay;
        const auto restore_overlay = qScopeGuard([&] {
          s.view->flag = old_flags;
          s.view->flag2 = old_flags2;
          s.view->gridflag = old_grid;
          s.view->overlay = old_overlay;
        });
        if (frame.wireframe) {
          s.view->flag2 &= ~V3D_HIDE_OVERLAYS;
          s.view->flag &= ~V3D_SELECT_OUTLINE;
          s.view->gridflag = eView3D_GridFlag(0);
          s.view->overlay.flag = (frame.wireframe_triangles ? eView3DOverlay_Flag(0) :
                                                              V3D_OVERLAY_WIREFRAMES) |
                                 V3D_OVERLAY_HIDE_CURSOR | V3D_OVERLAY_HIDE_TEXT |
                                 V3D_OVERLAY_HIDE_BONES | V3D_OVERLAY_HIDE_MOTION_PATHS |
                                 V3D_OVERLAY_HIDE_OBJECT_XTRAS | V3D_OVERLAY_HIDE_OBJECT_ORIGINS;
          s.view->overlay.wireframe_threshold = 1;
          s.view->overlay.wireframe_opacity = .65f;
        }

        auto *region_view = static_cast<RegionView3D *>(s.region->regiondata);
        struct PaintingHint {
          RegionView3D &view;
          decltype(RegionView3D::rflag) before;
          ~PaintingHint()
          {
            view.rflag = before;
          }
        } hint{*region_view, region_view->rflag};
        // Native paint_stroke.cc uses this flag to reset EEVEE sampling without
        // rebuilding a scene or material whose nodes and geometry did not change.
        if (image_changed)
          region_view->rflag |= RV3D_PAINTING;
        ED_view3d_draw_offscreen(s.depsgraph,
                                 s.scene,
                                 STREQ(s.scene->r.engine, "CYCLES") ? OB_RENDER : OB_MATERIAL,
                                 s.view,
                                 s.region,
                                 STREQ(s.scene->r.engine, "CYCLES") ? s.context : nullptr,
                                 pane.width,
                                 pane.height,
                                 reinterpret_cast<const float (*)[4]>(frame.view.data()),
                                 reinterpret_cast<const float (*)[4]>(frame.projection.data()),
                                 false,
                                 true,
                                 "",
                                 int(s.inspection_mode) <= 0,
                                 !STREQ(s.scene->r.engine, "CYCLES"),
                                 target.offscreen,
                                 target.viewport);
        if (STREQ(s.scene->r.engine, "BLENDER_EEVEE")) {
          auto snapshot = detail::capture_eevee_completion(*target.viewport);
          snapshot.host_draw = s.host_draw_serial;
          snapshot.target_generation = target.generation;
          snapshot.width = pane.width;
          snapshot.height = pane.height;
          s.last_eevee_completion = snapshot;
          if (s.surface_graph_test) {
            s.last_surface_graph = detail::capture_surface_graph(
                *s.main, *s.depsgraph, *s.view_layer, *s.view, s.selected_material.value_or(0));
            s.last_surface_graph.host_draw = s.host_draw_serial;
            s.last_surface_graph.target_generation = target.generation;
          }
        }
        if (frame.wireframe && qEnvironmentVariableIsSet("BTS_OVERLAY_PROFILE")) {
          for (const Base &base : *BKE_view_layer_object_bases_get(s.view_layer)) {
            if (base.object->type != OB_MESH || !BKE_base_is_visible(s.view, &base))
              continue;
            auto *object = DEG_get_evaluated(s.depsgraph, base.object);
            auto *batch = blender::draw::DRW_cache_mesh_face_wireframe_get(object);
            if (batch && batch->verts[0]) {
              auto &prior = s.wire_vbos[base.object->id.session_uid];
              if (prior != batch->verts[0]) {
                prior = batch->verts[0];
                ++s.wire_cache_changes;
              }
            }
          }
        }
        s.last_tangent_shader_state = tangent_shader_state();
        if (qEnvironmentVariableIsSet("BTS_CYCLES_PREVIEW_PROBE")) {
          static int count = 0;
          if (++count % 60 == 0) {
            auto *engine = region_view->view_render ?
                               RE_view_engine_get(region_view->view_render) :
                               nullptr;
            std::fprintf(stderr,
                         "BTS_CYCLES_FRAME engine=%s info=%s\n",
                         engine ? engine->type->idname : "none",
                         engine ? engine->text : "");
          }
        }
        if (qEnvironmentVariableIsSet("BTS_DISPLAY_POST_DEBUG")) {
          static int post_frame = 0;
          if (++post_frame % 60 == 0) {
            auto *evaluated = DEG_get_evaluated_scene(s.depsgraph);
            auto *tree = evaluated->compositing_node_group;
            float factor = -1;
            if (tree) {
              auto *mix = bke::node_find_node_by_name(*tree, "__BTS_DisplayVignette");
              auto *socket = mix ? bke::node_find_enabled_input_socket(*mix, "Factor") : nullptr;
              if (socket)
                factor = static_cast<bNodeSocketValueFloat *>(socket->default_value)->value;
            }
            std::fprintf(stderr,
                         "BTS_POST_GRAPH original=%d evaluated=%d shading=%d mode=%d factor=%g\n",
                         s.scene->compositing_node_group != nullptr,
                         tree != nullptr,
                         int(s.view->shading.type),
                         int(s.view->shading.use_compositor),
                         factor);
          }
        }
      }
      else if (!s.uv_preview.draw(pane.width,
                                  pane.height,
                                  reinterpret_cast<const float (*)[4]>(frame.view.data()),
                                  reinterpret_cast<const float (*)[4]>(frame.projection.data()),
                                  target.offscreen,
                                  target.viewport,
                                  *s.view,
                                  int(s.inspection_mode) <= 0))
      {
        GPU_framebuffer_clear_color(GPU_framebuffer_active_get(), {0.10, 0.10, 0.10, 1.0});
      }
      if (performance::enabled()) {
        const auto counters = GPU_viewport_offscreen_cache_stats(target.viewport);
        if (counters.reuses == 1 || counters.allocations != target.logged_allocations)
          std::printf(
              "BTS_OFFSCREEN_CACHE kind=%s allocations=%llu reuses=%llu width=%d height=%d\n",
              pane.kind == ViewKind::Scene ? "scene" : "uv",
              (unsigned long long)counters.allocations,
              (unsigned long long)counters.reuses,
              pane.width,
              pane.height);
        target.logged_allocations = counters.allocations;
      }
      GPU_offscreen_bind(target.offscreen, false);
      if (pane.kind == ViewKind::Uv && !defer_material_render)
        s.uv_overlay.draw(s.uv_summary, frame);
      if (pane.kind == ViewKind::Scene && frame.wireframe && frame.wireframe_triangles &&
          !defer_material_render)
      {
        // ED_view3d_draw_offscreen unbinds the viewport and clears its borrowed
        // depth pointer. The offscreen owns that same rendered depth texture.
        gpu::FrameBuffer *framebuffer = nullptr;
        gpu::Texture *color = nullptr, *depth = nullptr;
        GPU_offscreen_viewport_data_get(target.offscreen, &framebuffer, &color, &depth);
        std::map<std::uint32_t, std::pair<const gpu::VertBuf *, std::array<float, 16>>> geometry;
        for (const Base &base : *BKE_view_layer_object_bases_get(s.view_layer)) {
          if (base.object->type != OB_MESH || !BKE_base_is_visible(s.view, &base))
            continue;
          auto *object = DEG_get_evaluated(s.depsgraph, base.object);
          auto *batch = blender::draw::DRW_cache_mesh_face_wireframe_get(object);
          std::array<float, 16> transform;
          std::copy_n(&object->object_to_world()[0][0], 16, transform.data());
          geometry[base.object->id.session_uid] = {batch ? batch->verts[0] : nullptr, transform};
        }
        if (geometry != s.triangle_geometry || s.triangle_scene.uv_triangles.empty()) {
          // Match rendered/evaluated geometry, including preview subdivision;
          // original source meshes remain the authority for picking/export/UV.
          s.triangle_scene = read_scene_snapshot(s.context, s.view);
          s.triangle_geometry = std::move(geometry);
        }
        auto triangle_frame = frame;
        triangle_frame.wire_source = &s.triangle_scene;
        s.triangle_overlay.draw(triangle_frame, depth, color);
      }
#ifdef __APPLE__
      // Native Metal target -> shared-device QRhi -> offscreen/raster widget.
      // This readback is deliberately synchronous; direct swapchain presentation
      // can replace it without changing BlenderHost's rendering or input logic.
      auto image = s.metal_rhi->capture(GPU_offscreen_color_texture(target.offscreen));
      metal_composite.drawImage(pane.x, pane.y, image.mirrored(false, true));
#else
      GLint source_fbo = 0;
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &source_fbo);
      source_fbos.push_back(source_fbo);
#endif
      GPU_offscreen_unbind(target.offscreen, true);
    }
    s.residency.finish_frame();
    s.inspection.resume_gpu_channels();
    s.inspection.collect_retired_visibility_images(*s.main);
    s.performance.end();
    s.performance.submitted();
    DRW_gpu_context_disable_ex(false);
    gpu_active = false;
    s.restore_surface();
#ifndef __APPLE__
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
      glBlitFramebuffer(0,
                        0,
                        pane.width,
                        pane.height,
                        pane.x,
                        bottom,
                        pane.x + pane.width,
                        bottom + pane.height,
                        GL_COLOR_BUFFER_BIT,
                        GL_NEAREST);
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
    if (qEnvironmentVariableIsSet("BTS_OVERLAY_PROBE")) {
      static int count = 0;
      if (++count <= 8) {
        GLint viewport[4], fbo;
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        std::fprintf(
            stderr,
            "BTS_OVERLAY_GL viewport=%d,%d,%d,%d fbo=%d expected=%u cull=%d depth=%d stencil=%d blend=%d contours=%zu\n",
            viewport[0],
            viewport[1],
            viewport[2],
            viewport[3],
            fbo,
            target_fbo,
            int(glIsEnabled(GL_CULL_FACE)),
            int(glIsEnabled(GL_DEPTH_TEST)),
            int(glIsEnabled(GL_STENCIL_TEST)),
            int(glIsEnabled(GL_BLEND)),
            s.uv_summary.contours ? s.uv_summary.contours->size() : 0);
      }
    }
#else
    metal_composite.end();
#endif
    s.drawing = false;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
  }
  catch (...) {
    s.last_eevee_completion = {};
    s.last_eevee_completion.host_draw = s.host_draw_serial;
    s.last_eevee_completion.status = EeveeCompletionStatus::DrawFailed;
    if (s.surface_graph_test) {
      s.last_surface_graph = {};
      s.last_surface_graph.status = SurfaceGraphStatus::DrawFailed;
      s.last_surface_graph.host_draw = s.host_draw_serial;
    }
    if (gpu_active) {
      s.performance.end();
      GPU_framebuffer_restore();
      DRW_gpu_context_disable_ex(false);
    }
    s.restore_surface();
    s.drawing = false;
    throw;
  }
}

EeveeCompletionSnapshot BlenderHost::eevee_completion_snapshot() const
{
  impl_->require_owner();
  return impl_->last_eevee_completion;
}

SurfaceGraphSnapshot BlenderHost::surface_graph_snapshot() const
{
  impl_->require_owner();
  return impl_->last_surface_graph;
}

