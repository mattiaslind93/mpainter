// SPDX-License-Identifier: GPL-3.0-or-later
// update_pixels(), uniform previews, displacement content and shader parameters.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
bool MaterialInspection::update_pixels(Main &main,
                                       const TextureSet &source,
                                       int resolution,
                                       PaintPixels::Bounds bounds,
                                       bool interactive,
                                       bool allocate_only)
{
  const auto set = resident_texture_view(source);
  performance::Scope timing_all(allocate_only ? "allocate_gpu_images" : "update_pixels_cpu");
  impl_->invalidate(set.material_session_uid);
  const int height = set.rows();
  require_gpu_texture_extent(resolution, height, "Native paint image");
  const auto uid = set.material_session_uid;
  if (!impl_->bindings.contains(uid))
    impl_->bindings.emplace(uid, bind(main, uid));
  auto &binding = impl_->bindings.at(uid);
  binding.mask_snapshot = std::make_unique<TextureSet>(set);
  binding.shader_parameters = set.shader_parameters;
  restore_preview(main, binding);
  auto &tree = *binding.material->nodetree;
  set.native_per_uv = binding.native_per_uv;
  const bool created = binding.resolution == 0;
  bool shader_changed = created;
  std::map<int, std::pair<int, int>> target_sizes;
  for (int number : set.tiles)
    target_sizes[number] = {set.width(number), set.rows(number)};
  if (!created && (binding.resolution != resolution || binding.height != height ||
                   binding.tile_sizes != target_sizes))
  {
    std::vector<Image *> images(binding.images.begin(), binding.images.end());
    if (binding.ao_image)
      images.push_back(binding.ao_image);
    if (binding.authored_normal_image)
      images.push_back(binding.authored_normal_image);
    if (binding.mask_image)
      images.push_back(binding.mask_image);
    if (binding.displacement_image)
      images.push_back(binding.displacement_image);
    for (auto *image : binding.user_images)
      if (image)
        images.push_back(image);
    for (auto *image : images)
      if (image) {
        for (ImageTile &tile : image->tiles) {
          ImageUser user{};
          user.tile = tile.tile_number;
          if (!native_scale_image(
                  image, set.width(tile.tile_number), set.rows(tile.tile_number), &user))
            throw std::runtime_error("Cannot resize native channel image");
          tile.gen_x = set.width(tile.tile_number);
          tile.gen_y = set.rows(tile.tile_number);
        }
        BKE_image_free_gputextures(image);
      }
    for (auto *image : binding.world_normal_images)
      if (image)
        native_world_normal_resize(*image, set);
    binding.world_images_full = false;
    binding.world_published_signatures.clear();
    binding.world_scratch = {};
    binding.resolution = resolution;
    binding.height = height;
    binding.preview_images = {};
    binding.world_context.invalidate_owners();
    binding.world_ao_lease.reset();
    binding.world_ao_tile = 0;
    if (binding.world_geometry_job)
      binding.world_geometry_job->cancelled = true;
    binding.world_geometry_job.reset();
    binding.world_geometry.reset();
    binding.world_signature.clear();
    binding.world_queued_generation = ~std::uint64_t(0);
    binding.world_registry_generation = 0;
    binding.projection_cache.clear();
    binding.ownership_cache.clear();
    binding.geometry_names.clear();
    if (binding.projection_job) {
      binding.projection_job->cancelled = true;
      binding.projection_job.reset();
    }
    binding.normal_presence.clear();
    binding.normal_count = 0;
    binding.ao_presence.clear();
    binding.ao_count = 0;
    binding.displacement_presence.clear();
    binding.displacement_count = 0;
    binding.displacement_minimum = 0;
    binding.displacement_maximum = 1;
    binding.mask_cached = false;
    binding.displayed_channel = -2;
    bounds = {0, 0, resolution, height};
  }
  if (created) {
    auto *principled = native_principled_surface(*binding.material);
    if (!principled)
      throw std::runtime_error("Paint material is missing its Principled surface");
    const float empty[4] = {0, 0, 0, 0};
    // Allocate all native buffers before linking their outputs into the material.
    for (int channel = 0; channel < 5; ++channel) {
      const std::string name = "BTS Composite " + std::to_string(uid) + " " +
                               channel_names[channel];
      auto *image = native_generated_image(&main,
                                           set.width(1001),
                                           set.rows(1001),
                                           name.c_str(),
                                           32,
                                           1,
                                           IMA_GENTYPE_BLANK,
                                           empty,
                                           false,
                                           channel != 0,
                                           set.tiles != std::set<int>{1001});
      if (!image)
        throw std::runtime_error("Cannot allocate native paint image");
      image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
      binding.images[channel] = image;
      if (channel == 0)
        STRNCPY(image->colorspace_settings.name, main.colorspace.scene_linear_name);
      if (image->source == IMA_SRC_TILED) {
        for (int number : set.tiles)
          if (number != 1001) {
            auto *tile = BKE_image_add_tile(image, number, nullptr);
            if (!tile)
              throw std::runtime_error("Cannot allocate UDIM tile");
            tile->gen_x = set.width(number);
            tile->gen_y = set.rows(number);
            tile->gen_depth = 32;
            tile->gen_type = IMA_GENTYPE_BLANK;
            tile->gen_flag |= IMA_GEN_FLOAT;
            if (!native_fill_tile(image, tile))
              throw std::runtime_error("Cannot initialize UDIM tile");
          }
        if (!set.tiles.contains(1001))
          BKE_image_remove_tile(image, BKE_image_get_tile(image, 1001));
      }
      if (channel == 0 && !native_gpu_image(image))
        for (int number : set.tiles) {
          STRNCPY(image->colorspace_settings.name, main.colorspace.scene_linear_name);
          ImageUser user{};
          user.tile = number;
          auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
          if (!buffer)
            throw std::runtime_error("Cannot acquire native color image");
          IMB_colormanagement_assign_float_colorspace(buffer, main.colorspace.scene_linear_name);
          BKE_image_release_ibuf(image, buffer, nullptr);
        }
    }
    const char *inputs[] = {"Base Color", "Metallic", "Roughness"};
    for (int channel = 0; channel < 5; ++channel) {
      auto &texture = node(
          tree, "ShaderNodeTexImage", ("__BTS_Pixels" + std::to_string(channel)).c_str());
      texture.id = &binding.images[channel]->id;
      id_us_plus(texture.id);
      auto &mix = node(
          tree, "ShaderNodeMixRGB", ("__BTS_BaseMix" + std::to_string(channel)).c_str());
      bke::node_add_link(tree, texture, output(texture, "Alpha"), mix, input(mix, 0));
      // Filter associated color/data and coverage together, then unassociate exactly once.
      // This avoids undefined transparent pixels tinting soft stroke edges under bilinear
      // sampling.
      auto &divide = node(
          tree, "ShaderNodeVectorMath", ("__BTS_Unpremultiply" + std::to_string(channel)).c_str());
      divide.custom1 = NODE_VECTOR_MATH_DIVIDE;
      BKE_ntree_update_tag_node_property(&tree, &divide);
      bke::node_add_link(tree, texture, output(texture, "Color"), divide, input(divide, 0));
      bke::node_add_link(tree, texture, output(texture, "Alpha"), divide, input(divide, 1));
      bke::node_add_link(tree, divide, output(divide, "Vector"), mix, input(mix, 2));
      binding.textures[channel] = &texture;
      binding.unpremultiply[channel] = &divide;
      binding.base_mix[channel] = &mix;
      if (channel < 3) {
        auto *destination = bke::node_find_enabled_input_socket(*principled, inputs[channel]);
        if (!destination)
          throw std::runtime_error("Native paint channel input is missing");
        bke::node_add_link(tree, mix, output(mix, "Color"), *principled, *destination);
      }
    }
    auto &normal = node(tree, "ShaderNodeNormalMap", "__BTS_PaintNormal");
    auto &bump = node(tree, "ShaderNodeBump", "__BTS_PaintHeight");
    auto *normal_color = bke::node_find_enabled_input_socket(normal, "Color");
    auto *bump_normal = bke::node_find_enabled_input_socket(bump, "Normal");
    auto *bump_height = bke::node_find_enabled_input_socket(bump, "Height");
    auto *surface_normal = bke::node_find_enabled_input_socket(*principled, "Normal");
    if (!normal_color || !bump_normal || !bump_height || !surface_normal)
      throw std::runtime_error("Native normal/height socket missing");
    bke::node_add_link(
        tree, *binding.base_mix[3], output(*binding.base_mix[3], "Color"), normal, *normal_color);
    bke::node_add_link(tree, normal, output(normal, "Normal"), bump, *bump_normal);
    bke::node_add_link(
        tree, *binding.base_mix[4], output(*binding.base_mix[4], "Color"), bump, *bump_height);
    bke::node_add_link(tree, bump, output(bump, "Normal"), *principled, *surface_normal);
    binding.resolution = resolution;
    binding.height = height;
    binding.displayed_channel = -2;
    bounds = {0, 0, resolution, height};
  }
  // CPU metadata/oracle setup retains the legacy graph. Install GPU-only
  // world targets lazily, including bindings first bootstrapped without a GPU.
  if (GPU_context_active_get() && !binding.world_normal_images[0]) {
    auto *height_node = bke::node_find_node_by_name(tree, "__BTS_PaintHeight");
    auto *surface = native_principled_surface(*binding.material);
    if (!height_node || !surface)
      throw std::runtime_error("Native world Normal shader endpoints are missing");
    install_native_world_normal_shader(main,
                                       tree,
                                       set,
                                       binding.world_normal_images,
                                       binding.world_normal_enabled,
                                       *height_node,
                                       *surface);
    for (int side = 0; side < 2; ++side) {
      native_world_normal_placeholder(*binding.world_normal_images[side]);
    }
    shader_changed = true;
  }
  if (binding.tile_sizes != target_sizes) {
    if (binding.margin_job) {
      binding.margin_job->cancelled = true;
      binding.margin_job.reset();
    }
    binding.margins_ready = false;
    if (binding.padding)
      impl_->gpu_preview.set_padding_geometry(uid, {}, {});
  }
  binding.tile_sizes = target_sizes;
  if (binding.padding && !binding.margin_job && !binding.margins_ready)
    binding.margin_job = MarginGeometryJob::start(binding.padding, target_sizes);
  ensure_authored_normal_image(main, binding, set);
  if (set.ao_channel || set.mesh_maps[3] || binding.ao_image)
    ensure_ao_image(main, binding, set, resolution);
  shader_changed |= shader_parameters(main, set);
  auto *normal_node = bke::node_find_node_by_name(tree, "__BTS_PaintNormal");
  if (!normal_node)
    throw std::runtime_error("Paint normal node is missing");
  const int tangent_flags = (normal_node->custom1 & ~1) | (set.tangent_per_fragment ? 1 : 0);
  if (normal_node->custom1 != tangent_flags) {
    shader_changed = true;
    normal_node->custom1 = tangent_flags;
    BKE_ntree_update_tag_node_property(&tree, normal_node);
  }
  auto *bump = bke::node_find_node_by_name(tree, "__BTS_PaintHeight");
  auto *strength = bump ? bke::node_find_enabled_input_socket(*bump, "Strength") : nullptr;
  if (!strength)
    throw std::runtime_error("Height shader strength is missing");
  scalar(tree, *strength, 0);  // Height is converted once into the complete tangent-normal image.
  if (allocate_only) {
    // Native Image/node allocation is owner-thread work. No layer evaluation,
    // pixel hashing or CPU padding belongs to interactive initialization.
    for (int channel = 0; channel < 5; ++channel) {
      const auto base = encoded_material_base(set, channel);
      color(tree, input(*binding.base_mix[channel], 1), base);
      binding.last_base[channel] = base;
    }
    binding.previous_height_base = precision_channel(
        set.channels[4] ? set.base_values : MaterialValues{}, 4, set.precision[4])[0];
    binding.bases_valid = true;
    BKE_ntree_update_after_single_tree_change(main, tree);
    DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
    DEG_id_tag_update_ex(&main, &tree.id, ID_RECALC_NTREE_OUTPUT);
    std::printf("BTS_GPU_IMAGE_BOOTSTRAP uid=%u tiles=%zu resolution=%d cpu_composite=0\n",
                uid,
                set.tiles.size(),
                resolution);
    return created;
  }
  throw std::logic_error(
      "CPU pixel updates are forbidden; allocate a GPU target and queue the GPU stack");
}

bool MaterialInspection::preview_uniform(Main &, const TextureSet &set, int channel)
{
  CompositeChannels channels{};
  channels.at(channel) = true;
  return queue_layer_edit(set, channels, false);
}
bool MaterialInspection::displacement_content(std::uint32_t uid) const
{
  const auto found = impl_->bindings.find(uid);
  return found != impl_->bindings.end() &&
         (found->second.preview_active[16] ? found->second.preview_displacement_content :
                                             found->second.gpu_displacement_presence.value_or(
                                                 found->second.displacement_count > 0));
}
bool MaterialInspection::shader_parameters(Main &main, const TextureSet &set)
{
  set.shader_parameters.validate();
  const auto found = impl_->bindings.find(set.material_session_uid);
  if (found == impl_->bindings.end())
    return false;
  auto &binding = found->second;
  binding.shader_parameters = set.shader_parameters;
  binding.material_layering = set.material_layering;
  binding.layering_directx = set.normal_format == NormalMapFormat::DirectX;
  auto &tree = *binding.material->nodetree;
  bool changed = false;
  // Custom channels need fixed GPU targets even without the optional layered
  // shader. Allocate once when a slot is introduced, never evaluate its pixels
  // on the owner thread. Subsequent layer edits only publish into this image.
  if (binding.resolution)
    for (int slot = 0; slot < custom_channel_count; ++slot) {
      const bool needed = set.users[slot].allocated || set.users[slot].present ||
                          (set.material_layering.enabled &&
                           std::find(set.material_layering.mask_slots.begin(),
                                     set.material_layering.mask_slots.end(),
                                     slot) != set.material_layering.mask_slots.end());
      if (needed && !binding.user_images[slot]) {
        update_user_image(main, binding, set, slot, {}, true, true);
        changed = true;
      }
      if (binding.user_present[slot] != set.users[slot].present) {
        binding.user_present[slot] = set.users[slot].present;
        binding.displayed_channel = -2;
      }
    }
  const auto update = [&](const char *name, const char *socket, float value) {
    auto *n = bke::node_find_node_by_name(tree, name);
    auto *s = n ? bke::node_find_enabled_input_socket(*n, socket) : nullptr;
    if (!s || !s->default_value)
      return;
    if (static_cast<bNodeSocketValueFloat *>(s->default_value)->value == value)
      return;
    scalar(tree, *s, value);
    changed = true;
  };
  binding.scattering_slot = -1;
  for (int i = 0; i < custom_channel_count; ++i)
    if (set.users[i].present && set.users[i].scattering)
      binding.scattering_slot = i;
  if (!set.shader_parameters.openpbr) {
    changed |= apply_openpbr_shader(
        main, *binding.material, std::nullopt, impl_->subsurface_enabled);
    changed |= subsurface_shader(binding, impl_->subsurface_enabled);
  }
  update("__BTS_PaintNormal", "Strength", set.shader_parameters.normal_intensity);
  update("__BTS_AOMaterial", "Factor", set.shader_parameters.ao_intensity);
  if (binding.resolution || !set.material_layering.enabled) {
    if (const auto sources = impl_->layering_sources.images(set.material_layering))
      changed |= binding.layered_shader.apply(main,
                                              *binding.material,
                                              set.material_layering,
                                              set.shader_parameters.normal_intensity,
                                              set.shader_parameters.ao_intensity,
                                              binding.layering_directx,
                                              *sources);
    else
      impl_->layering_sources_pending = true;
  }
  if (set.shader_parameters.openpbr)
    changed |= apply_openpbr_shader(
        main, *binding.material, set.shader_parameters.openpbr, impl_->subsurface_enabled);
  if (!set.material_layering.enabled) {
    auto &library = impl_->gpu_preview.native_world_sources();
    const TextureLayer *coat = nullptr;
    for (const auto &layer : evaluation_layers(set))
      if (layer.visible && layer.kind == LayerKind::Fill && layer.channels[0] &&
          layer.opacity[0] > 0)
      {
        if (layer.fill.native_material && library.contains(layer.fill.material_resource))
          coat = &layer;
        else
          coat = nullptr;
      }
    const auto signature = coat ? coat->fill.material_resource +
                                      QJsonDocument(native_material_description_json(
                                                        *coat->fill.native_material))
                                          .toJson(QJsonDocument::Compact)
                                          .toStdString() +
                                      std::to_string(coat->opacity[0]) :
                                  std::string{};
    if (signature != binding.coat_signature) {
      if (auto *surface = native_principled_surface(*binding.material))
        changed |= apply_native_material_coat(
            main,
            tree,
            *surface,
            coat ? &library.source_graph(coat->fill.material_resource) : nullptr,
            coat ? coat->fill.native_material.get() : nullptr,
            coat ? coat->opacity[0] : 0.f);
      binding.coat_signature = signature;
    }
  }
  if (changed) {
    BKE_ntree_update_after_single_tree_change(main, tree);
    DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
    DEG_id_tag_update_ex(&main, &tree.id, ID_RECALC_NTREE_OUTPUT);
  }
  return changed;
}
bool MaterialInspection::openpbr_reapply_is_stable(Main &main, std::uint32_t uid)
{
  const auto found = impl_->bindings.find(uid);
  if (found == impl_->bindings.end())
    return true;
  auto &b = found->second;
  return !apply_openpbr_shader(
      main, *b.material, b.shader_parameters.openpbr, impl_->subsurface_enabled);
}
bool MaterialInspection::set_subsurface_enabled(Main &main, bool enabled)
{
  if (impl_->subsurface_enabled == enabled)
    return false;
  impl_->subsurface_enabled = enabled;
  bool changed = false;
  for (auto &[uid, binding] : impl_->bindings)
    if (binding.shader_parameters.openpbr ?
            apply_openpbr_shader(
                main, *binding.material, binding.shader_parameters.openpbr, enabled) :
            subsurface_shader(binding, enabled))
    {
      auto &tree = *binding.material->nodetree;
      BKE_ntree_update_after_single_tree_change(main, tree);
      DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
      DEG_id_tag_update_ex(&main, &tree.id, ID_RECALC_NTREE_OUTPUT);
      changed = true;
    }
  return changed;
}
bool MaterialInspection::preview_displacement(Main &main, const TextureSet &set)
{
  const auto found = impl_->bindings.find(set.material_session_uid);
  if (found == impl_->bindings.end() || !found->second.displacement_image)
    return false;
  found->second.displacement_settings = set.displacement;
  const bool changed = displacement_shader(main, found->second);
  if (changed) {
    impl_->gpu_displacement_changed = true;
    impl_->cycles.changed(*found->second.displacement_image, set.material_session_uid, 16);
  }
  return changed;
}
void MaterialInspection::prepare_pixels(Main &main, const TextureSet &source)
{
  const auto set = resident_texture_view(source);
  if (!has_pixels(set.material_session_uid)) {
    update_pixels(main, set, set.texture_resolution, {}, true, true);
    if (!queue_layer_edit(set, complete_composite_channels, true))
      throw std::runtime_error("GPU initial image queue failed");
  }
}
void MaterialInspection::queue_composite(const TextureSet &set)
{
  // Compatibility entry point: it must never schedule a CPU persistence or
  // interactive fallback. Unsupported input remains visible as an explicit error.
  if (!queue_layer_edit(set, complete_composite_channels, true))
    throw std::runtime_error("GPU layer stack target is unavailable");
}

