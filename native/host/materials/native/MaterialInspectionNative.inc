// SPDX-License-Identifier: GPL-3.0-or-later
// Layer edits and the native material path: world-normal status, source queues, source edits.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
bool MaterialInspection::queue_layer_edit(const TextureSet &source,
                                          CompositeChannels channels,
                                          bool finalize)
{
  const auto set = resident_texture_view(source);
  PaintMetadataScope metadata;
  performance::Scope timing("queue_layer_edit");
  if (set.disabled || !set.uniform_editable)
    return true;
  impl_->gpu_preview.validate_native_mapping_instances(set);
  const auto invalidation_start = PropertyClock::now();
  auto found = impl_->bindings.find(set.material_session_uid);
  if (found == impl_->bindings.end())
    return false;
  if (!found->second.resolution) {
    update_pixels(*found->second.owner_main, set, set.texture_resolution, {}, true, true);
    channels = complete_composite_channels;
  }
  auto &binding = found->second;
  binding.mask_snapshot = std::make_unique<TextureSet>(set);
  if (const auto failed = impl_->failed_channels.find(set.material_session_uid);
      failed != impl_->failed_channels.end())
  {
    for (int c = 0; c < 17; ++c)
      channels[c] = channels[c] || failed->second[c];
  }
  std::map<int, std::pair<int, int>> sizes;
  for (int tile : set.tiles)
    sizes[tile] = {set.width(tile), set.rows(tile)};
  if (sizes != binding.tile_sizes)
    return false;
  // Prepare immutable geometry while ordinary layer edits are already settling,
  // so switching to a projection does not introduce a first-use CPU detour.
  if (binding.padding) {
    if (!binding.margin_job && !binding.margins_ready)
      binding.margin_job = MarginGeometryJob::start(binding.padding, sizes);
    const bool needs_surface = std::any_of(
        evaluation_layers(set).begin(), evaluation_layers(set).end(), [](const auto &layer) {
          return (layer.kind == LayerKind::Fill && bool(layer.fill.native_material)) ||
                 (layer.geometry.type == 0 && !layer.geometry.meshes.empty()) ||
                 std::any_of(layer.effects.begin(),
                             layer.effects.end(),
                             [](const auto &effect) {
                               return effect.enabled &&
                                      effect.kind == ContentEffectKind::Generator &&
                                      (effect.triplanar_grunge || effect.surface_fallback);
                             }) ||
                 (layer.mask &&
                  any_mask_effect(layer.mask->effects,
                                  [](const auto &effect) {
                                    return effect.enabled &&
                                           effect.kind == MaskEffectKind::Generator &&
                                           (effect.triplanar_grunge || effect.surface_fallback);
                                  })) ||
                 (layer.kind == LayerKind::Fill && fill_surface_projection(layer.fill) &&
                  (layer.fill.atom || fill_has_texture(layer.fill) ||
                   fill_spatial_projection(layer.fill))) ||
                 (layer.mask && any_mask_effect(layer.mask->effects, [](const auto &effect) {
                    return effect.enabled && effect.kind == MaskEffectKind::Fill &&
                           effect.anchor_source.isNull() &&
                           fill_surface_projection(effect.mapping.projection);
                  }));
        });
    if (needs_surface) {
      bool complete = binding.ownership_cache.size() == sizes.size();
      for (const auto &[tile, size] : sizes) {
        const auto found = binding.projection_cache.find(tile);
        complete &= found != binding.projection_cache.end() &&
                    found->second->resolution == size.first &&
                    found->second->height == size.second;
      }
      if (complete) {
        set.geometry_ownership = binding.ownership_cache;
        set.geometry_names = binding.geometry_names;
        set.projection_tiles = binding.projection_cache;
        set.projection_geometry = binding.projection_cache.begin()->second;
      }
      else if (!binding.projection_job)
        binding.projection_job = ProjectionGeometryJob::start(binding.padding, sizes);
    }
  }
  // Optional channel allocation changes topology once; its pixels are derived
  // by the existing preview/final jobs rather than recomputing every channel.
  if (set.displacement.allocated || binding.displacement_image)
    ensure_displacement_image(*binding.owner_main, binding, set);
  shader_parameters(*binding.owner_main, set);
  if (set.ao_channel || set.mesh_maps[3])
    ensure_ao_image(*binding.owner_main, binding, set, set.texture_resolution);
  for (int c = 0; c < 5; ++c)
    native_image_precision(binding.images[c], set.precision[c]);
  native_image_precision(binding.authored_normal_image, set.precision[3]);
  native_image_precision(binding.ao_image, set.precision[5]);
  native_image_precision(binding.displacement_image, set.displacement.precision);
  for (int i = 0; i < 8; ++i)
    native_image_precision(binding.user_images[i], set.users[i].precision);
  // Channel enablement changes the uniform base, not the image dimensions.
  // Keep the GPU stack path instead of a synchronous full-resolution re-evaluation.
  bool base_changed = false;
  auto &tree = *binding.material->nodetree;
  for (int c = 0; c < 5; ++c) {
    const auto base = encoded_material_base(set, c);
    if (!binding.bases_valid || base != binding.last_base[c]) {
      color(tree, input(*binding.base_mix[c], 1), base);
      binding.last_base[c] = base;
      base_changed = true;
    }
  }
  if (base_changed) {
    binding.bases_valid = true;
    BKE_ntree_update_after_single_tree_change(*binding.owner_main, tree);
    DEG_id_tag_update(&binding.material->id, ID_RECALC_SHADING);
  }
  // Presentation-only masks share GPU prefixes with material consumers. Instance
  // consumers with channel-dependent masks must recompute their coupled outputs.
  if ([&] {
        for (int c = 0; c < 17; ++c)
          if (c != 15 && channels[c])
            return true;
        return false;
      }() &&
      std::any_of(evaluation_layers(set).begin(),
                  evaluation_layers(set).end(),
                  [](const auto &layer) { return TextureDocument::anchor_dependent(layer); }))
    for (int c = 0; c < 17; ++c)
      channels[c] = channels[c] || complete_composite_channels[c];
  const auto &layers = evaluation_layers(set);
  const auto selected_mask = std::find_if(layers.begin(), layers.end(), [&](const auto &layer) {
    return layer.id == set.selected_layer;
  });
  const bool has_selected_mask = selected_mask != layers.end() && bool(selected_mask->mask);
  // A mask image can outlive its selection. Do not carry that presentation job
  // into a newly selected unmasked layer, or poison its next save as unsupported.
  if (!has_selected_mask) {
    binding.edit_channels[15] = false;
    binding.gpu_stack_active[15] = false;
    binding.mask_async_layer = QUuid();
    if (auto job = impl_->gpu_stack_queued.find(set.material_session_uid);
        job != impl_->gpu_stack_queued.end())
    {
      job->second.channels[15] = false;
      if (std::none_of(job->second.channels.begin(), job->second.channels.end(), [](bool value) {
            return value;
          }))
        impl_->gpu_stack_queued.erase(job);
    }
    if (auto failed = impl_->failed_channels.find(set.material_session_uid);
        failed != impl_->failed_channels.end())
    {
      failed->second[15] = false;
      if (std::none_of(
              failed->second.begin(), failed->second.end(), [](bool value) { return value; }))
      {
        impl_->failed_channels.erase(failed);
        impl_->failures.erase(set.material_session_uid);
      }
    }
  }
  // Material-view edits can change an upstream content anchor while its old
  // mask inspection image is unbound. Its layer ID alone does not establish
  // freshness: reselecting that same consumer must queue the latest prefix.
  if (!channels[15])
    for (int c = 0; c < 17; ++c)
      if (c != 15 && channels[c]) {
        binding.mask_async_layer = QUuid();
        break;
      }
  channels[15] = (channels[15] || set.selected_mask ||
                  binding.displayed_channel == int(InspectionMode::Mask)) &&
                 has_selected_mask && binding.mask_image && binding.mask_texture;
  if (channels[15])
    binding.mask_async_layer = set.selected_layer;
  bool any = false;
  for (int c = 0; c < 17; ++c) {
    if (channels[c] && composite_image(binding, c) && composite_texture(binding, c))
      any = true;
    else
      channels[c] = false;
  }
  if (!any)
    return true;
  impl_->failed_channels.erase(set.material_session_uid);
  impl_->failures.erase(set.material_session_uid);
  for (int c = 0; c < 17; ++c)
    binding.edit_channels[c] = binding.edit_channels[c] || channels[c];
  const auto uid = set.material_session_uid;
  // Keep the full-size GPU images bound. The newest revision replaces queued work.
  if (impl_->pending && impl_->pending->result.uid == uid)
    impl_->pending.reset();
  const auto generation = ++impl_->generation[uid];
  impl_->queued.erase(uid);
  if (impl_->job.valid() && impl_->running_uid == uid && impl_->cancel)
    impl_->cancel->store(true);
  Impl::Request request{
      set, binding.padding, generation, bool(binding.ao_image), binding.edit_channels, true};
  request.trace = property_current;
  request.trace.invalidation_ms = property_ms(invalidation_start);
  request.finalize = finalize;
  binding.gpu_uniform_values = {};
  // Uniform layers use the same GPU stack as textured layers. Evaluating even
  // a single CPU pixel here bypassed its cache/dependency rules and duplicated
  // blend logic on every property event.
  impl_->gpu_queued.erase(uid);
  auto unsupported = request.channels;
  auto gpu_request = std::move(request);
  gpu_request.channels = {};
  bool stack = false;
  for (int c = 0; c < 17; ++c)
    if (binding.edit_channels[c]) {
      binding.gpu_unbound_waiting[c] = false;
      binding.gpu_stack_active[c] = false;
      if (!(c < 5 && binding.gpu_uniform_values[c]) && GpuLayerPreview::supported(set, c)) {
        gpu_request.channels[c] = true;
        stack = true;
      }
    }
  // Any logged unsupported fallback also stages at document resolution.
  for (int c = 0; c < 17; ++c)
    if (gpu_request.channels[c])
      unsupported[c] = false;
  if (stack) {
    if (performance::enabled() && impl_->gpu_stack_queued.contains(uid))
      performance::record({performance::flow(), "gpu_stack_replace", 0, 0, 0, true});
    impl_->gpu_stack_queued.insert_or_assign(uid, std::move(gpu_request));
  }
  else
    impl_->gpu_stack_queued.erase(uid);
  property_pipeline_pending = true;
  if (std::any_of(unsupported.begin(), unsupported.end(), [](bool on) { return on; }))
  {
    pending_composite_error =
        "GPU layer operation is unsupported; retaining the previous image (CPU fallback disabled)";
    impl_->failures[uid] = pending_composite_error;
    impl_->failed_channels[uid] = unsupported;
    std::fprintf(
        stderr,
        "BTS_GPU_STACK_ERROR uid=%u reason=unsupported-operator-or-inspection cpu_fallback=0\n",
        uid);
  }
  return true;
}
QJsonObject MaterialInspection::native_world_normal_status(std::uint32_t uid) const
{
  const auto at = impl_->bindings.find(uid);
  if (at == impl_->bindings.end())
    return {{"enabled", false}, {"full_targets", false}, {"pending", false}};
  const auto &b = at->second;
  const auto &library = impl_->gpu_preview.native_world_sources();
  const auto stats = library.statistics();
  const bool enabled = b.world_error.empty() && !b.world_layers_ready.empty() &&
                       b.world_tile == b.world_tiles.size();
  QJsonObject result{
      {"route", "layered-tangent-normal"},
      {"normal_layer_tiles", qint64(b.world_layers_ready.size())},
      {"material_uid", QString::number(uid)},
      {"enabled", enabled},
      {"full_targets", b.world_images_full},
      {"error", QString::fromStdString(b.world_error)},
      {"geometry_revision", QString::number(b.world_geometry_revision)},
      {"publication_generation", QString::number(b.world_publication_generation)},
      {"pending",
       bool(b.world_geometry_job) || (b.world_set && b.world_tile < b.world_tiles.size())},
      {"completed_tiles", qint64(b.world_tile)},
      {"target_tiles", qint64(b.world_tiles.size())},
      {"rendered_tiles", QString::number(b.world_rendered_tiles)},
      {"programs", qint64(stats.world_programs)},
      {"dispatches", QString::number(stats.world_dispatches)},
      {"cycles_entry_epoch", QString::number(impl_->cycles.entry_epoch())},
      {"cycles_pair_ready",
       b.world_normal_images[0] && b.world_normal_images[1] &&
           impl_->cycles.image_ready(b.world_normal_images[0]->id.session_uid) &&
           impl_->cycles.image_ready(b.world_normal_images[1]->id.session_uid)}};
  int private_images = 0, private_cpu_buffers = 0;
  QJsonArray handles, mappings;
  for (auto *image : b.world_normal_images) {
    private_images += image != nullptr;
    private_cpu_buffers += image && BKE_image_has_loaded_ibuf(image);
    blender::gpu::Texture *texture = nullptr;
    if (image) {
      const auto target = BKE_image_gpu_override_texture(image);
      if (target.texture)
        texture = *target.texture;
    }
    handles.append(QString::number(quintptr(texture), 16));
    mappings.append(QString::number(
        quintptr(image && image->runtime ? image->runtime->gputexture[TEXTARGET_TILE_MAPPING][0] :
                                           nullptr),
        16));
  }
  result["private_images"] = private_images;
  result["private_cpu_buffers"] = private_cpu_buffers;
  result["gpu_handles"] = handles;
  result["gpu_tile_maps"] = mappings;
  if (b.mask_snapshot) {
    result["artist_normal_enabled"] = b.mask_snapshot->channels[3];
    if (!b.mask_snapshot->selected_layer.isNull()) {
      const auto &layers = evaluation_layers(*b.mask_snapshot);
      const auto selected = std::find_if(layers.begin(), layers.end(), [&](const auto &l) {
        return l.id == b.mask_snapshot->selected_layer;
      });
      if (selected != layers.end()) {
        result["layer_bump_enabled"] = selected->channels[4];
        if (selected->fill.native_material)
          result["source_policy"] = native_height_policy_name(
              selected->fill.native_material->height_policy);
      }
    }
  }
  const auto signature = QByteArray::fromStdString(b.world_published_source_signature);
  result["published_source_signature"] = QString::fromLatin1(
      QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex());
  if (b.world_geometry) {
    const auto &g = b.world_geometry_diagnostics;
    result["vertex_uploads"] = qint64(g.vertex_uploads);
    result["index_uploads"] = qint64(g.index_uploads);
    result["object_uploads"] = qint64(g.object_uploads);
  }
  return result;
}
QJsonArray MaterialInspection::diagnostic_native_world_normal_samples(std::uint32_t uid,
                                                                      bool edge) const
{
  if (!qEnvironmentVariableIsSet("BTS_NATIVE_WORLD_NORMAL_UI"))
    throw std::logic_error("Native world Normal probes require the explicit diagnostic gate");
  const auto at = impl_->bindings.find(uid);
  if (at == impl_->bindings.end() || !at->second.mask_snapshot)
    return {};
  const auto &b = at->second;
  const auto &set = *b.mask_snapshot;
  QJsonArray samples;
  for (int tile : set.tiles) {
    const std::array<std::array<int, 2>, 2> points =
        edge ? std::array<std::array<int, 2>, 2>{{{std::max(0, set.width(tile) / 8 - 1),
                                                   set.rows(tile) / 2},
                                                  {set.width(tile) / 8, set.rows(tile) / 2}}} :
               std::array<std::array<int, 2>, 2>{{{set.width(tile) / 4, set.rows(tile) / 2},
                                                  {set.width(tile) / 2, set.rows(tile) / 2}}};
    std::array<std::vector<std::array<float, 4>>, 2> sides;
    if (b.world_images_full)
      for (int side = 0; side < 2; ++side) {
        ImageGpuTileRegion region;
        if (!BKE_image_gpu_override_tile_region(b.world_normal_images[side], tile, region))
          throw std::runtime_error("Native world probe tile missing");
        const auto target = BKE_image_gpu_override_texture(b.world_normal_images[side]);
        if (!target.texture || !*target.texture)
          throw std::runtime_error("Native world probe target missing");
        std::vector<std::array<int, 3>> positions;
        for (auto p : points)
          positions.push_back({p[0] + region.x, p[1] + region.y, region.layer});
        sides[side] = probe_gpu_texture(*target.texture, positions);
      }
    const auto bump = diagnostic_gpu_pixels(uid, 4, points, tile);
    for (std::size_t i = 0; i < points.size(); ++i) {
      QJsonObject row{{"tile", tile}, {"x", points[i][0]}, {"y", points[i][1]}};
      for (int side = 0; side < 2; ++side) {
        QJsonArray values;
        if (!sides[side].empty())
          for (float v : sides[side][i])
            values.append(v);
        row[side ? "back" : "front"] = values;
      }
      QJsonArray scalar;
      for (float v : bump[i])
        scalar.append(v);
      row["bump"] = scalar;
      samples.append(row);
    }
  }
  return samples;
}
QJsonObject MaterialInspection::diagnostic_native_source_queues() const
{
  const auto flags = [](const CompositeChannels &channels) {
    QJsonArray values;
    for (bool value : channels)
      values.append(value);
    return values;
  };
  const auto values = [](const auto &uniform) {
    QJsonArray result;
    for (const auto &value : uniform) {
      if (!value) {
        result.append(QJsonValue());
        continue;
      }
      QJsonArray rgba;
      for (float v : *value)
        rgba.append(v);
      result.append(rgba);
    }
    return result;
  };
  const auto snapshot = [](const TextureSet &set) {
    QJsonArray layers;
    for (const auto &layer : evaluation_layers(set)) {
      QJsonObject row{{"id", layer.id.toString()},
                      {"source", QString::fromStdString(layer.fill.material_resource)}};
      if (layer.fill.native_material)
        row["native"] = native_material_description_json(*layer.fill.native_material);
      layers.append(row);
    }
    return QJsonObject{{"set", set.id.toString()},
                       {"selected_layer", set.selected_layer.toString()},
                       {"layers", layers}};
  };
  const auto requests = [&](const auto &queue) {
    QJsonObject result;
    for (const auto &[uid, request] : queue)
      result[QString::number(uid)] = QJsonObject{
          {"generation", QString::number(request.generation)},
          {"channels", flags(request.channels)},
          {"snapshot", snapshot(request.set)}};
    return result;
  };
  QJsonObject generations, failures, failed, uniform, bindings;
  for (const auto &[uid, generation] : impl_->generation)
    generations[QString::number(uid)] = QString::number(generation);
  for (const auto &[uid, error] : impl_->failures)
    failures[QString::number(uid)] = QString::fromStdString(error);
  for (const auto &[uid, channels] : impl_->failed_channels)
    failed[QString::number(uid)] = flags(channels);
  for (const auto &[uid, request] : impl_->gpu_queued)
    uniform[QString::number(uid)] = QJsonObject{
        {"generation", QString::number(request.generation)}, {"values", values(request.values)}};
  for (auto &[uid, binding] : impl_->bindings) {
    QJsonArray handles;
    for (int c = 0; c < 17; ++c) {
      auto *image = composite_image(binding, c);
      QJsonArray native{QString::number(quintptr(image), 16)};
      for (int target : {TEXTARGET_2D, TEXTARGET_2D_ARRAY})
        native.append(QString::number(
            quintptr(image && image->runtime ? image->runtime->gputexture[target][0] : nullptr),
            16));
      handles.append(native);
    }
    QJsonObject state{{"handles", handles},
                      {"edit", flags(binding.edit_channels)},
                      {"active", flags(binding.gpu_stack_active)},
                      {"waiting", flags(binding.gpu_unbound_waiting)},
                      {"mask_async", binding.mask_async_layer.toString()},
                      {"uniform", values(binding.gpu_uniform_values)},
                      {"snapshot_generation", QString::number(binding.gpu_snapshot_generation)}};
    state["native_world_error"] = QString::fromStdString(binding.world_error);
    state["native_world_tile"] = qint64(binding.world_tile);
    state["native_world_geometry_revision"] = QString::number(binding.world_geometry_revision);
    state["native_world_view_generation"] = QString::number(
        binding.world_context.view.generation());
    if (binding.mask_snapshot)
      state["mask_snapshot"] = snapshot(*binding.mask_snapshot);
    if (binding.gpu_stack_set)
      state["gpu_snapshot"] = snapshot(*binding.gpu_stack_set);
    bindings[QString::number(uid)] = state;
  }
  QJsonObject result{{"generation", generations},
                     {"queued", requests(impl_->queued)},
                     {"gpu_queued", uniform},
                     {"stack_queued", requests(impl_->gpu_stack_queued)},
                     {"failures", failures},
                     {"failed_channels", failed},
                     {"bindings", bindings},
                     {"pending_cpu", impl_->pending ? qint64(impl_->pending->result.uid) : -1},
                     {"running_cpu", impl_->job.valid() ? qint64(impl_->running_uid) : -1},
                     {"cpu_cancel", impl_->cancel ? impl_->cancel->load() : false}};
  if (std::getenv("BTS_BM_ADJUSTMENT_DEMO"))
    result["bm_cycles_mirrors"] = impl_->cycles.diagnostic_publication_packets();
  if (std::getenv("BTS_BK_FOLDER_MASK"))
    result["bk_cycles_mirrors"] = impl_->cycles.diagnostic_publication_packets();
  if (std::getenv("BTS_BJ_PROJECTION_UI")) {
    const auto native = impl_->gpu_preview.native_statistics();
    result["bj_native_gpu"] = QJsonObject{{"sources", qint64(native[0])},
                                          {"programs", qint64(native[1])},
                                          {"dispatches", qint64(native[2])}};
    result["bj_cycles_mirrors"] = impl_->cycles.diagnostic_publication_packets();
  }
  return result;
}
NativeSourceEditBatch MaterialInspection::prepare_native_source_edits(
    std::span<const NativeSourceEdit> edits)
{
  // Admit every candidate before even constructing a queue transaction.
  for (const auto &edit : edits) {
    if (!edit.before || !edit.after)
      throw std::invalid_argument("Native source queue snapshot is missing");
    validate_native_mapping_edit(*edit.before, *edit.after);
  }
  PaintMetadataScope metadata;
  struct Patch {
    Binding *binding;
    std::unique_ptr<TextureSet> mask_snapshot;
    QUuid mask_async_layer;
    CompositeChannels edit_channels, gpu_stack_active, gpu_unbound_waiting;
    std::array<std::optional<std::array<float, 4>>, 5> gpu_uniform_values{};
  };
  struct Plan {
    decltype(Impl::generation) generation;
    decltype(Impl::queued) queued;
    decltype(Impl::gpu_queued) gpu_queued;
    decltype(Impl::gpu_stack_queued) gpu_stack_queued;
    decltype(Impl::failures) failures;
    decltype(Impl::failed_channels) failed_channels;
    std::vector<Patch> patches;
  };
  std::uint64_t bytes = resource_product(edits.size(), sizeof(Patch) + 2 * sizeof(Impl::Request));
  for (const auto &[uid, request] : impl_->gpu_stack_queued)
    bytes = resource_sum(
        bytes,
        resource_sum(sizeof(Impl::Request),
                     resource_product(request.set.layers.size(), sizeof(TextureLayer))));
  bytes = resource_sum(
      bytes,
      resource_product(
          resource_sum(impl_->generation.size(),
                       resource_sum(impl_->queued.size(),
                                    resource_sum(impl_->gpu_queued.size(),
                                                 resource_sum(impl_->failures.size(),
                                                              impl_->failed_channels.size())))),
          sizeof(Impl::Request)));

  auto plan = std::make_shared<Plan>();
  plan->generation = impl_->generation;
  plan->queued = impl_->queued;
  plan->gpu_queued = impl_->gpu_queued;
  plan->gpu_stack_queued = impl_->gpu_stack_queued;
  plan->failures = impl_->failures;
  plan->failed_channels = impl_->failed_channels;
  plan->patches.reserve(edits.size());
  std::set<std::uint32_t> seen;
  for (const auto &edit : edits) {
    if (!edit.before || !edit.after)
      throw std::invalid_argument("Native source queue snapshot is missing");
    const auto &before = *edit.before;
    const auto &set = *edit.after;
    const auto uid = set.material_session_uid;
    if (uid != before.material_session_uid || set.id != before.id || !seen.insert(uid).second)
      throw std::invalid_argument("Native source queue identity changed");
    if (set.disabled || !set.uniform_editable)
      continue;
    const auto found = impl_->bindings.find(uid);
    if (found == impl_->bindings.end() || !found->second.resolution)
      throw std::runtime_error(
          "Native image replacement requires an initialized material preview");
    auto &binding = found->second;
    if (impl_->queued.contains(uid) || (impl_->pending && impl_->pending->result.uid == uid) ||
        (impl_->job.valid() && impl_->running_uid == uid))
      throw std::runtime_error(
          "Wait for the previous material preview before replacing its image");
    std::map<int, std::pair<int, int>> sizes;
    for (int tile : set.tiles)
      sizes[tile] = {set.width(tile), set.rows(tile)};
    if (sizes != binding.tile_sizes || set.precision != before.precision ||
        set.base_values != before.base_values || set.channels != before.channels ||
        set.shader_parameters != before.shader_parameters ||
        set.shader_parameters != binding.shader_parameters ||
        set.displacement != before.displacement ||
        set.displacement != binding.displacement_settings ||
        set.normal_format != before.normal_format || set.ao_channel != before.ao_channel)
      throw std::runtime_error(
          "Native image replacement requires unchanged ready material targets and shader settings");
    const auto same_layering = [](const MaterialLayering &a, const MaterialLayering &b) {
      if (a.enabled != b.enabled || a.mask_slots != b.mask_slots || a.mask_roots != b.mask_roots ||
          a.active_mask != b.active_mask || a.main_selection != b.main_selection ||
          a.mask_selections != b.mask_selections)
        return false;
      for (int i = 0; i < 4; ++i) {
        const auto &x = a.materials[i], &y = b.materials[i];
        if (x.name != y.name || x.resource != y.resource || x.values != y.values ||
            x.textures != y.textures || x.tiling != y.tiling || x.offset != y.offset ||
            x.rotation != y.rotation)
          return false;
      }
      return true;
    };
    if (!same_layering(before.material_layering, set.material_layering) ||
        !same_layering(binding.material_layering, set.material_layering) ||
        binding.layering_directx != (set.normal_format == NormalMapFormat::DirectX))
      throw std::runtime_error(
          "Native image replacement is waiting for its layered shader settings");
    const auto check_precision = [](Image *image, ChannelPrecision precision) {
      if (image && native_gpu_image(image) &&
          bool(image->flag & IMA_HIGH_BITDEPTH) !=
              (precision == ChannelPrecision::L32F || precision == ChannelPrecision::RGB32F))
        throw std::runtime_error("Native image replacement is waiting for target precision");
    };
    for (int c = 0; c < 5; ++c) {
      if (!binding.bases_valid || binding.last_base[c] != encoded_material_base(set, c))
        throw std::runtime_error("Native image replacement is waiting for its material base");
      check_precision(binding.images[c], set.precision[c]);
    }
    check_precision(binding.authored_normal_image, set.precision[3]);
    check_precision(binding.ao_image, set.precision[5]);
    check_precision(binding.displacement_image, set.displacement.precision);
    for (int c = 0; c < custom_channel_count; ++c) {
      if (set.users[c].present != binding.user_present[c] ||
          set.users[c].present != before.users[c].present ||
          set.users[c].allocated != before.users[c].allocated ||
          set.users[c].precision != before.users[c].precision)
        throw std::runtime_error("Native image replacement is waiting for custom channel targets");
      check_precision(binding.user_images[c], set.users[c].precision);
    }
    // Adopt only geometry already prepared by normal rendering. No workers or
    // native image/node allocation are started by this transaction.
    TextureSet snapshot = set;
    snapshot.geometry_ownership = binding.ownership_cache;
    snapshot.geometry_names = binding.geometry_names;
    snapshot.projection_tiles = binding.projection_cache;
    if (!binding.projection_cache.empty())
      snapshot.projection_geometry = binding.projection_cache.begin()->second;
    auto channels = edit.channels;
    if (const auto failed = plan->failed_channels.find(uid); failed != plan->failed_channels.end())
      for (int c = 0; c < 17; ++c)
        channels[c] = channels[c] || failed->second[c];
    if (std::any_of(evaluation_layers(snapshot).begin(),
                    evaluation_layers(snapshot).end(),
                    [](const auto &layer) { return TextureDocument::anchor_dependent(layer); }))
      for (int c = 0; c < 17; ++c)
        channels[c] = channels[c] || complete_composite_channels[c];
    Patch patch{&binding,
                std::make_unique<TextureSet>(snapshot),
                binding.mask_async_layer,
                binding.edit_channels,
                binding.gpu_stack_active,
                binding.gpu_unbound_waiting};
    const auto &layers = evaluation_layers(snapshot);
    const auto selected = std::find_if(layers.begin(), layers.end(), [&](const auto &layer) {
      return layer.id == snapshot.selected_layer;
    });
    const bool has_mask = selected != layers.end() && bool(selected->mask);
    if (!has_mask) {
      patch.edit_channels[15] = patch.gpu_stack_active[15] = false;
      patch.mask_async_layer = QUuid();
    }
    if (!channels[15])
      for (int c = 0; c < 17; ++c)
        if (c != 15 && channels[c]) {
          patch.mask_async_layer = QUuid();
          break;
        }
    channels[15] = (channels[15] || snapshot.selected_mask ||
                    binding.displayed_channel == int(InspectionMode::Mask)) &&
                   has_mask && binding.mask_image && binding.mask_texture;
    if (channels[15])
      patch.mask_async_layer = snapshot.selected_layer;
    bool any = false;
    for (int c = 0; c < 17; ++c) {
      const bool required =
          c < 5 || c == 6 || (c == 5 && (set.ao_channel || set.mesh_maps[3])) ||
          (c >= 7 && c < 15 && (set.users[c - 7].allocated || set.users[c - 7].present)) ||
          (c == 15 && has_mask &&
           (set.selected_mask || binding.displayed_channel == int(InspectionMode::Mask))) ||
          (c == 16 && set.displacement.allocated);
      const bool target = composite_image(binding, c) && composite_texture(binding, c);
      if (channels[c] && required && !target)
        throw std::runtime_error(
            "Native image replacement is waiting for a required material channel target");
      if (channels[c] && target)
        any = true;
      else
        channels[c] = false;
      patch.edit_channels[c] = patch.edit_channels[c] || channels[c];
    }
    if (!any)
      continue;
    auto &generation = plan->generation[uid];
    if (generation == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Native image queue generation exhausted");
    ++generation;
    Impl::Request request{snapshot, binding.padding, generation, bool(binding.ao_image), {}, true};
    request.trace = property_current;
    bool stack = false;
    for (int c = 0; c < 17; ++c)
      if (patch.edit_channels[c]) {
        if (!composite_image(binding, c) || !composite_texture(binding, c) ||
            !GpuLayerPreview::supported(snapshot, c))
          throw std::runtime_error(
              "Native image replacement has an unsupported active material channel");
        request.channels[c] = true;
        stack = true;
        patch.gpu_stack_active[c] = patch.gpu_unbound_waiting[c] = false;
      }
    plan->queued.erase(uid);
    plan->gpu_queued.erase(uid);
    plan->failures.erase(uid);
    plan->failed_channels.erase(uid);
    if (stack)
      plan->gpu_stack_queued.insert_or_assign(uid, std::move(request));
    else
      plan->gpu_stack_queued.erase(uid);
    plan->patches.push_back(std::move(patch));
  }
  static_assert(std::is_nothrow_swappable_v<decltype(Plan::generation)>);
  static_assert(std::is_nothrow_swappable_v<decltype(Plan::gpu_stack_queued)>);
  // MSVC containers can allocate during moves. Allocate snapshots while
  // preparing the plan so publication still performs only nonthrowing swaps.
  static_assert(std::is_nothrow_swappable_v<decltype(Patch::mask_snapshot)>);
  return NativeSourceEditBatch([this, plan]() noexcept {
    impl_->generation.swap(plan->generation);
    impl_->queued.swap(plan->queued);
    impl_->gpu_queued.swap(plan->gpu_queued);
    impl_->gpu_stack_queued.swap(plan->gpu_stack_queued);
    impl_->failures.swap(plan->failures);
    impl_->failed_channels.swap(plan->failed_channels);
    for (auto &patch : plan->patches) {
      auto &binding = *patch.binding;
      binding.mask_snapshot.swap(patch.mask_snapshot);
      std::swap(binding.mask_async_layer, patch.mask_async_layer);
      binding.edit_channels.swap(patch.edit_channels);
      binding.gpu_stack_active.swap(patch.gpu_stack_active);
      binding.gpu_unbound_waiting.swap(patch.gpu_unbound_waiting);
      binding.gpu_uniform_values.swap(patch.gpu_uniform_values);
    }
    if (!plan->patches.empty())
      property_pipeline_pending = true;
  });
}
bool MaterialInspection::take_displacement_updates()
{
  return std::exchange(impl_->gpu_displacement_changed, false);
}
void MaterialInspection::diagnostic_adjustment_budget(std::optional<std::uint64_t> limit)
{
  impl_->gpu_preview.diagnostic_adjustment_budget(limit);
}
std::shared_ptr<const AdjustmentPaintSnapshot> MaterialInspection::begin_adjustment_source(
    const TextureSet &source, const QUuid &layer, const QUuid &document)
{
  const auto found = impl_->bindings.find(source.material_session_uid);
  if (found == impl_->bindings.end())
    throw std::runtime_error("Adjustment target has no prepared material binding");
  const auto &binding = found->second;
  auto set = source;
  set.projection_tiles = binding.projection_cache;
  set.geometry_ownership = binding.ownership_cache;
  set.geometry_names = binding.geometry_names;
  if (!binding.projection_cache.empty())
    set.projection_geometry = binding.projection_cache.begin()->second;
  return impl_->gpu_preview.begin_adjustment_source(set, layer, document);
}
TextureSet MaterialInspection::prepare_layer_capture(const TextureSet &set) const
{
  return impl_->gpu_preview.prepare_layer_capture(set);
}
std::uint64_t MaterialInspection::request_layer_capture(const TextureSet &set,
                                                        const LayerCaptureRequest &request)
{
  const auto ticket = ++impl_->layer_capture_ticket;
  impl_->layer_capture_failure.reset();
  impl_->layer_capture_job = Impl::LayerCaptureJob{set, request, ticket};
  return ticket;
}
std::optional<LayerCaptureResult> MaterialInspection::layer_capture_result() const
{
  if (impl_->layer_capture_failure)
    return impl_->layer_capture_failure;
  return impl_->gpu_preview.layer_capture_result();
}
std::uint64_t MaterialInspection::request_thumbnail(const TextureSet &set,
                                                    const ThumbnailRequest &request)
{
  const auto ticket = ++impl_->thumbnail_ticket;
  impl_->thumbnail_failure.reset();
  impl_->thumbnail_job = Impl::ThumbnailJob{set, request, ticket};
  return ticket;
}
std::optional<ThumbnailMetadata> MaterialInspection::thumbnail_result() const
{
  if (impl_->thumbnail_failure)
    return impl_->thumbnail_failure;
  return impl_->gpu_preview.thumbnail_result();
}
std::uint64_t MaterialInspection::request_material_sample(const TextureSet &set,
                                                          const MaterialSampleRequest &request)
{
  const auto ticket = ++impl_->material_sample_ticket;
  impl_->material_sample_failure.reset();
  impl_->material_sample_job = Impl::MaterialSampleJob{set, request, ticket};
  return ticket;
}
std::optional<MaterialSample> MaterialInspection::material_sample_result() const
{
  if (impl_->material_sample_failure)
    return impl_->material_sample_failure;
  return impl_->gpu_preview.material_sample_result();
}
std::uint64_t MaterialInspection::request_histogram(const TextureSet &set,
                                                    const HistogramRequest &request)
{
  const auto ticket = ++impl_->histogram_ticket;
  impl_->histogram_failure.reset();
  impl_->histogram_job = Impl::HistogramJob{set, request, ticket};
  return ticket;
}
std::optional<HistogramMetadata> MaterialInspection::histogram_result(const QUuid &id) const
{
  if (impl_->histogram_failure && impl_->histogram_failure->first == id)
    return impl_->histogram_failure->second;
  return impl_->gpu_preview.histogram_result(id);
}
bool MaterialInspection::prepare_texture_resource(
    const std::shared_ptr<const FillTexture> &texture)
{
  return prepare_texture_resources({texture});
}
bool MaterialInspection::prepare_texture_resources(
    const std::vector<std::shared_ptr<const FillTexture>> &textures)
{
  if (!impl_->texture_preparation_owner.isNull())
    return false;
  if (textures.empty() ||
      std::any_of(textures.begin(), textures.end(), [](const auto &texture) { return !texture; }))
    throw std::invalid_argument("Cannot prepare an empty texture input group");
  if (impl_->preparing_textures != textures) {
    impl_->preparing_textures = textures;
    impl_->texture_upload_error.clear();
  }
  if (!impl_->texture_upload_error.empty())
    throw std::runtime_error(impl_->texture_upload_error);
  impl_->texture_upload_waiting = !std::all_of(
      textures.begin(), textures.end(), [&](const auto &texture) {
        return impl_->gpu_preview.texture_resource_ready(*texture);
      });
  return !impl_->texture_upload_waiting;
}
void MaterialInspection::retire_layer_channel(const TextureDocument &document,
                                              const TextureSet &set,
                                              const TextureLayer &previous,
                                              int channel)
{
  const std::vector<std::shared_ptr<const FillTexture>> empty;
  impl_->gpu_preview.retire_layer_channel(
      document,
      set,
      previous,
      channel,
      impl_->texture_upload_waiting ? impl_->preparing_textures : empty);
}
void MaterialInspection::discard_binding(std::uint32_t uid)
{
  impl_->invalidate(uid);
  if (auto found = impl_->bindings.find(uid); found != impl_->bindings.end()) {
    if (found->second.margin_job)
      found->second.margin_job->cancelled = true;
    if (found->second.projection_job)
      found->second.projection_job->cancelled = true;
    if (found->second.world_geometry_job)
      found->second.world_geometry_job->cancelled = true;
    impl_->bindings.erase(found);
  }
  ++impl_->generation[uid];
}
void MaterialInspection::cancel_texture_preparation()
{
  if (!impl_->texture_preparation_owner.isNull())
    return;
  impl_->preparing_textures.clear();
  impl_->texture_upload_waiting = false;
  impl_->texture_upload_error.clear();
}
bool MaterialInspection::claim_texture_preparation(const QUuid &owner)
{
  if (owner.isNull())
    throw std::invalid_argument("Texture preparation requires a nonempty owner token");
  if (!impl_->texture_preparation_owner.isNull())
    return impl_->texture_preparation_owner == owner;
  if (impl_->texture_upload_waiting || !impl_->texture_upload_error.empty())
    return false;
  // Legacy callers retain their ready vector after successful uploads. Clearing
  // only this completed bookkeeping does not evict any resident GPU resource.
  impl_->preparing_textures.clear();
  impl_->texture_preparation_owner = owner;
  return true;
}
bool MaterialInspection::prepare_texture_resources(
    const QUuid &owner, const std::vector<std::shared_ptr<const FillTexture>> &textures)
{
  if (owner.isNull() || impl_->texture_preparation_owner != owner)
    throw std::logic_error("Texture preparation owner changed");
  if (textures.empty() ||
      std::any_of(textures.begin(), textures.end(), [](const auto &texture) { return !texture; }))
    throw std::invalid_argument("Cannot prepare an empty texture input group");
  if (impl_->preparing_textures != textures) {
    impl_->preparing_textures = textures;
    impl_->texture_upload_error.clear();
  }
  if (!impl_->texture_upload_error.empty())
    throw std::runtime_error(impl_->texture_upload_error);
  impl_->texture_upload_waiting = !std::all_of(
      textures.begin(), textures.end(), [&](const auto &texture) {
        return impl_->gpu_preview.texture_resource_ready(*texture);
      });
  return !impl_->texture_upload_waiting;
}
bool MaterialInspection::cancel_texture_preparation(const QUuid &owner)
{
  if (owner.isNull() || impl_->texture_preparation_owner != owner)
    return false;
  impl_->texture_preparation_owner = QUuid();
  impl_->preparing_textures.clear();
  impl_->texture_upload_waiting = false;
  impl_->texture_upload_error.clear();
  return true;
}

bool MaterialInspection::updates_pending(bool include_preparation) const
{
  return impl_->gpu_preview.programs_pending() || impl_->cycles.pending() ||
         impl_->layering_sources_pending ||
         (include_preparation &&
          (impl_->gpu_preview.native_preparations_pending() || impl_->texture_upload_waiting)) ||
         bool(impl_->layer_capture_job) || bool(impl_->thumbnail_job) ||
         bool(impl_->material_sample_job) || bool(impl_->histogram_job) ||
         impl_->gpu_preview.metadata_pending() || impl_->cpu_pending() ||
         impl_->gpu_mips_pending || !impl_->gpu_queued.empty() ||
         !impl_->gpu_stack_queued.empty() ||
         std::any_of(impl_->bindings.begin(), impl_->bindings.end(), [](const auto &entry) {
           return entry.second.world_view_dirty || entry.second.world_objects_dirty ||
                  bool(entry.second.margin_job) || bool(entry.second.projection_job) ||
                  NativeWorldNormalContextRoute::producer_pending(
                      entry.second.world_set.has_value(), bool(entry.second.world_geometry_job)) ||
                  (entry.second.world_set &&
                   entry.second.world_tile < entry.second.world_tiles.size());
         });
}
