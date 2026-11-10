// SPDX-License-Identifier: GPL-3.0-or-later
// Texture sets: residency, apply, shader parameters, imported materials and export sources.
// A section of native/host/BlenderHost.cc, which includes it in place; not a standalone file.
void BlenderHost::change_texture_residency(const TextureSet &set)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing)
    throw std::logic_error("Residency requires an idle viewport");
  s.enter_gpu();
  try {
    if (!set.uniform_editable) {
      auto *material = static_cast<Material *>(nullptr);
      for (auto &m : s.main->materials)
        if (m.id.session_uid == set.material_session_uid)
          material = &m;
      if (!material || !material->nodetree)
        throw std::runtime_error("Imported material is unavailable");
      s.free_targets();
      s.imported_shader_cache[set.material_session_uid].change(*s.main, *material, set);
      DEG_id_tag_update_ex(s.main, &material->id, ID_RECALC_SHADING | ID_RECALC_SYNC_TO_EVAL);
      DEG_id_tag_update_ex(s.main, &material->nodetree->id, ID_RECALC_NTREE_OUTPUT);
      BKE_scene_graph_evaluated_ensure(s.depsgraph, s.main);
    }
    else
      s.inspection.change_residency(*s.main, set);
    s.uv_dirty = true;
    s.texture_dirty = true;
    s.uv_preview.tag_images_updated();
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::apply_texture_set(const TextureSet &set,
                                    int resolution,
                                    PaintPixels::Bounds bounds,
                                    bool interactive)
{
  // All workspace callers share the GPU path, including first-use, resize and
  // reimport. CPU evaluation is reserved for explicit export/diagnostic APIs.
  if (set.disabled || !set.uniform_editable)
    return;
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing)
    throw std::logic_error("Paint update outside idle session");
  prepare_paint_texture(set);
  if (!s.inspection.queue_layer_edit(set, complete_composite_channels, !interactive)) {
    s.inspection.update_pixels(*s.main, set, set.texture_resolution, {}, true, true);
    if (!s.inspection.queue_layer_edit(set, complete_composite_channels, !interactive))
      throw std::runtime_error("GPU texture-set allocation failed");
  }
  if (set.displacement.allocated)
    s.displacement_preview.apply(
        *s.main, *s.depsgraph, set, s.inspection.displacement_content(set.material_session_uid));
  const InspectionMaterial material{
      set.material_session_uid,
      set.base_values,
      {},
      s.selected_material == set.material_session_uid && set.selected_mask ? &set : nullptr,
      set.texture_resolution};
  s.uv_dirty |= s.inspection.update(*s.main, s.inspection_mode, std::span(&material, 1));
  s.texture_dirty = true;
  s.uv_preview.tag_images_updated();
}

void BlenderHost::preview_displacement(const TextureSet &set)
{
  auto &s = *impl_;
  s.require_owner();
  s.inspection.preview_displacement(*s.main, set);
  s.displacement_preview.apply(
      *s.main, *s.depsgraph, set, s.inspection.displacement_content(set.material_session_uid));
  s.texture_dirty = true;
  s.uv_preview.tag_images_updated();
}
QJsonObject BlenderHost::displacement_preview_state()
{
  auto &s = *impl_;
  s.require_owner();
  BKE_scene_graph_evaluated_ensure(s.depsgraph, s.main);
  return s.displacement_preview.state(*s.main, *s.depsgraph);
}
bool BlenderHost::preview_layer_edit(const TextureSet &source,
                                     CompositeChannels channels,
                                     bool finalize)
{
  const auto set = resident_texture_view(source);
  performance::Scope timing("preview_layer_edit");
  auto &s = *impl_;
  s.require_owner();
  prepare_warp_projections(set);
  if (!s.inspection.queue_layer_edit(set, channels, finalize))
    return false;
  if (channels[16])
    s.displacement_preview.apply(
        *s.main, *s.depsgraph, set, s.inspection.displacement_content(set.material_session_uid));
  s.texture_dirty = true;
  s.uv_preview.tag_images_updated();
  return true;
}
NativeSourceEditBatch BlenderHost::prepare_native_source_edits(
    std::span<const NativeSourceEdit> edits)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing)
    throw std::logic_error("Native source publication requires an idle owner");
  // No warp, shader/topology, displacement or UV rebinding on this source-only
  // boundary. Normal frame publication handles the resulting image updates.
  auto batch = std::make_shared<NativeSourceEditBatch>(
      s.inspection.prepare_native_source_edits(edits));
  return NativeSourceEditBatch([&s, batch]() noexcept {
    batch->commit();
    s.texture_dirty = true;
  });
}
QJsonObject BlenderHost::diagnostic_native_source_queues() const
{
  impl_->require_owner();
  return impl_->inspection.diagnostic_native_source_queues();
}
QJsonObject BlenderHost::native_world_normal_status(std::uint32_t uid) const
{
  impl_->require_owner();
  return impl_->inspection.native_world_normal_status(uid);
}
QJsonArray BlenderHost::diagnostic_native_world_normal_samples(std::uint32_t uid, bool edge)
{
  auto &s = *impl_;
  s.require_owner();
  if (!qEnvironmentVariableIsSet("BTS_NATIVE_WORLD_NORMAL_UI"))
    throw std::logic_error("Native world probe is diagnostic-only");
  s.enter_gpu();
  try {
    auto result = s.inspection.diagnostic_native_world_normal_samples(uid, edge);
    DRW_gpu_context_disable_ex(false);
    return result;
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::preview_material_roughness(const TextureSet &set)
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.inspection.preview_roughness(*s.main, set))
    s.inspection.queue_composite(set);
  s.texture_dirty = true;
  s.uv_preview.tag_images_updated();
}
void BlenderHost::set_shader_parameters(const TextureSet &set)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Shader change requires an idle viewport");
  if (set.disabled || !set.uniform_editable)
    return;
  if (s.inspection.shader_parameters(*s.main, set)) {
    s.texture_dirty = true;
    s.uv_dirty = true;
  }
}
QJsonObject BlenderHost::shader_parameter_state() const
{
  auto &s = *impl_;
  s.require_owner();
  QJsonObject result;
  for (Material &material : s.main->materials)
    if (material.nodetree) {
      QJsonObject parameters;
      for (auto entry :
           std::array{
               std::array<const char *, 3>{"normal_intensity", "__BTS_PaintNormal", "Strength"},
               std::array<const char *, 3>{"ao_intensity", "__BTS_AOMaterial", "Factor"}})
      {
        auto *node = bke::node_find_node_by_name(*material.nodetree, entry[1]);
        auto *socket = node ? bke::node_find_enabled_input_socket(*node, entry[2]) : nullptr;
        if (socket && socket->default_value)
          parameters[entry[0]] =
              static_cast<bNodeSocketValueFloat *>(socket->default_value)->value;
      }
      if (auto *weight = bke::node_find_node_by_name(*material.nodetree, "__BTS_SubsurfaceWeight"))
      {
        auto *scale = bke::node_find_node_by_name(*material.nodetree, "__BTS_SubsurfaceScale");
        auto *radius = bke::node_find_node_by_name(*material.nodetree, "__BTS_SubsurfaceRadius");
        auto *surface = native_principled_surface(material);
        const auto number = [](bNode *node, int index) {
          if (!node)
            return -1.f;
          for (auto &socket : node->inputs)
            if (index-- == 0)
              return static_cast<bNodeSocketValueFloat *>(socket.default_value)->value;
          return -1.f;
        };
        parameters["subsurface"] = QJsonObject{
            {"gate", number(weight, 1)},
            {"scale", number(scale, 0)},
            {"radius", QJsonArray{number(radius, 0), number(radius, 1), number(radius, 2)}},
            {"method", surface ? surface->custom2 : -1}};
      }
      if (auto *bump = bke::node_find_node_by_name(*material.nodetree, "__BTS_PaintHeight")) {
        auto *distance = bke::node_find_enabled_input_socket(*bump, "Distance");
        if (distance && distance->default_value)
          parameters["bump_distance"] =
              static_cast<bNodeSocketValueFloat *>(distance->default_value)->value;
      }
      parameters["openpbr"] = openpbr_shader_state(material);
      if (qEnvironmentVariableIsSet("BTS_OPENPBR_IMPORT_DEMO")) {
        auto *properties = IDP_GetProperties(&material.id);
        auto *source = properties ? IDP_GetPropertyFromGroup(properties, "bts_imported_openpbr") :
                                    nullptr;
        if (source && source->type == IDP_STRING)
          parameters["imported_source"] =
              QJsonDocument::fromJson(QByteArray(IDP_string_get(source))).object();
      }
      if (qEnvironmentVariableIsSet("BTS_OPENPBR_DEMO"))
        parameters["openpbr_idempotent"] = s.inspection.openpbr_reapply_is_stable(
            *s.main, material.id.session_uid);
      result[QString::fromUtf8(material.id.name + 2)] = parameters;
    }
  return result;
}
void BlenderHost::preview_material_color(const TextureSet &set)
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.inspection.preview_color(*s.main, set)) {
    CompositeChannels channels{};
    channels[0] = true;
    if (!s.inspection.queue_layer_edit(set, channels))
      s.inspection.queue_composite(set);
  }
  s.texture_dirty = true;
  s.uv_preview.tag_images_updated();
}
void BlenderHost::prepare_paint_texture(const TextureSet &source)
{
  const auto set = resident_texture_view(source);
  auto &s = *impl_;
  s.require_owner();
  prepare_warp_projections(set);
  if (s.inspection.shader_parameters(*s.main, set))
    s.uv_preview.tag_images_updated();
  if (!s.inspection.has_padding(set.material_session_uid))
    s.inspection.set_padding_geometry(*s.main, set.material_session_uid, s.source_snapshot());
  if (!s.inspection.has_pixels(set.material_session_uid)) {
    s.inspection.prepare_pixels(*s.main, set);
    s.uv_dirty = true;
  }
}
QString BlenderHost::viewport_texture_status() const
{
  auto &s = *impl_;
  s.require_owner();
  if (s.inspection.gpu_programs_pending())
    return "  |  Preparing GPU layer and mask shaders…";
  QStringList parts;
  const auto images = s.residency.snapshot()["images"].toArray();
  for (const auto &entry : images) {
    const auto value = entry.toObject();
    if (!value["shader_bound"].toBool(true))
      continue;
    if (s.selected_material &&
        std::uint32_t(value["material_uid"].toDouble()) != *s.selected_material)
      continue;
    auto name = value["image"].toString();
    if (name.startsWith("BTS Composite ")) {
      name = name.mid(14);
      name = name.mid(name.indexOf(' ') + 1);
    }
    else if (name.startsWith("BTS Edit Preview ")) {
      static const QStringList channels{"Base Color",
                                        "Metallic",
                                        "Roughness",
                                        "Normal",
                                        "Bump",
                                        "AO",
                                        "Authored Normal",
                                        "User0",
                                        "User1",
                                        "User2",
                                        "User3",
                                        "User4",
                                        "User5",
                                        "User6",
                                        "User7",
                                        "Mask",
                                        "Displacement"};
      name = channels.value(name.section(' ', 4, 4).toInt(), "Texture") + " (preview)";
    }
    else if (name == "BTS Complete Ambient Occlusion")
      name = "AO";
    else if (name == "BTS Displacement")
      name = "Displacement";
    else
      continue;
    const int size = std::min(value["sampling_cap"].toInt(),
                              std::max(value["width"].toInt(), value["height"].toInt()));
    parts.append(name + " " + (size ? QString::number(size) : QString("loading")));
  }
  const auto residency = s.residency.snapshot();
  for (const auto *key : {"warning", "memory_warning"})
    if (!residency[key].toString().isEmpty())
      parts.append(residency[key].toString());
  return parts.empty() ? QString{} : QString("  |  Textures: ") + parts.join(", ");
}
bool BlenderHost::viewport_redraw_pending() const
{
  auto &s = *impl_;
  s.require_owner();
  s.arm_program_poll();
  if (s.inspection.cycles_images_pending() ||
      s.inspection.updates_pending(!s.texture_interacting) ||
      s.residency.snapshot()["refining"].toBool())
    return true;
  if (STREQ(s.scene->r.engine, "CYCLES")) {
    const auto state = render_state();
    if (state["image_mirror_paused"].toBool())
      return false;
    const auto status = state["status"].toString();
    return !status.contains("Done", Qt::CaseInsensitive) &&
           !status.contains("Finished", Qt::CaseInsensitive);
  }
  return false;
}
void BlenderHost::set_texture_interaction(bool active)
{
  impl_->require_owner();
  impl_->texture_interacting = active;
}
void BlenderHost::diagnostic_adjustment_budget(std::optional<std::uint64_t> limit)
{
  impl_->require_owner();
  impl_->inspection.diagnostic_adjustment_budget(limit);
}
std::shared_ptr<const AdjustmentPaintSnapshot> BlenderHost::begin_adjustment_source(
    const TextureSet &set, const QUuid &layer, const QUuid &document)
{
  impl_->require_owner();
  return impl_->inspection.begin_adjustment_source(set, layer, document);
}
TextureSet BlenderHost::prepare_layer_capture(const TextureSet &set) const
{
  impl_->require_owner();
  return impl_->inspection.prepare_layer_capture(set);
}
std::uint64_t BlenderHost::request_layer_capture(const TextureSet &set,
                                                 const LayerCaptureRequest &request)
{
  impl_->require_owner();
  return impl_->inspection.request_layer_capture(set, request);
}
std::optional<LayerCaptureResult> BlenderHost::layer_capture_result() const
{
  impl_->require_owner();
  return impl_->inspection.layer_capture_result();
}
std::uint64_t BlenderHost::request_thumbnail(const TextureSet &set,
                                             const ThumbnailRequest &request)
{
  impl_->require_owner();
  return impl_->inspection.request_thumbnail(set, request);
}
std::optional<ThumbnailMetadata> BlenderHost::thumbnail_result() const
{
  impl_->require_owner();
  return impl_->inspection.thumbnail_result();
}
std::uint64_t BlenderHost::request_material_sample(const TextureSet &set,
                                                   const MaterialSampleRequest &request)
{
  impl_->require_owner();
  return impl_->inspection.request_material_sample(set, request);
}
std::optional<MaterialSample> BlenderHost::material_sample_result() const
{
  impl_->require_owner();
  return impl_->inspection.material_sample_result();
}
std::uint64_t BlenderHost::request_histogram(const TextureSet &set,
                                             const HistogramRequest &request)
{
  impl_->require_owner();
  return impl_->inspection.request_histogram(set, request);
}
std::optional<HistogramMetadata> BlenderHost::histogram_result(const QUuid &id) const
{
  impl_->require_owner();
  return impl_->inspection.histogram_result(id);
}
bool BlenderHost::prepare_texture_resources(
    const std::vector<std::shared_ptr<const FillTexture>> &textures)
{
  impl_->require_owner();
  return impl_->inspection.prepare_texture_resources(textures);
}
bool BlenderHost::claim_texture_preparation(const QUuid &owner)
{
  impl_->require_owner();
  return impl_->inspection.claim_texture_preparation(owner);
}
bool BlenderHost::prepare_texture_resources(
    const QUuid &owner, const std::vector<std::shared_ptr<const FillTexture>> &textures)
{
  impl_->require_owner();
  return impl_->inspection.prepare_texture_resources(owner, textures);
}
bool BlenderHost::cancel_texture_preparation(const QUuid &owner)
{
  impl_->require_owner();
  return impl_->inspection.cancel_texture_preparation(owner);
}
std::map<std::uint32_t, QJsonObject> BlenderHost::deferred_imported_materials() const
{
  auto &s = *impl_;
  s.require_owner();
  std::map<std::uint32_t, QJsonObject> result;
  for (Material &material : s.main->materials) {
    auto *properties = IDP_GetProperties(&material.id);
    auto *source = properties ? IDP_GetPropertyFromGroup(properties, "bts_imported_openpbr") :
                                nullptr;
    if (!source || source->type != IDP_STRING)
      continue;
    const auto metadata = QJsonDocument::fromJson(QByteArray(IDP_string_get(source))).object();
    if (!metadata["converted"].toBool() && !metadata["images"].toObject().isEmpty())
      result.emplace(material.id.session_uid, metadata);
  }
  return result;
}
void BlenderHost::adopt_imported_material(const TextureSet &set,
                                          float bump_distance,
                                          const SceneSnapshot &geometry)
{
  auto &s = *impl_;
  s.require_owner();
  if (!std::isfinite(bump_distance) || bump_distance < 0)
    throw std::invalid_argument("Invalid imported Bump Distance");
  Material *material = nullptr;
  for (Material &candidate : s.main->materials)
    if (candidate.id.session_uid == set.material_session_uid) {
      material = &candidate;
      break;
    }
  if (!material)
    throw std::invalid_argument("Imported material disappeared");
  auto *properties = IDP_GetProperties(&material->id);
  auto *source = properties ? IDP_GetPropertyFromGroup(properties, "bts_imported_openpbr") :
                              nullptr;
  if (!source || source->type != IDP_STRING)
    throw std::invalid_argument("Imported source metadata is missing");
  auto metadata = QJsonDocument::fromJson(QByteArray(IDP_string_get(source))).object();
  auto *previous = material->nodetree;
  std::vector<Image *> foreign_images;
  if (previous)
    for (bNode &node : previous->nodes)
      if (node.id && GS(node.id->name) == ID_IM)
        foreign_images.push_back(reinterpret_cast<Image *>(node.id));
  s.enter_gpu();
  try {
    GPU_material_free(&material->gpumaterial);
    material->nodetree = nullptr;
    nodes::node_tree_shader_default(s.context, s.main, &material->id);
    write_material_values(*s.main, *material, set.base_values);
    s.inspection.set_padding_geometry(*s.main, set.material_session_uid, geometry);
    s.inspection.prepare_pixels(*s.main, set);
    auto *bump = bke::node_find_node_by_name(*material->nodetree, "__BTS_PaintHeight");
    auto *distance = bump ? bke::node_find_enabled_input_socket(*bump, "Distance") : nullptr;
    if (!distance)
      throw std::runtime_error("Imported Bump node is missing");
    static_cast<bNodeSocketValueFloat *>(distance->default_value)->value = bump_distance;
    s.inspection.shader_parameters(*s.main, set);
    metadata["converted"] = true;
    metadata["activate"] = true;
    metadata["reason"] = "Imported as GPU fill resources";
    const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    IDP_FreeFromGroup(properties, source);
    IDP_AddToGroup(properties, IDP_NewString(json.constData(), "bts_imported_openpbr"));
    DEG_id_tag_update_ex(s.main, &material->id, ID_RECALC_SHADING | ID_RECALC_SYNC_TO_EVAL);
    s.texture_dirty = true;
    s.uv_dirty = true;
    s.uv_preview.tag_images_updated();
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    s.inspection.discard_binding(set.material_session_uid);
    if (material->nodetree && material->nodetree != previous) {
      bke::node_tree_free_embedded_tree(material->nodetree);
      MEM_delete(material->nodetree);
    }
    material->nodetree = previous;
    DRW_gpu_context_disable_ex(false);
    throw;
  }
  // The previous tree remains available throughout the fallible adoption transaction.
  if (previous) {
    bke::node_tree_free_embedded_tree(previous);
    MEM_delete(previous);
  }
  BKE_main_id_refcount_recompute(s.main, false);
  for (auto *image : foreign_images)
    if (image->id.us == 0) {
      BKE_image_free_buffers(image);
      BKE_image_free_packedfiles(image);
    }
}
bool BlenderHost::prepare_texture_resource(const std::shared_ptr<const FillTexture> &texture)
{
  impl_->require_owner();
  return impl_->inspection.prepare_texture_resource(texture);
}
void BlenderHost::cancel_texture_preparation()
{
  impl_->require_owner();
  impl_->inspection.cancel_texture_preparation();
}
bool BlenderHost::texture_updates_pending() const
{
  impl_->require_owner();
  impl_->arm_program_poll();
  return impl_->inspection.updates_pending();
}
void BlenderHost::flush_texture_updates()
{
  auto &s = *impl_;
  s.require_owner();
  s.enter_gpu();
  try {
    s.inspection.upload_staging_images(true, false);
    s.inspection.finalize_gpu_images(*s.main);
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
  if (s.inspection.poll_composites(*s.main, true)) {
    s.displacement_preview.sync_contents(
        *s.main, *s.depsgraph, [&](auto uid) { return s.inspection.displacement_content(uid); });
    s.texture_dirty = true;
    s.uv_preview.tag_images_updated();
  }
}
void BlenderHost::finish_texture_set(const TextureSet &set)
{
  auto &s = *impl_;
  s.require_owner();
  // Release may quantize the working stroke. Publish that final revision to the
  // GPU as well as the export/packing worker, without invalidating the last dab.
  if (!s.inspection.queue_layer_edit(set, complete_composite_channels, true))
    s.inspection.queue_composite(set);
  s.texture_dirty = true;
  s.uv_preview.tag_images_updated();
}

std::vector<unsigned char> BlenderHost::texture_coverage(std::uint32_t uid, int tile) const
{
  impl_->require_owner();
  return impl_->inspection.uv_coverage(uid, tile);
}
std::vector<ExportTile> BlenderHost::export_source(const TextureDocument &document,
                                                   const std::vector<QUuid> &selected)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing)
    throw std::logic_error("Export outside idle session");
  std::uint64_t bytes = 0;
  for (const auto &set : document.texture_sets())
    if (!set.disabled && std::find(selected.begin(), selected.end(), set.id) != selected.end())
      bytes += set.evaluation_texels() * 16 * (set.ao_channel || set.mesh_maps[3] ? 2 : 1);

  std::map<std::uint32_t, float> bump_distances;
  for (Material &material : s.main->materials)
    if (material.nodetree) {
      auto *bump = bke::node_find_node_by_name(*material.nodetree, "__BTS_PaintHeight");
      if (!bump)
        bump = bke::node_find_node_by_name(*material.nodetree, "__BTS_PaintBump");
      auto *distance = bump ? bke::node_find_enabled_input_socket(*bump, "Distance") : nullptr;
      if (distance && distance->default_value)
        bump_distances[material.id.session_uid] =
            static_cast<bNodeSocketValueFloat *>(distance->default_value)->value;
    }
  std::vector<ExportTile> result;
  for (const auto &set : document.texture_sets())
    if (!set.disabled && std::find(selected.begin(), selected.end(), set.id) != selected.end()) {
      prepare_warp_projections(set);
      const int resolution = set.texture_resolution;
      if (!s.inspection.has_padding(set.material_session_uid))
        s.inspection.set_padding_geometry(*s.main, set.material_session_uid, s.source_snapshot());
      (void)resolution;
      for (int number : set.tiles) {
        ExportTile tile{set, number, set.width(number)};
        tile.height = set.rows(number);
        if (const auto distance = bump_distances.find(set.material_session_uid);
            distance != bump_distances.end())
          tile.bump_distance = distance->second;
        tile.deferred_padding = s.inspection.export_padding(set.material_session_uid);
        result.push_back(std::move(tile));
      }
    }
  return result;
}

bool BlenderHost::has_texture_pixels(std::uint32_t uid) const
{
  impl_->require_owner();
  return impl_->inspection.has_pixels(uid);
}
std::vector<float> BlenderHost::texture_pixels(std::uint32_t uid, int channel, int tile) const
{
  impl_->require_owner();
  return impl_->inspection.copy_pixels(uid, channel, tile);
}
std::uint64_t BlenderHost::texture_hash(std::uint32_t uid, int channel, int tile) const
{
  impl_->require_owner();
  return impl_->inspection.image_hash(uid, channel, tile);
}
double BlenderHost::verify_gpu_texture_stack(std::uint32_t uid) const
{
  auto &s = *impl_;
  s.require_owner();
  s.enter_gpu();
  try {
    const auto error = s.inspection.verify_gpu_pixels(uid);
    DRW_gpu_context_disable_ex(false);
    return error;
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
std::vector<std::array<float, 4>> BlenderHost::diagnostic_gpu_pixels(
    std::uint32_t uid,
    int channel,
    std::span<const std::array<int, 2>> coordinates,
    int tile) const
{
  auto &s = *impl_;
  s.require_owner();
  s.enter_gpu();
  try {
    auto values = s.inspection.diagnostic_gpu_pixels(uid, channel, coordinates, tile);
    DRW_gpu_context_disable_ex(false);
    return values;
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
std::optional<std::array<float, 4>> BlenderHost::diagnostic_gpu_pixel(
    std::uint32_t uid, int channel, int x, int y, int tile) const
{
  auto &s = *impl_;
  s.require_owner();
  s.enter_gpu();
  try {
    auto value = s.inspection.diagnostic_gpu_pixel(uid, channel, x, y, tile);
    DRW_gpu_context_disable_ex(false);
    return value;
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
std::array<float, 4> BlenderHost::sample_texture(
    std::uint32_t uid, int channel, int x, int y, int tile) const
{
  impl_->require_owner();
  return impl_->inspection.sample_pixel(uid, channel, x, y, tile);
}

void BlenderHost::set_material_values(std::uint32_t session_uid, const MaterialValues &values)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase == Impl::Phase::Closed || s.drawing)
    throw std::logic_error("Material edit outside idle session");
  for (Material &material : s.main->materials) {
    if (material.id.session_uid == session_uid) {
      write_material_values(*s.main, material, values);
      s.uv_dirty = true;
      std::printf(
          "BTS_MATERIAL_EDIT pid=%lld roughness=%.3f metallic=%.3f materials=1 session_uid=%u\n",
          static_cast<long long>(QCoreApplication::applicationPid()),
          values.roughness,
          values.metallic,
          session_uid);
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
                std::clamp(roughness, 0.0f, 1.0f),
                session_uid);
    std::fflush(stdout);
    return;
  }
  throw std::out_of_range("The selected material is no longer in the project.");
}

