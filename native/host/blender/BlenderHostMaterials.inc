// SPDX-License-Identifier: GPL-3.0-or-later
// Native materials: GPU preparation, validation, installation and previews.
// A section of native/host/BlenderHost.cc, which includes it in place; not a standalone file.

void BlenderHost::initialize_empty_workspace_working_space()
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::SceneReady || !s.factory_workspace_color_pending || !s.main ||
      s.main->filepath[0] || s.surface || s.ghost || s.drawing ||
      !s.material_import_jobs.empty() || !s.material_graph_jobs.empty() ||
      !s.material_prepare_jobs.empty() || !s.asset_scan_jobs.empty() || s.native_preview ||
      s.save_snapshot || s.save_readback || s.save_writer.valid() || s.material_preset ||
      s.mask_preset || s.parameter_export)
    throw std::logic_error(
        "Empty workspace color initialization requires an unused factory startup before GPU or worker activity");
  NewProjectProfile::initialize_working_space(*s.main);
  s.factory_workspace_color_pending = false;
}

void BlenderHost::retire_layer_channel(const TextureDocument &document,
                                       const TextureSet &set,
                                       const TextureLayer &previous,
                                       int channel)
{
  auto &s = *impl_;
  s.require_owner();
  s.enter_gpu();
  try {
    s.inspection.retire_layer_channel(document, set, previous, channel);
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
std::string BlenderHost::rename_material(std::uint32_t uid, const std::string &name)
{
  auto &s = *impl_;
  s.require_owner();
  for (Material &material : s.main->materials)
    if (material.id.session_uid == uid) {
      if (name.empty() || name.find('\0') != std::string::npos)
        throw std::invalid_argument("Material name must be nonempty text without NUL");
      if (name.size() > sizeof(material.id.name) - 3)
        throw std::length_error("Material name exceeds Blender ID name storage (" +
                                std::to_string(sizeof(material.id.name) - 3) + " UTF-8 bytes)");
      BKE_id_rename(*s.main, material.id, name);
      return material.id.name + 2;
    }
  throw std::invalid_argument("Material no longer exists");
}
std::shared_future<BlendAssetLibrary> BlenderHost::inspect_asset_library(
    const QString &root,
    std::shared_ptr<std::stop_source> cancel,
    std::function<void(const QString &)> progress)
{
  auto &s = *impl_;
  s.require_owner();
  if (!cancel)
    throw std::invalid_argument("Missing Asset Library cancellation");
  std::erase_if(s.asset_scan_jobs, [](const auto &job) {
    return job.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
  auto result = std::async(std::launch::async, [root, cancel, progress = std::move(progress)] {
                  return inspect_blend_asset_library(root, cancel->get_token(), progress);
                }).share();
  s.asset_scan_jobs.push_back({std::move(cancel), result});
  return result;
}
void BlenderHost::publish_native_material_preview(const QUuid &resource, const QString &hash)
{
  auto &s = *impl_;
  s.require_owner();
  s.inspection.publish_native_material_preview(
      resource.toString(QUuid::WithoutBraces).toStdString(), hash.toStdString());
}
void BlenderHost::prepare_native_material_gpu(const QUuid &resource,
                                              const QString &content,
                                              std::shared_ptr<NativeMaterialGraph> graph)
{
  auto &s = *impl_;
  s.require_owner();
  if (resource.isNull() || !graph)
    throw std::invalid_argument("Native material project resource is invalid");
  s.enter_gpu();
  try {
    s.inspection.prepare_native_material(resource.toString(QUuid::WithoutBraces).toStdString(),
                                         content.toStdString(),
                                         std::move(graph));
    s.inspection.set_native_fragment_geometry_provider([&s](NativeFragmentGeometryRequest request,
                                                            const std::atomic_bool &cancel) {
      return capture_native_fragment_geometry(
          s.context, s.view, s.displacement_preview.source_meshes(), std::move(request), cancel);
    });
    s.inspection.set_native_fragment_object_provider([&s](std::uint32_t uid) {
      NativeMaterialGpuContext context;
      bind_native_material_objects(*s.main, s.depsgraph, uid, context, false);
      return context.objects;
    });
    s.inspection.set_native_context_provider([&s](const TextureSet &set,
                                                  const TextureLayer &,
                                                  int,
                                                  const NativeMaterialDependencies &dependencies,
                                                  NativeMaterialGpuContext &context) {
      if (dependencies.object)
        bind_native_material_objects(*s.main, s.depsgraph, set.material_session_uid, context);
      if (dependencies.view) {
        if (!s.native_view_matrices)
          throw std::runtime_error("Native material view is not ready");
        bind_native_material_view(
            s.native_view_matrices->first, s.native_view_matrices->second, context);
      }
    });
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::cancel_native_material_preparation(const QUuid &resource)
{
  auto &s = *impl_;
  s.require_owner();
  s.enter_gpu();
  try {
    s.inspection.cancel_native_preparation(resource.toString(QUuid::WithoutBraces).toStdString());
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::discard_native_material_candidate(const QUuid &resource)
{
  auto &s = *impl_;
  s.require_owner();
  if (resource.isNull())
    throw std::invalid_argument("Native candidate UUID is missing");
  s.enter_gpu();
  try {
    s.inspection.discard_native_candidate(resource.toString(QUuid::WithoutBraces).toStdString());
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
void BlenderHost::alias_native_material(const QUuid &source, const QUuid &destination)
{
  auto &s = *impl_;
  s.require_owner();
  if (source.isNull() || destination.isNull())
    throw std::invalid_argument("Native material alias identity is invalid");
  s.enter_gpu();
  try {
    s.inspection.alias_native_material(source.toString(QUuid::WithoutBraces).toStdString(),
                                       destination.toString(QUuid::WithoutBraces).toStdString());
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
bool BlenderHost::has_native_material(const QUuid &resource) const
{
  impl_->require_owner();
  return impl_->inspection.has_native_material(
      resource.toString(QUuid::WithoutBraces).toStdString());
}
void BlenderHost::validate_material_layering(const TextureSet &set) const
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.main || s.main != G_MAIN)
    throw std::runtime_error("Material layering requires the live owner project");
  s.inspection.validate_material_layering(set);
}
void BlenderHost::validate_material_parameter_import(const TextureSet &set) const
{
  auto &s = *impl_;
  s.require_owner();
  if (!s.main || s.main != G_MAIN)
    throw std::runtime_error("Parameter Import requires the live owner project");
  if (set.material_layering.allocated())
    throw std::invalid_argument(
        "Import parameters into a material without allocated four-material stacks");
  require_material_parameter_main(set);
  s.inspection.validate_material_layering(set);
  for (const Material &material : s.main->materials)
    if (material.id.session_uid == set.material_session_uid) {
      require_material_parameter_surface(*s.main, material);
      return;
    }
  throw std::runtime_error("Parameter Import material no longer belongs to the live project");
}
void BlenderHost::validate_native_mapping_edit(const TextureSet &before,
                                               const TextureSet &after,
                                               NativeMappingEditOrigin origin) const
{
  impl_->require_owner();
  impl_->inspection.validate_native_mapping_edit(before, after, origin);
}
void BlenderHost::validate_native_material_target(const QUuid &resource,
                                                  const TextureSet &set) const
{
  auto &s = *impl_;
  s.require_owner();
  if (!has_native_material(resource))
    throw std::invalid_argument("Native material is still preparing");
  const auto key = resource.toString(QUuid::WithoutBraces).toStdString();
  bool matched = false, object = false, ao = false;
  for (const auto &layer : evaluation_layers(set))
    if (layer.fill.native_material && layer.fill.material_resource == key) {
      const auto requirements = s.inspection.native_requirements(key, *layer.fill.native_material);
      matched = true;
      object |= requirements.object;
      ao |= requirements.ao;
    }
  if (!matched) {
    object = s.inspection.native_material_uses_object(key);
    ao = s.inspection.native_material_uses_ao(key);
  }
  if (object) {
    NativeMaterialGpuContext context;
    bind_native_material_objects(*s.main, s.depsgraph, set.material_session_uid, context);
  }
  if (ao)
    for (int tile : set.tiles)
      if (!set.mesh_maps[3] || !set.mesh_maps[3]->tiles.contains(tile) ||
          !set.mesh_maps[3]->tiles.at(tile).pixels)
        throw std::invalid_argument(
            "Native material requires a baked AO map for every UV tile — Bake Mesh Maps…");
}

NativeMaterialPreparationStatus BlenderHost::native_material_status(const QUuid &resource) const
{
  impl_->require_owner();
  return impl_->inspection.native_preparation_status(
      resource.toString(QUuid::WithoutBraces).toStdString());
}
void BlenderHost::install_native_material(const QUuid &resource,
                                          const QString &content,
                                          std::shared_ptr<NativeMaterialGraph> graph)
{
  auto &s = *impl_;
  s.require_owner();
  if (resource.isNull() || !graph)
    throw std::invalid_argument("Native material project resource is invalid");
  s.enter_gpu();
  try {
    s.inspection.install_native_material(resource.toString(QUuid::WithoutBraces).toStdString(),
                                         content.toStdString(),
                                         std::move(graph));
    s.inspection.set_native_fragment_geometry_provider([&s](NativeFragmentGeometryRequest request,
                                                            const std::atomic_bool &cancel) {
      return capture_native_fragment_geometry(
          s.context, s.view, s.displacement_preview.source_meshes(), std::move(request), cancel);
    });
    s.inspection.set_native_fragment_object_provider([&s](std::uint32_t uid) {
      NativeMaterialGpuContext context;
      bind_native_material_objects(*s.main, s.depsgraph, uid, context, false);
      return context.objects;
    });
    s.inspection.set_native_context_provider([&s](const TextureSet &set,
                                                  const TextureLayer &,
                                                  int,
                                                  const NativeMaterialDependencies &dependencies,
                                                  NativeMaterialGpuContext &context) {
      if (dependencies.object)
        bind_native_material_objects(*s.main, s.depsgraph, set.material_session_uid, context);
      if (dependencies.view) {
        if (!s.native_view_matrices)
          throw std::runtime_error("Native material view is not ready");
        bind_native_material_view(
            s.native_view_matrices->first, s.native_view_matrices->second, context);
      }
    });
    DRW_gpu_context_disable_ex(false);
  }
  catch (...) {
    DRW_gpu_context_disable_ex(false);
    throw;
  }
}
std::shared_future<NativeMaterialPreviewResult> BlenderHost::preview_native_material(
    const QString &path, const QString &name, std::shared_ptr<std::atomic_bool> cancel)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady)
    throw std::logic_error("Native preview requires an initialized GPU viewport");
  if (!s.native_preview)
    s.native_preview = std::make_unique<NativeMaterialPreview>(
        *this,
        [&s](const QString &file,
             const QString &material,
             std::shared_ptr<std::atomic_bool> cancellation) {
          const auto colorspace = s.main->colorspace;
          return std::
              async(std::launch::async,
                    [file, material, colorspace, cancellation] {
                      std::unique_ptr<Main, decltype(&BKE_main_free)> reference(BKE_main_new(),
                                                                                BKE_main_free);
                      reference->colorspace = colorspace;
                      auto graph = prepare_native_material_graph(
                          *reference, file, material, *cancellation);
                      const auto channel = std::find_if(
                          graph->channels.begin(), graph->channels.end(), [](const auto &c) {
                            return c.channel == 0;
                          });
                      if (channel == graph->channels.end() || !channel->tree)
                        throw std::runtime_error("Native Base Color channel is unavailable");
                      const auto dependencies = native_material_dependencies(
                          *channel->tree, graph->description.parameters.size());
                      if (dependencies.surface || dependencies.object || dependencies.view ||
                          dependencies.ao)
                        throw std::runtime_error(
                            "Base Color preview unavailable: source requires object, surface, view or baked AO context");
                      // This private adapter carries only Base Color. Both decoder and uploader
                      // traverse its AOV roots, so unrelated channels never force image loading.
                      // Preserve the encoded material and its authored capability metadata.
                      std::erase_if(graph->channels, [](const auto &c) { return c.channel != 0; });
                      prepare_native_material_preview_images(*graph, *cancellation);
                      return graph;
                    })
                  .share();
        });
  return s.native_preview->request(path, name, std::move(cancel));
}
void BlenderHost::cancel_native_material_preview()
{
  auto &s = *impl_;
  s.require_owner();
  if (s.native_preview)
    s.native_preview->cancel();
}
bool BlenderHost::native_material_preview_pending() const
{
  impl_->require_owner();
  return impl_->native_preview && impl_->native_preview->pending();
}
std::shared_future<std::shared_ptr<NativeMaterialGraph>> BlenderHost::prepare_material_resource(
    const StudioAsset &asset,
    std::shared_ptr<std::atomic_bool> cancel,
    std::vector<StudioAsset> textures)
{
  auto &s = *impl_;
  s.require_owner();
  if (!cancel)
    throw std::invalid_argument("Missing native source hydration cancellation");
  std::erase_if(s.material_prepare_jobs, [](const auto &job) {
    return job.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
  const auto colorspace = s.main->colorspace;
  auto result =
      std::async(std::launch::async, [asset, colorspace, cancel, textures = std::move(textures)] {
        std::unique_ptr<Main, decltype(&BKE_main_free)> reference(BKE_main_new(), BKE_main_free);
        reference->colorspace = colorspace;
        return hydrate_native_material_resource(*reference, asset, *cancel, textures);
      }).share();
  s.material_prepare_jobs.push_back({std::move(cancel), result});
  return result;
}
std::shared_future<std::shared_ptr<NativeMaterialGraph>> BlenderHost::prepare_material_graph(
    const QString &path, const QString &name, std::shared_ptr<std::atomic_bool> cancel)
{
  auto &s = *impl_;
  s.require_owner();
  if (!cancel)
    throw std::invalid_argument("Missing material preparation cancellation");
  std::erase_if(s.material_prepare_jobs, [](const auto &job) {
    return job.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
  const auto colorspace = s.main->colorspace;
  auto result = std::async(std::launch::async, [path, name, colorspace, cancel] {
                  std::unique_ptr<Main, decltype(&BKE_main_free)> reference(BKE_main_new(),
                                                                            BKE_main_free);
                  reference->colorspace = colorspace;
                  return prepare_native_material_graph(*reference, path, name, *cancel);
                }).share();
  s.material_prepare_jobs.push_back({std::move(cancel), result});
  return result;
}
std::shared_future<NativeMaterialDescription> BlenderHost::inspect_material_graph(
    const QString &path, const QString &name, std::shared_ptr<std::atomic_bool> cancellation)
{
  auto &s = *impl_;
  s.require_owner();
  if (!cancellation)
    throw std::invalid_argument("Missing material inspection cancellation");
  std::erase_if(s.material_graph_jobs, [](const auto &job) {
    return job.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
  const auto colorspace = s.main->colorspace;
  auto result = std::async(std::launch::async, [path, name, colorspace, cancellation] {
                  std::unique_ptr<Main, decltype(&BKE_main_free)> reference(BKE_main_new(),
                                                                            BKE_main_free);
                  reference->colorspace = colorspace;
                  return inspect_blend_material_graph(*reference, path, name, *cancellation);
                }).share();
  s.material_graph_jobs.push_back({std::move(cancellation), result});
  return result;
}
std::shared_future<PreparedBlendMaterial> BlenderHost::import_native_material(
    const QString &path, const QString &name, std::shared_ptr<std::atomic_bool> cancel)
{
  auto &s = *impl_;
  s.require_owner();
  if (!cancel)
    throw std::invalid_argument("Missing native material import cancellation");
  std::erase_if(s.material_import_jobs, [](const auto &job) {
    return job.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
  const auto colorspace = s.main->colorspace;
  auto result = std::async(std::launch::async, [path, name, colorspace, cancel] {
                  std::unique_ptr<Main, decltype(&BKE_main_free)> reference(BKE_main_new(),
                                                                            BKE_main_free);
                  reference->colorspace = colorspace;
                  auto prepared = prepare_native_material_resource(
                      *reference, path, name, *cancel);
                  PreparedBlendMaterial result;
                  result.material = std::move(prepared.asset);
                  result.native_graph = std::move(prepared.graph);
                  result.textures = std::move(prepared.textures);
                  return result;
                }).share();
  s.material_import_jobs.push_back({std::move(cancel), result});
  return result;
}
std::shared_future<PreparedBlendMaterial> BlenderHost::import_blend_material(
    const QString &path,
    const QString &name,
    std::shared_ptr<std::atomic_bool> cancellation,
    bool preview_only)
{
  auto &s = *impl_;
  s.require_owner();
  if (!cancellation)
    throw std::invalid_argument("Missing material-import cancellation");
  std::erase_if(s.material_import_jobs, [](const auto &job) {
    return job.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
  const auto colorspace = s.main->colorspace;
  auto result =
      std::async(std::launch::async, [path, name, colorspace, cancellation, preview_only] {
        std::unique_ptr<Main, decltype(&BKE_main_free)> reference(BKE_main_new(), BKE_main_free);
        reference->colorspace = colorspace;
        return bts::import_blend_material(*reference, path, name, *cancellation, preview_only);
      }).share();
  s.material_import_jobs.push_back({std::move(cancellation), result});
  return result;
}
MeshReplacement BlenderHost::prepare_reimport(const TextureDocument &document,
                                              const std::string &path,
                                              bool preserve_world,
                                              std::optional<bool> import_cameras,
                                              std::optional<AutoUnwrapOptions> auto_unwrap,
                                              std::optional<UsdImportOptions> usd,
                                              std::optional<UnitSettings> units,
                                              std::optional<BlendMeshSelection> blend)
{
  auto &s = *impl_;
  s.require_owner();
  if (s.phase != Impl::Phase::GpuReady || s.drawing)
    throw std::logic_error("Reimport requires an idle viewport");
  auto result = prepare_mesh_replacement(
      *s.main,
      document,
      path,
      import_cameras.value_or(document.settings().import_cameras),
      auto_unwrap.value_or(document.settings().auto_unwrap),
      usd.value_or(document.settings().usd),
      units.value_or(document.settings().units),
      blend.value_or(document.settings().blend));
  reproject_spatial_strokes(
      *result.authored, s.source_snapshot(), result.source, result.materials, preserve_world);
  return result;
}
void BlenderHost::rebind_texture_sets(const TextureDocument &document)
{
  document.resolve_instances();
  auto &s = *impl_;
  s.require_owner();
  s.reset_warp_geometry();
  const auto geometry = scene_snapshot();
  for (const auto &set : document.texture_sets())
    if (!set.disabled)
      s.inspection.set_padding_geometry(*s.main, set.material_session_uid, geometry);
  for (const auto &set : document.texture_sets())
    apply_texture_set(set, int(document.settings().resolution));
  s.uv_dirty = true;
}
