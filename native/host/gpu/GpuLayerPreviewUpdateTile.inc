// SPDX-License-Identifier: GPL-3.0-or-later
// GpuLayerPreview::update_tile(): evaluates one channel's layer stack for a tile on the GPU. The
// longest member function of the preview; GpuLayerPreview.cc includes it inside namespace bts.
GpuPreviewUpdate GpuLayerPreview::update_tile(const TextureSet &source_set,
                                              int channel,
                                              gpu::Texture *target,
                                              int tile,
                                              std::array<int, 3> target_offset,
                                              bool prefix_only,
                                              const QUuid *query_layer,
                                              bool query_source,
                                              const QUuid *query_effect,
                                              bool thumbnail,
                                              bool raw_anchor,
                                              bool authored_ao_output)
{

  const auto normal_set = channel == 6 ?
                              std::optional<TextureSet>(native_normal_stack(source_set)) :
                              std::nullopt;
  auto view = normal_set ? *normal_set : source_set;
  if (view.residency_view && !view.tile_loaded(tile)) {
    view.layers = {TextureLayer{}};
    view.layers.front().channels.fill(false);
    view.layers.front().ao_enabled = false;
    view.instance_layers.reset();
    view.mesh_maps = {};
    view.projection_geometry.reset();
    view.projection_tiles.clear();
    view.geometry_ownership.clear();
  }
  const auto &set = view;
  PaintMetadataScope metadata;
  if (!supported(set, channel) || !set.tiles.contains(tile) ||
      GPU_texture_dimensions(target) != 2 || target_offset[0] < 0 || target_offset[1] < 0 ||
      target_offset[2] < 0 ||
      (!query_layer && !prefix_only &&
       (GPU_texture_width(target) < target_offset[0] + set.width(tile) ||
        GPU_texture_height(target) < target_offset[1] + set.rows(tile))) ||
      target_offset[2] >= std::max(1, GPU_texture_layer_count(target)))
    return GpuPreviewUpdate::Unsupported;
  const auto format = GPU_texture_format(target);
  if (format != gpu::TextureFormat::SFLOAT_16_16_16_16 &&
      format != gpu::TextureFormat::SFLOAT_32_32_32_32)
    return GpuPreviewUpdate::Unsupported;
  auto &s = *impl_;
  performance::Scope timing("gpu_stack_submit");
  s.input_rejected = false;
  s.native_image_failure.clear();
  ++s.clock;
  const auto upload_before = s.remaining;
  const int width = set.width(tile), height = set.rows(tile);
  if (const auto state = s.initialize(width, height, format, GPU_texture_is_array(target));
      state != GpuPreviewUpdate::Updated)
    return state;
  // Keep disabled masks cached, but release deleted masks/layers and old tiles.
  const auto set_prefix = set.id.toString().toStdString();
  std::set<std::string> live_masks;
  for (const auto &layer : evaluation_layers(set))
    if (layer.mask)
      for (int number : set.tiles)
        live_masks.insert(paint_key(set, layer, 2, true, number));
  std::erase_if(s.mask_stacks, [&](const auto &item) {
    return item.first.starts_with(set_prefix) && !live_masks.contains(item.first);
  });
  // Resolving an adjustment payload can recursively initialize a 2D source
  // query. Keep this caller's target-format/array variant across that recursion.
  auto *const target_final_shader = s.final_shader;
  const auto stack_key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                         std::to_string(tile) +
                         (raw_anchor ? ":anchor:" + query_layer->toString().toStdString() :
                                       std::string{});
  const auto target_key = std::pair{reinterpret_cast<std::uintptr_t>(target), target_offset};
  auto &stack = s.stacks[stack_key];
  stack.last_used = s.clock;
  const bool target_changed = !prefix_only && s.target_owners[target_key] != stack_key;
  if (stack.width != width || stack.height != height) {
    stack.outputs.clear();
    stack.node_owners.clear();
    stack.queries.clear();
    stack.node_versions.clear();
    stack.coverage.reset();
    stack.height_image.reset();
    stack.output_dirty.clear();
    stack.height_dirty.clear();
    stack.head = nullptr;
    stack.plan.reset();
    stack.width = width;
    stack.height = height;
  }
  std::shared_ptr<const GpuMarginPlan> margin;
  if (!query_layer)
    if (const auto found = s.margin_plans.find(set.material_session_uid);
        found != s.margin_plans.end())
    {
      const auto &plans = set.padding_3d && channel != 3 && channel != 6 ? found->second.adjacent :
                                                                           found->second.extend;
      if (!plans.contains(tile))
        return GpuPreviewUpdate::Pending;
      margin = plans.at(tile);
      if (margin->width != width || margin->height != height)
        return GpuPreviewUpdate::Pending;
      if (!s.margins.prepare(margin, s.remaining, s.upload_start, testing_stack))
        return GpuPreviewUpdate::Pending;
    }
  struct Input {
    std::shared_ptr<NativeMaterialGpuContext> native_context;
    const MeshMapPlane *native_ao = nullptr;
    const TextureLayer *layer;
    GpuLayerChannel channel;
    GpuAnchorInput anchor;
    std::optional<MeshGeneratorSettings> generator;
    GpuGeneratorInputs generator_inputs;
    gpu::Texture *source = nullptr, *mask = nullptr, *surface = nullptr, *effects = nullptr,
                 *ownership_image = nullptr, *selection_image = nullptr;
    std::shared_ptr<const GeometryOwnershipTile> ownership;
    std::array<const MeshMapPlane *, 6> mask_planes{};
    std::array<gpu::Texture *, 6> mask_maps{};
    std::vector<GpuEffectRow> effect_rows;
    std::vector<GpuMaskStage> mask_stages;
    std::string mask_signature;
    int content_effects = 0, content_pass = 0, effect_plane = 0;
    const FillProjection *effect_fill = nullptr;
    const TilePaintPixels *effect_pixels = nullptr;
    std::string effect_key;
    std::shared_ptr<const FillSurfaceAtlas> geometry;
    int kind = 0;
    bool enabled = false, constant = false, zero_opacity = false, raw_linear_source = false;
    std::string below, group;
    std::array<float, 3> map_u{}, map_v{};
  };
  GpuMaskBuilder mask_builder(set, tile);
  const GpuComparisonResolver resolve = [&](const GpuMaskStage &stage) {
    return comparison_inputs(set, stage, target, tile);
  };
  const GpuAnchorResolver resolve_anchor = [&](const QUuid &consumer, const AnchorReference &ref) {
    return anchor_input(set, consumer, ref, tile);
  };
  const std::string query_id = query_layer ?
                                   "query/" + query_layer->toString().toStdString() +
                                       (query_source ? "/source" : "/below") +
                                       (query_effect ? "/before-selected-effect" : "") +
                                       (thumbnail ? "/thumbnail" : "") +
                                       (raw_anchor ?
                                            "/anchor/" + query_effect->toString().toStdString() :
                                            std::string{}) :
                                   std::string{};
  std::string query_head;
  bool query_enabled = false;
  if (query_layer && query_source)
    stack.queried_sources.insert(query_layer->toString().toStdString());
  std::vector<Input> inputs;
  std::vector<GpuStackNode> descriptors;
  std::set<std::string> protected_keys;
  QVarLengthArray<std::string, 17> heads;
  heads.resize(17);
  std::map<std::string, bool> constant_nodes;
  const auto append = [](std::string &data, const auto &value) {
    data.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  const auto pages = [&](GpuStackNode &node, const PaintPixels &pixels, int plane, int slot) {
    if (!pixels.resolution())
      return;
    const double sx = double(width) / pixels.resolution(),
                 sy = double(height) / pixels.resolution();
    const auto versions = pixels.page_versions();
    for (const auto &[page, revision] : versions[plane]) {
      const int px = int(page % 65536) * 64, py = int(page / 65536) * 64;
      const int x = int(std::floor(px * sx)) - 1, y = int(std::floor(py * sy)) - 1,
                right = int(std::ceil((px + 64) * sx)) + 1,
                bottom = int(std::ceil((py + 64) * sy)) + 1;
      node.pages[(std::uint64_t(slot) << 32) | page] = {x, y, right - x, bottom - y, revision};
    }
  };
  const auto &layers = evaluation_layers(set);
  auto folder_entries = folder_entry_plan(layers);
  const int first_layer = precompose_evaluation_start(layers, query_effect, -1, query_layer);
  while (!folder_entries.empty() && folder_entries.back().first < first_layer)
    folder_entries.pop_back();
  for (int layer_number = first_layer; layer_number < int(layers.size()); ++layer_number) {
    while (!folder_entries.empty() && folder_entries.back().first == layer_number) {
      const auto &folder = layers[folder_entries.back().folder];
      folder_entries.pop_back();
      if (folder.depth + 1 >= heads.size())
        heads.resize(folder.depth + 2);
      const bool raw_folder = query_layer && folder.id == *query_layer &&
                              (thumbnail || raw_anchor);
      heads[folder.depth + 1] = !raw_folder && gpu_layer_channel(folder, channel).blend ==
                                                   LayerBlend::Passthrough ?
                                    heads[folder.depth] :
                                    std::string{};
    }
    const auto &layer = layers[layer_number];
    // An actual source Normal contribution has no authored scalar H. Bump4 is
    // its AG enable/opacity control, not an opaque invented 0.5 Fill over others.
    if (channel == 4 && layer.fill.native_material &&
        layer.fill.native_material->height_policy == NativeHeightPolicy::None &&
        s.native_materials.contains(layer.fill.material_resource) &&
        s.native_materials.world_normal_perturbed(layer.fill.material_resource) &&
        layer.effects.empty() && layer.pixels.page_count(4) == 0)
      continue;
    const bool querying = query_layer && layer.id == *query_layer;
    if (querying) {
      query_enabled = raw_anchor || gpu_channel_enabled(set, channel);
      if (!query_source) {
        query_head = heads.at(layer.depth);
        break;
      }
      const auto values = gpu_layer_channel(layer, channel);
      if (!raw_anchor)
        query_enabled &= values.enabled && (thumbnail || values.blend != LayerBlend::Disabled);
      if (!query_enabled)
        break;
    }
    if (!querying && (!gpu_channel_enabled(set, channel) || !active(layer, channel, tile))) {
      if (layer.kind == LayerKind::Folder)
        heads[layer.depth + 1].clear();
      continue;
    }
    GpuStackNode node;
    node.id = layer.id.toString().toStdString();
    Input in;
    in.layer = &layer;
    in.channel = gpu_layer_channel(layer, channel);
    in.raw_linear_source = channel == 0 && raw_linear_color_source(layer);
    // Ordinary source queries share canonical pixels. A raw thumbnail of a
    // zero-opacity/Passthrough source needs different coverage/compositing; keep
    // those scratch outputs distinct from Compare's already-cached inputs.
    const bool isolated_thumbnail = querying && thumbnail &&
                                    (in.channel.opacity <= 0 ||
                                     in.channel.blend == LayerBlend::Passthrough ||
                                     in.channel.blend == LayerBlend::Disabled);
    if (isolated_thumbnail)
      node.id += "/thumbnail-raw";
    if (querying && (thumbnail || raw_anchor)) {
      in.channel.enabled = true;
      in.channel.opacity = 1;
      in.channel.blend = LayerBlend::Normal;
    }
    in.enabled = querying || active(layer, channel, tile);
    in.below = heads.at(layer.depth);
    if (!in.below.empty())
      node.dependencies.push_back(in.below);
    if (layer.kind == LayerKind::Folder) {
      in.kind = in.channel.blend == LayerBlend::Passthrough ? 11 : 3;
      in.group = heads.at(layer.depth + 1);
      heads[layer.depth + 1].clear();
      if (!in.group.empty())
        node.dependencies.push_back(in.group);
    }
    auto &parameters = node.parameters;
    append(parameters, layer.kind);
    append(parameters, layer.precompose_below);
    append(parameters, in.enabled);
    const auto source_opacity_offset = parameters.size();
    append(parameters, in.channel.opacity);
    const auto source_blend_offset = parameters.size();
    append(parameters, in.channel.blend);
    append(parameters, gpu_channel_precision(set, channel));
    const auto source_value_offset = parameters.size();
    const auto value = in.channel.value;
    append(parameters, value);
    append(parameters, in.raw_linear_source);
    if (channel == 0 && layer.quick_overlay) {
      in.kind = 8;
      const auto &overlay = *layer.quick_overlay;
      append(parameters, overlay.editing);
      append(parameters, overlay.field && overlay.field->inverted);
      if (overlay.field) {
        protected_keys.insert(paint_key(set, layer, 0, false, tile));
        const auto &pixels = overlay.field->pixels.tile(tile);
        append(parameters, pixels.resolution());
        pages(node, pixels, 2, 0);
        const int halo_x = int(std::ceil(double(width) / std::max(1, pixels.resolution()) +
                                         double(width) / 256)),
                  halo_y = int(std::ceil(double(height) / std::max(1, pixels.resolution()) +
                                         double(height) / 256));
        for (auto &[id, page] : node.pages) {
          page.x -= halo_x;
          page.y -= halo_y;
          page.width += 2 * halo_x;
          page.height += 2 * halo_y;
        }
      }
    }
    else if (layer.kind == LayerKind::Paint) {
      in.kind = 1;
      protected_keys.insert(paint_key(set, layer, channel, false, tile));
      pages(node, in.channel.pixels->tile(tile), in.channel.plane, 0);
    }
    else if (layer.kind == LayerKind::Fill) {
      if (channel == 6 && layer.fill.native_material) {
        const auto found = s.native_normals.find({set.id, layer.id, tile});
        in.kind = 12;
        in.source = found == s.native_normals.end() ? nullptr : found->second.get();
        append(parameters, reinterpret_cast<std::uintptr_t>(in.source));
        append(parameters, s.native_normal_revision);
      }
      else if (channel == 4 && layer.fill.native_material &&
               layer.fill.native_material->height_policy == NativeHeightPolicy::None &&
               s.native_materials.contains(layer.fill.material_resource) &&
               s.native_materials.world_normal_perturbed(layer.fill.material_resource))
      {
        // Keep authored scalar Paint/effects over an unavailable native scalar
        // source. A private world-vector contribution is never converted into H.
        in.kind = 1;
        protected_keys.insert(paint_key(set, layer, channel, false, tile));
        pages(node, in.channel.pixels->tile(tile), in.channel.plane, 0);
      }
      else if (layer.fill.native_material) {
        require_native_height_fill(layer.fill.native_material->height_policy,
                                   set.bump_origin,
                                   layer.fill.mode,
                                   layer.channels[3]);
        if (!s.native_materials.contains(layer.fill.material_resource))
          return GpuPreviewUpdate::Pending;
        if (!s.native_materials.prepare_images(layer.fill.material_resource)) {
          const auto error = s.native_materials.image_error(layer.fill.material_resource);
          if (error.isEmpty())
            return GpuPreviewUpdate::Pending;
          s.native_image_failure = error.toStdString();
          s.input_rejected = true;
          return GpuPreviewUpdate::Unsupported;
        }
        in.kind = 10;
        in.native_context = std::make_shared<NativeMaterialGpuContext>(
            s.native_materials.default_context(layer.fill.material_resource));
        in.native_context->tile = tile;
        const auto &dependencies = s.native_materials.dependencies(
            layer.fill.material_resource, in.channel.fill_channel, *layer.fill.native_material);
        parameters += s.native_materials.signature(
            layer.fill.material_resource, in.channel.fill_channel, *layer.fill.native_material);
        if (dependencies.surface || fill_surface_projection(layer.fill)) {
          in.geometry = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                              set.projection_geometry;
          if (!in.geometry || !in.geometry->tiles.contains(tile))
            return GpuPreviewUpdate::Pending;
          append(parameters, reinterpret_cast<std::uintptr_t>(in.geometry.get()));
        }
        if (dependencies.ao) {
          if (set.mesh_maps[3])
            if (auto found = set.mesh_maps[3]->tiles.find(tile);
                found != set.mesh_maps[3]->tiles.end() && found->second.pixels)
              in.native_ao = &found->second;
          if (!in.native_ao)
            throw std::runtime_error("Native material requires a baked AO map — Bake Mesh Maps…");
          const auto &pixels = *in.native_ao->pixels;
          const auto key = "baked:" + pixels.hash + ":" + std::to_string(pixels.resolution) + ":" +
                           std::to_string(pixels.height);
          protected_keys.insert(key);
          parameters += key;
        }
        in.native_context->projection = native_material_projection(
            layer.fill, in.geometry.get(), set);
        if (auto snapshot = s.adjustment_sources.find(set.id);
            snapshot != s.adjustment_sources.end())
        {
          const auto saved = snapshot->second.contexts.find(
              {layer.id, tile, in.channel.fill_channel});
          if (saved == snapshot->second.contexts.end())
            throw std::logic_error("Frozen native material context missing");
          auto projection = in.native_context->projection;
          *in.native_context = saved->second;
          in.native_context->projection = std::move(projection);
        }
        else if (s.native_context_provider)
          s.native_context_provider(set, layer, tile, dependencies, *in.native_context);
        else if (dependencies.object || dependencies.view)
          throw std::runtime_error("Native material object/view context is not installed");
        const auto projection_rows = in.native_context->projection->encoded();
        for (std::size_t row = 0; row < in.native_context->projection->row_count(); ++row)
          append(parameters, projection_rows[row]);
        if (dependencies.object) {
          append(parameters, in.native_context->object_to_world);
          append(parameters, in.native_context->world_to_object);
          append(parameters, in.native_context->bounds_min);
          append(parameters, in.native_context->bounds_max);
          append(parameters, in.native_context->object_color);
          append(parameters, in.native_context->object_random);
          append(parameters,
                 in.native_context->objects ? in.native_context->objects->hash() :
                                              std::uint64_t(0));
        }
        if (dependencies.view) {
          append(parameters, in.native_context->camera_position);
          append(parameters, in.native_context->camera_forward);
          append(parameters, in.native_context->perspective);
        }
      }
      else {

        const auto &reference = layer.fill.anchors.at(in.channel.fill_channel);
        if (!reference.source.isNull()) {
          auto input = resolve_anchor(layer.id, reference);
          if (!input)
            return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
          in.kind = 9;
          in.anchor = *input;
          append(parameters, reference.channel);
          append(parameters, reference.component);
          append(parameters, reference.coverage);
          int slot = 0;
          gpu_anchor_dependencies(node, *input, slot);
        }
        else {
          append(parameters, layer.fill.alpha_mask.at(in.channel.fill_channel));
          if (fill_channel_texture(layer.fill, in.channel.fill_channel)) {
            const auto *resource = tile_resource(layer.fill, in.channel.fill_channel, tile);
            in.kind = resource ? (fill_surface_projection(layer.fill) ? 5 : 2) : 4;
            if (resource) {
              const auto key = resource_key(*resource);
              protected_keys.insert(key);
              parameters += key;
            }
            else
              parameters += "missing tile";
          }
          if (in.kind != 4 &&
              (fill_spatial_projection(layer.fill) || (channel == 0 && layer.fill.atom)))
            in.kind = fill_surface_projection(layer.fill) ? 5 : 2;
          if (layer.fill.mode == 5) {
            const auto grid = fill_warp_grid(layer.fill);
            append(parameters, grid->width());
            append(parameters, grid->height());
            for (double v : grid->columns)
              append(parameters, v);
            for (double v : grid->rows)
              append(parameters, v);
            for (const auto &node : grid->nodes) {
              append(parameters, node.position);
              append(parameters, node.du);
              append(parameters, node.dv);
              append(parameters, node.duv);
            }
            append(parameters, layer.fill.warp_depth);
            append(parameters,
                   bool(layer.fill.warp_geometry && (layer.fill.warp_geometry->enabled ||
                                                     layer.fill.warp_geometry->frozen_reference)));
          }
          append(parameters, in.channel.fill_channel == 4 ? layer.fill.bump_bias : 0.f);
          append(parameters, layer.fill.atom);
          append(parameters, layer.fill.border_width);
          append(parameters, layer.fill.border_dots);
          append(parameters, layer.fill.middle_dot);
          append(parameters, layer.fill.invert);
          append(parameters, layer.fill.wrap);
          append(parameters, layer.fill.shape_crop);
          append(parameters, layer.fill.depth_culling);
          append(parameters, layer.fill.depth_hardness);
          append(parameters, layer.fill.backface_culling);
          append(parameters, layer.fill.backface_angle);
          append(parameters, layer.fill.backface_hardness);
          append(parameters, layer.fill.cylinder_angle);
          if (layer.fill.symmetry) {
            const auto &sym = *layer.fill.symmetry;
            append(parameters, sym.constraint.enabled);
            append(parameters, sym.constraint.mode);
            append(parameters, sym.constraint.axis);
            append(parameters, sym.constraint.offset);
            append(parameters, sym.constraint.center);
            append(parameters, sym.constraint.copies);
            append(parameters, sym.flip_u);
            append(parameters, sym.flip_v);
          }
          auto mapping = layer.fill;
          if (mapping.mode == 1) {
            mapping.offset = {};
            mapping.physical = false;
          }
          if (fill_spatial_projection(mapping))
            mapping.physical = false;
          mapping.native_per_uv = set.native_per_uv;
          mapping.cm_per_native = set.cm_per_native;
          const double u = fill_surface_projection(mapping) ? 0 : (tile - 1001) % 10,
                       v = fill_surface_projection(mapping) ? 0 : (tile - 1001) / 10;
          const auto a = mapping.mode == 6 ? std::array<double, 2>{0, 0} :
                                             fill_uv_map(mapping, u, v),
                     b = mapping.mode == 6 ? std::array<double, 2>{1, 0} :
                                             fill_uv_map(mapping, u + 1, v),
                     c = mapping.mode == 6 ? std::array<double, 2>{0, 1} :
                                             fill_uv_map(mapping, u, v + 1);
          in.map_u = {float(b[0] - a[0]), float(c[0] - a[0]), float(a[0])};
          in.map_v = {float(b[1] - a[1]), float(c[1] - a[1]), float(a[1])};
          append(parameters, in.map_u);
          append(parameters, in.map_v);
          append(parameters, layer.fill.mode);
          append(parameters, layer.fill.filtering);
          if (in.kind == 5) {
            in.geometry = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                                set.projection_geometry;
            const auto identity = reinterpret_cast<std::uintptr_t>(in.geometry.get());
            append(parameters, identity);
            append(parameters, layer.fill.offset3);
            append(parameters, layer.fill.rotation3);
            append(parameters, layer.fill.scale3);
            append(parameters, layer.fill.hardness);
            append(parameters, layer.fill.physical);
            append(parameters, fill_physical_size(layer.fill));
            append(parameters, set.cm_per_native);
          }
        }
      }
    }
    if (in.kind == 5 && (!in.geometry || !in.geometry->tiles.contains(tile)))
      return GpuPreviewUpdate::Pending;
    const bool staged_content =
        (querying && query_source) || stack.queried_sources.contains(node.id) ||
        (layer.kind == LayerKind::Fill && layer.fill.mode == 5) ||
        std::any_of(layer.effects.begin(), layer.effects.end(), [&](const auto &e) {
          return e.enabled &&
                 (e.kind == ContentEffectKind::Paint || e.kind == ContentEffectKind::Fill ||
                  (e.kind == ContentEffectKind::Generator &&
                   e.channel == in.channel.fill_channel) ||
                  (e.kind == ContentEffectKind::Filter && e.channel == in.channel.fill_channel &&
                   e.filter.resource >= 7));
        });
    if (staged_content) {
      auto source_node = node;
      source_node.id = node.id + "/source";
      auto source_input = in;
      source_input.content_pass = 1;
      if (in.raw_linear_source) {
        source_input.channel.value = {1, 1, 1};
        source_node.parameters.replace(
            source_value_offset,
            sizeof(source_input.channel.value),
            reinterpret_cast<const char *>(source_input.channel.value.data()),
            sizeof(source_input.channel.value));
      }
      // Authoring source pixels are independent of layer opacity and ordinary
      // blend modes. Keep the expensive Warp source across those final-blend edits.
      source_input.zero_opacity = in.channel.opacity <= 0;
      append(source_node.parameters, source_input.zero_opacity);
      source_input.channel.opacity = 1;
      source_node.parameters.replace(source_opacity_offset,
                                     sizeof(float),
                                     reinterpret_cast<const char *>(&source_input.channel.opacity),
                                     sizeof(float));
      if (in.channel.blend != LayerBlend::Passthrough) {
        source_input.channel.blend = LayerBlend::Normal;
        source_node.parameters.replace(source_blend_offset,
                                       sizeof(LayerBlend),
                                       reinterpret_cast<const char *>(&source_input.channel.blend),
                                       sizeof(LayerBlend));
      }
      if (in.channel.blend != LayerBlend::Passthrough) {
        source_input.below.clear();
        source_node.dependencies.clear();
        if (!in.group.empty())
          source_node.dependencies.push_back(in.group);
      }
      source_input.constant = (in.kind == 0 ||
                               (in.kind == 9 && GPU_texture_width(in.anchor.image) == 1 &&
                                GPU_texture_height(in.anchor.image) == 1) ||
                               ((in.kind == 3 || in.kind == 11) &&
                                (in.group.empty() || constant_nodes.at(in.group)))) &&
                              (source_input.below.empty() ||
                               constant_nodes.at(source_input.below));
      append(source_node.parameters, source_input.content_pass);
      append(source_node.parameters, source_input.constant);
      constant_nodes[source_node.id] = source_input.constant;
      std::string previous = source_node.id;
      descriptors.push_back(std::move(source_node));
      inputs.push_back(std::move(source_input));
      for (const auto &effect : layer.effects) {
        if (querying && query_effect && effect.id == *query_effect)
          break;
        if (effect.kind == ContentEffectKind::Anchor)
          continue;
        if ((!effect.enabled && effect.kind != ContentEffectKind::LinearColorTransform) ||
            (effect.kind != ContentEffectKind::Paint && effect.channel != in.channel.fill_channel))
          continue;
        GpuStackNode effect_node;
        effect_node.id = node.id + "/effect/" + effect.id.toString().toStdString();
        effect_node.dependencies = {previous};
        Input effect_input;
        effect_input.layer = &layer;
        effect_input.channel = in.channel;
        effect_input.enabled = true;
        effect_input.below = previous;
        effect_input.content_pass = effect.kind == ContentEffectKind::Paint ? 3 : 2;
        append(effect_node.parameters, effect_input.content_pass);
        append(effect_node.parameters, gpu_channel_precision(set, channel));
        if (effect.kind == ContentEffectKind::Paint) {
          effect_input.kind = 7;
          effect_input.channel.blend = effect.blending;
          effect_input.channel.opacity = effect.opacity;
          const int c = in.channel.fill_channel;
          effect_input.effect_pixels = c < 5   ? &effect.paint.pixels :
                                       c == 5  ? &effect.paint.ao :
                                       c == 14 ? &effect.paint.displacement :
                                                 &effect.paint.users.at(c - 6);
          effect_input.effect_plane = c < 5 ? c : 2;
          effect_input.effect_key = stack_key + ":" + effect_node.id;
          protected_keys.insert(effect_input.effect_key);
          pages(effect_node, effect_input.effect_pixels->tile(tile), effect_input.effect_plane, 0);
          append(effect_node.parameters, effect.opacity);
          append(effect_node.parameters, effect.blending);
        }
        else if (effect.kind == ContentEffectKind::Fill) {
          effect_input.content_pass = effect.initialize_content ? 5 : 4;
          effect_input.channel.blend = effect.blending;
          effect_input.channel.opacity = effect.opacity;
          effect_input.effect_rows = {gpu_effect_row(effect, in.channel.fill_channel)};
          effect_input.effect_rows.front()[1][3] = float(effect.source_coverage);
          effect_input.effect_rows.front()[0][1] = float(LayerBlend::Normal);
          effect_input.effect_rows.front()[0][2] = 1;
          effect_input.content_effects = 1;
          append(effect_node.parameters, effect_input.content_pass);
          append(effect_node.parameters, effect.blending);
          append(effect_node.parameters, effect.opacity);
          append(effect_node.parameters, effect_input.effect_rows.front());
          effect_input.constant = constant_nodes.at(previous);
          if (effect.bitmap_fill) {
            const auto &mapping = effect.bitmap_fill->projection;
            effect_input.effect_fill = &mapping;
            effect_input.kind = 2;
            effect_input.content_pass = effect.initialize_content ? 7 : 6;
            effect_input.constant = false;
            const auto key = resource_key(*mapping.texture);
            protected_keys.insert(key);
            effect_node.parameters += key;
            const double u = (tile - 1001) % 10, v = (tile - 1001) / 10;
            const auto a = fill_uv_map(mapping, u, v), b = fill_uv_map(mapping, u + 1, v),
                       c = fill_uv_map(mapping, u, v + 1);
            effect_input.map_u = {float(b[0] - a[0]), float(c[0] - a[0]), float(a[0])};
            effect_input.map_v = {float(b[1] - a[1]), float(c[1] - a[1]), float(a[1])};
            append(effect_node.parameters, effect_input.content_pass);
            append(effect_node.parameters, effect_input.map_u);
            append(effect_node.parameters, effect_input.map_v);
            if (effect.linear_color) {
              effect_input.channel.value = {1, 1, 1};
              LevelsEffect transform;
              transform.kind = ContentEffectKind::LinearColorTransform;
              transform.linear_color = effect.linear_color;
              effect_input.effect_rows.push_back(gpu_effect_row(transform, 0));
              effect_input.content_effects = 2;
              append(effect_node.parameters, effect_input.effect_rows.back());
            }
          }
        }
        else if (effect.kind == ContentEffectKind::Generator) {
          effect_input.content_pass = 4;
          effect_input.channel.blend = effect.blending;
          effect_input.channel.opacity = effect.opacity;
          effect_input.channel.value = content_generator_color(effect);
          append(effect_node.parameters, effect_input.channel.value);
          effect_input.effect_rows = {gpu_generator_row(effect, set.mesh_maps)};
          effect_input.content_effects = 1;
          append(effect_node.parameters, effect_input.content_pass);
          append(effect_node.parameters, effect.blending);
          append(effect_node.parameters, effect.opacity);
          append(effect_node.parameters, effect_input.effect_rows.front());
          effect_input.constant = !generator_resources_present(set.mesh_maps, effect) &&
                                  !generator_has_anchors(effect) && constant_nodes.at(previous);
          if (effect.triplanar_grunge || effect.surface_fallback) {
            effect_input.geometry = set.projection_tiles.contains(tile) ?
                                        set.projection_tiles.at(tile) :
                                        set.projection_geometry;
            effect_input.constant = false;
            append(effect_node.parameters,
                   reinterpret_cast<std::uintptr_t>(effect_input.geometry.get()));
          }
          if (generator_has_anchors(effect)) {
            auto values = gpu_generator_inputs(effect, layer.id, resolve_anchor);
            if (!values)
              return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
            effect_input.generator = effect;
            effect_input.generator_inputs = *values;
            gpu_generator_options(effect_input.effect_rows.front(), effect, *values);
            int slot = 1;
            gpu_generator_dependencies(effect_node, effect, *values, slot);
            append(effect_node.parameters, effect_input.effect_rows.front());
          }
          std::array<bool, 6> needed{};
          gpu_need_generator_maps(needed, effect);
          for (int i = 0; i < 6; ++i)
            if (needed[i])
              if (const auto &map = set.mesh_maps[gpu_mask_map_roles[i]]; map)
                if (const auto member = map->tiles.find(tile);
                    member != map->tiles.end() && member->second.pixels)
                {
                  effect_input.mask_planes[i] = &member->second;
                  const auto &pixels = *member->second.pixels;
                  const auto key = "baked:" + pixels.hash + ":" +
                                   std::to_string(pixels.resolution) + ":" +
                                   std::to_string(pixels.height);
                  protected_keys.insert(key);
                  effect_node.parameters += key;
                  effect_input.mask_signature += key;
                }
        }
        else {
          effect_input.effect_rows = {gpu_effect_row(effect, in.channel.fill_channel)};
          effect_input.content_effects = 1;
          append(effect_node.parameters, effect_input.effect_rows.front());
          if (effect.kind == ContentEffectKind::LinearColorTransform)
            append(effect_node.parameters, effect_input.channel.value);
          effect_input.constant = constant_nodes.at(previous);
        }
        append(effect_node.parameters, effect_input.constant);
        constant_nodes[effect_node.id] = effect_input.constant;
        previous = effect_node.id;
        descriptors.push_back(std::move(effect_node));
        inputs.push_back(std::move(effect_input));
      }
      if (querying) {
        query_head = previous;
        break;
      }
      in.kind = 6;
      in.group = previous;
      node.dependencies.clear();
      if (!in.below.empty())
        node.dependencies.push_back(in.below);
      node.dependencies.push_back(previous);
      node.pages.clear();
    }
    if (!staged_content)
      in.effect_rows = gpu_content_effects(layer, in.channel.fill_channel);
    in.content_effects = int(in.effect_rows.size());
    in.mask_stages = mask_builder.build(layer);
    if (in.mask_stages.empty())
      gpu_append_mask_effects(in.effect_rows, layer, set, tile);
    append(parameters, in.content_effects);
    for (const auto &row : in.effect_rows)
      append(parameters, row);
    const bool masked = layer.mask && layer.mask->enabled;
    append(parameters, masked);
    if (masked) {
      append(parameters, layer.mask->background);
      protected_keys.insert(paint_key(set, layer, 2, true, tile));
      pages(node, layer.mask->pixels.tile(tile), 2, 1);
      bool comparisons_ready = true;
      int slot = 2;
      for (const auto &stage : in.mask_stages)
        gpu_visit_mask_stage(stage, [&](const auto &part) {
          parameters += part.owner_key + part.id + part.anchor_key + part.anchor_id;
          if (part.bitmap)
            parameters += part.bitmap->asset + part.bitmap->hash;
          append(parameters, part.kind);
          append(parameters, part.background);
          append(parameters, part.opacity);
          append(parameters, part.blend);
          if (part.kind == GpuMaskStage::AnchorFill) {
            append(parameters, part.reference.channel);
            append(parameters, part.reference.component);
            append(parameters, part.reference.coverage);
          }
          if (part.generator) {
            auto values = gpu_generator_inputs(
                *part.generator, part.comparison_layer, resolve_anchor);
            if (!values)
              comparisons_ready = false;
            else
              gpu_generator_dependencies(node, *part.generator, *values, slot);
          }
          if (part.kind == GpuMaskStage::Compare) {
            auto values = resolve(part);
            if (!values)
              comparisons_ready = false;
            else
              gpu_comparison_dependencies(node, *values, slot);
          }
          if (part.kind == GpuMaskStage::AnchorFill && part.content_anchor) {
            append(parameters, part.reference.channel);
            append(parameters, part.reference.component);
            append(parameters, part.reference.coverage);
            auto value = resolve_anchor(part.comparison_layer, part.reference);
            if (!value)
              comparisons_ready = false;
            else
              gpu_anchor_dependencies(node, *value, slot);
          }
          for (const auto &row : part.rows)
            append(parameters, row);
          if (part.spatial) {
            append(parameters, part.spatial->values);
            append(parameters, reinterpret_cast<std::uintptr_t>(part.geometry.get()));
          }
          if (part.pixels)
            pages(node, part.pixels->tile(tile), 2, slot++);
        });
      if (!comparisons_ready)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
      for (const auto &stage : in.mask_stages)
        gpu_visit_mask_stage(
            stage,
            [&](const auto &part) {
              if (part.pixels)
                protected_keys.insert(part.owner_key + ":" + part.id);
            },
            true);
    }
    if (masked) {
      std::array<bool, 6> needed{};
      for (const auto &effect : layer.mask->effects)
        if (effect.enabled) {
          needed[0] |= effect.kind == MaskEffectKind::ColorSelection && effect.use_id_map;
          if (effect.kind == MaskEffectKind::Generator)
            gpu_need_generator_maps(needed, effect);
        }
      for (const auto &stage : in.mask_stages)
        gpu_visit_mask_stage(
            stage,
            [&](const auto &part) {
              for (const auto &row : part.rows) {
                needed[0] |= row[0][0] == 4 && row[1][0] != 0;
                if (row[0][0] == 3)
                  gpu_need_generator_row_maps(needed, row);
              }
            },
            true);
      for (int i = 0; i < 6; ++i)
        if (needed[i]) {
          const auto &map = set.mesh_maps[gpu_mask_map_roles[i]];
          if (map)
            if (auto found = map->tiles.find(tile);
                found != map->tiles.end() && found->second.pixels)
            {
              in.mask_planes[i] = &found->second;
              const auto &pixels = *found->second.pixels;
              const auto key = "baked:" + pixels.hash + ":" + std::to_string(pixels.resolution) +
                               ":" + std::to_string(pixels.height);
              protected_keys.insert(key);
              parameters += key;
              in.mask_signature += key;
            }
        }
    }
    if (layer.geometry.type == 0 && !layer.geometry.meshes.empty()) {
      if (auto found = set.geometry_ownership.find(tile); found != set.geometry_ownership.end())
        in.ownership = found->second;
      if (!in.ownership || in.ownership->width != width || in.ownership->height != height)
        return GpuPreviewUpdate::Pending;
      append(parameters, reinterpret_cast<std::uintptr_t>(in.ownership.get()));
      append(parameters, layer.geometry.default_include);
      for (const auto &[id, name] : set.geometry_names) {
        append(parameters, id);
        append(parameters, layer.geometry.includes(name));
      }
    }
    // Uniform prefixes are represented by one GPU texel, never by a CPU blend.
    // An opaque normal/replacement fill has no dependency on hidden lower work.
    const bool unmasked = (!layer.mask || !layer.mask->enabled) &&
                          (layer.geometry.type == 1 || layer.geometry.meshes.empty());
    const bool uniform_source = in.kind == 0 ||
                                (in.kind == 9 && GPU_texture_width(in.anchor.image) == 1 &&
                                 GPU_texture_height(in.anchor.image) == 1) ||
                                ((in.kind == 3 || in.kind == 6 || in.kind == 11) &&
                                 (in.group.empty() || constant_nodes.at(in.group)));
    if (unmasked && uniform_source) {
      // A source query (for example a thumbnail) can stage a uniform fill as
      // kind 6. Its RGB effects preserve full coverage; keep the child dependency
      // while pruning the hidden backdrop. Fractional folders are not opaque fills.
      if (in.enabled && (in.kind == 0 || (in.kind == 6 && layer.kind == LayerKind::Fill)) &&
          (in.channel.blend == LayerBlend::Replace ||
           (in.channel.blend == LayerBlend::Normal && in.channel.opacity == 1)))
      {
        in.below.clear();
        node.dependencies.clear();
        if (!in.group.empty())
          node.dependencies.push_back(in.group);
      }
      in.constant = in.below.empty() || constant_nodes.at(in.below);
    }
    append(parameters, in.constant);
    constant_nodes[node.id] = in.constant;
    heads[layer.depth] = node.id;
    descriptors.push_back(std::move(node));
    inputs.push_back(std::move(in));
  }
  if (query_layer && query_layer->isNull()) {
    query_head = heads[0];
    query_enabled = gpu_channel_enabled(set, channel);
  }
  GpuStackNode final_node;
  final_node.id = query_layer ? query_id : "output";
  const auto output_head = query_layer ? query_head : heads[0];
  if (!output_head.empty())
    final_node.dependencies.push_back(output_head);
  append(final_node.parameters, query_enabled);
  const auto base = gpu_channel_base(set, channel);
  append(final_node.parameters, base);
  append(final_node.parameters, gpu_channel_precision(set, channel));
  append(final_node.parameters, reinterpret_cast<std::uintptr_t>(margin.get()));
  descriptors.push_back(std::move(final_node));
  // Inactive folders disconnect their children. Do not allocate/evaluate a
  // prefix that cannot contribute to the visible channel output.
  std::vector<std::string> logical_order;
  for (const auto &node : descriptors)
    logical_order.push_back(node.id);
  std::set<std::string> required{query_layer ? query_id : "output"};
  for (auto node = descriptors.rbegin(); node != descriptors.rend(); ++node)
    if (required.contains(node->id))
      required.insert(node->dependencies.begin(), node->dependencies.end());
  for (std::size_t i = inputs.size(); i-- > 0;)
    if (!required.contains(descriptors[i].id)) {
      inputs.erase(inputs.begin() + i);
      descriptors.erase(descriptors.begin() + i);
    }
  for (std::size_t i = 0; i < inputs.size(); ++i)
    stack.node_owners.try_emplace(descriptors[i].id, inputs[i].layer->id);
  auto transaction = stack.plan.prepare(width, height, descriptors);
  std::set<std::string> live;
  for (const auto &node : descriptors)
    live.insert(node.id);
  if (query_layer)
    stack.queries[query_id] = {query_head, query_enabled, *query_layer, live};
  else {
    std::set<QUuid> owners;
    for (const auto &layer : evaluation_layers(set))
      if (layer.mask)
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          if (effect.enabled && effect.kind == MaskEffectKind::Compare &&
              (effect.left < 2 || effect.right < 2))
            owners.insert(layer.id);
    std::erase_if(stack.queries, [&](const auto &item) {
      return item.first.ends_with("/thumbnail") || !owners.contains(item.second.owner);
    });
  }
  for (const auto &[id, query] : stack.queries)
    live.insert(query.nodes.begin(), query.nodes.end());
  // Keep auxiliary Compare/Anchor query closures: they are sampled inputs,
  // not edges in this compositor transaction. The snapshot seed already owns
  // only below-layer nodes, including when an auxiliary query defers early.
  if (query_layer && !query_source && !raw_anchor) {
    if (auto frozen = s.adjustment_sources.find(set.id);
        frozen != s.adjustment_sources.end() && frozen->second.layer == *query_layer)
    {
      std::erase_if(stack.outputs, [&](const auto &item) { return !live.contains(item.first); });
      std::erase_if(stack.node_versions,
                    [&](const auto &item) { return !live.contains(item.first); });
      std::erase_if(stack.node_owners,
                    [&](const auto &item) { return !live.contains(item.first); });
      stack.plan.retain(live);
      stack.head = nullptr;
    }
  }
  if (!query_layer) {
    // Occlusion is not invalidation. Keep already computed lower/folder prefixes
    // so the first opacity/visibility edit can reuse them. Never evaluate hidden
    // work just to populate this cache. Prefer the nearest complete prefix and
    // admit inactive storage against current free VRAM, not a fixed byte cap.
    const auto budget = source_cache_budget(0);
    std::uint64_t retained_bytes = 0, retained_nodes = 0;
    for (auto id = logical_order.rbegin(); id != logical_order.rend(); ++id) {
      if (live.contains(*id))
        continue;
      auto found = stack.outputs.find(*id);
      if (found == stack.outputs.end())
        continue;
      const auto bytes = std::uint64_t(GPU_texture_width(found->second.get())) *
                         GPU_texture_height(found->second.get()) * 16;
      if (bytes <= budget - retained_bytes) {
        live.insert(*id);
        retained_bytes += bytes;
        ++retained_nodes;
      }
    }
    if (performance::enabled() && retained_nodes)
      std::printf("BTS_GPU_PREFIX_RETAIN nodes=%llu bytes=%llu budget=%llu\n",
                  (unsigned long long)retained_nodes,
                  (unsigned long long)retained_bytes,
                  (unsigned long long)budget);
    std::erase_if(stack.outputs, [&](const auto &item) { return !live.contains(item.first); });
    // Upload can defer below. Retire revision records now, together with their
    // textures, so an intervening source query cannot reuse a freed output.
    stack.plan.retain(live);
  }
  // Resolve/uploads before committing any new revision. A partially uploaded
  // resource leaves the previous full-resolution result untouched.
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    auto &in = inputs[index];
    const auto &layer = *in.layer;
    if (transaction.work[index].regions.empty())
      continue;
    in.ownership_image = s.geometry_masks.empty();
    in.selection_image = s.geometry_masks.empty();
    if (in.ownership) {
      in.ownership_image = s.geometry_masks.atlas(
          in.ownership,
          s.remaining,
          testing_stack ? performance::Clock::time_point::max() :
                          s.upload_start + std::chrono::milliseconds(4));
      if (!in.ownership_image)
        return GpuPreviewUpdate::Pending;
      in.selection_image = s.geometry_masks.selection(
          stack_key + ":" + descriptors[index].id, set.geometry_names, layer.geometry);
    }

    for (int i = 0; i < 6; ++i) {
      in.mask_maps[i] = s.baked(in.mask_planes[i], protected_keys);
      if (!in.mask_maps[i])
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    if (in.generator)
      gpu_generator_maps(in.mask_maps, *in.generator, in.generator_inputs);
    if (in.kind != 12)
      in.source = s.zero.get();
    in.mask = s.zero.get();
    in.surface = s.surface({}, tile);
    in.effects = s.effects(stack_key + ":" + descriptors[index].id, in.effect_rows);
    if (!in.effects)
      return GpuPreviewUpdate::Unsupported;
    if (in.enabled && in.kind == 10) {
      if (in.geometry) {
        in.native_context->surface = s.surface(in.geometry, tile);
        if (!in.native_context->surface)
          return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
        in.native_context->surface_size = {in.geometry->resolution,
                                           in.geometry->height ? in.geometry->height :
                                                                 in.geometry->resolution};
        in.native_context->surface_stride = sizeof(FillSurfacePoint) / 4;
      }
      if (in.native_ao) {
        in.native_context->ambient_occlusion = s.baked(in.native_ao, protected_keys);
        if (!in.native_context->ambient_occlusion)
          return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
      }
    }
    else if (in.enabled && in.kind == 9)
      in.source = in.anchor.image;
    else if (in.enabled && in.kind == 8 && layer.quick_overlay->field)
      in.source = s.paint(paint_key(set, layer, 0, false, tile),
                          layer.quick_overlay->field->pixels.tile(tile),
                          2,
                          protected_keys);
    else if (in.enabled && in.kind == 7)
      in.source = s.paint(
          in.effect_key, in.effect_pixels->tile(tile), in.effect_plane, protected_keys);
    else if (in.enabled && in.kind == 1)
      in.source = s.paint(paint_key(set, layer, channel, false, tile),
                          in.channel.pixels->tile(tile),
                          in.channel.plane,
                          protected_keys);
    else if (in.enabled && in.effect_fill)
      in.source = s.resource(*in.effect_fill->texture, protected_keys);
    else if (in.enabled && (in.kind == 2 || in.kind == 5) &&
             fill_channel_texture(layer.fill, in.channel.fill_channel))
      in.source = s.resource(*tile_resource(layer.fill, in.channel.fill_channel, tile),
                             protected_keys);
    if (in.enabled && (in.kind == 5 || (in.content_pass == 4 && in.geometry))) {
      in.surface = s.surface(in.geometry, tile);
      if (!in.surface)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    if (in.kind == 12 && !in.source) {
      if (const auto error = s.native_normal_errors.find(set.id);
          error != s.native_normal_errors.end())
      {
        s.native_image_failure = error->second;
        s.input_rejected = true;
        return GpuPreviewUpdate::Unsupported;
      }
      return GpuPreviewUpdate::Pending;
    }
    if (!in.source)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    if (!in.content_pass && in.enabled && layer.mask && layer.mask->enabled) {
      in.mask = in.mask_stages.empty() ? s.paint(paint_key(set, layer, 2, true, tile),
                                                 layer.mask->pixels.tile(tile),
                                                 2,
                                                 protected_keys) :
                                         s.mask_prefix(paint_key(set, layer, 2, true, tile),
                                                       in.mask_stages,
                                                       in.mask_maps,
                                                       tile,
                                                       width,
                                                       height,
                                                       protected_keys,
                                                       in.mask_signature,
                                                       resolve,
                                                       resolve_anchor);
      if (!in.mask)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
  }
  s.last_dispatch_texels = transaction.texels;
  s.last_cached_nodes = 0;
  s.last_active_nodes = inputs.size();
  const auto region_uniforms = [](gpu::Shader *shader, const GpuStackRegion &r) {
    const int origin[2] = {r.x, r.y}, size[2] = {r.width, r.height};
    GPU_shader_uniform_2iv(shader, "region_origin", origin);
    GPU_shader_uniform_2iv(shader, "region_size", size);
  };
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    const auto &work = transaction.work[index];
    if (work.regions.empty()) {
      ++s.last_cached_nodes;
      continue;
    }
    const auto &in = inputs[index];
    const auto &layer = *in.layer;
    const int output_width = in.constant ? 1 : width, output_height = in.constant ? 1 : height;
    if (!stack.outputs.contains(work.id) ||
        GPU_texture_width(stack.outputs.at(work.id).get()) != output_width ||
        GPU_texture_height(stack.outputs.at(work.id).get()) != output_height)
    {
      auto output = texture("BTS GPU cached layer prefix",
                            output_width,
                            output_height,
                            gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!output)
        return GpuPreviewUpdate::Unsupported;
      stack.outputs.insert_or_assign(work.id, std::move(output));
    }
    // A stroke snapshot can share an unchanged cached prefix. Detach on the GPU
    // before writing; never let later lower-layer edits mutate its read source.
    if (stack.outputs.at(work.id).use_count() > 1) {
      auto replacement = texture("BTS COW frozen prefix",
                                 output_width,
                                 output_height,
                                 gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!replacement)
        return GpuPreviewUpdate::Unsupported;
      GPU_texture_copy(replacement.get(), stack.outputs.at(work.id).get());
      ++s.frozen_prefix_copies;
      s.frozen_prefix_copy_texels += std::uint64_t(output_width) * output_height;
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                         GPU_BARRIER_TEXTURE_UPDATE);
      stack.outputs.at(work.id) = std::move(replacement);
    }
    auto *output = stack.outputs.at(work.id).get();
    auto *backdrop = in.below.empty() ? s.zero.get() : stack.outputs.at(in.below).get();
    auto *source = (in.kind == 3 || in.kind == 6 || in.kind == 11) ?
                       (in.group.empty() ? s.zero.get() : stack.outputs.at(in.group).get()) :
                       in.source;
    if (in.kind == 10) {
      if (!s.native_source || GPU_texture_width(s.native_source.get()) != width ||
          GPU_texture_height(s.native_source.get()) != height)
      {
        s.native_source = texture("BTS shared native source scratch",
                                  width,
                                  height,
                                  gpu::TextureFormat::SFLOAT_32_32_32_32);
        if (!s.native_source)
          return GpuPreviewUpdate::Unsupported;
      }
      performance::Scope timing("gpu_native_material_source");
      try {
        for (const auto &region : work.regions)
          s.native_materials.render(layer.fill.material_resource,
                                    in.channel.fill_channel,
                                    *layer.fill.native_material,
                                    s.native_source.get(),
                                    *in.native_context,
                                    {region.x, region.y, region.width, region.height});
      }
      catch (const NativeFragmentImagePending &) {
        return GpuPreviewUpdate::Pending;
      }
      catch (const std::exception &error) {
        s.native_image_failure = error.what();
        s.input_rejected = true;
        return GpuPreviewUpdate::Unsupported;
      }
      source = s.native_source.get();
    }
    GpuLayerParameters parameters;
    auto *shader = s.layer_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_bind(source, 0);
    GPU_texture_bind(in.mask, 1);
    GPU_texture_bind(backdrop, 2);
    GPU_texture_bind(in.surface, 3);
    GPU_texture_bind(in.effects, 4);
    for (int i = 0; i < 6; ++i)
      GPU_texture_bind(in.mask_maps[i], gpu_mask_map_slot(i));
    GPU_texture_bind(in.ownership_image, 9);
    GPU_texture_bind(in.selection_image, 10);
    GPU_texture_image_bind(output, 0);
    parameters.set("precompose_source", layer.precompose_below);
    parameters.set("raw_linear_source", in.raw_linear_source);
    parameters.set("source_zero_opacity", in.zero_opacity);
    parameters.set("content_pass", in.content_pass);
    parameters.set("geometry_masked", bool(in.ownership));
    parameters.set("content_effects", in.content_effects);
    parameters.set("effect_count", int(in.effect_rows.size()));
    if (in.kind == 9) {
      const auto &ref = layer.fill.anchors.at(in.channel.fill_channel);
      const float options[4] = {float(ref.component),
                                float(ref.coverage),
                                float(in.anchor.mask),
                                float(in.anchor.enabled)};
      parameters.set4("anchor_options", options);
    }
    parameters.set("quick_editing", layer.quick_overlay && layer.quick_overlay->editing);
    parameters.set("quick_inverted",
                   layer.quick_overlay && layer.quick_overlay->field &&
                       layer.quick_overlay->field->inverted);
    parameters.set("fill_bump_bias", in.channel.fill_channel == 4 ? layer.fill.bump_bias : 0);
    parameters.set("source_ignore_alpha",
                   in.effect_fill || !layer.fill.alpha_mask.at(in.channel.fill_channel));
    parameters.set("source_wrap", !in.effect_fill && layer.fill.mode == 6);
    parameters.set("source_filter",
                   in.effect_fill       ? 0 :
                   layer.fill.mode >= 2 ? layer.fill.filtering :
                                          0);
    parameters.set("fill_resource_kind",
                   in.effect_fill || fill_channel_texture(layer.fill, in.channel.fill_channel) ?
                       1 :
                   channel == 0 && layer.fill.atom ? 2 :
                                                     0);
    parameters.set("projection_mode", layer.fill.mode);
    parameters.set("projection_wrap", layer.fill.wrap);
    const float atom_options[4] = {float(layer.fill.border_width),
                                   float(layer.fill.middle_dot),
                                   float(layer.fill.border_dots),
                                   float(layer.fill.invert)};
    parameters.set4("atom_options", atom_options);
    parameters.set("layer_active", in.enabled);
    parameters.set("source_kind", in.kind);
    parameters.set("scalar_channel",
                   channel == 6                                   ? 3 :
                   channel == 0                                   ? 0 :
                   (channel == 1 || channel == 2 || channel == 5) ? 1 :
                                                                    2);
    parameters.set("value_precision",
                   int(layer.precompose_below ? ChannelPrecision::RGB32F :
                                                gpu_channel_precision(set, channel)));
    parameters.set("blend_mode", int(in.channel.blend));
    parameters.set("opacity_value", in.channel.opacity);
    const auto value = in.channel.value;
    parameters.set3("source_value", value.data());
    parameters.set("has_mask",
                   !in.content_pass &&
                       (!in.mask_stages.empty() ||
                        (layer.mask && layer.mask->enabled && !layer.mask->pixels.empty())));
    parameters.set("mask_background",
                   in.content_pass                   ? 1 :
                   !in.mask_stages.empty()           ? 0 :
                   layer.mask && layer.mask->enabled ? layer.mask->background :
                                                       1);
    parameters.set3("map_u", in.map_u.data());
    parameters.set3("map_v", in.map_v.data());
    gpu_spatial_parameters(
        parameters, layer.fill, in.kind == 5 ? in.geometry.get() : nullptr, set);
    if (in.content_pass == 4 && in.geometry)
      gpu_generator_surface_parameters(parameters, in.geometry.get());
    auto *parameter_image = s.parameters(stack_key + ":" + work.id, parameters);
    if (!parameter_image)
      return GpuPreviewUpdate::Unsupported;
    GPU_texture_bind(parameter_image, 5);
    if (in.constant) {
      region_uniforms(shader, {0, 0, 1, 1});
      GPU_compute_dispatch(shader, 1, 1, 1);
    }
    else
      for (const auto &r : work.regions) {
        region_uniforms(shader, r);
        GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
      }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
    GPU_texture_image_unbind(output);
  }
  // The versions belong to GPU output chunks, not to CPU source pixels.
  for (const auto &work : transaction.work)
    if (!work.regions.empty()) {
      const auto version = ++stack.node_clock;
      for (const auto &r : work.regions) {
        const auto chunk = std::uint64_t(r.y / 256) * ((width + 255) / 256) + r.x / 256;
        stack.node_versions[work.id][chunk] = {r.x, r.y, r.width, r.height, version};
      }
    }
  if (query_layer) {
    GPU_texture_unbind_all();
    GPU_texture_image_unbind_all();
    GPU_shader_unbind();
    stack.plan.commit(std::move(transaction), true);
    return GpuPreviewUpdate::Updated;
  }
  auto final_work = transaction.work.back();
  const int chunk_columns = (width + 255) / 256;
  if (!final_work.regions.empty()) {
    ++stack.revision;
    stack.last_changes.clear();
    for (const auto &r : final_work.regions)
      stack.last_changes.insert((r.y / 256) * chunk_columns + r.x / 256);
  }
  for (const auto &r : final_work.regions) {
    const int index = (r.y / 256) * chunk_columns + r.x / 256;
    stack.output_dirty.insert(index);
    stack.height_dirty.insert(index);
  }
  stack.head = heads[0].empty() ? s.zero.get() : stack.outputs.at(heads[0]).get();
  if ((channel == 5 || channel == 6) && !final_work.regions.empty()) {
    if (!s.coverage_shader)
      s.coverage_shader = Shader(gpu_derived::coverage_shader(), GPU_shader_free);
    if (!stack.coverage)
      stack.coverage = texture("BTS cached AO coverage",
                               (width + 15) / 16,
                               (height + 15) / 16,
                               gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!s.coverage_shader || !stack.coverage)
      return GpuPreviewUpdate::Unsupported;
    auto *shader = s.coverage_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_bind(stack.head, 0);
    GPU_texture_image_bind(stack.coverage.get(), 0);
    const int size[2] = {width, height};
    GPU_shader_uniform_2iv(shader, "size", size);
    for (const auto &r : final_work.regions) {
      const int origin[2] = {r.x, r.y};
      GPU_shader_uniform_2iv(shader, "origin", origin);
      GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(stack.coverage.get());
  }
  if (channel == 4 && margin) {
    const auto float_format = gpu::TextureFormat::SFLOAT_32_32_32_32;
    if (!stack.height_image) {
      stack.height_image = texture("BTS padded bump for normals", width, height, float_format);
      if (!stack.height_image)
        return GpuPreviewUpdate::Unsupported;
      for (int i = 0; i < chunk_columns * ((height + 255) / 256); ++i)
        stack.height_dirty.insert(i);
    }
    if (!stack.height_dirty.empty()) {
      if (const auto state = s.initialize(width, height, float_format, false);
          state != GpuPreviewUpdate::Updated)
        return state;
      auto *shader = s.final_shader;
      GPU_shader_bind(shader);
      GPU_texture_bind(stack.head, 0);
      GPU_texture_image_bind(stack.height_image.get(), 0);
      const int size[2] = {width, height}, offset[3] = {};
      GPU_shader_uniform_2iv(shader, "tile_size", size);
      GPU_shader_uniform_3iv(shader, "target_offset", offset);
      GPU_shader_uniform_3fv(shader, "base_value", base.data());
      GPU_shader_uniform_1i(shader, "value_precision", int(gpu_channel_precision(set, channel)));
      GPU_shader_uniform_3fv(shader, "output_bias", std::array<float, 3>{}.data());
      GPU_shader_uniform_1i(shader, "output_kind", 0);
      GPU_shader_uniform_1i(shader, "finite_half", 0);
      std::vector<std::uint32_t> dirty(chunk_columns * ((height + 255) / 256));
      for (auto chunk : stack.height_dirty) {
        dirty[chunk] = 1;
        const int x = chunk % chunk_columns * 256, y = chunk / chunk_columns * 256;
        const GpuStackRegion r{x, y, std::min(256, width - x), std::min(256, height - y)};
        region_uniforms(shader, r);
        GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
      }
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
      GPU_texture_image_unbind_all();
      GPU_texture_unbind_all();
      GPU_shader_unbind();
      if (!s.margins.apply(stack_key + ":height", margin, stack.height_image.get(), {}, dirty))
        return GpuPreviewUpdate::Unsupported;
      stack.height_dirty.clear();
      if (const auto state = s.initialize(width, height, format, GPU_texture_is_array(target));
          state != GpuPreviewUpdate::Updated)
        return state;
    }
  }
  if (!prefix_only) {
    final_work.regions.clear();
    if (target_changed)
      for (int i = 0; i < chunk_columns * ((height + 255) / 256); ++i)
        stack.output_dirty.insert(i);
    for (auto chunk : stack.output_dirty) {
      const int x = chunk % chunk_columns * 256, y = chunk / chunk_columns * 256;
      final_work.regions.push_back({x, y, std::min(256, width - x), std::min(256, height - y)});
    }
  }
  if (!prefix_only && (channel != 5 || authored_ao_output) && !final_work.regions.empty()) {
    auto *shader = target_final_shader;
    GPU_shader_bind(shader);
    GPU_texture_bind(heads[0].empty() ? s.zero.get() : stack.outputs.at(heads[0]).get(), 0);
    GPU_texture_image_bind(target, 0);
    const int logical_size[2] = {width, height};
    GPU_shader_uniform_2iv(shader, "tile_size", logical_size);
    GPU_shader_uniform_3iv(shader, "target_offset", target_offset.data());
    GPU_shader_uniform_3fv(shader, "base_value", base.data());
    GPU_shader_uniform_1i(shader, "value_precision", int(gpu_channel_precision(set, channel)));
    const float bump_bias = channel == 4 ? set.bump_offset() : 0;
    GPU_shader_uniform_3fv(
        shader, "output_bias", std::array<float, 3>{bump_bias, bump_bias, bump_bias}.data());
    GPU_shader_uniform_1i(shader,
                          "output_kind",
                          channel == 6                ? 2 :
                          gpu_scalar_channel(channel) ? 1 :
                                                        0);
    GPU_shader_uniform_1i(shader, "finite_half", format == gpu::TextureFormat::SFLOAT_16_16_16_16);
    for (const auto &r : final_work.regions) {
      region_uniforms(shader, r);
      GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(target);
    if (margin) {
      std::vector<std::uint32_t> dirty(chunk_columns * ((height + 255) / 256));
      for (auto chunk : stack.output_dirty)
        dirty[chunk] = 1;
      if (!s.margins.apply(
              stack_key + ":output:" + std::to_string(reinterpret_cast<std::uintptr_t>(target)),
              margin,
              target,
              target_offset,
              dirty))
        return GpuPreviewUpdate::Unsupported;
    }
    stack.output_dirty.clear();
    s.image_changed = true;
  }
  GPU_texture_unbind_all();
  GPU_texture_image_unbind_all();
  GPU_shader_unbind();
  stack.plan.commit(std::move(transaction), true);
  stack.plan.retain(live);
  std::erase_if(stack.node_versions,
                [&](const auto &entry) { return !live.contains(entry.first); });
  if (!prefix_only)
    s.target_owners[target_key] = stack_key;
  if (!benchmark_stack && !prefix_only && channel != 5 && !margin &&
      std::getenv("BTS_VERIFY_GPU_STACK") &&
      !std::any_of(evaluation_layers(set).begin(),
                   evaluation_layers(set).end(),
                   [](const auto &layer) { return bool(layer.fill.native_material); }))
  {
    std::string signature = set.id.toString().toStdString() + std::to_string(channel) + ":" +
                            std::to_string(tile);
    for (const auto &layer : evaluation_layers(set))
      if (active(layer, channel, tile)) {
        signature += std::to_string(int(layer.kind)) + ":" +
                     std::to_string(int(gpu_layer_channel(layer, channel).blend)) + ":" +
                     std::to_string(gpu_layer_channel(layer, channel).opacity < 1) + ":" +
                     std::to_string(bool(layer.mask)) + ":" +
                     std::to_string(layer.mask && !layer.mask->pixels.empty()) + ":" +
                     std::to_string(layer.fill.scale != std::array<double, 2>{1, 1}) + ":" +
                     std::to_string(layer.fill.rotation != 0) + ":" +
                     std::to_string(layer.fill.offset != std::array<double, 2>{});
      }
    if (std::any_of(
            evaluation_layers(set).begin(), evaluation_layers(set).end(), [](const auto &layer) {
              return layer.fill.mode == 5 ||
                     (layer.mask && any_mask_effect(layer.mask->effects, [](const auto &effect) {
                        return effect.enabled && effect.kind == MaskEffectKind::Fill &&
                               effect.mapping.projection.mode == 5;
                      }));
            }))
      signature += ":warp:" + std::to_string(stack.revision);
    static std::set<std::string> verified;
    const bool neighborhood =
        spatial_oracle_unavailable(set) ||
        std::any_of(
            evaluation_layers(set).begin(), evaluation_layers(set).end(), [&](const auto &layer) {
              const bool content = std::any_of(
                  layer.effects.begin(), layer.effects.end(), [&](const auto &effect) {
                    return effect.enabled && effect.kind == ContentEffectKind::Filter &&
                           effect.filter.resource >= 7 &&
                           effect.channel == gpu_layer_channel(layer, channel).fill_channel;
                  });
              const bool mask = layer.mask &&
                                std::any_of(layer.mask->effects.begin(),
                                            layer.mask->effects.end(),
                                            [](const auto &effect) {
                                              return effect.enabled &&
                                                     effect.kind == MaskEffectKind::Filter &&
                                                     effect.filter.resource >= 7;
                                            });
              return content || mask;
            });
    if (neighborhood)
      std::printf("BTS_GPU_STACK_NEIGHBORHOOD skipped_cpu_oracle=1\n");
    else if (testing_stack || verified.insert(signature).second) {
      auto *pixels = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
      double error = 0;
      bool valid = true;
      std::array<bool, 6> wanted{};
      if (channel < 6)
        wanted[channel] = true;
      else if (channel == 6)
        wanted[3] = true;
      for (int gy = 0; gy < 8; ++gy)
        for (int gx = 0; gx < 8; ++gx) {
          const int x = std::min(s.width - 1, int((gx + .5) * s.width / 8)),
                    y = std::min(s.height - 1, int((gy + .5) * s.height / 8));
          std::array<float, 4> expected{};
          if (channel == 6) {
            const auto result = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
            const auto full = precision_channel(result.material, 3, set.precision[3]);
            expected = {full[0], full[1], full[2], 1};
          }
          else if (gpu_scalar_channel(channel)) {
            const auto value = channel == 16 ?
                                   TextureDocument::composite_displacement(set, x, y, tile) :
                                   TextureDocument::composite_user(set, channel - 7, x, y, tile);
            expected = {value[0] * value[1], value[0] * value[1], value[0] * value[1], value[1]};
          }
          else {
            const auto result = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
            const auto full = precision_channel(
                result.material, channel, gpu_channel_precision(set, channel));
            const float a = result.coverage[channel];
            expected = {full[0] - base[0] * (1 - a),
                        full[1] - base[1] * (1 - a),
                        full[2] - base[2] * (1 - a),
                        a};
          }
          for (int k = 0; k < 4; ++k) {
            if (format == gpu::TextureFormat::SFLOAT_16_16_16_16)
              expected[k] = math::half_to_float(math::float_to_half_make_finite(expected[k]));
            const float actual =
                pixels[((std::size_t(target_offset[2]) * GPU_texture_height(target) + y +
                         target_offset[1]) *
                            GPU_texture_width(target) +
                        x + target_offset[0]) *
                           4 +
                       k];
            const double difference = std::abs(double(actual) - expected[k]);
            const auto precision = gpu_channel_precision(set, channel);
            double tolerance = (format == gpu::TextureFormat::SFLOAT_32_32_32_32 ? 1e-6 : .001) *
                               std::max(1.f, std::abs(expected[k]));
            if (k < 3 &&
                (precision == ChannelPrecision::SRGB8 || precision == ChannelPrecision::RGB8 ||
                 precision == ChannelPrecision::L8))
              tolerance += precision == ChannelPrecision::SRGB8 ? .009 : 1. / 255;
            if (valid && difference > tolerance && channel == 4) {
              const auto cpu = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
              auto *input = heads[0].empty() ? s.zero.get() : stack.outputs.at(heads[0]).get();
              const std::array<std::array<int, 3>, 1> point{
                  {{std::min(x, GPU_texture_width(input) - 1),
                    std::min(y, GPU_texture_height(input) - 1),
                    0}}};
              const auto raw = probe_gpu_texture(input, point).front();
              std::fprintf(
                  stderr,
                  "BTS_GPU_BUMP_FAILURE_RAW cpu=%.12g cpu_coverage=%.12g gpu_prefix=%.12g,%.12g,%.12g,%.12g base=%.12g bias=%.12g precision=%d target_format=%d\n",
                  cpu.material.bump,
                  cpu.coverage[4],
                  raw[0],
                  raw[1],
                  raw[2],
                  raw[3],
                  base[0],
                  set.bump_offset(),
                  int(precision),
                  int(format));
            }
            if (valid && difference > tolerance)
              s.oracle_position = {x, y};
            if (valid && difference > tolerance)
              std::fprintf(
                  stderr,
                  "BTS_GPU_STACK_DIFFERENCE channel=%d mode=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",
                  channel,
                  set.layers.back().fill.mode,
                  x,
                  y,
                  k,
                  expected[k],
                  actual);
            valid &= std::isfinite(actual) && difference <= tolerance;
            error = std::max(error, difference);
          }
        }
      MEM_delete(pixels);
      std::printf("BTS_GPU_STACK_VERIFIED channel=%d probes=64 max_error=%.9g valid=%d\n",
                  channel,
                  error,
                  int(valid));
      if (!valid)
        throw std::runtime_error("GPU layer preview differs from CPU reference");
    }
  }
  std::printf("BTS_GPU_STACK_CACHE dispatch_texels=%llu cached_nodes=%llu nodes=%zu\n",
              (unsigned long long)s.last_dispatch_texels,
              (unsigned long long)s.last_cached_nodes,
              inputs.size());
  std::printf("BTS_GPU_STACK channel=%d layers=%zu texels=%llu source_upload_bytes=%llu\n",
              channel,
              inputs.size(),
              static_cast<unsigned long long>(std::uint64_t(s.width) * s.height),
              static_cast<unsigned long long>(upload_before - s.remaining));
  return GpuPreviewUpdate::Updated;
}
