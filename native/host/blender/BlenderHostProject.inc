// SPDX-License-Identifier: GPL-3.0-or-later
// Projects: mesh reimport, shader profiles, new project, save, the project GPU stages and adoption.
// A section of native/host/BlenderHost.cc, which includes it in place; not a standalone file.
void BlenderHost::commit_reimport(TextureDocument &document, MeshReplacement &replacement)
{
  auto &s = *impl_;
  s.require_owner();
  s.reset_warp_geometry();
  if (s.phase != Impl::Phase::GpuReady || s.drawing)
    throw std::logic_error("Reimport requires an idle viewport");
  auto candidate = replacement.authored ? std::move(*replacement.authored) : document;
  candidate.replace_mesh_source(replacement.summary.source_path);
  s.enter_gpu();
  try {
    s.free_targets();
    s.displacement_preview.reset(*s.main);
    adopt_mesh_replacement(*s.main, *s.scene, replacement);
    DEG_relations_tag_update(s.main);
    BKE_view_layer_synced_ensure(*s.main, s.scene, s.view_layer);
    BKE_scene_set_background(s.main, s.scene);
    s.depsgraph = BKE_scene_ensure_depsgraph(s.main, s.scene, s.view_layer);
    DEG_make_active(s.depsgraph);
    BKE_scene_graph_evaluated_ensure(s.depsgraph, s.main);
    const auto geometry = s.source_snapshot();
    for (const auto &description : candidate.texture_sets()) {
      auto &set = candidate.set(description.material_session_uid);
      if (set.disabled)
        continue;
      s.inspection.set_padding_geometry(*s.main, set.material_session_uid, geometry);
    }
    document = std::move(candidate);
    s.uv_dirty = true;
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
  for (const auto &set : document.texture_sets())
    apply_texture_set(set, int(document.settings().resolution));
  std::printf("BTS_MESH_REIMPORTED pid=%lld objects=%zu triangles=%zu\n",
              static_cast<long long>(QCoreApplication::applicationPid()),
              replacement.summary.objects,
              replacement.summary.triangles);
  std::fflush(stdout);
}

std::vector<std::string> BlenderHost::apply_imported_shader_profiles(TextureDocument &document)
{
  auto &s = *impl_;
  s.require_owner();
  auto library = document.shader_instances();
  int applied = 0;
  std::vector<std::string> notes;
  for (blender::Material &material : s.main->materials) {
    auto *properties = IDP_GetProperties(&material.id);
    auto *property = properties ? IDP_GetPropertyFromGroup(properties, "bts_imported_openpbr") :
                                  nullptr;
    if (!property || property->type != IDP_STRING)
      continue;
    const auto metadata = QJsonDocument::fromJson(QByteArray(IDP_string_get(property))).object();
    if (!metadata["activate"].toBool())
      continue;
    const auto found = std::find_if(
        document.texture_sets().begin(), document.texture_sets().end(), [&](const auto &set) {
          return set.material_session_uid == material.id.session_uid;
        });
    if (found == document.texture_sets().end() || !found->uniform_editable)
      continue;
    try {
      const auto constants = openpbr_import_constants(
          OpenPbrParameters::from_json(metadata["parameters"].toObject()));
      ShaderParameters parameters;
      parameters.openpbr = constants.shader;
      auto match = std::find_if(
          library.instances.begin(), library.instances.end(), [&](const auto &entry) {
            return entry.second.parameters == parameters;
          });
      QUuid id;
      if (match != library.instances.end())
        id = match->first;
      else {
        id = library.create(library.default_instance,
                            QString("Imported OpenPBR %1").arg(++applied));
        library.parameters(id, parameters);
      }
      library.assign(found->id, id);
      notes.push_back("Imported OpenPBR compatibility profile: " + found->name);
    }
    catch (const std::exception &error) {
      notes.push_back(std::string("OpenPBR profile deferred: ") + error.what());
    }
  }
  document.set_shader_instances(std::move(library));
  return notes;
}

ImportSummary BlenderHost::new_project(const std::string &mesh_path,
                                       const std::string &hdr_path,
                                       const NewProjectOptions *options)
{
  auto &s = *impl_;
  s.require_owner();
  s.reset_warp_geometry();
  if (s.phase != Impl::Phase::GpuReady || s.drawing) {
    throw std::logic_error("New requires an idle initialized viewport");
  }
  auto profile = NewProjectProfile::prepare(hdr_path, *s.scene, *s.view);
  auto pending = PendingMeshProject::from_scene(*s.main,
                                                mesh_path,
                                                options ? options->import_cameras : true,
                                                {},
                                                options ? options->usd : UsdImportOptions{},
                                                options ? options->blend : BlendMeshSelection{});
  pending->prepare_uvs(options ? options->auto_unwrap : AutoUnwrapOptions{});
  const ImportSummary summary = pending->summary();
  if (options && options->uv_workflow == UvWorkflow::PreserveTiles)
    for (const auto &material : summary.materials) {
      if (!material.udim_valid || material.tiles.empty())
        throw std::runtime_error("UV Tile projects require valid 1001–2000 UVs");
    }
  Set<ID *> retired;
  ID *id;
  FOREACH_MAIN_ID_BEGIN (s.main, id) {
    if (!ELEM(GS(id->name), ID_WM, ID_SCR, ID_WS))
      retired.add(id);
  }
  FOREACH_MAIN_ID_END;
  s.enter_gpu();
  try {
    s.free_targets();
    s.imported_shader_cache.clear();
    s.inspection.reset();
    s.displacement_preview.reset(*s.main);
    s.last_tangent_shader_state = {};
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
    for (Object &object : s.main->objects)
      if (summary.camera_names.contains(object.id.session_uid))
        BKE_id_rename(*s.main, object.id, summary.camera_names.at(object.id.session_uid));
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
  std::printf(
      "BTS_NEW_PROJECT pid=%lld objects=%zu materials=%zu triangles=%zu working_space=%s\n",
      static_cast<long long>(QCoreApplication::applicationPid()),
      summary.objects,
      summary.materials.size(),
      summary.triangles,
      s.main->colorspace.scene_linear_name);
  std::fflush(stdout);
  return summary;
}

void BlenderHost::begin_mask_preset(const LayerMask &mask)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.mask_preset)
    throw std::logic_error("A mask generator is already being saved");
  s.mask_preset = std::make_unique<MaskPresetReadback>(mask);
}
std::optional<QByteArray> BlenderHost::poll_mask_preset()
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.mask_preset)
    return {};
  s.enter_gpu();
  try {
    auto result = s.mask_preset->poll(s.inspection);
    if (result)
      s.mask_preset.reset();
    DRW_gpu_context_disable_ex(false);
    return result;
  }
  catch (...) {
    s.mask_preset.reset();
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::begin_material_preset(const TextureSet &set, const QUuid &folder)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.material_preset)
    throw std::logic_error("A material preset is already being saved");
  s.material_preset = std::make_unique<MaterialPresetReadback>(set, folder);
}
void BlenderHost::begin_material_parameter_export(const TextureSet &set,
                                                  const std::vector<StudioAsset> &resources,
                                                  const QString &directory)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing || s.parameter_export ||
      save_project_pending() || s.material_preset || s.mask_preset)
    throw std::logic_error("Another capture/export is still finishing");
  if (s.inspection.updates_pending())
    throw std::logic_error("Wait for the selected set's GPU changes before exporting parameters");
  std::array<double, 9> matrix;
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      matrix[row * 3 + column] = s.main->colorspace.scene_linear_to_xyz[column][row];
  auto job = std::make_unique<MaterialParameterExport>(
      set, resources, directory, QString::fromUtf8(s.main->colorspace.scene_linear_name), matrix);
  s.enter_gpu();
  try {
    job->capture(s.inspection, set);
    s.parameter_export = std::move(job);
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    job.reset();
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
bool BlenderHost::material_parameter_export_pending() const
{
  impl_->require_owner();
  return bool(impl_->parameter_export);
}
void BlenderHost::cancel_material_parameter_export()
{
  impl_->require_owner();
  if (impl_->parameter_export)
    impl_->parameter_export->cancel();
}
bool BlenderHost::poll_material_parameter_export(bool current)
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.parameter_export)
    return true;
  if (!current)
    s.parameter_export->cancel();
  if (s.phase != Impl::Phase::GpuReady)
    return false;
  s.enter_gpu();
  try {
    const bool ready = s.parameter_export->poll(current);
    if (ready)
      s.parameter_export.reset();
    DRW_gpu_context_disable_ex(false);
    return ready;
  }
  catch (...) {
    s.parameter_export.reset();
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
std::optional<QByteArray> BlenderHost::poll_material_preset()
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.material_preset)
    return {};
  s.enter_gpu();
  try {
    auto result = s.material_preset->poll(s.inspection);
    if (result)
      s.material_preset.reset();
    DRW_gpu_context_disable_ex(false);
    return result;
  }
  catch (...) {
    s.material_preset.reset();
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}

void BlenderHost::begin_save_project(const TextureDocument &document,
                                     const std::string &path,
                                     const std::vector<StudioAsset> &resources,
                                     std::function<void()> persist_metadata,
                                     std::optional<ProjectFileIdentity> expected)
{
  auto &s = *impl_;
  s.require_owner();
  performance::Scope timing("save_snapshot_request");
  if (s.phase != Impl::Phase::GpuReady || s.drawing || save_project_pending())
    throw std::logic_error("Another project save is in progress");
  if (s.inspection.updates_pending())
    throw std::logic_error("Project GPU changes are still being published");
  for (auto &material : s.main->materials)
    if (auto found = s.imported_shader_cache.find(material.id.session_uid);
        found != s.imported_shader_cache.end())
      found->second.restore(*s.main, material);
  auto snapshot = std::make_unique<ProjectSaveSnapshot>(
      *s.main, *s.scene, *s.view, document, resources);
  s.enter_gpu();
  try {
    auto readback = std::make_unique<GpuProjectReadback>();
    s.inspection.capture_project_images(*readback);
    s.save_snapshot = std::move(snapshot);
    s.save_readback = std::move(readback);
    s.save_path = path;
    s.save_metadata = std::move(persist_metadata);
    s.save_expected = expected.value_or(ProjectFileIdentity::read(QString::fromStdString(path)));
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
ProjectFileIdentity BlenderHost::saved_project_identity() const
{
  impl_->require_owner();
  return impl_->saved_identity;
}
bool BlenderHost::save_project_pending() const
{
  impl_->require_owner();
  return bool(impl_->save_readback) || impl_->save_writer.valid();
}
bool BlenderHost::poll_save_project()
{
  auto &s = *impl_;
  s.require_owner();
  if (s.save_snapshot) {
    bool ready = false;
    s.enter_gpu();
    try {
      const auto before = s.save_readback->images().size();
      const bool pixels_ready = s.save_readback->poll();
      const bool sources_ready = s.save_snapshot->capture_authored(s.inspection, *s.save_readback);
      ready = pixels_ready && sources_ready && before == s.save_readback->images().size();
      DRW_gpu_context_disable_ex(false);
    }
    catch (...) {
      s.save_readback.reset();
      s.save_snapshot.reset();
      DRW_gpu_context_disable_ex(false);
      throw;
    }
    if (!ready)
      return false;
    s.save_writer = std::async(std::launch::async,
                               [snapshot = std::move(s.save_snapshot),
                                readback = s.save_readback.get(),
                                path = s.save_path,
                                expected = s.save_expected,
                                metadata = std::move(s.save_metadata)]() mutable {
                                 auto owned = std::move(snapshot);
                                 struct RetireCpu {
                                   GpuProjectReadback *readback;
                                   ~RetireCpu()
                                   {
                                     readback->release_cpu();
                                   }
                                 } retire{readback};
                                 const auto identity = owned->write(path, *readback, expected);
                                 owned.reset();
                                 readback->release_cpu();
                                 if (metadata) {
                                   performance::Scope timing("save_metadata_io");
                                   metadata();
                                 }
                                 return identity;
                               });
  }
  if (s.save_writer.valid()) {
    if (s.save_writer.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return false;
    std::exception_ptr failure;
    try {
      s.saved_identity = s.save_writer.get();
    }
    catch (...) {
      failure = std::current_exception();
    }
    s.enter_gpu();
    s.save_readback.reset();
    DRW_gpu_context_disable_ex(false);
    if (failure)
      std::rethrow_exception(failure);
    std::printf("BTS_PROJECT_SAVED path=%s pid=%lld background=1 cpu_composite=0\n",
                s.save_path.c_str(),
                static_cast<long long>(QCoreApplication::applicationPid()));
    std::fflush(stdout);
  }
  return true;
}
void BlenderHost::save_project(const TextureDocument &document,
                               const std::string &path,
                               const std::vector<StudioAsset> &resources,
                               bool recovery_copy,
                               std::optional<ProjectFileIdentity> expected)
{
  // Synchronous diagnostic/export bridge. Workspace actions use the asynchronous
  // API above; this bridge retains deterministic CLI/demo round trips.
  auto &s = *impl_;
  const auto started = performance::Clock::now();
  while (s.inspection.updates_pending()) {
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
    if (performance::ms(started) > 30000)
      throw std::runtime_error("Diagnostic GPU save publication timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  s.inspection.poll_composites(*s.main, true);
  begin_save_project(document, path, resources, {}, expected);
  while (!poll_save_project())
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if (!recovery_copy)
    STRNCPY(impl_->main->filepath, path.c_str());
}
void BlenderHost::prepare_native_height(const QUuid &resource, NativeMaterialHeightResource value)
{
  impl_->require_owner();
  if (resource.isNull())
    throw std::invalid_argument("Native Height source identity is empty");
  with_gpu([&] {
    impl_->inspection.prepare_native_height(resource.toString(QUuid::WithoutBraces).toStdString(),
                                            std::move(value));
  });
}
std::unique_ptr<NativeMaterialHeightGpu> BlenderHost::stage_native_height_gpu(
    const QUuid &resource, NativeMaterialHeightResource value)
{
  impl_->require_owner();
  if (resource.isNull() || !project_gpu_available())
    throw std::logic_error(
        "Native Height preparation requires an idle viewport and source identity");
  auto work = std::unique_ptr<NativeMaterialHeightGpu>(new NativeMaterialHeightGpu(*this));
  work->impl_->key = resource.toString(QUuid::WithoutBraces).toStdString();
  with_gpu([&] {
    work->gpu_owned_ = true;
    impl_->inspection.stage_native_height(*work, std::move(value));
  });
  return work;
}
bool BlenderHost::step_native_height_gpu(NativeMaterialHeightGpu &work)
{
  impl_->require_owner();
  if (work.host_ != this || !work.impl_ || !project_gpu_available())
    throw std::logic_error("Native Height candidate belongs to a different or unavailable owner");
  bool ready = false;
  with_gpu([&] { ready = impl_->inspection.step_native_height(work); });
  return ready;
}
void BlenderHost::install_native_height_gpu(NativeMaterialHeightGpu &work)
{
  impl_->require_owner();
  if (work.host_ != this || !work.impl_ || !project_gpu_available())
    throw std::logic_error("Native Height candidate belongs to a different or unavailable owner");
  with_gpu([&] { impl_->inspection.install_native_height(work); });
}
void BlenderHost::probe_native_height(const TextureSet &set, const QUuid &id)
{
  impl_->require_owner();
  const auto &layer = TextureDocument::layer(const_cast<TextureSet &>(set), id);
  if (!layer.fill.native_material)
    throw std::invalid_argument("Native Height target is not a native Fill");
  require_native_height_origin(layer.fill.native_material->height_policy, set.bump_origin);
  const auto requirements = impl_->inspection.native_requirements(layer.fill.material_resource,
                                                                  *layer.fill.native_material);
  NativeMaterialGpuContext context;
  if (requirements.object)
    bind_native_material_objects(
        *impl_->main, impl_->depsgraph, set.material_session_uid, context);
  context.tile = *set.tiles.begin();
  with_gpu([&] {
    impl_->inspection.probe_native_height(
        layer.fill.material_resource, *layer.fill.native_material, std::move(context));
  });
}
void BlenderHost::discard_native_height_gpu(NativeMaterialHeightGpu &work)
{
  impl_->require_owner();
  if (work.host_ != this)
    throw std::logic_error("Native Height candidate belongs to a different owner");
  if (work.gpu_owned_)
    with_gpu([&] { work.impl_.reset(); });
  else
    work.impl_.reset();
  work.host_ = nullptr;
}
bool BlenderHost::set_native_height_policy(TextureSet &set,
                                           const QUuid &layer,
                                           NativeHeightPolicy policy) const
{
  impl_->require_owner();
  if (impl_->phase != Impl::Phase::GpuReady || impl_->drawing)
    throw std::logic_error("Native Height policy requires an idle viewport");
  return impl_->inspection.set_native_height_policy(set, layer, policy);
}
namespace {
void require_height_open_color_context(const blender::Main &live, const blender::Main &pending)
{
  const auto recorded = [](const blender::Main &main) {
    const auto &name = main.colorspace.scene_linear_name;
    const auto length = strnlen(name, sizeof(name));
    if (length == sizeof(name))
      throw std::invalid_argument("Native Height working-space name is not terminated");
    NativeHeightColorContext result;
    result.name = QString::fromUtf8(name, int(length));
    for (int c = 0; c < 3; ++c)
      for (int r = 0; r < 3; ++r)
        result.scene_linear_to_xyz[c * 3 + r] = main.colorspace.scene_linear_to_xyz[c][r];
    return result;
  };
  NativeHeightColorContext active;
  active.name = QString::fromUtf8(blender::IMB_colormanagement_working_space_get());
  const auto matrix = blender::IMB_colormanagement_get_scene_linear_to_xyz();
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r)
      active.scene_linear_to_xyz[c * 3 + r] = matrix[c][r];
  require_native_height_color_context(recorded(live), recorded(pending), active);
}
}  // namespace
std::unique_ptr<PendingProjectGpu> BlenderHost::begin_project_gpu(PendingProject &pending)
{
  auto &s = *impl_;
  s.require_owner();
  pending.validate();
  const bool raw = native_height_project(*pending.document);
  if (!raw && !pending.layer_sources)
    return {};
  if (s.phase != Impl::Phase::GpuReady || s.drawing || save_project_pending())
    throw std::logic_error("Source Open requires an idle viewport with no pending save");
  if (raw)
    require_height_open_color_context(*s.main, *pending.file->main);
  if (!pending.validated_inspection || (raw && pending.native_sources.empty()))
    throw std::logic_error("Source Open has no validated worker state");
  if (pending.layer_sources)
    pending.validated_inspection->validate_material_layer_sources(*pending.document);
  auto *wm = static_cast<wmWindowManager *>(pending.file->main->wm.first);
  if (!wm || wm->windows.is_empty())
    throw std::invalid_argument("Staged source project has no recorded native window context");
  for (const auto &window : wm->windows)
    if (!window.runtime || window.runtime->ghostwin)
      throw std::invalid_argument("Staged source project cannot adopt a native system window");
  const auto *window = static_cast<const wmWindow *>(wm->windows.first);
  auto *screen = WM_window_get_active_screen(window);
  if (!screen || window->scene != pending.file->curscene ||
      BKE_view_layer_find(window->scene, window->view_layer_name) != pending.file->cur_view_layer)
    throw std::invalid_argument(
        "Staged source project window/scene/view-layer context does not match its validated file context");
  bool viewport = false;
  for (const auto &area : screen->areabase)
    if (area.spacetype == SPACE_VIEW3D)
      for (const auto &region : area.regionbase)
        if (region.regiontype == RGN_TYPE_WINDOW && region.regiondata)
          viewport = true;
  if (!viewport)
    throw std::invalid_argument(
        "Staged source project first native window has no usable View3D region");
  auto stage = std::unique_ptr<PendingProjectGpu>(new PendingProjectGpu(*this));
  stage->pending_ = &pending;
  stage->inspection_ = std::move(pending.validated_inspection);
  stage->sources_ = std::move(pending.native_sources);
  // Capture only stable Host Impl. Pending/staging pointers never enter the
  // long-lived context provider which will run after the accepted Main switch.
  stage->inspection_->set_native_fragment_geometry_provider(
      [&s](NativeFragmentGeometryRequest request, const std::atomic_bool &cancel) {
        return capture_native_fragment_geometry(
            s.context, s.view, s.displacement_preview.source_meshes(), std::move(request), cancel);
      });
  stage->inspection_->set_native_fragment_object_provider([&s](std::uint32_t uid) {
    NativeMaterialGpuContext context;
    bind_native_material_objects(*s.main, s.depsgraph, uid, context, false);
    return context.objects;
  });
  stage->inspection_->set_native_context_provider(
      [&s](const TextureSet &set,
           const TextureLayer &,
           int,
           const NativeMaterialDependencies &requirements,
           NativeMaterialGpuContext &context) {
        if (requirements.object)
          bind_native_material_objects(*s.main, s.depsgraph, set.material_session_uid, context);
        if (requirements.view) {
          if (!s.native_view_matrices)
            throw std::runtime_error("Native material view is not ready");
          bind_native_material_view(
              s.native_view_matrices->first, s.native_view_matrices->second, context);
        }
      });
  return stage;
}
bool BlenderHost::step_project_gpu(PendingProject &pending, PendingProjectGpu &stage)
{
  auto &s = *impl_;
  s.require_owner();
  pending.validate();
  if (stage.host_ != this || stage.pending_ != &pending || !stage.inspection_)
    throw std::logic_error("Pending native GPU project belongs to a different owner");
  if (pending.layer_sources)
    stage.inspection_->validate_material_layer_sources(*pending.document,
                                                       stage.layer_sources_ready_);
  if (stage.ready_)
    return true;
  // Release a completed wrapper outside the active with_gpu scope. Its owner
  // cleanup activates the context itself, so do not nest context entry/exit.
  if (stage.height_work_ && stage.height_work_->ready())
    stage.height_work_.reset();
  with_gpu([&] {
    stage.gpu_owned_ = true;
    auto &inspection = *stage.inspection_;
    if (pending.layer_sources && !stage.layer_sources_ready_) {
      stage.layer_sources_ready_ = inspection.prepare_material_layer_sources();
      return;
    }
    if (!native_height_project(*pending.document)) {
      stage.ready_ = true;
      return;
    }
    if (stage.source_ < stage.sources_.size()) {
      auto &source = stage.sources_[stage.source_];
      const auto key = source.id.toString(QUuid::WithoutBraces).toStdString();
      if (!stage.queued_) {
        inspection.prepare_native_material(key, source.hash.toStdString(), source.graph);
        stage.queued_ = true;
      }
      inspection.step_native_preparations();
      const auto status = inspection.native_preparation_status(key);
      if (!status.error.isEmpty())
        throw std::runtime_error(status.error.toStdString());
      if (status.ready) {
        ++stage.source_;
        stage.queued_ = false;
      }
      else if (!status.pending)
        throw std::runtime_error("Pending native source GPU preparation disappeared");
      return;
    }
    if (stage.height_ < stage.sources_.size()) {
      auto &source = stage.sources_[stage.height_];
      if (!source.height && !stage.height_work_) {
        ++stage.height_;
        return;
      }
      if (!stage.height_work_) {
        stage.height_work_ = std::unique_ptr<NativeMaterialHeightGpu>(
            new NativeMaterialHeightGpu(*this));
        auto &work = *stage.height_work_;
        work.impl_->key = source.id.toString(QUuid::WithoutBraces).toStdString();
        work.gpu_owned_ = true;
        inspection.stage_native_height(work, std::move(*source.height));
        source.height.reset();
        return;
      }
      auto &work = *stage.height_work_;
      if (!inspection.step_native_height(work))
        return;
      inspection.install_native_height(work);
      work.commit();
      ++stage.height_;
      return;
    }
    for (const auto &set : pending.document->texture_sets())
      if (!set.disabled && set.uniform_editable)
        for (const auto &layer : evaluation_layers(set))
          if (layer.fill.native_material) {
            const auto requirements = inspection.native_requirements(layer.fill.material_resource,
                                                                     *layer.fill.native_material);
            if (requirements.object) {
              const auto found = pending.native_object_contexts.find(set.material_session_uid);
              if (found == pending.native_object_contexts.end() || !found->second.objects ||
                  !found->second.objects->size())
                throw std::invalid_argument(
                    "Pending native Height project has no validated object context");
            }
            if (requirements.ao)
              for (int tile : set.tiles)
                if (!set.mesh_maps[3] || !set.mesh_maps[3]->tiles.contains(tile) ||
                    !set.mesh_maps[3]->tiles.at(tile).pixels)
                  throw std::invalid_argument(
                      "Pending native source requires a baked AO map for every UV tile");
          }
    std::size_t probe = 0;
    for (const auto &set : pending.document->texture_sets())
      if (!set.disabled && set.uniform_editable)
        for (const auto &layer : evaluation_layers(set))
          if (layer.fill.native_material &&
              layer.fill.native_material->height_policy == NativeHeightPolicy::RawAuthoredFloatV1)
          {
            if (probe++ != stage.probe_)
              continue;
            NativeMaterialGpuContext context;
            const auto found = pending.native_object_contexts.find(set.material_session_uid);
            if (found != pending.native_object_contexts.end())
              context = found->second;
            context.tile = *set.tiles.begin();
            inspection.probe_native_height(
                layer.fill.material_resource, *layer.fill.native_material, std::move(context));
            ++stage.probe_;
            return;
          }
    stage.ready_ = true;
  });
  return stage.ready_;
}
bool BlenderHost::project_gpu_available() const
{
  impl_->require_owner();
  return impl_->phase == Impl::Phase::GpuReady && !impl_->drawing;
}
void BlenderHost::discard_project_gpu(PendingProjectGpu &stage)
{
  impl_->require_owner();
  if (stage.host_ != this)
    throw std::logic_error("Pending native GPU project belongs to a different owner");
  stage.height_work_.reset();  // Wrapper owns its own context boundary.
  const auto release = [&] {
    stage.inspection_.reset();
    stage.sources_.clear();
  };
  if (stage.gpu_owned_)
    with_gpu([&] {
      release();
      GpuLayerPreview::collect_retired();
    });
  else
    release();
  stage.host_ = nullptr;
}
TextureDocument BlenderHost::open_project(
    const std::string &path,
    std::function<void(const std::vector<StudioAsset> &)> validate_resources)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing)
    throw std::logic_error("Open requires an idle viewport");
  // Batch bridge only. Workspace/Python Open use the nonblocking owner job.
  if (QThread::currentThread()->loopLevel() > 0)
    throw std::logic_error(
        "Synchronous project Open is batch-only; use asynchronous Open in the workspace");
  auto reader = std::async(std::launch::async, [path] { return PendingProject::prepare(path); });
  auto pending = reader.get();
  pending->validate();  // Ready batch result; detached cleanup stays on the owner.
  if (validate_resources)
    validate_resources(pending->resources);
  auto gpu = begin_project_gpu(*pending);
  // Batch-only bridge: no GUI event loop is running and no interactive owner
  // waits on a future. Use the same admission/ready state as asynchronous Open.
  while (gpu && !step_project_gpu(*pending, *gpu))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return adopt_project(std::move(pending), path, std::move(gpu));
}
TextureDocument BlenderHost::adopt_project(std::unique_ptr<PendingProject> pending,
                                           const std::string &path)
{
  return adopt_project(std::move(pending), path, {});
}
void BlenderHost::validate_project_adoption(const PendingProject &pending,
                                            const PendingProjectGpu *gpu) const
{
  const auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing || save_project_pending())
    throw std::logic_error("Open requires an idle viewport with no pending save");
  pending.validate();
  const bool raw = native_height_project(*pending.document);
  const bool staged = raw || pending.layer_sources;
  if (staged && (!gpu || gpu->host_ != this || gpu->pending_ != &pending || !gpu->ready_ ||
                 !gpu->inspection_))
    throw std::logic_error(
        "Source Open requires complete isolated owner GPU staging before adoption");
  if (!staged && gpu)
    throw std::logic_error("Ordinary project cannot adopt an unrelated source GPU stage");
  if (pending.layer_sources)
    gpu->inspection_->validate_material_layer_sources(*pending.document, true);
  if (raw)
    require_height_open_color_context(*s.main, *pending.file->main);
}
TextureDocument BlenderHost::adopt_project(std::unique_ptr<PendingProject> pending,
                                           const std::string &path,
                                           std::unique_ptr<PendingProjectGpu> gpu)
{
  // Parameter destruction order is unspecified. This guard always releases
  // the GPU side before the pending BFD on success and every rejection path.
  struct GpuBeforeFile {
    std::unique_ptr<PendingProjectGpu> &stage;
    ~GpuBeforeFile()
    {
      stage.reset();
    }
  } release{gpu};
  auto &s = *impl_;
  s.require_owner();
  if (!pending)
    throw std::invalid_argument("Cannot adopt an empty project");
  validate_project_adoption(*pending, gpu.get());
  const bool staged = native_height_project(*pending->document) || pending->layer_sources;
  // Every new policy/context/source/program failure precedes this accepted
  // native adoption boundary. This is not a general rollback guarantee for
  // Blender's existing file setup, after-setup versioning or allocation errors.
  pending->activate_environment();  // Global StudioLight registration is owner-only.
  s.enter_gpu();
  try {
    s.free_targets();
    s.imported_shader_cache.clear();
    s.inspection.reset();
    s.displacement_preview.reset(*s.main);
    s.last_tangent_shader_state = {};
    s.selected_material.reset();
    s.inspection_mode = InspectionMode::Material;
    // reset joins old readers; drain retired GPU owners while their original
    // Main is still alive. No queue/reference-bound preparation is moved.
    if (staged)
      GpuLayerPreview::collect_retired();
    auto *file = pending->file;
    pending->file = nullptr;
    BTS_file_adopt(s.context, file);
    s.attach_context();
    STRNCPY(s.scene->r.engine, "BLENDER_EEVEE");
    s.view->shading.type = OB_MATERIAL;
    s.view->flag2 |= V3D_HIDE_OVERLAYS;
    if (staged)
      s.inspection.swap_prepared(*gpu->inspection_);  // noexcept stable-pimpl transfer.
    else
      s.inspection.restore(*s.main, *pending->document);
    s.depsgraph = BKE_scene_ensure_depsgraph(s.main, s.scene, s.view_layer);
    DEG_make_active(s.depsgraph);
    BKE_scene_graph_evaluated_ensure(s.depsgraph, s.main);
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
  for (const auto &set : pending->document->texture_sets())
    if (!set.disabled && set.uniform_editable) {
      prepare_paint_texture(set);
      if (!preview_layer_edit(set, complete_composite_channels, false))
        apply_texture_set(set, int(pending->document->settings().resolution));
    }
  std::printf("BTS_PROJECT_OPENED path=%s pid=%lld\n",
              path.c_str(),
              static_cast<long long>(QCoreApplication::applicationPid()));
  std::fflush(stdout);
  return std::move(*pending->document);
}

SymmetrySettings BlenderHost::symmetry_settings() const
{
  impl_->require_owner();
  return read_project_symmetry(*impl_->scene);
}
void BlenderHost::set_symmetry(const SymmetrySettings &settings)
{
  impl_->require_owner();
  write_project_symmetry(*impl_->scene, settings);
}

QJsonObject BlenderHost::project_profile() const
{
  auto &s = *impl_;
  s.require_owner();
  auto profile = NewProjectProfile::inspect(*s.main, *s.scene, *s.view);
  profile["texture_residency"] = s.residency.snapshot();
  return profile;
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
    if (item->identifier[0])
      result.emplace_back(item->identifier);
  if (owned)
    MEM_delete(items);
  return result;
}

void BlenderHost::set_inspection(InspectionMode mode,
                                 std::span<const InspectionMaterial> materials)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Inspection change outside idle session");
  s.uv_dirty |= s.inspection.update(*s.main, mode, materials);
  s.inspection_mode = mode;
}
void BlenderHost::set_material_layering_view(std::uint32_t uid, bool enabled, int active_mask)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Material layering view requires an idle viewport");
  s.uv_preview.set_material_layering_view(uid, enabled, active_mask);
}
void BlenderHost::set_geometry_visibility(const TextureSet *set)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.drawing || s.phase == Impl::Phase::Closed)
    throw std::logic_error("Geometry visibility requires an idle viewport");
  s.uv_dirty |= s.inspection.set_geometry_visibility(*s.main, set);
}
bool BlenderHost::geometry_visibility_enabled(std::uint32_t uid) const
{
  return impl_->inspection.geometry_visibility_enabled(uid);
}
std::size_t BlenderHost::geometry_visibility_image_count() const
{
  return impl_->inspection.geometry_visibility_image_count();
}
InspectionMode BlenderHost::inspection_mode() const
{
  return impl_->inspection_mode;
}

std::string BlenderHost::shader_failures() const
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.main || !s.depsgraph)
    return {};
  std::string result;
  for (Material &material : s.main->materials) {
    auto *evaluated = DEG_get_evaluated(s.depsgraph, &material);
    bool failed = false;
    for (Material *owner : std::set<Material *>{&material, evaluated}) {
      if (!owner)
        continue;
      for (LinkData &link : owner->gpumaterial)
        failed |= GPU_material_status(static_cast<GPUMaterial *>(link.data)) == GPU_MAT_FAILED;
    }
    if (failed) {
      if (!result.empty())
        result += ", ";
      result += material.id.name + 2;
    }
  }
  return result;
}
