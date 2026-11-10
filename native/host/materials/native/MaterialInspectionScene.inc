// SPDX-License-Identifier: GPL-3.0-or-later
// Layering validation, native view refresh, reset, geometry visibility, residency, captures and restore.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
std::uint64_t MaterialInspection::release_bake_scratch()
{
  return impl_->gpu_preview.release_bake_scratch();
}
void MaterialInspection::publish_native_material_preview(const std::string &resource,
                                                         const std::string &hash)
{
  impl_->gpu_preview.publish_native_material_preview(resource, hash);
}
void MaterialInspection::prepare_native_material(std::string resource,
                                                 std::string content,
                                                 std::shared_ptr<NativeMaterialGraph> graph)
{
  impl_->gpu_preview.prepare_native_material(
      std::move(resource), std::move(content), std::move(graph));
}
void MaterialInspection::cancel_native_preparation(const std::string &resource)
{
  impl_->gpu_preview.cancel_native_preparation(resource);
}
void MaterialInspection::discard_native_candidate(const std::string &resource)
{
  impl_->gpu_preview.discard_native_candidate(resource);
}
void MaterialInspection::alias_native_material(const std::string &source,
                                               const std::string &destination)
{
  impl_->gpu_preview.alias_native_material(source, destination);
}
void MaterialInspection::validate_material_layering(const TextureSet &set) const
{
  if (set.material_layering.enabled) {
    for (const auto &input : set.material_layering.materials)
      require_material_layer_udim_coverage(input, set.tiles);
  }
  using Check = LayeringTopology;
  const auto found = impl_->bindings.find(set.material_session_uid);
  Check::require(found != impl_->bindings.end(),
                 "Material layering requires an initialized ordinary paint binding");
  const auto &binding = found->second;
  // Authoring only: the authoritative live Main is checked before dereferencing
  // a retained Main/material pointer. Detached Open validation never calls here.
  Check::require(G_MAIN && binding.owner_main == G_MAIN,
                 "Material layering requires the live owner project");
  const auto &main = *G_MAIN;
  const Material *material = nullptr;
  for (const auto &candidate : main.materials)
    if (&candidate == binding.material) {
      material = &candidate;
      break;
    }
  Check::require(material && material->id.session_uid == set.material_session_uid,
                 "Material layering material binding is no longer owned by the project");
  Check::require(!ID_IS_LINKED(&material->id) && !ID_IS_OVERRIDE_LIBRARY(&material->id) &&
                     BKE_id_is_editable(&main, &material->id) &&
                     !(material->id.tag & (ID_TAG_NO_MAIN | ID_TAG_LOCALIZED |
                                           ID_TAG_COPIED_ON_EVAL | ID_TAG_MISSING)),
                 "Material layering requires an editable local material");
  // Embedded tree ownership comes from the verified live Material, never from a
  // caller's descriptor or an unverified saved node-name lookup.
  Check::require(material->nodetree && !material->adt && !binding.shader_parameters.openpbr,
                 "Material layering requires an ordinary Principled binding");
  const auto &tree = *material->nodetree;
  Check::require((tree.id.flag & ID_FLAG_EMBEDDED_DATA) &&
                     BKE_id_owner_get(const_cast<ID *>(&tree.id), false) == &material->id &&
                     tree.type == NTREE_SHADER && !tree.adt,
                 "Material layering requires the unanimated material-owned shader tree");
  // Initial Enable must not adopt or overwrite a pre-existing foreign builder
  // namespace. Allocated disabled legacy graphs keep their retained nodes.
  if (!set.material_layering.allocated())
    for (const auto &n : tree.nodes)
      Check::require(!STRPREFIX(n.name, "__BTS_Layer"),
                     "Material layering has unowned reserved Layer nodes");
  if (set.material_layering.enabled &&
      std::any_of(set.material_layering.materials.begin(),
                  set.material_layering.materials.end(),
                  [](const auto &input) {
                    return std::any_of(input.textures.begin(),
                                       input.textures.end(),
                                       [](const auto &source) { return source && source->tiles; });
                  }))
  {
    // Capacity depends on the device, not a fabricated source tile limit.
    // Follow owned incoming edges from material outputs; disconnected images
    // cannot invent capacity rejection. Count retained Image nodes conservatively
    // plus distinct candidate image/sampler pairs; engine overhead is separate.
    std::set<const bNode *> reachable;
    std::vector<const bNode *> visit;
    for (const auto &node : tree.nodes)
      if (STREQ(node.idname, "ShaderNodeOutputMaterial") && (node.flag & NODE_DO_OUTPUT))
        visit.push_back(&node);
    if (visit.empty())
      for (const auto &node : tree.nodes)
        if (STREQ(node.idname, "ShaderNodeOutputMaterial"))
          visit.push_back(&node);
    while (!visit.empty()) {
      const auto *node = visit.back();
      visit.pop_back();
      if (!reachable.insert(node).second)
        continue;
      for (const auto &link : tree.links)
        if (link.tonode == node && !(link.flag & NODE_LINK_MUTED))
          visit.push_back(link.fromnode);
    }
    std::set<std::string> samplers;
    for (const auto &node : tree.nodes)
      if (reachable.contains(&node) && STREQ(node.idname, "ShaderNodeTexImage") && node.id &&
          !STRPREFIX(node.name, "__BTS_LayerMaterial"))
      {
        samplers.insert("retained:" + std::to_string(node.identifier));
        if (GS(node.id->name) == ID_IM &&
            reinterpret_cast<const Image *>(node.id)->source == IMA_SRC_TILED)
          samplers.insert("mapping:" + std::to_string(node.identifier));
      }
    for (const auto &input : set.material_layering.materials)
      for (const auto &source : input.textures)
        if (source) {
          if (source->tiles)
            for (const auto &[tile, member] : *source->tiles)
              samplers.insert(gpu_texture_resource_key(*member) + ":extend");
          else
            samplers.insert(gpu_texture_resource_key(*source) + ":repeat");
        }
    const int capacity = GPU_max_textures();
    if (capacity <= 0 || samplers.size() > std::size_t(capacity))
      throw std::invalid_argument("Conservative reachable material sampler estimate " +
                                  std::to_string(samplers.size()) +
                                  " exceeds GPU fragment capacity " + std::to_string(capacity) +
                                  "; engine sampler overhead is separate");
  }
  Check t{main, tree};
  Check::require(binding.resolution > 0 && binding.height > 0,
                 "Material layering requires ready native paint targets");
  Check::require(!impl_->queued.contains(set.material_session_uid) &&
                     !impl_->gpu_queued.contains(set.material_session_uid) &&
                     !impl_->gpu_stack_queued.contains(set.material_session_uid) &&
                     !(impl_->pending && impl_->pending->result.uid == set.material_session_uid) &&
                     !(impl_->job.valid() && impl_->running_uid == set.material_session_uid),
                 "Wait for the material preview before changing its layered shader");
  constexpr const char *textures[]{
      "__BTS_Pixels0", "__BTS_Pixels1", "__BTS_Pixels2", "__BTS_Pixels3", "__BTS_Pixels4"};
  constexpr const char *divides[]{"__BTS_Unpremultiply0",
                                  "__BTS_Unpremultiply1",
                                  "__BTS_Unpremultiply2",
                                  "__BTS_Unpremultiply3",
                                  "__BTS_Unpremultiply4"};
  constexpr const char *mixes[]{
      "__BTS_BaseMix0", "__BTS_BaseMix1", "__BTS_BaseMix2", "__BTS_BaseMix3", "__BTS_BaseMix4"};
  for (int c = 0; c < 5; ++c) {
    const auto &image = t.image(binding.images[c]);
    t.image_shape(image, set);
    for (int previous = 0; previous < c; ++previous)
      Check::require(binding.images[c] != binding.images[previous],
                     "Material layering native channel images are aliased");
    Check::require(!binding.preview_active[c],
                   "Finish the native material preview before changing its layered shader");
    const auto &texture = t.node(binding.textures[c], "ShaderNodeTexImage", textures[c]),
               &divide = t.node(binding.unpremultiply[c], "ShaderNodeVectorMath", divides[c]),
               &mix = t.node(binding.base_mix[c], "ShaderNodeMixRGB", mixes[c]);
    t.texture(texture, image);
    Check::require(divide.custom1 == NODE_VECTOR_MATH_DIVIDE,
                   "Material layering native unpremultiply operation changed");
    t.mix(mix, MA_RAMP_BLEND);
    t.edge(
        texture, t.out(texture, "Color", SOCK_RGBA), divide, t.in(divide, "Vector", SOCK_VECTOR));
    t.edge(texture,
           t.out(texture, "Alpha", SOCK_FLOAT),
           divide,
           t.in(divide, "Vector_001", SOCK_VECTOR));
    t.edge(texture, t.out(texture, "Alpha", SOCK_FLOAT), mix, t.in(mix, "Fac", SOCK_FLOAT));
    t.edge(divide, t.out(divide, "Vector", SOCK_VECTOR), mix, t.in(mix, "Color2", SOCK_RGBA));
    t.color(t.in(mix, "Color1", SOCK_RGBA), encoded_material_base(set, c));
  }
  const auto &view = t.node(binding.view_switch, "ShaderNodeMixShader", "__BTS_ViewSwitch"),
             &coverage = t.node(binding.coverage_mix, "ShaderNodeMixRGB", "__BTS_InspectCoverage");
  const auto &emission = t.source(
      view, t.in(view, "Shader_001", SOCK_SHADER), "ShaderNodeEmission", "__BTS_Inspection");
  t.edge(emission,
         t.out(emission, "Emission", SOCK_SHADER),
         view,
         t.in(view, "Shader_001", SOCK_SHADER));
  t.edge(
      coverage, t.out(coverage, "Color", SOCK_RGBA), emission, t.in(emission, "Color", SOCK_RGBA));
  t.scalar(t.in(emission, "Strength", SOCK_FLOAT), 1);
  const auto &surface = t.source(
      view, t.in(view, "Shader", SOCK_SHADER), "ShaderNodeBsdfPrincipled");
  t.edge(surface, t.out(surface, "BSDF", SOCK_SHADER), view, t.in(view, "Shader", SOCK_SHADER));
  int surfaces = 0;
  for (const auto &n : tree.nodes)
    surfaces += STREQ(n.idname, "ShaderNodeBsdfPrincipled");
  Check::require(surfaces == 1, "Material layering requires one retained Principled surface");
  const auto &output = t.node(native_material_output(*binding.material),
                              "ShaderNodeOutputMaterial");
  const auto &endpoint = t.in(output, "Surface", SOCK_SHADER);
  const auto &outer = t.source(output, endpoint, "ShaderNodeMixShader");
  if (&outer == &view)
    t.edge(view, t.out(view, "Shader", SOCK_SHADER), output, endpoint);
  else {
    t.node(&outer, "ShaderNodeMixShader", "__BTS_GeometrySwitch");
    t.edge(view, t.out(view, "Shader", SOCK_SHADER), outer, t.in(outer, "Shader", SOCK_SHADER));
    const auto &transparent = t.source(outer,
                                       t.in(outer, "Shader_001", SOCK_SHADER),
                                       "ShaderNodeBsdfTransparent",
                                       "__BTS_GeometryTransparent");
    t.edge(transparent,
           t.out(transparent, "BSDF", SOCK_SHADER),
           outer,
           t.in(outer, "Shader_001", SOCK_SHADER));
    const auto &enabled = t.source(
        outer, t.in(outer, "Fac", SOCK_FLOAT), "ShaderNodeMath", "__BTS_GeometryEnabled");
    Check::require(enabled.custom1 == NODE_MATH_MULTIPLY,
                   "Material layering visibility operation changed");
    t.edge(enabled, t.out(enabled, "Value", SOCK_FLOAT), outer, t.in(outer, "Fac", SOCK_FLOAT));
    t.edge(outer, t.out(outer, "Shader", SOCK_SHADER), output, endpoint);
  }
  const auto &normal = t.named("__BTS_PaintNormal", "ShaderNodeNormalMap"),
             &height = t.named("__BTS_PaintHeight", "ShaderNodeBump");
  Check::require(normal.storage, "Material layering native tangent-normal settings are missing");
  const auto &normal_settings = *static_cast<const NodeShaderNormalMap *>(normal.storage);
  Check::require(normal_settings.space == SHD_SPACE_TANGENT &&
                     normal_settings.convention == SHD_NORMAL_MAP_CONVENTION_OPENGL &&
                     normal_settings.base == SHD_NORMAL_MAP_BASE_DISPLACED &&
                     !normal_settings.uv_map[0] &&
                     normal.custom1 == (set.tangent_per_fragment ? 1 : 0),
                 "Material layering native tangent-normal settings changed");
  t.scalar(t.in(normal, "Strength", SOCK_FLOAT), binding.shader_parameters.normal_intensity);
  t.edge(*binding.base_mix[3],
         t.out(*binding.base_mix[3], "Color", SOCK_RGBA),
         normal,
         t.in(normal, "Color", SOCK_RGBA));
  Check::require(height.custom1 == 0 && height.custom2 == 0,
                 "Material layering native Bump operation changed");
  t.scalar(t.in(height, "Strength", SOCK_FLOAT), 0);
  t.scalar(t.in(height, "Distance", SOCK_FLOAT), .001f);
  t.scalar(t.in(height, "Filter Width", SOCK_FLOAT), .1f);
  t.edge(
      normal, t.out(normal, "Normal", SOCK_VECTOR), height, t.in(height, "Normal", SOCK_VECTOR));
  t.edge(*binding.base_mix[4],
         t.out(*binding.base_mix[4], "Color", SOCK_RGBA),
         height,
         t.in(height, "Height", SOCK_FLOAT));
  const bNode *ordinary_color = binding.base_mix[0];
  if (binding.ao_image) {
    const auto &image = t.image(binding.ao_image);
    t.image_shape(image, set);
    const auto &texture = t.named("__BTS_AOPixels", "ShaderNodeTexImage"),
               &mix = t.named("__BTS_AOMaterial", "ShaderNodeMixRGB");
    t.texture(texture, image);
    t.mix(mix, MA_RAMP_MULT);
    t.scalar(t.in(mix, "Fac", SOCK_FLOAT), binding.shader_parameters.ao_intensity);
    t.edge(*binding.base_mix[0],
           t.out(*binding.base_mix[0], "Color", SOCK_RGBA),
           mix,
           t.in(mix, "Color1", SOCK_RGBA));
    t.edge(texture, t.out(texture, "Color", SOCK_RGBA), mix, t.in(mix, "Color2", SOCK_RGBA));
    ordinary_color = &mix;
  }
  if (!binding.ao_image)
    for (const auto &n : tree.nodes)
      Check::require(!STREQ(n.name, "__BTS_AOMaterial"),
                     "Material layering has an unowned AO restoration node");
  if (binding.material_layering.enabled) {
    // The old active program may contain retained legacy textures. Only its
    // typed endpoints are relevant to safe Disable; uniform eligibility is a
    // separate caller gate. All ordinary restore edges above remain verified.
    const auto &color = t.named("__BTS_LayeredColor", "ShaderNodeMixRGB"),
               &metal = t.named("__BTS_LayeredChannel1", "ShaderNodeMixRGB"),
               &rough = t.named("__BTS_LayeredChannel2", "ShaderNodeMixRGB"),
               &bump = t.named("__BTS_LayeredBump", "ShaderNodeBump");
    t.mix(color, MA_RAMP_MULT);
    t.mix(metal, MA_RAMP_BLEND);
    t.mix(rough, MA_RAMP_BLEND);
    t.edge(
        color, t.out(color, "Color", SOCK_RGBA), surface, t.in(surface, "Base Color", SOCK_RGBA));
    t.edge(
        metal, t.out(metal, "Color", SOCK_RGBA), surface, t.in(surface, "Metallic", SOCK_FLOAT));
    t.edge(
        rough, t.out(rough, "Color", SOCK_RGBA), surface, t.in(surface, "Roughness", SOCK_FLOAT));
    t.edge(
        bump, t.out(bump, "Normal", SOCK_VECTOR), surface, t.in(surface, "Normal", SOCK_VECTOR));
  }
  else {
    t.edge(*ordinary_color,
           t.out(*ordinary_color, "Color", SOCK_RGBA),
           surface,
           t.in(surface, "Base Color", SOCK_RGBA));
    t.edge(*binding.base_mix[1],
           t.out(*binding.base_mix[1], "Color", SOCK_RGBA),
           surface,
           t.in(surface, "Metallic", SOCK_FLOAT));
    t.edge(*binding.base_mix[2],
           t.out(*binding.base_mix[2], "Color", SOCK_RGBA),
           surface,
           t.in(surface, "Roughness", SOCK_FLOAT));
    if (binding.world_normal_images[0])
      validate_native_world_normal_shader(
          main, tree, set, binding.world_normal_images, height, surface);
    else
      t.edge(height,
             t.out(height, "Normal", SOCK_VECTOR),
             surface,
             t.in(surface, "Normal", SOCK_VECTOR));
  }
}
void MaterialInspection::validate_native_mapping_edit(const TextureSet &before,
                                                      const TextureSet &after,
                                                      NativeMappingEditOrigin origin) const
{
  impl_->gpu_preview.validate_native_mapping_instances(
      after, origin == NativeMappingEditOrigin::HistoryRestore ? nullptr : &before);
}
bool MaterialInspection::has_native_material(const std::string &resource) const
{
  return impl_->gpu_preview.has_native_material(resource);
}
bool MaterialInspection::native_material_uses_object(const std::string &resource) const
{
  return impl_->gpu_preview.native_material_uses_object(resource);
}
bool MaterialInspection::native_material_uses_ao(const std::string &resource) const
{
  return impl_->gpu_preview.native_material_uses_ao(resource);
}
NativeMaterialDependencies MaterialInspection::native_requirements(
    const std::string &resource, const NativeMaterialDescription &instance) const
{
  return impl_->gpu_preview.native_requirements(resource, instance);
}
void MaterialInspection::prepare_native_height(const std::string &resource,
                                               NativeMaterialHeightResource value)
{
  impl_->gpu_preview.prepare_native_height(resource, std::move(value));
}
void MaterialInspection::stage_native_height(NativeMaterialHeightGpu &work,
                                             NativeMaterialHeightResource resource)
{
  impl_->gpu_preview.stage_native_height(work, std::move(resource));
}
bool MaterialInspection::step_native_height(NativeMaterialHeightGpu &work)
{
  return impl_->gpu_preview.step_native_height(work);
}
void MaterialInspection::install_native_height(NativeMaterialHeightGpu &work)
{
  impl_->gpu_preview.install_native_height(work);
}
bool MaterialInspection::set_native_height_policy(TextureSet &set,
                                                  const QUuid &id,
                                                  NativeHeightPolicy policy) const
{
  return impl_->gpu_preview.set_native_height_policy(set, id, policy);
}
void MaterialInspection::probe_native_height(const std::string &key,
                                             const NativeMaterialDescription &instance,
                                             NativeMaterialGpuContext context)
{
  impl_->gpu_preview.probe_native_height(key, instance, std::move(context));
}
void MaterialInspection::step_native_preparations()
{
  impl_->gpu_preview.step_native_preparations();
}
void MaterialInspection::refresh_native_view()
{
  auto &s = *impl_;
  const auto &library = s.gpu_preview.native_world_sources();
  for (auto &[uid, binding] : s.bindings) {
    const bool source_pending = binding.world_queued_generation != s.generation[uid] ||
                                binding.world_registry_generation != library.source_generation();
    if (binding.gpu_stack_set) {
      const auto channels = s.gpu_preview.native_view_channels(*binding.gpu_stack_set);
      if (std::any_of(channels.begin(), channels.end(), [](bool value) { return value; }))
        queue_layer_edit(*binding.gpu_stack_set, channels, false);
    }
    if (!binding.mask_snapshot)
      continue;
    const auto &set = *binding.mask_snapshot;
    const auto &layers = evaluation_layers(set);
    const auto found = std::find_if(layers.begin(), layers.end(), [&](const auto &layer) {
      return layer.fill.native_material && library.contains(layer.fill.material_resource) &&
             library.world_normal_perturbed(layer.fill.material_resource) &&
             library.world_normal_requires_view(layer.fill.material_resource);
    });
    if (found == layers.end())
      continue;
    if (binding.world_context.view.notify(true, [&] {
          return s.gpu_preview.native_world_view(
              set, *found, set.tiles.empty() ? 1001 : *set.tiles.begin());
        }))
      binding.world_view_dirty = true;
    // Compute-view queues may advance the ordinary generation. Their camera-only
    // change must not turn this world output update into owner/mesh observation.
    if (!source_pending) {
      binding.world_queued_generation = s.generation[uid];
      binding.world_registry_generation = library.source_generation();
    }
  }
}
NativeMaterialPreparationStatus MaterialInspection::native_preparation_status(
    const std::string &resource) const
{
  return impl_->gpu_preview.native_preparation_status(resource);
}
void MaterialInspection::install_native_material(std::string resource,
                                                 std::string content,
                                                 std::shared_ptr<NativeMaterialGraph> graph)
{
  impl_->gpu_preview.install_native_material(
      std::move(resource), std::move(content), std::move(graph));
}
void MaterialInspection::set_native_fragment_geometry_provider(
    NativeFragmentGeometryProvider provider)
{
  impl_->world_geometry_provider = std::move(provider);
}
void MaterialInspection::set_native_fragment_object_provider(NativeFragmentObjectProvider provider)
{
  impl_->world_object_provider = std::move(provider);
}
void MaterialInspection::refresh_native_world_objects(bool owner_revision, bool geometry_revision)
{
  if (!owner_revision && !geometry_revision)
    return;
  if (!GPU_context_active_get())
    throw std::logic_error("Native world object refresh requires the GPU owner");
  auto &s = *impl_;
  const auto &library = s.gpu_preview.native_world_sources();
  for (auto &[uid, binding] : s.bindings)
    if (binding.mask_snapshot) {
      bool used = false;
      for (const auto &layer : evaluation_layers(*binding.mask_snapshot))
        if (layer.fill.native_material && library.contains(layer.fill.material_resource) &&
            library.world_normal_perturbed(layer.fill.material_resource))
        {
          used = true;
          break;
        }
      if (!used)
        continue;
      // Geometry Position/Normal and tangent maps also consume owner matrices.
      // Observing the full small table can conservatively invalidate unused color.
      const bool changed = binding.world_object_context.notify(
          owner_revision, s.world_object_provider, uid, geometry_revision);
      if (changed) {
        s.invalidate_world_geometry(binding);
        binding.world_objects_dirty = true;
      }
    }
}
void MaterialInspection::set_native_context_provider(
    std::function<void(const TextureSet &,
                       const TextureLayer &,
                       int,
                       const NativeMaterialDependencies &,
                       NativeMaterialGpuContext &)> provider)
{
  impl_->gpu_preview.set_native_context_provider(std::move(provider));
}
MaterialInspection::MaterialInspection() : impl_(std::make_unique<Impl>()) {}
MaterialInspection::~MaterialInspection()
{
  reset();
}
void MaterialInspection::initialize_gpu_programs()
{
  performance::Scope timing("gpu_stack_startup_programs");
  impl_->gpu_preview.begin_frame();
}
void MaterialInspection::poll_gpu_programs()
{
  impl_->gpu_preview.poll_programs();
  if (auto error = impl_->gpu_preview.take_program_error(); !error.empty()) {
    pending_composite_error = std::move(error);
    std::fprintf(stderr, "BTS_GPU_STACK_PROGRAM_FAILED %s\n", pending_composite_error.c_str());
  }
}
bool MaterialInspection::gpu_programs_pending() const
{
  return impl_->gpu_preview.programs_pending();
}
void MaterialInspection::reset(bool terminal)
{
  if (terminal)
    NativeWorldNormalGeometryJob::shutdown();
  impl_->cycles.reset();
  impl_->cycles_entry_waiting = false;
  impl_->cycles_entry_requested = false;
  impl_->cycles_preparation_failed = false;
  impl_->cycles_preparation_error.clear();
  impl_->texture_preparation_owner = QUuid();
  impl_->preparing_textures.clear();
  impl_->texture_upload_waiting = false;
  impl_->texture_upload_error.clear();
  bool projection_jobs = false, margin_jobs = false;
  for (auto &[uid, binding] : impl_->bindings)
    if (binding.world_geometry_job)
      binding.world_geometry_job->cancelled = true;
  for (auto &[uid, binding] : impl_->bindings)
    if (binding.projection_job) {
      binding.projection_job->cancelled = true;
      projection_jobs = true;
    }
  if (projection_jobs)
    ProjectionGeometryJob::pool().waitForDone();
  for (auto &[uid, binding] : impl_->bindings)
    if (binding.margin_job) {
      binding.margin_job->cancelled = true;
      margin_jobs = true;
    }
  if (margin_jobs)
    MarginGeometryJob::pool().waitForDone();
  impl_->gpu_mips_pending = false;
  impl_->histogram_job.reset();
  impl_->histogram_failure.reset();
  impl_->material_sample_job.reset();
  impl_->material_sample_failure.reset();
  impl_->thumbnail_job.reset();
  impl_->thumbnail_failure.reset();
  impl_->layer_capture_job.reset();
  impl_->layer_capture_failure.reset();
  impl_->layering_sources.reset();
  impl_->layering_sources_pending = false;
  for (auto &[uid, binding] : impl_->bindings) {
    binding.world_ao_lease.reset();
    binding.world_ao = {};
  }
  impl_->gpu_preview.reset(terminal);
  impl_->gpu_stack_queued.clear();
  impl_->gpu_queued.clear();
  impl_->pending.reset();
  if (impl_->cancel)
    impl_->cancel->store(true);
  if (impl_->job.valid()) {
    try {
      impl_->job.get();
    }
    catch (...) {
    }
  }
  impl_->queued.clear();
  impl_->running.reset();
  impl_->cancel.reset();
  impl_->generation.clear();
  impl_->bindings.clear();
}
bool MaterialInspection::geometry_visibility_enabled(std::uint32_t uid) const
{
  const auto found = impl_->bindings.find(uid);
  return found != impl_->bindings.end() && found->second.geometry_visibility_on;
}
// UV helper nodes borrow image IDs without incrementing their users. The host calls
// collection only after releasing helper/engine caches and refreshing the source depsgraph.
void MaterialInspection::collect_retired_visibility_images(Main &main)
{
  for (auto it = impl_->retired_visibility_images.begin();
       it != impl_->retired_visibility_images.end();)
  {
    if ((*it)->id.us == 0) {
      BKE_id_free(&main, *it);
      it = impl_->retired_visibility_images.erase(it);
    }
    else
      ++it;
  }
}
std::size_t MaterialInspection::geometry_visibility_image_count() const
{
  std::size_t count = impl_->retired_visibility_images.size();
  for (const auto &[uid, binding] : impl_->bindings) {
    const auto *texture = bke::node_find_node_by_name(*binding.material->nodetree,
                                                      "__BTS_GeometryVisibility");
    count += texture && texture->id;
  }
  return count;
}
bool MaterialInspection::set_geometry_visibility(Main &main,
                                                 const TextureSet *set,
                                                 bool preserve_others)
{
  bool changed = false;
  // One selected-layer presentation target at a time. Other Texture Sets remain visible.
  for (auto &[uid, binding] : impl_->bindings)
    if (!preserve_others && binding.geometry_visibility_on &&
        (!set || uid != set->material_session_uid))
    {
      if (binding.residency_hidden && binding.mask_snapshot) {
        auto proxy = *binding.mask_snapshot;
        proxy.residency_view = false;
        proxy.layers = {TextureLayer{}};
        proxy.instance_layers.reset();
        proxy.selected_layer = proxy.layers.front().id;
        changed |= set_geometry_visibility(main, &proxy, true);
        continue;
      }
      auto &tree = *binding.material->nodetree;
      auto *enabled = bke::node_find_node_by_name(tree, "__BTS_GeometryEnabled");
      if (enabled)
        scalar(tree, input(*enabled, 1), 0);
      binding.geometry_visibility_on = false;
      BKE_ntree_update_after_single_tree_change(main, tree);
      DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
      DEG_id_tag_update_ex(&main, &tree.id, ID_RECALC_NTREE_OUTPUT);
      changed = true;
    }
  if (!set)
    return changed;
  const auto &layer = TextureDocument::layer(const_cast<TextureSet &>(*set), set->selected_layer);
  if (!impl_->bindings.contains(set->material_session_uid))
    impl_->bindings.emplace(set->material_session_uid, bind(main, set->material_session_uid));
  auto &binding = impl_->bindings.at(set->material_session_uid);
  auto &tree = *binding.material->nodetree;
  std::string key = QJsonDocument(geometry_mask_json(layer.geometry))
                        .toJson(QJsonDocument::Compact)
                        .toStdString();
  for (int tile : set->tiles)
    key += std::to_string(tile) + ":" + std::to_string(set->width(tile)) + ":" +
           std::to_string(set->rows(tile)) + ";";
  key += set->loaded ? "loaded" : "unloaded";
  for (int tile : set->unloaded_tiles)
    key += "off:" + std::to_string(tile) + ";";
  if (binding.geometry_visibility_on && binding.geometry_visibility_key == key)
    return changed;
  auto &texture = node(tree, "ShaderNodeTexImage", "__BTS_GeometryVisibility");
  auto *storage = static_cast<NodeTexImage *>(texture.storage);
  storage->interpolation = SHD_INTERP_CLOSEST;
  auto &enabled = node(tree, "ShaderNodeMath", "__BTS_GeometryEnabled");
  enabled.custom1 = NODE_MATH_MULTIPLY;
  auto &transparent = node(tree, "ShaderNodeBsdfTransparent", "__BTS_GeometryTransparent");
  auto &mix = node(tree, "ShaderNodeMixShader", "__BTS_GeometrySwitch");
  if (!(input(mix, 1).flag & SOCK_IS_LINKED)) {  // Establish the known presentation wrapper once.
    auto *surface = native_material_output(*binding.material);
    if (!surface)
      throw std::runtime_error("Geometry visibility material output missing");
    auto &destination = input(*surface, 0);
    bke::node_remove_socket_links(tree, destination);
    bke::node_add_link(
        tree, *binding.view_switch, output(*binding.view_switch, "Shader"), mix, input(mix, 1));
    bke::node_add_link(tree, transparent, output(transparent, "BSDF"), mix, input(mix, 2));
    bke::node_add_link(tree, mix, output(mix, "Shader"), *surface, destination);
    bke::node_add_link(tree, texture, output(texture, "Color"), enabled, input(enabled, 0));
    bke::node_add_link(tree, enabled, output(enabled, "Value"), mix, input(mix, 0));
  }
  if (binding.geometry_visibility_key != key) {
    const bool tiled = set->tiles != std::set<int>{1001};
    const bool mesh = layer.geometry.type == 0 && !layer.geometry.meshes.empty();
    const auto width = [&](int tile) { return mesh ? set->width(tile) : 2; };
    const auto height = [&](int tile) { return mesh ? set->rows(tile) : 2; };
    const float zero[4] = {0, 0, 0, 1};
    Image *image = nullptr;
    if (texture.id) {
      if (GS(texture.id->name) != ID_IM)
        throw std::runtime_error("Geometry visibility image type changed");
      image = reinterpret_cast<Image *>(texture.id);
      bool same = (image->source == IMA_SRC_TILED) == tiled;
      std::size_t count = 0;
      for (const ImageTile &tile : image->tiles) {
        ++count;
        if (!set->tiles.contains(tile.tile_number))
          same = false;
      }
      if (count != set->tiles.size())
        same = false;
      for (int tile : set->tiles)
        if (same) {
          ImageUser user{};
          user.tile = tile;
          auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
          same = buffer && buffer->x == width(tile) && buffer->y == height(tile) &&
                 buffer->float_data();
          if (buffer)
            BKE_image_release_ibuf(image, buffer, nullptr);
        }
      if (!same)
        image = nullptr;
    }
    if (!image) {
      image = native_generated_image(&main,
                                     width(1001),
                                     height(1001),
                                     "BTS Geometry Visibility",
                                     32,
                                     1,
                                     IMA_GENTYPE_BLANK,
                                     zero,
                                     false,
                                     true,
                                     tiled);
      if (!image)
        throw std::runtime_error("Cannot allocate geometry visibility image");
      image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
      if (tiled) {
        for (int number : set->tiles)
          if (number != 1001) {
            auto *tile = BKE_image_add_tile(image, number, nullptr);
            if (!tile)
              throw std::runtime_error("Cannot create geometry visibility tile");
            tile->gen_x = width(number);
            tile->gen_y = height(number);
            tile->gen_depth = 32;
            tile->gen_type = IMA_GENTYPE_BLANK;
            tile->gen_flag |= IMA_GEN_FLOAT;
            std::copy_n(zero, 4, tile->gen_color);
            if (!native_fill_tile(image, tile))
              throw std::runtime_error("Cannot fill geometry visibility tile");
          }
        if (!set->tiles.contains(1001))
          BKE_image_remove_tile(image, BKE_image_get_tile(image, 1001));
      }
      // Transfer the generated image's initial user to this node.
      auto *old = texture.id;
      texture.id = &image->id;
      if (old) {
        id_us_min(old);
        impl_->retired_visibility_images.push_back(reinterpret_cast<Image *>(old));
      }
    }
    for (int tile : set->tiles) {
      ImageUser user{};
      user.tile = tile;
      auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
      if (!buffer || !buffer->float_data()) {
        if (buffer)
          BKE_image_release_ibuf(image, buffer, nullptr);
        throw std::runtime_error("Geometry visibility buffer unavailable");
      }
      auto *data = buffer->float_data_for_write();
      for (int y = 0; y < height(tile); ++y)
        for (int x = 0; x < width(tile); ++x) {
          const float excluded = !set->tile_loaded(tile) ||
                                 !TextureDocument::geometry_includes(*set, layer, x, y, tile);
          const auto offset = (std::size_t(y) * width(tile) + x) * 4;
          data[offset] = data[offset + 1] = data[offset + 2] = excluded;
          data[offset + 3] = 1;
        }
      BKE_image_mark_dirty(image, buffer);
      BKE_image_update_gputexture_delayed(
          image, BKE_image_get_tile(image, tile), buffer, 0, 0, width(tile), height(tile));
      BKE_image_release_ibuf(image, buffer, nullptr);
    }
    DEG_id_tag_update_ex(&main, &image->id, ID_RECALC_SHADING);
    binding.geometry_visibility_key = key;
  }
  scalar(tree, input(enabled, 1), 1);
  binding.geometry_visibility_on = true;
  BKE_ntree_update_after_single_tree_change(main, tree);
  DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
  DEG_id_tag_update_ex(&main, &tree.id, ID_RECALC_NTREE_OUTPUT);
  return true;
}
void MaterialInspection::evict_unloaded_resources(const TextureDocument &document)
{
  impl_->cycles.reset();
  impl_->gpu_preview.evict_residency(document.texture_sets());
}
void MaterialInspection::change_residency(Main &main, const TextureSet &source)
{
  const auto set = resident_texture_view(source);
  impl_->cycles.reset();
  update_pixels(main, set, set.texture_resolution, {}, true, true);
  auto &binding = impl_->bindings.at(set.material_session_uid);
  binding.residency_hidden = !source.fully_loaded();
  binding.world_set.reset();
  binding.world_geometry.reset();
  binding.world_ao_lease.reset();
  binding.world_ao = {};
  binding.projection_cache.clear();
  binding.ownership_cache.clear();
  impl_->histogram_job.reset();
  impl_->thumbnail_job.reset();
  impl_->layer_capture_job.reset();
  impl_->material_sample_job.reset();
  std::vector<TextureSet> resident;
  for (const auto &[uid, other] : impl_->bindings)
    if (other.mask_snapshot)
      resident.push_back(*other.mask_snapshot);
  impl_->gpu_preview.evict_residency(resident);
  if (!queue_layer_edit(set, complete_composite_channels, true))
    throw std::runtime_error("Cannot restore resident texture targets");
  auto visibility = source;
  visibility.layers = {TextureLayer{}};
  visibility.instance_layers.reset();
  visibility.selected_layer = visibility.layers.front().id;
  set_geometry_visibility(main, &visibility, true);
}
void MaterialInspection::begin_project_source_capture()
{
  impl_->gpu_preview.begin_frame();
}
bool MaterialInspection::capture_project_source(const std::string &key,
                                                const PaintPixels &pixels,
                                                int channel,
                                                int tile,
                                                int x,
                                                int y,
                                                int width,
                                                int height,
                                                GpuProjectReadback &readback)
{
  const auto state = impl_->gpu_preview.capture_author_chunk(
      key, pixels, channel, tile, x, y, width, height, readback);
  if (state == GpuPreviewUpdate::Unsupported)
    throw std::runtime_error("Cannot snapshot GPU-authored source pixels; CPU fallback disabled");
  return state == GpuPreviewUpdate::Updated;
}
void MaterialInspection::capture_project_images(GpuProjectReadback &readback)
{
  for (auto &[uid, binding] : impl_->bindings)
    for (int channel = 0; channel < 17; ++channel) {
      if (channel == 6 || channel == 15)
        continue;  // Regenerable inspection-only caches, never stock shading.
      auto *image = composite_image(binding, channel);
      if (!image)
        continue;
      const auto overrides = BKE_image_gpu_override_texture(image);
      auto *texture = overrides.texture ? *overrides.texture : nullptr;
      if (!texture)
        continue;  // Unbound inspection caches are not presentation data.
      for (ImageTile &tile : image->tiles) {
        if (tile.gen_x == 1 && tile.gen_y == 1)
          continue;
        ImageGpuTileRegion region;
        if (!BKE_image_gpu_override_tile_region(image, tile.tile_number, region))
          throw std::runtime_error("Cannot snapshot GPU project tile region");
        readback.submit_native(image->id.session_uid,
                               tile.tile_number,
                               texture,
                               region.x,
                               region.y,
                               region.layer,
                               region.width,
                               region.height);
      }
    }
  for (auto &[uid, binding] : impl_->bindings)
    for (auto *image : binding.world_normal_images)
      if (image) {
        const auto overrides = BKE_image_gpu_override_texture(image);
        auto *texture = overrides.texture ? *overrides.texture : nullptr;
        if (!texture)
          continue;
        for (ImageTile &tile : image->tiles) {
          if (tile.gen_x == 1 && tile.gen_y == 1)
            continue;
          ImageGpuTileRegion region;
          if (!BKE_image_gpu_override_tile_region(image, tile.tile_number, region))
            throw std::runtime_error("Cannot snapshot private world Normal tile");
          readback.submit_native(image->id.session_uid,
                                 tile.tile_number,
                                 texture,
                                 region.x,
                                 region.y,
                                 region.layer,
                                 region.width,
                                 region.height);
        }
      }
}
std::vector<MaterialParameterMaskCapture> MaterialInspection::capture_material_layer_masks(
    const TextureSet &set, GpuProjectReadback &readback)
{
  require_material_parameter_main(set);
  validate_material_layering(set);
  if (!set.material_layering.allocated())
    throw std::invalid_argument(
        "Apply four-material mode before exporting its three opacity maps");
  auto &binding = impl_->bindings.at(set.material_session_uid);
  require_material_parameter_surface(*binding.owner_main, *binding.material);
  if (binding.material_layering.mask_slots != set.material_layering.mask_slots ||
      binding.material_layering.mask_roots != set.material_layering.mask_roots)
    throw std::invalid_argument("Opacity ownership is still being published");
  struct Target {
    Image *image;
    gpu::Texture *texture;
    ImageGpuTileRegion region;
    int stack, tile;
  };
  std::vector<Target> targets;
  std::uint64_t bytes = 0;
  auto &tree = *binding.material->nodetree;
  // Authoring admission checks the retained Surface endpoints; export additionally
  // proves the exact three mask factors feed the six serialized material roles.
  LayeringTopology topology{*binding.owner_main, tree};
  for (int channel = 0; channel < 6; ++channel) {
    const bNode *previous = nullptr;
    for (int stack = 0; stack < 3; ++stack) {
      const auto &factor = topology.named(("__BTS_LayerMask" + std::to_string(stack)).c_str(),
                                          "ShaderNodeClamp");
      const auto &mix = topology.named(
          ("__BTS_LayerMix" + std::to_string(channel) + "_" + std::to_string(stack)).c_str(),
          "ShaderNodeMixRGB");
      topology.mix(mix, MA_RAMP_BLEND);
      topology.edge(factor,
                    topology.out(factor, "Result", SOCK_FLOAT),
                    mix,
                    topology.in(mix, "Fac", SOCK_FLOAT));
      if (previous)
        topology.edge(*previous,
                      topology.out(*previous, "Color", SOCK_RGBA),
                      mix,
                      topology.in(mix, "Color1", SOCK_RGBA));
      previous = &mix;
    }
    const auto &final = topology.named(("__BTS_LayeredChannel" + std::to_string(channel)).c_str(),
                                       "ShaderNodeMixRGB");
    topology.mix(final, MA_RAMP_BLEND);
    topology.scalar(topology.in(final, "Fac", SOCK_FLOAT), 0);
    topology.edge(*previous,
                  topology.out(*previous, "Color", SOCK_RGBA),
                  final,
                  topology.in(final, "Color1", SOCK_RGBA));
  }
  const auto edge = [&](const char *from,
                        const char *from_type,
                        const char *output,
                        int output_type,
                        const char *to,
                        const char *to_type,
                        const char *input,
                        int input_type) {
    const auto &a = topology.named(from, from_type), &b = topology.named(to, to_type);
    topology.edge(a, topology.out(a, output, output_type), b, topology.in(b, input, input_type));
  };
  edge("__BTS_LayeredChannel0",
       "ShaderNodeMixRGB",
       "Color",
       SOCK_RGBA,
       "__BTS_LayeredColor",
       "ShaderNodeMixRGB",
       "Color1",
       SOCK_RGBA);
  edge("__BTS_LayeredChannel5",
       "ShaderNodeMixRGB",
       "Color",
       SOCK_RGBA,
       "__BTS_LayeredCompleteAO",
       "ShaderNodeMixRGB",
       "Color1",
       SOCK_RGBA);
  edge("__BTS_LayeredCompleteAO",
       "ShaderNodeMixRGB",
       "Color",
       SOCK_RGBA,
       "__BTS_LayeredAOIntensity",
       "ShaderNodeMixRGB",
       "Color2",
       SOCK_RGBA);
  edge("__BTS_LayeredAOIntensity",
       "ShaderNodeMixRGB",
       "Color",
       SOCK_RGBA,
       "__BTS_LayeredColor",
       "ShaderNodeMixRGB",
       "Color2",
       SOCK_RGBA);
  edge("__BTS_LayeredChannel3",
       "ShaderNodeMixRGB",
       "Color",
       SOCK_RGBA,
       "__BTS_LayerDetailScale",
       "ShaderNodeVectorMath",
       "Vector",
       SOCK_VECTOR);
  edge("__BTS_LayeredChannel4",
       "ShaderNodeMixRGB",
       "Color",
       SOCK_RGBA,
       "__BTS_LayerSharedHeight",
       "ShaderNodeMath",
       "Value",
       SOCK_FLOAT);
  edge("__BTS_LayerSharedHeight",
       "ShaderNodeMath",
       "Value",
       SOCK_FLOAT,
       "__BTS_LayeredBump",
       "ShaderNodeBump",
       "Height",
       SOCK_FLOAT);
  for (int stack = 0; stack < 3; ++stack) {
    const int slot = set.material_layering.mask_slots[stack];
    auto *image = binding.user_images.at(slot);
    auto *sample = binding.user_textures.at(slot);
    auto *clamp = bke::node_find_node_by_name(tree,
                                              ("__BTS_LayerMask" + std::to_string(stack)).c_str());
    bool owned = false;
    for (const Image &item : binding.owner_main->images)
      owned |= &item == image;
    if (!owned || !sample ||
        bke::node_find_node_by_name(tree, ("__BTS_UserPixels" + std::to_string(slot)).c_str()) !=
            sample ||
        sample->id != &image->id || !STREQ(sample->idname, "ShaderNodeTexImage") ||
        (sample->flag & NODE_MUTED) || !clamp || !STREQ(clamp->idname, "ShaderNodeClamp") ||
        clamp->custom1 != NODE_CLAMP_MINMAX || (clamp->flag & NODE_MUTED) || !sample->storage)
      throw std::invalid_argument("Opacity stack " + std::to_string(stack + 1) +
                                  " has no exact owned shader image");
    require_material_parameter_mask_sampling(*sample);
    for (const auto &link : tree.links)
      if (link.tonode == sample || (link.tonode == clamp && link.tosock != &input(*clamp, 0)))
        throw std::invalid_argument("Opacity sampling/clamp has an unsupported input link");
    if (image->alpha_mode != IMA_ALPHA_CHANNEL_PACKED ||
        !STREQ(image->colorspace_settings.name,
               IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA)))
      throw std::invalid_argument("Opacity export requires raw packed-alpha image interpretation");
    auto &value = input(*clamp, 0);
    int links = 0;
    for (const auto &link : tree.links)
      if (link.tosock == &value) {
        ++links;
        if (link.fromnode != sample || link.fromsock != &output(*sample, "Color") ||
            !(link.flag & NODE_LINK_VALID) || (link.flag & NODE_LINK_MUTED))
          throw std::invalid_argument("Opacity shader source link changed");
      }
    if (links != 1 ||
        static_cast<const bNodeSocketValueFloat *>(input(*clamp, 1).default_value)->value != 0 ||
        static_cast<const bNodeSocketValueFloat *>(input(*clamp, 2).default_value)->value != 1)
      throw std::invalid_argument("Opacity shader clamp contract changed");
    int consumers = 0;
    for (const auto &link : tree.links)
      if (link.fromnode == clamp) {
        bool known = false;
        for (int channel = 0; channel < 6; ++channel) {
          const auto &mix = topology.named(
              ("__BTS_LayerMix" + std::to_string(channel) + "_" + std::to_string(stack)).c_str(),
              "ShaderNodeMixRGB");
          known |= link.tonode == &mix && link.tosock == &topology.in(mix, "Fac", SOCK_FLOAT) &&
                   link.fromsock == &topology.out(*clamp, "Result", SOCK_FLOAT);
        }
        if (!known || !(link.flag & NODE_LINK_VALID) || (link.flag & NODE_LINK_MUTED))
          throw std::invalid_argument("Opacity factor has an unrepresented shader consumer");
        ++consumers;
      }
    if (consumers != 6)
      throw std::invalid_argument("Opacity factor shader-role fanout changed");
    const auto override = BKE_image_gpu_override_texture(image);
    auto *texture = override.texture ? *override.texture : nullptr;
    std::set<int> tiles;
    for (const ImageTile &tile : image->tiles)
      tiles.insert(tile.tile_number);
    if (!texture || tiles != set.tiles)
      throw std::invalid_argument(
          "Opacity images are not fully published; enable/apply the mode and wait before export");
    for (int tile : set.tiles) {
      ImageGpuTileRegion region;
      if (!BKE_image_gpu_override_tile_region(image, tile, region) ||
          region.width != set.width(tile) || region.height != set.rows(tile))
        throw std::invalid_argument(
            "Opacity tile dimensions/publication differ from the selected set");
      bytes = resource_sum(bytes,
                           resource_product(resource_product(region.width, region.height), 16));
      targets.push_back({image, texture, region, stack, tile});
    }
  }
  // All roles/tiles are admitted before retaining the first native allocation.
  std::vector<MaterialParameterMaskCapture> result;
  for (const auto &target : targets) {
    const auto &r = target.region;
    result.push_back({target.stack, target.tile, r.width, r.height, readback.images().size()});
    readback.submit_native(target.image->id.session_uid,
                           target.tile,
                           target.texture,
                           r.x,
                           r.y,
                           r.layer,
                           r.width,
                           r.height);
  }
  return result;
}
void MaterialInspection::pack_images()
{
  for (auto &[uid, binding] : impl_->bindings) {
    for (Image *image : binding.images)
      if (image && !BKE_image_memorypack(image))
        throw std::runtime_error("Cannot pack derived composite image");
    for (auto *image : binding.world_normal_images)
      if (image && BKE_image_gpu_override_texture(image).texture &&
          *BKE_image_gpu_override_texture(image).texture && !BKE_image_memorypack(image))
        throw std::runtime_error("Cannot pack private world Normal image");
    if (binding.authored_normal_image && !BKE_image_memorypack(binding.authored_normal_image))
      throw std::runtime_error("Cannot pack authored normal inspection");
    if (binding.ao_image && !BKE_image_memorypack(binding.ao_image))
      throw std::runtime_error("Cannot pack AO image");
    if (binding.displacement_image && !BKE_image_memorypack(binding.displacement_image))
      throw std::runtime_error("Cannot pack displacement image");
    for (auto *image : binding.user_images)
      if (image && !BKE_image_memorypack(image))
        throw std::runtime_error("Cannot pack custom channel image");
    if (binding.mask_image && !BKE_image_memorypack(binding.mask_image))
      throw std::runtime_error("Cannot pack mask inspection image");
  }
  if (!impl_->bindings.empty())
    MaterialLayerShader::pack_images(*impl_->bindings.begin()->second.owner_main);
}
void MaterialInspection::restore(Main &main, const TextureDocument &document)
{
  auto pending = std::make_unique<Impl>();
  pending->subsurface_enabled = impl_->subsurface_enabled;
  pending->world_geometry_provider = impl_->world_geometry_provider;
  pending->world_object_provider = impl_->world_object_provider;
  for (const auto &stored : document.texture_sets()) {
    const auto set = resident_texture_view(stored);
    // A read-only imported material retains its native graph, not BTS targets.
    if (!set.uniform_editable)
      continue;
    Material *material = nullptr;
    for (Material &candidate : main.materials)
      if (candidate.id.session_uid == set.material_session_uid)
        material = &candidate;
    if (!material || !material->nodetree)
      throw std::runtime_error("Saved Texture Set material is missing");
    auto &tree = *material->nodetree;
    BKE_ntree_update_after_single_tree_change(main, tree);
    const auto required = [&](const std::string &name, const char *type) {
      auto *n = bke::node_find_node_by_name(tree, name);
      if (!n || !STREQ(n->idname, type))
        throw std::runtime_error("Saved material node missing or changed: " + name);
      return n;
    };
    if (!native_principled_surface(*material))
      throw std::runtime_error("Saved material surface was replaced");
    if (auto *enabled = bke::node_find_node_by_name(tree, "__BTS_GeometryEnabled"))
      scalar(tree, input(*enabled, 1), 0);
    Binding binding{material,
                    required("__BTS_ViewSwitch", "ShaderNodeMixShader"),
                    required("__BTS_InspectCoverage", "ShaderNodeMixRGB")};
    binding.owner_main = &main;
    required("__BTS_Inspection", "ShaderNodeEmission");
    required("__BTS_Checker", "ShaderNodeTexChecker");
    required("__BTS_UvCoordinates", "ShaderNodeTexCoord");
    if (bke::node_find_node_by_name(tree, "__BTS_Pixels0")) {
      binding.resolution = set.texture_resolution;
      binding.height = set.rows();
      for (int number : set.tiles)
        binding.tile_sizes[number] = {set.width(number), set.rows(number)};
      for (int c = 0; c < 5; ++c) {
        binding.textures[c] = required("__BTS_Pixels" + std::to_string(c), "ShaderNodeTexImage");
        binding.unpremultiply[c] = required("__BTS_Unpremultiply" + std::to_string(c),
                                            "ShaderNodeVectorMath");
        binding.base_mix[c] = required("__BTS_BaseMix" + std::to_string(c), "ShaderNodeMixRGB");
        auto *id = binding.textures[c]->id;
        if (!id || GS(id->name) != ID_IM)
          throw std::runtime_error("Saved composite image is missing");
        binding.images[c] = reinterpret_cast<Image *>(id);
        auto *image = binding.images[c];
        if (set.tiles != std::set<int>{1001} && image->source != IMA_SRC_TILED)
          throw std::runtime_error("Saved UDIM image lost its tiles");
        for (int number : set.tiles) {
          if (!BKE_image_get_tile(image, number))
            throw std::runtime_error("Saved composite tile missing");
          ImageUser user{};
          user.tile = number;
          auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
          const bool valid = buffer && buffer->x == set.width(number) &&
                             buffer->y == set.rows(number) && buffer->float_data();
          if (buffer)
            BKE_image_release_ibuf(image, buffer, nullptr);
          if (!valid)
            throw std::runtime_error("Saved composite image dimensions or format changed");
        }
      }
      required("__BTS_PaintNormal", "ShaderNodeNormalMap");
      required("__BTS_PaintHeight", "ShaderNodeBump");
    }
    if (auto *ao = bke::node_find_node_by_name(tree, "__BTS_AOPixels")) {
      if (!STREQ(ao->idname, "ShaderNodeTexImage") || !ao->id || GS(ao->id->name) != ID_IM)
        throw std::runtime_error("Saved AO image is invalid");
      binding.ao_image = reinterpret_cast<Image *>(ao->id);
      required("__BTS_AOMaterial", "ShaderNodeMixRGB");
      for (int number : set.tiles) {
        ImageUser user{};
        user.tile = number;
        auto *buffer = BKE_image_acquire_ibuf(binding.ao_image, &user, nullptr);
        const bool valid = buffer && buffer->x == set.width(number) &&
                           buffer->y == set.rows(number) && buffer->float_data();
        if (buffer)
          BKE_image_release_ibuf(binding.ao_image, buffer, nullptr);
        if (!valid)
          throw std::runtime_error("Saved AO buffer is invalid");
      }
    }
    for (int side = 0; side < 2; ++side) {
      const auto name = "__BTS_NativeWorldPixels" + std::to_string(side);
      if (auto *texture = bke::node_find_node_by_name(tree, name)) {
        if (!STREQ(texture->idname, "ShaderNodeTexImage") || !texture->id ||
            GS(texture->id->name) != ID_IM)
          throw std::runtime_error("Saved world Normal image is invalid");
        binding.world_normal_images[side] = reinterpret_cast<Image *>(texture->id);
      }
    }
    binding.world_normal_enabled = bke::node_find_node_by_name(tree, "__BTS_NativeWorldEnabled");
    if (binding.resolution && !set.material_layering.enabled) {
      auto *height = required("__BTS_PaintHeight", "ShaderNodeBump");
      auto *surface = native_principled_surface(*material);
      install_native_world_normal_shader(main,
                                         tree,
                                         set,
                                         binding.world_normal_images,
                                         binding.world_normal_enabled,
                                         *height,
                                         *surface);
    }
    if (auto *texture = bke::node_find_node_by_name(tree, "__BTS_AuthoredNormalPixels")) {
      if (!STREQ(texture->idname, "ShaderNodeTexImage") || !texture->id ||
          GS(texture->id->name) != ID_IM)
        throw std::runtime_error("Saved authored normal image is invalid");
      binding.authored_normal_image = reinterpret_cast<Image *>(texture->id);
    }
    for (int slot = 0; slot < custom_channel_count; ++slot) {
      const auto name = "__BTS_UserPixels" + std::to_string(slot);
      if (auto *texture = bke::node_find_node_by_name(tree, name)) {
        if (!STREQ(texture->idname, "ShaderNodeTexImage") || !texture->id ||
            GS(texture->id->name) != ID_IM)
          throw std::runtime_error("Saved custom image is invalid");
        binding.user_images[slot] = reinterpret_cast<Image *>(texture->id);
        binding.user_textures[slot] = texture;
        binding.user_unpremultiply[slot] = required(
            "__BTS_UserUnpremultiply" + std::to_string(slot), "ShaderNodeVectorMath");
      }
      binding.user_present[slot] = set.users[slot].present;
    }
    if (auto *texture = bke::node_find_node_by_name(tree, "__BTS_PixelsDisp")) {
      if (!STREQ(texture->idname, "ShaderNodeTexImage") || !texture->id ||
          GS(texture->id->name) != ID_IM)
        throw std::runtime_error("Saved displacement image is invalid");
      binding.displacement_image = reinterpret_cast<Image *>(texture->id);
      binding.displacement_texture = texture;
      binding.displacement_unpremultiply = required("__BTS_UnpremultiplyDisp",
                                                    "ShaderNodeVectorMath");
      binding.displacement_mix = required("__BTS_BaseMixDisp", "ShaderNodeMixRGB");
      binding.displacement_node = required("__BTS_Displacement", "ShaderNodeDisplacement");
    }
    binding.displacement_settings = set.displacement;
    pending->bindings.emplace(set.material_session_uid, std::move(binding));
  }
  impl_->cycles.reset();
  impl_ = std::move(pending);
  for (const auto &set : document.texture_sets())
    shader_parameters(main, set);
}
void MaterialInspection::swap_prepared(MaterialInspection &prepared) noexcept
{
  impl_.swap(prepared.impl_);
}
