// SPDX-License-Identifier: GPL-3.0-or-later
// update(): binding a texture set's material, and padding geometry.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
bool MaterialInspection::update(Main &main,
                                InspectionMode mode,
                                std::span<const InspectionMaterial> materials)
{
  const int channel = int(mode);
  if (channel < -1 || channel > 7 + custom_channel_count)
    throw std::invalid_argument("Invalid inspection channel");
  if (mode == InspectionMode::Material && impl_->bindings.empty())
    return false;
  // Resolve the whole set before changing the active view switch.
  for (const auto &source : materials) {
    if (!impl_->bindings.contains(source.material_uid))
      impl_->bindings.emplace(source.material_uid, bind(main, source.material_uid));
    for (float coverage : source.coverage)
      if (!std::isfinite(coverage) || coverage < 0 || coverage > 1)
        throw std::invalid_argument("Invalid inspection coverage");
  }
  bool changed = false;
  for (const auto &source : materials) {
    auto &binding = impl_->bindings.at(source.material_uid);
    if (source.mask_set)
      binding.mask_snapshot = std::make_unique<TextureSet>(*source.mask_set);
    if (!source.mesh_inspection && !binding.mesh_displayed && mode == InspectionMode::Mask &&
        binding.displayed_channel == channel && binding.mask_display_linked && source.mask_set &&
        GpuLayerPreview::supported(*source.mask_set, 15))
    {
      if (binding.mask_async_layer != source.mask_set->selected_layer) {
        CompositeChannels channels{};
        channels[15] = true;
        queue_layer_edit(*source.mask_set, channels, false);
        changed = true;
      }
      continue;
    }
    if (!source.mesh_inspection && !binding.mesh_displayed && mode != InspectionMode::Mask &&
        binding.displayed_channel == channel &&
        (binding.resolution || (binding.displayed_values == source.authored &&
                                binding.displayed_coverage == source.coverage)))
      continue;
    changed = true;
    binding.mask_display_linked = false;
    auto &tree = *binding.material->nodetree;
    scalar(tree, input(*binding.view_switch, 0), channel < 0 ? 0 : 1);
    if (source.mesh_inspection) {
      auto &factor = input(*binding.coverage_mix, 0),
           &destination = input(*binding.coverage_mix, 2);
      bke::node_remove_socket_links(tree, factor);
      bke::node_remove_socket_links(tree, destination);
      BKE_ntree_update_after_single_tree_change(main, tree);
      if (source.mesh_map) {
        baked_image(main, binding, *source.mesh_map, *source.mask_set);
        bke::node_add_link(tree,
                           *binding.bake_texture,
                           output(*binding.bake_texture, "Alpha"),
                           *binding.coverage_mix,
                           factor);
        bke::node_add_link(tree,
                           *binding.bake_unpremultiply,
                           output(*binding.bake_unpremultiply, "Vector"),
                           *binding.coverage_mix,
                           destination);
      }
      else {
        scalar(tree, factor, 0);
        color(tree, destination, {0, 0, 0, 1});
      }
    }
    else if (mode == InspectionMode::Normal) {
      auto &factor = input(*binding.coverage_mix, 0),
           &destination = input(*binding.coverage_mix, 2);
      bke::node_remove_socket_links(tree, factor);
      bke::node_remove_socket_links(tree, destination);
      BKE_ntree_update_after_single_tree_change(main, tree);
      scalar(tree, factor, 1);
      if (binding.authored_normal_image) {
        auto &texture = node(tree, "ShaderNodeTexImage", "__BTS_AuthoredNormalPixels");
        bke::node_add_link(
            tree, texture, output(texture, "Color"), *binding.coverage_mix, destination);
      }
      else
        color(tree, destination, {.5, .5, 1, 1});
    }
    else if (mode == InspectionMode::Displacement) {
      auto &factor = input(*binding.coverage_mix, 0),
           &destination = input(*binding.coverage_mix, 2);
      bke::node_remove_socket_links(tree, factor);
      bke::node_remove_socket_links(tree, destination);
      BKE_ntree_update_after_single_tree_change(main, tree);
      scalar(tree, factor, 1);
      if (binding.displacement_mix)
        bke::node_add_link(tree,
                           *binding.displacement_mix,
                           output(*binding.displacement_mix, "Color"),
                           *binding.coverage_mix,
                           destination);
      else
        color(tree, destination, {.5f, .5f, .5f, 1});
    }
    else if (channel >= 7) {
      const int slot = channel - 7;
      auto &factor = input(*binding.coverage_mix, 0),
           &destination = input(*binding.coverage_mix, 2);
      bke::node_remove_socket_links(tree, factor);
      bke::node_remove_socket_links(tree, destination);
      BKE_ntree_update_after_single_tree_change(main, tree);
      if (binding.user_present[slot] && binding.user_textures[slot] &&
          binding.user_unpremultiply[slot])
      {
        bke::node_add_link(tree,
                           *binding.user_textures[slot],
                           output(*binding.user_textures[slot], "Alpha"),
                           *binding.coverage_mix,
                           factor);
        bke::node_add_link(tree,
                           *binding.user_unpremultiply[slot],
                           output(*binding.user_unpremultiply[slot], "Vector"),
                           *binding.coverage_mix,
                           destination);
      }
      else {
        scalar(tree, factor, 0);
        color(tree, destination, {0, 0, 0, 1});
      }
    }
    else if (mode == InspectionMode::AmbientOcclusion) {
      auto &factor = input(*binding.coverage_mix, 0),
           &destination = input(*binding.coverage_mix, 2);
      bke::node_remove_socket_links(tree, factor);
      bke::node_remove_socket_links(tree, destination);
      BKE_ntree_update_after_single_tree_change(main, tree);
      scalar(tree, factor, 1);
      if (binding.ao_image) {
        auto &texture = node(tree, "ShaderNodeTexImage", "__BTS_AOPixels");
        bke::node_add_link(
            tree, texture, output(texture, "Color"), *binding.coverage_mix, destination);
      }
      else
        color(tree, destination, {1, 1, 1, 1});
    }
    else if (mode == InspectionMode::Mask) {
      auto &factor = input(*binding.coverage_mix, 0),
           &destination = input(*binding.coverage_mix, 2);
      bke::node_remove_socket_links(tree, factor);
      bke::node_remove_socket_links(tree, destination);
      BKE_ntree_update_after_single_tree_change(
          main, tree);  // Refresh linked socket flags before assigning constants.
      const LayerMask *mask = nullptr;
      if (source.mask_set)
        for (const auto &layer : source.mask_set->layers)
          if (layer.id == source.mask_set->selected_layer && layer.mask)
            mask = &*layer.mask;
      scalar(tree, factor, mask ? 1 : 0);
      if (mask && (GpuLayerPreview::supported(*source.mask_set, 15) || !mask->pixels.empty() ||
                   !mask->effects.empty()))
      {
        if (GpuLayerPreview::supported(*source.mask_set, 15)) {
          mask_image(main, binding, *source.mask_set, *mask, source.resolution, true);
          if (binding.mask_async_layer != source.mask_set->selected_layer) {
            CompositeChannels channels{};
            channels[15] = true;
            queue_layer_edit(*source.mask_set, channels, false);
          }
        }
        else if (binding.mask_async_layer != source.mask_set->selected_layer) {
          std::fprintf(stderr,
                       "BTS_GPU_CPU_FALLBACK reason=unsupported-mask-inspection background=0\n");
          mask_image(main, binding, *source.mask_set, *mask, source.resolution);
        }
        bke::node_add_link(tree,
                           *binding.mask_texture,
                           output(*binding.mask_texture, "Color"),
                           *binding.coverage_mix,
                           destination);
        binding.mask_display_linked = true;
      }
      else {
        const float value = mask ? mask->background : 0;
        color(tree, destination, {value, value, value, 1});
      }
    }
    else if (channel >= 0) {
      const auto &v = source.authored;
      std::array<float, 4> value;
      switch (mode) {
        case InspectionMode::BaseColor:
          value = v.base_color;
          break;
        case InspectionMode::Metallic:
          value = {v.metallic, v.metallic, v.metallic, 1};
          break;
        case InspectionMode::Roughness:
          value = {v.roughness, v.roughness, v.roughness, 1};
          break;
        case InspectionMode::Normal:
          value = {v.normal[0] * .5f + .5f, v.normal[1] * .5f + .5f, v.normal[2] * .5f + .5f, 1};
          break;
        case InspectionMode::Bump:
          value = {v.bump, v.bump, v.bump, 1};
          break;
        default:
          value = {};
          break;
      }
      auto &factor = input(*binding.coverage_mix, 0),
           &destination = input(*binding.coverage_mix, 2);
      if (binding.textures[channel]) {
        bke::node_remove_socket_links(tree, factor);
        bke::node_remove_socket_links(tree, destination);
        auto &texture = *binding.textures[channel];
        bke::node_add_link(tree, texture, output(texture, "Alpha"), *binding.coverage_mix, factor);
        bke::node_add_link(tree,
                           *binding.unpremultiply[channel],
                           output(*binding.unpremultiply[channel], "Vector"),
                           *binding.coverage_mix,
                           destination);
      }
      else {
        bke::node_remove_socket_links(tree, factor);
        bke::node_remove_socket_links(tree, destination);
        BKE_ntree_update_after_single_tree_change(main, tree);
        scalar(tree, factor, source.coverage[channel]);
        color(tree, destination, value);
      }
    }
    BKE_ntree_update_after_single_tree_change(main, tree);
    DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
    DEG_id_tag_update_ex(&main, &tree.id, ID_RECALC_NTREE_OUTPUT);
    binding.mesh_displayed = source.mesh_inspection;
    binding.displayed_channel = channel;
    binding.displayed_values = source.authored;
    binding.displayed_coverage = source.coverage;
    if (mode == InspectionMode::Normal && !source.mesh_inspection) {
      // The authored-normal inspection image may never have been sampled by
      // the material. Request it on demand without cancelling other channels.
      if (auto queued = impl_->gpu_stack_queued.find(source.material_uid);
          queued != impl_->gpu_stack_queued.end())
        queued->second.channels[6] = true;
      else if (binding.gpu_stack_set && GpuLayerPreview::supported(*binding.gpu_stack_set, 6)) {
        Impl::Request request{*binding.gpu_stack_set,
                              binding.padding,
                              impl_->generation[source.material_uid],
                              bool(binding.ao_image)};
        request.channels = {};
        request.channels[6] = true;
        impl_->gpu_stack_queued.emplace(source.material_uid, std::move(request));
      }
    }
  }
  return changed;
}
std::shared_ptr<TexturePadding> MaterialInspection::export_padding(std::uint32_t uid) const
{
  return impl_->bindings.at(uid).padding;
}
bool MaterialInspection::has_padding(std::uint32_t uid) const
{
  const auto found = impl_->bindings.find(uid);
  return found != impl_->bindings.end() && bool(found->second.padding);
}
void MaterialInspection::set_padding_geometry(Main &main,
                                              std::uint32_t uid,
                                              const SceneSnapshot &snapshot)
{
  auto padding = std::make_shared<TexturePadding>(snapshot, uid);
  if (!impl_->bindings.contains(uid))
    impl_->bindings.emplace(uid, bind(main, uid));
  if (auto &binding = impl_->bindings.at(uid); true) {
    binding.world_context.invalidate_owners();
    binding.world_ao_lease.reset();
    binding.world_ao_tile = 0;
    if (binding.world_geometry_job)
      binding.world_geometry_job->cancelled = true;
    binding.world_geometry_job.reset();
    binding.world_geometry.reset();
    binding.world_signature.clear();
    ++binding.world_geometry_revision;
  }
  impl_->bindings.at(uid).mask_cached = false;
  impl_->bindings.at(uid).projection_cache.clear();
  impl_->bindings.at(uid).ownership_cache.clear();
  impl_->bindings.at(uid).geometry_names.clear();
  if (auto &job = impl_->bindings.at(uid).projection_job) {
    job->cancelled = true;
    job.reset();
  }
  if (auto &job = impl_->bindings.at(uid).margin_job) {
    job->cancelled = true;
    job.reset();
  }
  impl_->bindings.at(uid).margins_ready = false;
  impl_->gpu_preview.set_padding_geometry(uid, {}, {});
  impl_->bindings.at(uid).padding = std::move(padding);
  impl_->bindings.at(uid).native_per_uv = native_length_per_uv(snapshot, uid);
}
std::vector<unsigned char> MaterialInspection::uv_coverage(std::uint32_t uid, int tile) const
{
  const auto &binding = impl_->bindings.at(uid);
  if (!binding.padding)
    throw std::runtime_error("Export UV geometry is missing");
  return binding.padding->uv_coverage(
      binding.tile_sizes.at(tile).first, tile, binding.tile_sizes.at(tile).second);
}
