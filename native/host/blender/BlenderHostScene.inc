// SPDX-License-Identifier: GPL-3.0-or-later
// The scene: viewport recreation, roughness, bake sources, snapshots and warp projections.
// A section of native/host/BlenderHost.cc, which includes it in place; not a standalone file.
std::uint64_t BlenderHost::diagnostic_recreate_scene_viewport()
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.surface_cold_control || !s.surface_graph_test || s.phase != Impl::Phase::GpuReady ||
      s.drawing || !s.scene || !STREQ(s.scene->r.engine, "BLENDER_EEVEE") ||
      !s.scene_target.offscreen || !s.scene_target.viewport)
    throw std::logic_error(
        "Cold Scene viewport diagnostic requires explicit idle live EEVEE target");
  if (s.scene_target.generation == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("Scene target generation exhausted");
  std::uint64_t generation = 0;
  with_gpu([&] {
    // Construct first: allocation failure leaves the current target untouched.
    std::unique_ptr<GPUViewport, decltype(&GPU_viewport_free)> fresh(GPU_viewport_create(),
                                                                     GPU_viewport_free);
    if (!fresh)
      throw std::runtime_error("Cannot allocate cold diagnostic viewport");
    GPU_viewport_offscreen_cache_set(fresh.get(), true);
    auto *previous = s.scene_target.viewport;
    s.scene_target.viewport = fresh.release();
    generation = ++s.scene_target.generation;
    // These are unavailable observations until the NEXT actual Scene draw.
    // Keep its prior draw serial as the actor's phase floor, not a new draw.
    s.last_eevee_completion = {};
    s.last_eevee_completion.host_draw = s.host_draw_serial;
    s.last_eevee_completion.target_generation = generation;
    s.last_surface_graph = {};
    s.last_surface_graph.status = SurfaceGraphStatus::NoSceneDraw;
    s.last_surface_graph.host_draw = s.host_draw_serial;
    s.last_surface_graph.target_generation = generation;
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
    static_cast<bNodeSocketValueFloat *>(socket->default_value)->value = std::clamp(
        roughness, 0.0f, 1.0f);
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
              static_cast<long long>(QCoreApplication::applicationPid()),
              roughness,
              changed);
  std::fflush(stdout);
}

SceneSnapshot BlenderHost::load_bake_source(const std::string &path,
                                            const BakeSourceOptions &options)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing)
    throw std::logic_error("Bake source import outside idle native session");
  bts::performance::Scope timing("bake_source_import");
  const auto started = std::chrono::steady_clock::now();
  options.validate();
  UsdImportOptions usd;
  if (options.frame) {
    usd.explicit_frame = true;
    usd.frame = *options.frame;
  }
  auto pending =
      QFileInfo(QString::fromStdString(path)).suffix().compare("abc", Qt::CaseInsensitive) == 0 ?
          PendingMeshProject::from_alembic(*s.main, path, false, {}, options.frame) :
          PendingMeshProject::from_scene(*s.main, path, false, {}, usd);
  auto result = pending->snapshot();
  scale_bake_source(result, options.scale);
  std::printf(
      "BTS_BAKE_SOURCE file=%s objects=%zu triangles=%zu scale=%.6g frame=%s bounds=(%.6g,%.6g,%.6g)-(%.6g,%.6g,%.6g) ms=%.3f\n",
      qPrintable(QFileInfo(QString::fromStdString(path)).fileName()),
      result.mesh_objects,
      result.uv_triangles.size(),
      options.scale,
      options.frame ? std::to_string(*options.frame).c_str() : "first",
      result.minimum[0],
      result.minimum[1],
      result.minimum[2],
      result.maximum[0],
      result.maximum[1],
      result.maximum[2],
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
          .count());
  std::fflush(stdout);
  return result;
}

bool BlenderHost::mesh_export_displacement_available(const TextureDocument &document) const
{
  const auto &s = *impl_;
  s.require_owner();
  for (const auto &set : document.texture_sets())
    if (!set.disabled && set.displacement.channel && set.displacement.enabled &&
        set.displacement.strength > 0 &&
        s.inspection.displacement_content(set.material_session_uid))
      return true;
  return false;
}
std::shared_ptr<NativeMeshExport> BlenderHost::prepare_mesh_export(const TextureDocument &document,
                                                                   bool displaced,
                                                                   bool recompute_normals)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing)
    throw std::logic_error("Mesh export requires an idle viewport");
  if (!displaced)
    return NativeMeshExport::prepare(
        *s.main, *s.scene, *s.depsgraph, s.view, s.displacement_preview.source_meshes());
  std::set<std::uint32_t> active;
  for (const auto &set : document.texture_sets())
    if (!set.disabled && set.displacement.channel && set.displacement.enabled &&
        set.displacement.strength > 0 &&
        s.inspection.displacement_content(set.material_session_uid))
      active.insert(set.material_session_uid);
  const auto geometry = s.source_snapshot();
  return NativeMeshExport::prepare(*s.main,
                                   *s.scene,
                                   *s.depsgraph,
                                   s.view,
                                   s.displacement_preview.source_meshes(),
                                   &document,
                                   &geometry,
                                   active,
                                   recompute_normals);
}

QJsonObject BlenderHost::overlay_profile() const
{
  auto &s = *impl_;
  s.require_owner();
  auto result = s.uv_overlay.stats();
  const auto triangle = s.triangle_overlay.stats();
  for (auto i = triangle.begin(); i != triangle.end(); ++i)
    result[i.key()] = i.value();
  result["native_wire_vbos"] = qint64(s.wire_vbos.size());
  result["native_wire_cache_changes"] = qint64(s.wire_cache_changes);
  result["uv_contour_edges"] = qint64(s.uv_summary.contours ? s.uv_summary.contours->size() : 0);
  result["uv_wire_edges"] = qint64(s.uv_summary.wireframe ? s.uv_summary.wireframe->size() : 0);
  result["uv_triangles"] = qint64(s.uv_summary.uv_triangles);
  return result;
}

SceneSnapshot BlenderHost::scene_snapshot(bool evaluated_subdivision)
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
    SceneSnapshot snapshot = evaluated_subdivision ? read_scene_snapshot(s.context, s.view) :
                                                     s.source_snapshot();
    snapshot.evaluated_subdivision = evaluated_subdivision;
    snapshot.preview_subdivision_levels = s.displacement_preview.material_levels();
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
                                                             const std::array<float, 3> &origin,
                                                             const std::array<float, 3> &direction,
                                                             float diameter,
                                                             bool coverage,
                                                             bool square,
                                                             const BrushProjection &projection)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing)
    throw std::logic_error("Surface hit outside idle session");
  return surface_brush_stamp(*s.main,
                             *s.scene,
                             *s.depsgraph,
                             *s.view,
                             selected,
                             origin,
                             direction,
                             diameter,
                             coverage,
                             square,
                             projection,
                             &s.displacement_preview.source_meshes(),
                             s.surface_cache.get());
}

void BlenderHost::prepare_warp_projections(const TextureSet &set)
{
  auto &s = *impl_;
  s.require_owner();
  const auto needs = [](const TextureLayer &layer) {
    return (layer.kind == LayerKind::Fill && warp_geometry_enabled(layer.fill)) ||
           (layer.mask && any_mask_effect(layer.mask->effects, [](const auto &effect) {
              return effect.kind == MaskEffectKind::Fill && effect.anchor_source.isNull() &&
                     warp_geometry_enabled(effect.mapping.projection);
            }));
  };
  if (!std::any_of(evaluation_layers(set).begin(), evaluation_layers(set).end(), needs))
    return;
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Warp geometry fitting requires an idle scene");
  if (!s.warp_bounds_ready) {
    const auto geometry = s.source_snapshot();
    s.warp_extent = 0;
    for (int c = 0; c < 3; ++c) {
      s.warp_center[c] = (double(geometry.minimum[c]) + geometry.maximum[c]) * .5;
      s.warp_extent = std::max(s.warp_extent, double(geometry.maximum[c] - geometry.minimum[c]));
    }
    if (s.warp_extent < 1e-6)
      s.warp_extent = 1;
    s.warp_bounds_ready = true;
  }
  const auto prepare = [&](const FillProjection &projection) {
    if (!warp_geometry_enabled(projection))
      return;
    auto p = projection;
    p.cm_per_native = set.cm_per_native;
    p.native_per_uv = set.native_per_uv;
    if (warp_fit_pose_matches(p) && p.warp_fit->generation == s.warp_generation &&
        p.warp_fit->material == set.material_session_uid &&
        p.warp_fit->reach ==
            std::max(s.warp_extent * 2,
                     std::sqrt(warp_dot(p.warp_fit->frame.axes[1], p.warp_fit->frame.axes[1])) *
                         (p.warp_depth + 1)))
      return;
    const auto author = warp_authored_grid(p);
    const auto frame = warp_projection_frame(p, s.warp_center, s.warp_extent);
    const double reach = std::max(
        s.warp_extent * 2, std::sqrt(warp_dot(frame.axes[1], frame.axes[1])) * (p.warp_depth + 1));
    for (const auto &cache : s.warp_caches)
      if (cache->generation == s.warp_generation && cache->material == set.material_session_uid &&
          cache->authored == author && cache->frame.origin == frame.origin &&
          cache->frame.axes == frame.axes && cache->reach == reach)
      {
        projection.warp_fit = cache;
        return;
      }
    const auto start = std::chrono::steady_clock::now();
    const auto cpu = std::clock();
    auto cache = std::make_shared<WarpGeometryCache>();
    cache->authored = author;
    cache->frame = frame;
    cache->center = s.warp_center;
    cache->extent = s.warp_extent;
    cache->reach = reach;
    cache->generation = s.warp_generation;
    cache->material = set.material_session_uid;
    cache->offset = p.offset3;
    cache->rotation = p.rotation3;
    cache->scale = p.scale3;
    cache->physical = p.physical;
    cache->physical_size = fill_physical_size(p);
    cache->cm_per_native = p.cm_per_native;
    BrushProjection ray_projection;
    ray_projection.cull = false;
    ray_projection.symmetry_ray = true;
    cache->fitted = fit_warp_geometry(*author,
                                      frame,
                                      reach,
                                      [&](const WarpVector &origin,
                                          const WarpVector &direction,
                                          double maximum) -> std::optional<WarpVector> {
                                        std::array<float, 3> from, to;
                                        for (int c = 0; c < 3; ++c) {
                                          from[c] = float(origin[c]);
                                          to[c] = float(direction[c]);
                                        }
                                        const auto hit = surface_brush_stamp(
                                            *s.main,
                                            *s.scene,
                                            *s.depsgraph,
                                            *s.view,
                                            set.material_session_uid,
                                            from,
                                            to,
                                            .001f,
                                            false,
                                            false,
                                            ray_projection,
                                            &s.displacement_preview.source_meshes());
                                        if (!hit)
                                          return {};
                                        WarpVector point;
                                        for (int c = 0; c < 3; ++c)
                                          point[c] = hit->position[c];
                                        const auto delta = warp_add(point, origin, -1);
                                        if (warp_dot(delta, direction) > maximum)
                                          return {};
                                        return point;
                                      });
    std::printf(
        "BTS_WARP_FIT_MS material=%u nodes=%zu hits=%d misses=%d wall_ms=%.3f cpu_ms=%.3f\n",
        set.material_session_uid,
        author->nodes.size(),
        cache->fitted.hits,
        cache->fitted.misses,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count(),
        1000. * double(std::clock() - cpu) / CLOCKS_PER_SEC);
    if (s.warp_caches.size() >= 128)
      s.warp_caches.erase(s.warp_caches.begin());
    s.warp_caches.push_back(cache);
    projection.warp_fit = std::move(cache);
  };
  // Instance evaluations are shared immutable snapshots. Clone before installing
  // consumer-specific fit caches so existing background jobs retain their grids.
  const auto prepare_layer = [&](const TextureLayer &layer) {
    if (layer.kind == LayerKind::Fill)
      prepare(layer.fill);
    if (layer.mask)
      for (const auto &effect : mask_effects_recursive(layer.mask->effects))
        if (effect.kind == MaskEffectKind::Fill && effect.anchor_source.isNull())
          prepare(effect.mapping.projection);
  };
  for (const auto &layer : set.layers)
    if (!layer.instance)
      prepare_layer(layer);
  if (set.instance_layers) {
    auto instances = std::make_shared<std::vector<TextureLayer>>(*set.instance_layers);
    for (const auto &layer : *instances)
      prepare_layer(layer);
    set.instance_layers = std::move(instances);
  }
}

void BlenderHost::evict_unloaded_resources(const TextureDocument &document)
{
  auto &s = *impl_;
  s.require_owner();
  s.enter_gpu();
  try {
    s.inspection.evict_unloaded_resources(document);
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
