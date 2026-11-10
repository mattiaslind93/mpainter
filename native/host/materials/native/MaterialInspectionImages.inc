// SPDX-License-Identifier: GPL-3.0-or-later
// MaterialInspection's internal helpers: material bindings, layering topology and the Blender images they feed.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
bNodeSocket &input(bNode &node, int index)
{
  int i = 0;
  for (bNodeSocket &socket : node.inputs)
    if (i++ == index)
      return socket;
  throw std::runtime_error("Inspection node input is missing");
}
bNodeSocket &output(bNode &node, const char *identifier)
{
  for (bNodeSocket &socket : node.outputs)
    if (STREQ(socket.identifier, identifier))
      return socket;
  throw std::runtime_error("Inspection node output is missing");
}
void scalar(bNodeTree &tree, bNodeSocket &socket, float value)
{
  if (socket.type != SOCK_FLOAT || !socket.default_value ||
      (socket.flag & ((socket.in_out == SOCK_IN ? SOCK_IS_LINKED : 0) | SOCK_UNAVAIL)))
    throw std::runtime_error("Inspection scalar socket mismatch");
  if (static_cast<bNodeSocketValueFloat *>(socket.default_value)->value == value)
    return;
  static_cast<bNodeSocketValueFloat *>(socket.default_value)->value = value;
  BKE_ntree_update_tag_socket_property(&tree, &socket);
}
void color(bNodeTree &tree, bNodeSocket &socket, const std::array<float, 4> &value)
{
  if (socket.type != SOCK_RGBA || !socket.default_value ||
      (socket.flag & (SOCK_IS_LINKED | SOCK_UNAVAIL)))
    throw std::runtime_error("Inspection color socket mismatch");
  if (std::equal(value.begin(),
                 value.end(),
                 static_cast<bNodeSocketValueRGBA *>(socket.default_value)->value))
    return;
  std::copy(value.begin(),
            value.end(),
            static_cast<bNodeSocketValueRGBA *>(socket.default_value)->value);
  BKE_ntree_update_tag_socket_property(&tree, &socket);
}
bNode &node(bNodeTree &tree, const char *type, const char *name)
{
  if (auto *existing = bke::node_find_node_by_name(tree, name)) {
    if (!STREQ(existing->idname, type))
      throw std::runtime_error("Inspection node name conflicts with another node");
    return *existing;
  }
  auto *created = bke::node_add_node(nullptr, tree, UString(type));
  if (!created)
    throw std::runtime_error("Cannot create native inspection shader");
  STRNCPY(created->name, name);
  return *created;
}
struct Binding {
  Material *material;
  bNode *view_switch;
  bNode *coverage_mix;
  MaterialLayerShader layered_shader;
  MaterialLayering material_layering;
  bool layering_directx = false;
  std::string geometry_visibility_key;
  bool geometry_visibility_on = false;
  bool residency_hidden = false;
  std::array<Image *, 5> images{};
  std::array<Image *, custom_channel_count> user_images{};
  std::array<bNode *, custom_channel_count> user_textures{}, user_unpremultiply{};
  std::array<bool, custom_channel_count> user_present{};
  std::array<bNode *, 5> textures{}, unpremultiply{}, base_mix{};
  Image *bake_image = nullptr;
  bNode *bake_texture = nullptr, *bake_unpremultiply = nullptr;
  bool mesh_displayed = false;
  std::string bake_hash, bake_shape;
  Image *authored_normal_image = nullptr;
  // Signed WORLD direction is deliberately separate from tangent authored6.
  std::array<Image *, 2> world_normal_images{};
  bNode *world_normal_enabled = nullptr;
  bool world_images_full = false;
  std::shared_ptr<NativeWorldNormalGeometryJob> world_geometry_job;
  std::unique_ptr<NativeMaterialFragmentGpuGeometry> world_geometry;
  NativeFragmentResidentDiagnostics world_geometry_diagnostics;
  std::uint64_t world_publication_generation = 0, world_rendered_tiles = 0;
  std::uint64_t world_geometry_revision = 1, world_queued_generation = ~std::uint64_t(0),
                world_registry_generation = 0;
  NativeWorldNormalObjectObserver world_object_context;
  bool world_objects_dirty = false, world_view_dirty = false;
  NativeWorldNormalContextRoute world_context;
  NativeMaterialBakedAoContext world_ao;
  NativeBakedAoLease world_ao_lease;
  int world_ao_tile = 0;
  std::string coat_signature;
  std::string world_source_uv, world_resource;
  bool world_generated = false;
  std::uint64_t world_source_revision = 0;
  std::array<std::shared_ptr<gpu::Texture>, 2> world_scratch;
  std::string world_signature, world_published_source_signature, world_error;
  std::map<int, std::string> world_published_signatures, world_pending_signatures;
  std::set<std::pair<QUuid, int>> world_layers_ready;
  std::optional<TextureSet> world_set;
  std::vector<int> world_tiles;
  std::size_t world_tile = 0;

  Image *ao_image = nullptr;
  std::map<int, std::vector<unsigned char>> ao_presence;
  std::size_t ao_count = 0;
  std::uint64_t preview_generation = 0, final_generation = 0;
  bool preview_displacement_content = false;
  std::array<Image *, 17> preview_images{};
  CompositeChannels preview_active{}, edit_channels{};
  Image *displacement_image = nullptr;
  bNode *displacement_texture = nullptr, *displacement_unpremultiply = nullptr,
        *displacement_mix = nullptr, *displacement_node = nullptr;
  ShaderParameters shader_parameters;
  int scattering_slot = -1;
  DisplacementSettings displacement_settings;
  std::map<int, std::vector<unsigned char>> displacement_presence;
  std::size_t displacement_count = 0;
  std::optional<bool> gpu_displacement_presence;
  std::uint64_t displacement_cpu_epoch = 0;
  float displacement_minimum = 0, displacement_maximum = 1;
  Image *mask_image = nullptr;
  bNode *mask_texture = nullptr;
  std::unique_ptr<TextureSet> mask_snapshot;
  std::uint64_t mask_hash = 0;
  float mask_background = -1;
  bool mask_cached = false, mask_display_linked = false;
  QUuid mask_async_layer;
  std::shared_ptr<TexturePadding> padding;
  std::array<double, 2> native_per_uv{1, 1};
  std::map<int, std::vector<unsigned char>> normal_presence;
  std::size_t normal_count = 0;
  float previous_height_base = 0;
  std::array<std::array<float, 4>, 5> last_base{};
  bool bases_valid = false;
  int resolution = 0, height = 0;
  std::map<int, std::pair<int, int>> tile_sizes;
  std::map<int, std::shared_ptr<const FillSurfaceAtlas>> projection_cache;
  std::map<int, std::shared_ptr<const GeometryOwnershipTile>> ownership_cache;
  std::map<std::uint32_t, std::string> geometry_names;
  std::shared_ptr<ProjectionGeometryJob> projection_job;
  bool geometry_upload_pending = false;
  std::shared_ptr<MarginGeometryJob> margin_job;
  bool margins_ready = false;
  int displayed_channel = -2;
  MaterialValues displayed_values;
  std::array<float, 5> displayed_coverage{};
  Main *owner_main = nullptr;
  CompositeChannels gpu_stack_active{}, gpu_unbound_waiting{};
  std::optional<TextureSet> gpu_stack_set;
  bool gpu_inspection_waiting = false;
  std::uint64_t gpu_snapshot_generation = 0;
  std::array<std::optional<std::array<float, 4>>, 5> gpu_uniform_values;
};
// Stack-only, read-only checks for explicit shader authoring. Retained pointers
// are compared with their owner's containers before any pointed-to fields read.
struct LayeringTopology {
  const Main &main;
  const bNodeTree &tree;
  static void require(bool value, const char *message)
  {
    if (!value)
      throw std::runtime_error(message);
  }
  const bNode &node(const bNode *wanted, const char *type, const char *name = nullptr) const
  {
    const bNode *owned = nullptr;
    for (const auto &n : tree.nodes)
      if (&n == wanted) {
        owned = &n;
        break;
      }
    require(owned, "Material layering: retained node is not in its material tree");
    require(STREQ(owned->idname, type) && !(owned->flag & NODE_MUTED),
            "Material layering: native node type or mute state changed");
    require(!name || STREQ(owned->name, name),
            "Material layering: retained native node identity changed");
    if (name)
      for (const auto &n : tree.nodes)
        require(&n == owned || !STREQ(n.name, name),
                "Material layering: duplicate native node identity");
    return *owned;
  }
  const bNode &named(const char *name, const char *type) const
  {
    const bNode *found = nullptr;
    for (const auto &n : tree.nodes)
      if (STREQ(n.name, name)) {
        require(!found, "Material layering: duplicate native node identity");
        found = &n;
      }
    return node(found, type, name);
  }
  const bNodeSocket &socket(const bNode &n, bool inputs, const char *id, int type) const
  {
    const bNodeSocket *found = nullptr;
    const auto &sockets = inputs ? n.inputs : n.outputs;
    for (const auto &s : sockets)
      if (STREQ(s.identifier, id)) {
        require(!found, "Material layering: duplicate socket identifier");
        found = &s;
      }
    require(found && found->type == type && !(found->flag & SOCK_UNAVAIL),
            "Material layering: native socket type or availability changed");
    return *found;
  }
  const bNodeSocket &in(const bNode &n, const char *id, int type) const
  {
    return socket(n, true, id, type);
  }
  const bNodeSocket &out(const bNode &n, const char *id, int type) const
  {
    return socket(n, false, id, type);
  }
  const bNodeLink &incoming(const bNode &n, const bNodeSocket &s) const
  {
    const bNodeLink *found = nullptr;
    for (const auto &link : tree.links)
      if (link.tosock == &s) {
        require(!found, "Material layering: multiple incoming native links");
        found = &link;
      }
    require(found && found->tonode == &n && (found->flag & NODE_LINK_VALID) &&
                !(found->flag & NODE_LINK_MUTED) && (s.flag & SOCK_IS_LINKED),
            "Material layering: native link is missing, inactive or changed");
    return *found;
  }
  void edge(const bNode &from,
            const bNodeSocket &output,
            const bNode &to,
            const bNodeSocket &input) const
  {
    const auto &link = incoming(to, input);
    require(link.fromnode == &from && link.fromsock == &output,
            "Material layering: native source edge changed");
  }
  const bNode &source(const bNode &to,
                      const bNodeSocket &input,
                      const char *type,
                      const char *name = nullptr) const
  {
    return node(incoming(to, input).fromnode, type, name);
  }
  void unlinked(const bNodeSocket &s) const
  {
    for (const auto &link : tree.links)
      require(link.tosock != &s, "Material layering: expected native constant has a source link");
    require(!(s.flag & SOCK_IS_LINKED), "Material layering: stale native constant link flag");
  }
  void scalar(const bNodeSocket &s, float expected) const
  {
    unlinked(s);
    require(s.type == SOCK_FLOAT && s.default_value,
            "Material layering: native scalar storage changed");
    const float actual = static_cast<const bNodeSocketValueFloat *>(s.default_value)->value;
    require(std::isfinite(actual) && actual == expected,
            "Material layering: native scalar value changed");
  }
  void color(const bNodeSocket &s, const std::array<float, 4> &expected) const
  {
    unlinked(s);
    require(s.type == SOCK_RGBA && s.default_value,
            "Material layering: native color storage changed");
    const auto *actual = static_cast<const bNodeSocketValueRGBA *>(s.default_value)->value;
    require(std::equal(expected.begin(), expected.end(), actual),
            "Material layering: retained material base changed");
  }
  void mix(const bNode &n, int operation) const
  {
    require(n.custom1 == operation && n.custom2 == 0,
            "Material layering: native blend operation or flags changed");
  }
  const Image &image(const Image *wanted) const
  {
    const Image *owned = nullptr;
    for (const auto &image : main.images)
      if (&image == wanted) {
        owned = &image;
        break;
      }
    require(owned, "Material layering: retained image is not in the owner Main");
    require(!ID_IS_LINKED(&owned->id) && !ID_IS_OVERRIDE_LIBRARY(&owned->id) &&
                !(owned->id.tag &
                  (ID_TAG_NO_MAIN | ID_TAG_LOCALIZED | ID_TAG_COPIED_ON_EVAL | ID_TAG_MISSING)),
            "Material layering: native image ownership changed");
    // BKE_image_memorypack changes non-tiled generated images to FILE.
    // Exact retained identity plus owned packed storage also covers Save/Open;
    // arbitrary external FILE sources are not admitted. No buffer is decoded.
    const bool packed_file = owned->source == IMA_SRC_FILE && owned->packedfiles.first;
    require(
        (owned->source == IMA_SRC_GENERATED || owned->source == IMA_SRC_TILED || packed_file) &&
            owned->alpha_mode == IMA_ALPHA_CHANNEL_PACKED,
        "Material layering: native image source or alpha mode changed");
    return *owned;
  }
  void image_shape(const Image &image, const TextureSet &set) const
  {
    require((image.source == IMA_SRC_TILED) ==
                (set.tiles.size() != 1 || !set.tiles.contains(1001)),
            "Material layering: native image tiling changed");
    std::size_t count = 0;
    for (const auto &tile : image.tiles) {
      ++count;
      require(set.tiles.contains(tile.tile_number) && tile.gen_x == set.width(tile.tile_number) &&
                  tile.gen_y == set.rows(tile.tile_number),
              "Material layering: native image tile dimensions changed");
    }
    require(count == set.tiles.size(), "Material layering: native image tiles changed");
  }
  void texture(const bNode &node, const Image &image) const
  {
    require(node.id == &image.id && node.storage,
            "Material layering: native texture image ID changed");
    const auto &settings = *static_cast<const NodeTexImage *>(node.storage);
    require(settings.projection == SHD_PROJ_FLAT && settings.interpolation == SHD_INTERP_LINEAR &&
                settings.extension == SHD_IMAGE_EXTENSION_REPEAT,
            "Material layering: native texture sampling changed");
    unlinked(in(node, "Vector", SOCK_VECTOR));
  }
};
std::array<float, 4> encoded(const MaterialValues &v, int channel)
{
  switch (channel) {
    case 0:
      return v.base_color;
    case 1:
      return {v.metallic, v.metallic, v.metallic, 1};
    case 2:
      return {v.roughness, v.roughness, v.roughness, 1};
    case 3:
      return {v.normal[0] * .5f + .5f, v.normal[1] * .5f + .5f, v.normal[2] * .5f + .5f, 1};
    default:
      return {v.bump, v.bump, v.bump, 1};
  }
}
std::array<float, 4> encoded_precision(const MaterialValues &value,
                                       int channel,
                                       ChannelPrecision precision)
{
  const auto rgb = precision_channel(value, channel, precision);
  return {rgb[0], rgb[1], rgb[2], 1};
}
std::array<float, 4> encoded_material_base(const TextureSet &set, int channel)
{
  if (channel == 4) {
    const float q = precision_bump_base(set);
    return {q, q, q, 1};
  }
  return encoded_precision(
      set.channels[channel] ? set.base_values : MaterialValues{}, channel, set.precision[channel]);
}
bool gpu_readback_reference_ao_presence(const TextureSet &set)
{
  std::array<bool, 6> wanted{};
  wanted[5] = true;
  for (int tile : set.tiles)
    for (int y = 0; y < set.rows(tile); ++y)
      for (int x = 0; x < set.width(tile); ++x)
        if (TextureDocument::evaluate_channels(set, wanted, x, y, tile).ao_coverage > 0)
          return true;
  return false;
}
std::array<float, 4> gpu_logical_pixel(const TextureSet &set,
                                       int c,
                                       int x,
                                       int y,
                                       int tile,
                                       std::optional<bool> ao_present = std::nullopt)
{
  if (c == 15) {
    const float v = TextureDocument::mask_value(set, set.selected_layer, x, y, tile);
    return {v, v, v, 1};
  }
  if (c == 5) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      std::fprintf(stderr, "BTS_GPU_CPU_REFERENCE reason=diagnostic-or-export-query channel=5\n");
    }
    std::array<bool, 6> wanted{};
    wanted[5] = true;
    const auto composite = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
    const float value = mix_ambient_occlusion(
        set,
        composite.material.ambient_occlusion,
        ao_present ? *ao_present : gpu_readback_reference_ao_presence(set),
        sample_baked_ao(set, (x + .5) / set.width(tile), (y + .5) / set.rows(tile), tile));
    const float q = precision_rgb({value, value, value}, set.precision[5])[0];
    return {q, q, q, 1};
  }
  if (c == 6) {
    std::array<bool, 6> wanted{};
    wanted[3] = true;
    const auto result = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
    const auto full = precision_channel(result.material, 3, set.precision[3]);
    return {full[0], full[1], full[2], 1};
  }
  if (c >= 7) {
    const auto value = c == 16 ? TextureDocument::composite_displacement(set, x, y, tile) :
                                 TextureDocument::composite_user(set, c - 7, x, y, tile);
    return {value[0] * value[1], value[0] * value[1], value[0] * value[1], value[1]};
  }
  std::array<bool, 6> wanted{};
  wanted[c] = true;
  const auto composite = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
  auto full = precision_channel(composite.material, c, set.precision[c]),
       base = precision_channel(
           set.channels[c] ? set.base_values : MaterialValues{}, c, set.precision[c]);
  if (c == 4) {
    full.fill(precision_bump_output(set, composite.material.bump));
    base.fill(precision_bump_base(set));
  }
  const float alpha = composite.coverage[c];
  return {full[0] - base[0] * (1 - alpha),
          full[1] - base[1] * (1 - alpha),
          full[2] - base[2] * (1 - alpha),
          alpha};
}
// Keep original Principled socket defaults intact; only BTS-owned links are installed.
bool subsurface_shader(Binding &binding, bool globally_enabled)
{
  auto &tree = *binding.material->nodetree;
  auto *surface = native_principled_surface(*binding.material);
  if (!surface)
    return false;
  auto *existing = bke::node_find_node_by_name(tree, "__BTS_SubsurfaceWeight");
  const bool present = binding.scattering_slot >= 0 &&
                       binding.user_unpremultiply[binding.scattering_slot];
  if (!present && !existing)
    return false;
  bool changed = false;
  const auto destination = [&](const char *name) -> bNodeSocket & {
    auto *socket = bke::node_find_enabled_input_socket(*surface, name);
    if (!socket)
      throw std::runtime_error("Principled subsurface input missing");
    return *socket;
  };
  constexpr auto prior_method = "bts_subsurface_original_method";
  auto *prior = binding.material->id.properties ?
                    IDP_GetPropertyFromGroup(binding.material->id.properties, prior_method) :
                    nullptr;
  if (!present) {
    for (auto *name : {"Subsurface Weight", "Subsurface Scale", "Subsurface Radius"}) {
      auto &socket = destination(name);
      for (auto *link = static_cast<bNodeLink *>(tree.links.first); link;) {
        auto *next = link->next;
        if (link->tosock == &socket && STRPREFIX(link->fromnode->name, "__BTS_Subsurface")) {
          bke::node_remove_link(&tree, *link);
          changed = true;
        }
        link = next;
      }
    }
    if (prior && prior->type == IDP_INT && surface->custom2 != IDP_int_get(prior)) {
      surface->custom2 = IDP_int_get(prior);
      BKE_ntree_update_tag_node_property(&tree, surface);
      changed = true;
    }
    return changed;
  }
  if (!prior)
    IDP_AddToGroup(IDP_EnsureProperties(&binding.material->id),
                   IDP_NewInt(surface->custom2, prior_method));
  const auto &value = binding.shader_parameters.subsurface;
  const auto prior_count = tree.nodes.count();
  auto &weight = node(tree, "ShaderNodeMath", "__BTS_SubsurfaceWeight");
  weight.custom1 = NODE_MATH_MULTIPLY;
  auto &scale = node(tree, "ShaderNodeMath", "__BTS_SubsurfaceScale");
  scale.custom1 = NODE_MATH_MULTIPLY;
  auto &radius = node(tree, "ShaderNodeCombineXYZ", "__BTS_SubsurfaceRadius");
  changed |= prior_count != tree.nodes.count();
  const auto number = [&](bNodeSocket &socket, float v) {
    if (static_cast<bNodeSocketValueFloat *>(socket.default_value)->value != v) {
      scalar(tree, socket, v);
      changed = true;
    }
  };
  const auto connect = [&](bNode &from, const char *name, bNode &to, bNodeSocket &target) {
    auto &source = output(from, name);
    for (const auto &link : tree.links)
      if (link.fromsock == &source && link.tosock == &target)
        return;
    bke::node_remove_socket_links(tree, target);
    bke::node_add_link(tree, from, source, to, target);
    changed = true;
  };
  number(input(weight, 1), globally_enabled && value.enabled ? 1.f : 0.f);
  number(input(scale, 0), value.scale);
  number(input(scale, 1), 1);
  for (int i = 0; i < 3; ++i)
    number(input(radius, i), value.color[i]);
  connect(
      *binding.user_unpremultiply[binding.scattering_slot], "Vector", weight, input(weight, 0));
  connect(weight, "Value", *surface, destination("Subsurface Weight"));
  connect(scale, "Value", *surface, destination("Subsurface Scale"));
  connect(radius, "Vector", *surface, destination("Subsurface Radius"));
  const int method = value.type == 0 ? SHD_SUBSURFACE_RANDOM_WALK_SKIN :
                                       SHD_SUBSURFACE_RANDOM_WALK;
  if (surface->custom2 != method) {
    surface->custom2 = method;
    BKE_ntree_update_tag_node_property(&tree, surface);
    changed = true;
  }
  return changed;
}
Binding bind(Main &main, std::uint32_t uid)
{
  Material *material = nullptr;
  for (Material &candidate : main.materials)
    if (candidate.id.session_uid == uid)
      material = &candidate;
  if (!material || !material->nodetree)
    throw std::runtime_error("Inspection material is not in the live project");
  auto *principled = native_principled_surface(*material);
  auto *surface_output = native_material_output(*material);
  if (!principled || !surface_output)
    throw std::runtime_error("Inspection requires a direct Principled material");
  auto &tree = *material->nodetree;
  // Until native Open has an explicit schema/rebind contract, never adopt unknown nodes.
  for (const char *name : {"__BTS_ViewSwitch",
                           "__BTS_Inspection",
                           "__BTS_InspectCoverage",
                           "__BTS_Checker",
                           "__BTS_UvCoordinates"})
    if (bke::node_find_node_by_name(tree, name))
      throw std::runtime_error("Reserved inspection node name already exists");
  auto &view_switch = node(tree, "ShaderNodeMixShader", "__BTS_ViewSwitch");
  auto &emission = node(tree, "ShaderNodeEmission", "__BTS_Inspection");
  auto &coverage = node(tree, "ShaderNodeMixRGB", "__BTS_InspectCoverage");
  {
    auto &checker = node(tree, "ShaderNodeTexChecker", "__BTS_Checker");
    auto &coordinates = node(tree, "ShaderNodeTexCoord", "__BTS_UvCoordinates");
    scalar(tree, input(view_switch, 0), 0);
    scalar(tree, input(emission, 1), 1);
    color(tree, input(checker, 1), {.16f, .16f, .16f, 1});
    color(tree, input(checker, 2), {.32f, .32f, .32f, 1});
    scalar(tree, input(checker, 3), 16);
    auto *surface = bke::node_find_enabled_input_socket(*surface_output, "Surface");
    if (!surface)
      throw std::runtime_error("Material output Surface is missing");
    bke::node_remove_socket_links(tree, *surface);
    bke::node_add_link(
        tree, *principled, output(*principled, "BSDF"), view_switch, input(view_switch, 1));
    bke::node_add_link(
        tree, emission, output(emission, "Emission"), view_switch, input(view_switch, 2));
    bke::node_add_link(
        tree, view_switch, output(view_switch, "Shader"), *surface_output, *surface);
    bke::node_add_link(tree, coordinates, output(coordinates, "UV"), checker, input(checker, 0));
    bke::node_add_link(tree, checker, output(checker, "Color"), coverage, input(coverage, 1));
    bke::node_add_link(tree, coverage, output(coverage, "Color"), emission, input(emission, 0));
    BKE_ntree_update_after_single_tree_change(main, tree);
  }
  Binding binding{material, &view_switch, &coverage};
  binding.owner_main = &main;
  return binding;
}
// A derived, unmanaged mask image drives emission in both native views. It never
// replaces parent material channels or the sparse authoring pixels.
PaintPixels::Bounds evaluated_bounds(const TextureSet &set, int tile, PaintPixels::Bounds bounds)
{
  const double sx = double(set.width(tile)) / set.texture_resolution,
               sy = double(set.rows(tile)) / set.rows();
  return {std::clamp(int(std::floor(bounds.x0 * sx)), 0, set.width(tile)),
          std::clamp(int(std::floor(bounds.y0 * sy)), 0, set.rows(tile)),
          std::clamp(int(std::ceil(bounds.x1 * sx)), 0, set.width(tile)),
          std::clamp(int(std::ceil(bounds.y1 * sy)), 0, set.rows(tile))};
}
void ensure_ao_image(Main &main, Binding &binding, const TextureSet &set, int resolution)
{
  const int height = set.rows();
  if (binding.ao_image)
    return;
  auto &tree = *binding.material->nodetree;
  auto &texture = node(tree, "ShaderNodeTexImage", "__BTS_AOPixels");
  const float white[4] = {1, 1, 1, 1};
  binding.ao_image = native_generated_image(&main,
                                            set.width(1001),
                                            set.rows(1001),
                                            "BTS Complete Ambient Occlusion",
                                            32,
                                            1,
                                            IMA_GENTYPE_BLANK,
                                            white,
                                            false,
                                            true,
                                            set.tiles != std::set<int>{1001});
  if (!binding.ao_image)
    throw std::runtime_error("Cannot allocate ambient occlusion image");
  binding.ao_image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
  if (texture.id)
    id_us_min(texture.id);
  texture.id = &binding.ao_image->id;
  id_us_plus(texture.id);
  if (binding.ao_image->source == IMA_SRC_TILED) {
    for (int number : set.tiles)
      if (number != 1001) {
        auto *tile = BKE_image_add_tile(binding.ao_image, number, nullptr);
        if (!tile)
          throw std::runtime_error("Cannot allocate AO UV tile");
        tile->gen_x = set.width(number);
        tile->gen_y = set.rows(number);
        tile->gen_depth = 32;
        tile->gen_type = IMA_GENTYPE_BLANK;
        tile->gen_flag |= IMA_GEN_FLOAT;
        std::copy_n(white, 4, tile->gen_color);
        if (!native_fill_tile(binding.ao_image, tile))
          throw std::runtime_error("Cannot fill AO UV tile");
      }
    if (!set.tiles.contains(1001))
      BKE_image_remove_tile(binding.ao_image, BKE_image_get_tile(binding.ao_image, 1001));
  }
  auto &multiply = node(tree, "ShaderNodeMixRGB", "__BTS_AOMaterial");
  multiply.custom1 = MA_RAMP_MULT;
  scalar(tree, input(multiply, 0), binding.shader_parameters.ao_intensity);
  bke::node_add_link(tree,
                     *binding.base_mix[0],
                     output(*binding.base_mix[0], "Color"),
                     multiply,
                     input(multiply, 1));
  bke::node_add_link(tree, texture, output(texture, "Color"), multiply, input(multiply, 2));
  auto *principled = native_principled_surface(*binding.material);
  auto *base = principled ? bke::node_find_enabled_input_socket(*principled, "Base Color") :
                            nullptr;
  if (!base)
    throw std::runtime_error("AO material base input is missing");
  bke::node_remove_socket_links(tree, *base);
  bke::node_add_link(tree, multiply, output(multiply, "Color"), *principled, *base);
  BKE_ntree_update_after_single_tree_change(main, tree);
}

void baked_image(Main &main, Binding &binding, const MeshMap &map, const TextureSet &set)
{
  if (map.tiles.empty())
    throw std::runtime_error("Baked map has no tiles");
  std::string hash, shape;
  for (int number : set.tiles) {
    const auto found = map.tiles.find(number);
    const int size = found == map.tiles.end() ? map.tiles.begin()->second.resolution :
                                                found->second.resolution;
    shape += std::to_string(number) + ":" + std::to_string(size) + ":" +
             std::to_string(found == map.tiles.end() ? map.tiles.begin()->second.rows() :
                                                       found->second.rows()) +
             ";";
    if (found != map.tiles.end())
      hash += found->second.hash;
  }
  hash += shape;
  if (binding.bake_image && binding.bake_hash == hash)
    return;
  auto &tree = *binding.material->nodetree;
  if (!binding.bake_texture)
    binding.bake_texture = &node(tree, "ShaderNodeTexImage", "__BTS_MeshMapInspection");
  if (!binding.bake_image || binding.bake_shape != shape) {
    if (binding.bake_texture->id)
      id_us_min(binding.bake_texture->id);
    const float zero[4] = {0, 0, 0, 0};
    const int resolution = map.tiles.contains(1001) ? map.tiles.at(1001).resolution :
                                                      map.tiles.begin()->second.resolution;
    const int height = map.tiles.contains(1001) ? map.tiles.at(1001).rows() :
                                                  map.tiles.begin()->second.rows();
    binding.bake_image = native_generated_image(&main,
                                                resolution,
                                                height,
                                                "BTS Mesh Map Inspection",
                                                32,
                                                1,
                                                IMA_GENTYPE_BLANK,
                                                zero,
                                                false,
                                                true,
                                                set.tiles != std::set<int>{1001});
    if (!binding.bake_image)
      throw std::runtime_error("Cannot allocate mesh map inspection image");
    binding.bake_image->flag |= IMA_HIGH_BITDEPTH;
    binding.bake_image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
    binding.bake_texture->id = &binding.bake_image->id;
    id_us_plus(binding.bake_texture->id);
    binding.bake_shape = shape;
    reinterpret_cast<NodeTexImage *>(binding.bake_texture->storage)->extension =
        SHD_IMAGE_EXTENSION_CLIP;
    if (binding.bake_image->source == IMA_SRC_TILED) {
      for (int number : set.tiles)
        if (number != 1001) {
          auto *tile = BKE_image_add_tile(binding.bake_image, number, nullptr);
          if (!tile)
            throw std::runtime_error("Cannot allocate baked UV tile");
          tile->gen_x = map.tiles.contains(number) ? map.tiles.at(number).resolution : resolution;
          tile->gen_y = map.tiles.contains(number) ? map.tiles.at(number).rows() : height;
          tile->gen_depth = 32;
          tile->gen_type = IMA_GENTYPE_BLANK;
          tile->gen_flag |= IMA_GEN_FLOAT;
          if (!native_fill_tile(binding.bake_image, tile))
            throw std::runtime_error("Cannot fill baked UV tile");
        }
      if (!set.tiles.contains(1001))
        BKE_image_remove_tile(binding.bake_image, BKE_image_get_tile(binding.bake_image, 1001));
    }
  }
  for (int number : set.tiles) {
    const auto found = map.tiles.find(number);
    const MeshMapPlane *plane = found == map.tiles.end() ? nullptr : &found->second;
    ImageUser user{};
    user.tile = number;
    auto *buffer = BKE_image_acquire_ibuf(binding.bake_image, &user, nullptr);
    if (!buffer || !buffer->float_data() ||
        (plane &&
         (!plane->pixels || buffer->x != plane->resolution || buffer->y != plane->rows())))
    {
      if (buffer)
        BKE_image_release_ibuf(binding.bake_image, buffer, nullptr);
      throw std::runtime_error("Baked inspection buffer mismatch");
    }
    if (plane)
      std::copy(
          plane->pixels->rgba.begin(), plane->pixels->rgba.end(), buffer->float_data_for_write());
    else
      std::fill_n(buffer->float_data_for_write(), std::size_t(buffer->x) * buffer->y * 4, 0.f);
    BKE_image_mark_dirty(binding.bake_image, buffer);
    BKE_image_update_gputexture_delayed(binding.bake_image,
                                        BKE_image_get_tile(binding.bake_image, number),
                                        buffer,
                                        0,
                                        0,
                                        buffer->x,
                                        buffer->y);
    BKE_image_release_ibuf(binding.bake_image, buffer, nullptr);
  }
  if (!binding.bake_unpremultiply) {
    binding.bake_unpremultiply = &node(tree, "ShaderNodeVectorMath", "__BTS_MeshMapUnpremultiply");
    binding.bake_unpremultiply->custom1 = NODE_VECTOR_MATH_DIVIDE;
  }
  bke::node_remove_socket_links(tree, input(*binding.bake_unpremultiply, 0));
  bke::node_remove_socket_links(tree, input(*binding.bake_unpremultiply, 1));
  bke::node_add_link(tree,
                     *binding.bake_texture,
                     output(*binding.bake_texture, "Color"),
                     *binding.bake_unpremultiply,
                     input(*binding.bake_unpremultiply, 0));
  bke::node_add_link(tree,
                     *binding.bake_texture,
                     output(*binding.bake_texture, "Alpha"),
                     *binding.bake_unpremultiply,
                     input(*binding.bake_unpremultiply, 1));
  binding.bake_hash = hash;
}
void ensure_authored_normal_image(Main &main, Binding &binding, const TextureSet &set)
{
  if (binding.authored_normal_image)
    return;
  auto &tree = *binding.material->nodetree;
  auto &texture = node(tree, "ShaderNodeTexImage", "__BTS_AuthoredNormalPixels");
  if (texture.id) {
    if (GS(texture.id->name) != ID_IM)
      throw std::runtime_error("Invalid authored normal reference");
    binding.authored_normal_image = reinterpret_cast<Image *>(texture.id);
    return;
  }
  const float flat[4] = {.5f, .5f, 1, 1};
  auto *image = native_generated_image(&main,
                                       set.width(1001),
                                       set.rows(1001),
                                       "BTS Authored Normal Inspection",
                                       32,
                                       1,
                                       IMA_GENTYPE_BLANK,
                                       flat,
                                       false,
                                       true,
                                       set.tiles != std::set<int>{1001});
  if (!image)
    throw std::runtime_error("Cannot allocate authored normal inspection");
  binding.authored_normal_image = image;
  image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
  texture.id = &image->id;
  id_us_plus(texture.id);
  if (image->source == IMA_SRC_TILED) {
    for (int number : set.tiles)
      if (number != 1001) {
        auto *tile = BKE_image_add_tile(image, number, nullptr);
        if (!tile)
          throw std::runtime_error("Cannot add authored normal tile");
        tile->gen_x = set.width(number);
        tile->gen_y = set.rows(number);
        tile->gen_depth = 32;
        tile->gen_type = IMA_GENTYPE_BLANK;
        tile->gen_flag |= IMA_GEN_FLOAT;
        std::copy_n(flat, 4, tile->gen_color);
        if (!native_fill_tile(image, tile))
          throw std::runtime_error("Cannot fill authored normal tile");
      }
    if (!set.tiles.contains(1001))
      BKE_image_remove_tile(image, BKE_image_get_tile(image, 1001));
  }
  binding.displayed_channel = -2;
}
void update_user_image(Main &main,
                       Binding &binding,
                       const TextureSet &set,
                       int slot,
                       PaintPixels::Bounds bounds,
                       bool interactive,
                       bool allocate_only = false)
{
  const int width = set.texture_resolution, height = set.rows();
  auto &tree = *binding.material->nodetree;
  auto &image = binding.user_images[slot];
  const auto texture_name = "__BTS_UserPixels" + std::to_string(slot),
             divide_name = "__BTS_UserUnpremultiply" + std::to_string(slot);
  auto &texture = node(tree, "ShaderNodeTexImage", texture_name.c_str());
  binding.user_textures[slot] = &texture;
  if (!image && texture.id) {
    if (GS(texture.id->name) != ID_IM)
      throw std::runtime_error("Invalid custom image reference");
    image = reinterpret_cast<Image *>(texture.id);
  }
  if (!image) {
    const float empty[4] = {};
    image = native_generated_image(&main,
                                   set.width(1001),
                                   set.rows(1001),
                                   ("BTS Custom " + set.users[slot].name).c_str(),
                                   32,
                                   1,
                                   IMA_GENTYPE_BLANK,
                                   empty,
                                   false,
                                   true,
                                   set.tiles != std::set<int>{1001});
    if (!image)
      throw std::runtime_error("Cannot allocate custom channel image");
    image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
    texture.id = &image->id;
    id_us_plus(texture.id);
    if (image->source == IMA_SRC_TILED) {
      for (int number : set.tiles)
        if (number != 1001) {
          auto *tile = BKE_image_add_tile(image, number, nullptr);
          if (!tile)
            throw std::runtime_error("Cannot allocate custom UV tile");
          tile->gen_x = set.width(number);
          tile->gen_y = set.rows(number);
          tile->gen_depth = 32;
          tile->gen_type = IMA_GENTYPE_BLANK;
          tile->gen_flag |= IMA_GEN_FLOAT;
          if (!native_fill_tile(image, tile))
            throw std::runtime_error("Cannot fill custom UV tile");
        }
      if (!set.tiles.contains(1001))
        BKE_image_remove_tile(image, BKE_image_get_tile(image, 1001));
    }
    bounds = {0, 0, width, height};
  }
  if (!binding.user_unpremultiply[slot]) {
    auto &divide = node(tree, "ShaderNodeVectorMath", divide_name.c_str());
    divide.custom1 = NODE_VECTOR_MATH_DIVIDE;
    BKE_ntree_update_tag_node_property(&tree, &divide);
    bke::node_add_link(tree, texture, output(texture, "Color"), divide, input(divide, 0));
    bke::node_add_link(tree, texture, output(texture, "Alpha"), divide, input(divide, 1));
    binding.user_unpremultiply[slot] = &divide;
    binding.displayed_channel = -2;
  }
  if (allocate_only)
    return;
  const auto parent_bounds = bounds;
  for (int number : set.tiles) {
    const int width = set.width(number), height = set.rows(number);
    const auto bounds = evaluated_bounds(set, number, parent_bounds);
    ImageUser user{};
    user.tile = number;
    auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
    if (!buffer || buffer->x != width || buffer->y != height || !buffer->float_data()) {
      if (buffer)
        BKE_image_release_ibuf(image, buffer, nullptr);
      throw std::runtime_error("Invalid custom composite dimensions");
    }
    auto *data = buffer->float_data_for_write();
    for (int y = bounds.y0; y < bounds.y1; ++y)
      for (int x = bounds.x0; x < bounds.x1; ++x) {
        const auto value = TextureDocument::composite_user(set, slot, x, y, number);
        const auto offset = (std::size_t(y) * width + x) * 4;
        data[offset] = data[offset + 1] = data[offset + 2] = value[0] * value[1];
        data[offset + 3] = value[1];
      }
    if (binding.padding && !interactive)
      binding.padding->apply(*buffer, number, set.padding_3d);
    BKE_image_mark_dirty(image, buffer);
    const auto dirty = interactive ? bounds : PaintPixels::Bounds{0, 0, width, height};
    BKE_image_update_gputexture_delayed(image,
                                        BKE_image_get_tile(image, number),
                                        buffer,
                                        dirty.x0,
                                        dirty.y0,
                                        dirty.x1 - dirty.x0,
                                        dirty.y1 - dirty.y0);
    BKE_image_release_ibuf(image, buffer, nullptr);
  }
  DEG_id_tag_update_ex(&main, &image->id, ID_RECALC_SHADING);
}
// Scalar Displacement is independent of the five legacy material channels and eight user slots.
bool displacement_shader(Main &main, Binding &binding)
{
  if (!binding.displacement_image)
    return false;
  auto &tree = *binding.material->nodetree;
  auto *material_output = native_material_output(*binding.material);
  auto *destination = material_output ?
                          bke::node_find_enabled_input_socket(*material_output, "Displacement") :
                          nullptr;
  if (!destination)
    throw std::runtime_error("Material output Displacement is missing");
  const auto &settings = binding.displacement_settings;
  auto &mix = *binding.displacement_mix;
  auto &disp = *binding.displacement_node;
  const bool active = settings.channel && settings.enabled &&
                      (binding.preview_active[16] ? binding.preview_displacement_content :
                                                    binding.gpu_displacement_presence.value_or(
                                                        binding.displacement_count > 0)) &&
                      settings.strength > 0;
  const bool linked = destination->link && destination->link->fromnode == &disp;
  if (destination->link && !linked)
    throw std::runtime_error(
        "Displacement requires an unlinked Material Output Displacement input");
  bool changed = false;
  const auto assign = [&](bNodeSocket &socket, float value) {
    auto *storage = static_cast<bNodeSocketValueFloat *>(socket.default_value);
    if (storage->value != value) {
      scalar(tree, socket, value);
      changed = true;
    }
  };
  auto *base = static_cast<bNodeSocketValueRGBA *>(input(mix, 1).default_value);
  if (base->value[0] != settings.midlevel) {
    color(tree, input(mix, 1), {settings.midlevel, settings.midlevel, settings.midlevel, 1});
    changed = true;
  }
  assign(input(disp, 1), settings.midlevel);
  assign(input(disp, 2), settings.strength);
  if (active != linked) {
    bke::node_remove_socket_links(tree, *destination);
    if (active)
      bke::node_add_link(tree, disp, output(disp, "Displacement"), *material_output, *destination);
    changed = true;
  }
  const auto method = active ? MA_DISPLACEMENT_BOTH : MA_DISPLACEMENT_BUMP;
  if (binding.material->displacement_method != method) {
    binding.material->displacement_method = method;
    changed = true;
  }
  const float bounds =
      active ?
          std::max(settings.bounds(),
                   settings.strength *
                           std::max(std::abs(binding.displacement_minimum - settings.midlevel),
                                    std::abs(binding.displacement_maximum - settings.midlevel)) *
                           1.05f +
                       1e-6f) :
          0;
  const bool bounds_changed = binding.material->inflate_bounds != bounds;
  if (bounds_changed) {
    binding.material->inflate_bounds = bounds;
    DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SYNC_TO_EVAL);
  }
  if (changed) {
    BKE_ntree_update_after_single_tree_change(main, tree);
    DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING | ID_RECALC_SYNC_TO_EVAL);
    DEG_id_tag_update_ex(&main, &tree.id, ID_RECALC_NTREE_OUTPUT);
  }
  return changed || bounds_changed;
}
void ensure_displacement_image(Main &main, Binding &binding, const TextureSet &set)
{
  binding.displacement_settings = set.displacement;
  if (!set.displacement.allocated) {
    displacement_shader(main, binding);
    return;
  }
  auto &tree = *binding.material->nodetree;
  auto &image = binding.displacement_image;
  auto &texture = node(tree, "ShaderNodeTexImage", "__BTS_PixelsDisp");
  binding.displacement_texture = &texture;
  if (!image && texture.id) {
    if (GS(texture.id->name) != ID_IM)
      throw std::runtime_error("Invalid displacement image reference");
    image = reinterpret_cast<Image *>(texture.id);
  }
  if (!image) {
    const float empty[4] = {};
    image = native_generated_image(&main,
                                   set.width(1001),
                                   set.rows(1001),
                                   "BTS Displacement",
                                   32,
                                   1,
                                   IMA_GENTYPE_BLANK,
                                   empty,
                                   false,
                                   true,
                                   set.tiles != std::set<int>{1001});
    if (!image)
      throw std::runtime_error("Cannot allocate displacement image");
    image->flag |= IMA_HIGH_BITDEPTH;
    image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
    texture.id = &image->id;
    id_us_plus(texture.id);
    if (image->source == IMA_SRC_TILED) {
      for (int number : set.tiles)
        if (number != 1001) {
          auto *tile = BKE_image_add_tile(image, number, nullptr);
          if (!tile)
            throw std::runtime_error("Cannot allocate displacement UV tile");
          tile->gen_x = set.width(number);
          tile->gen_y = set.rows(number);
          tile->gen_depth = 32;
          tile->gen_type = IMA_GENTYPE_BLANK;
          tile->gen_flag |= IMA_GEN_FLOAT;
          if (!native_fill_tile(image, tile))
            throw std::runtime_error("Cannot fill displacement UV tile");
        }
      if (!set.tiles.contains(1001))
        BKE_image_remove_tile(image, BKE_image_get_tile(image, 1001));
    }
  }
  if (!binding.displacement_node) {
    auto &divide = node(tree, "ShaderNodeVectorMath", "__BTS_UnpremultiplyDisp");
    divide.custom1 = NODE_VECTOR_MATH_DIVIDE;
    BKE_ntree_update_tag_node_property(&tree, &divide);
    auto &mix = node(tree, "ShaderNodeMixRGB", "__BTS_BaseMixDisp");
    auto &disp = node(tree, "ShaderNodeDisplacement", "__BTS_Displacement");
    disp.custom1 = SHD_SPACE_WORLD;
    BKE_ntree_update_tag_node_property(&tree, &disp);
    bke::node_add_link(tree, texture, output(texture, "Color"), divide, input(divide, 0));
    bke::node_add_link(tree, texture, output(texture, "Alpha"), divide, input(divide, 1));
    bke::node_add_link(tree, texture, output(texture, "Alpha"), mix, input(mix, 0));
    bke::node_add_link(tree, divide, output(divide, "Vector"), mix, input(mix, 2));
    bke::node_add_link(tree, mix, output(mix, "Color"), disp, input(disp, 0));
    binding.displacement_unpremultiply = &divide;
    binding.displacement_mix = &mix;
    binding.displacement_node = &disp;
    binding.displayed_channel = -2;
    BKE_ntree_update_after_single_tree_change(main, tree);
  }
  displacement_shader(main, binding);
}
void update_displacement_image(Main &main,
                               Binding &binding,
                               const TextureSet &set,
                               PaintPixels::Bounds bounds,
                               bool interactive)
{
  binding.gpu_displacement_presence.reset();
  ++binding.displacement_cpu_epoch;
  const bool created = !binding.displacement_image;
  ensure_displacement_image(main, binding, set);
  if (!set.displacement.allocated)
    return;
  auto *image = binding.displacement_image;
  if (created)
    bounds = {0, 0, set.texture_resolution, set.rows()};
  const auto parent_bounds = bounds;
  for (int number : set.tiles) {
    const int width = set.width(number), height = set.rows(number);
    const auto dirty = evaluated_bounds(set, number, parent_bounds);
    auto &presence = binding.displacement_presence[number];
    if (presence.empty())
      presence.resize(std::size_t(width) * height);
    ImageUser user{};
    user.tile = number;
    auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
    if (!buffer || buffer->x != width || buffer->y != height || !buffer->float_data()) {
      if (buffer)
        BKE_image_release_ibuf(image, buffer, nullptr);
      throw std::runtime_error("Invalid displacement composite dimensions");
    }
    auto *data = buffer->float_data_for_write();
    for (int y = dirty.y0; y < dirty.y1; ++y)
      for (int x = dirty.x0; x < dirty.x1; ++x) {
        const auto value = TextureDocument::composite_displacement(set, x, y, number);
        const auto pixel = std::size_t(y) * width + x, index = pixel * 4;
        const bool content = value[1] > 0;
        if (content) {
          binding.displacement_minimum = std::min(binding.displacement_minimum, value[0]);
          binding.displacement_maximum = std::max(binding.displacement_maximum, value[0]);
        }
        if (bool(presence[pixel]) != content) {
          if (content)
            ++binding.displacement_count;
          else
            --binding.displacement_count;
          presence[pixel] = content;
        }
        data[index] = data[index + 1] = data[index + 2] = value[0] * value[1];
        data[index + 3] = value[1];
      }
    if (binding.padding && !interactive)
      binding.padding->apply(*buffer, number, set.padding_3d);
    BKE_image_mark_dirty(image, buffer);
    const auto upload = interactive ? dirty : PaintPixels::Bounds{0, 0, width, height};
    BKE_image_update_gputexture_delayed(image,
                                        BKE_image_get_tile(image, number),
                                        buffer,
                                        upload.x0,
                                        upload.y0,
                                        upload.x1 - upload.x0,
                                        upload.y1 - upload.y0);
    BKE_image_release_ibuf(image, buffer, nullptr);
  }
  DEG_id_tag_update_ex(&main, &image->id, ID_RECALC_SHADING);
  displacement_shader(main, binding);
}
void mask_image(Main &main,
                Binding &binding,
                const TextureSet &set,
                const LayerMask &mask,
                int resolution,
                bool prepare_only = false)
{
  const int height = set.rows();
  auto &tree = *binding.material->nodetree;
  if (!binding.mask_texture) {
    binding.mask_texture = &node(tree, "ShaderNodeTexImage", "__BTS_MaskInspection");
    if (binding.mask_texture->id) {
      if (GS(binding.mask_texture->id->name) != ID_IM)
        throw std::runtime_error("Mask inspection image reference is invalid");
      binding.mask_image = reinterpret_cast<Image *>(binding.mask_texture->id);
    }
  }
  std::uint64_t hash = 0;
  if (!prepare_only) {
    hash = mask.pixels.hash(2);
    const auto include = [&](const QByteArray &bytes) {
      for (unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
      }
    };
    include(QJsonDocument(mask_effects_json(mask.effects)).toJson(QJsonDocument::Compact));
    if (mask.channel_dependent())
      include(TextureDocument::mask_dependency_signature(set));
    for (int tile : set.tiles)
      include(QByteArray::number(tile) + ":" + QByteArray::number(set.width(tile)) + ":" +
              QByteArray::number(set.rows(tile)));
    for (int role = 0; role < 7; ++role)
      if (set.mesh_maps[role]) {
        include(QByteArray::number(role));
        for (const auto &[tile, plane] : set.mesh_maps[role]->tiles) {
          include(QByteArray::number(tile));
          include(QByteArray::fromStdString(plane.pixels ? plane.pixels->hash : plane.hash));
        }
      }
    if (binding.mask_cached && binding.mask_hash == hash &&
        binding.mask_background == mask.background)
      return;
  }
  if (!binding.mask_image) {
    const float empty[4] = {0, 0, 0, 1};
    binding.mask_image = native_generated_image(&main,
                                                set.width(1001),
                                                set.rows(1001),
                                                "BTS Mask Inspection",
                                                32,
                                                1,
                                                IMA_GENTYPE_BLANK,
                                                empty,
                                                false,
                                                true,
                                                set.tiles != std::set<int>{1001});
    if (!binding.mask_image)
      throw std::runtime_error("Cannot allocate mask inspection image");
    binding.mask_image->flag |= IMA_HIGH_BITDEPTH;
    binding.mask_image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
    binding.mask_texture->id = &binding.mask_image->id;
    id_us_plus(binding.mask_texture->id);
    if (binding.mask_image->source == IMA_SRC_TILED) {
      for (int number : set.tiles)
        if (number != 1001) {
          auto *tile = BKE_image_add_tile(binding.mask_image, number, nullptr);
          if (!tile)
            throw std::runtime_error("Cannot allocate mask UV tile");
          tile->gen_x = set.width(number);
          tile->gen_y = set.rows(number);
          tile->gen_depth = 32;
          tile->gen_type = IMA_GENTYPE_BLANK;
          tile->gen_flag |= IMA_GEN_FLOAT;
          if (!native_fill_tile(binding.mask_image, tile))
            throw std::runtime_error("Cannot initialize mask UV tile");
        }
      if (!set.tiles.contains(1001))
        BKE_image_remove_tile(binding.mask_image, BKE_image_get_tile(binding.mask_image, 1001));
    }
  }
  if (prepare_only) {
    binding.mask_cached = false;
    return;
  }
  auto *image = binding.mask_image;
  for (int number : set.tiles) {
    const int resolution = set.width(number), height = set.rows(number);
    ImageUser user{};
    user.tile = number;
    auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
    if (!buffer || buffer->x != resolution || buffer->y != height || !buffer->float_data()) {
      if (buffer)
        BKE_image_release_ibuf(image, buffer, nullptr);
      throw std::runtime_error("Mask inspection buffer mismatch");
    }
    auto *data = buffer->float_data_for_write();
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < resolution; ++x) {
        const float value = mask.channel_dependent() ?
                                TextureDocument::mask_value(
                                    set, set.selected_layer, x, y, number) :
                                mask.value(x,
                                           y,
                                           number,
                                           resolution,
                                           height,
                                           set.mesh_maps[2] ? &*set.mesh_maps[2] : nullptr,
                                           &set.mesh_maps);
        const auto i = (std::size_t(y) * resolution + x) * 4;
        data[i] = data[i + 1] = data[i + 2] = value;
        data[i + 3] = 1;
      }
    BKE_image_mark_dirty(image, buffer);
    BKE_image_update_gputexture_delayed(
        image, BKE_image_get_tile(image, number), buffer, 0, 0, resolution, height);
    BKE_image_release_ibuf(image, buffer, nullptr);
  }
  DEG_id_tag_update_ex(&main, &image->id, ID_RECALC_SHADING);
  binding.mask_cached = true;
  binding.mask_hash = hash;
  binding.mask_background = mask.background;
}
bool cycles_shader_channel(const Binding &binding, int channel)
{
  if (channel == 6)
    return binding.displayed_channel == int(InspectionMode::Normal);
  if (channel == 15)
    return binding.displayed_channel == int(InspectionMode::Mask);
  return true;
}
Image *composite_image(Binding &binding, int channel)
{
  return channel < 5   ? binding.images[channel] :
         channel == 5  ? binding.ao_image :
         channel == 6  ? binding.authored_normal_image :
         channel == 15 ? binding.mask_image :
         channel == 16 ? binding.displacement_image :
                         binding.user_images.at(channel - 7);
}
bNode *composite_texture(Binding &binding, int channel)
{
  auto &tree = *binding.material->nodetree;
  return channel < 5   ? binding.textures[channel] :
         channel == 5  ? bke::node_find_node_by_name(tree, "__BTS_AOPixels") :
         channel == 6  ? bke::node_find_node_by_name(tree, "__BTS_AuthoredNormalPixels") :
         channel == 15 ? binding.mask_texture :
         channel == 16 ? binding.displacement_texture :
                         binding.user_textures.at(channel - 7);
}
void image_binding(Main &main, Binding &binding, int channel, Image *image)
{
  auto *texture = composite_texture(binding, channel);
  if (!texture || texture->id == &image->id)
    return;
  auto &tree = *binding.material->nodetree;
  if (texture->id)
    id_us_min(texture->id);
  texture->id = &image->id;
  id_us_plus(texture->id);
  BKE_ntree_update_tag_node_property(&tree, texture);
}
bool restore_preview(Main &main, Binding &binding)
{
  bool changed = false;
  for (int c = 0; c < 17; ++c)
    if (binding.preview_active[c]) {
      if (auto *image = composite_image(binding, c))
        image_binding(main, binding, c, image);
      binding.preview_active[c] = false;
      changed = true;
    }
  binding.edit_channels = {};
  binding.gpu_uniform_values = {};
  binding.gpu_stack_active = {};
  binding.gpu_unbound_waiting = {};
  binding.gpu_stack_set.reset();
  if (changed) {
    auto &tree = *binding.material->nodetree;
    BKE_ntree_update_after_single_tree_change(main, tree);
    DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
  }
  return changed;
}
void finish_cpu_channels(Main &main, Binding &binding, const CompositeChannels &channels)
{
  bool rebound = false;
  for (int c = 0; c < 17; ++c)
    if (channels[c]) {
      if (binding.preview_active[c]) {
        if (auto *image = composite_image(binding, c))
          image_binding(main, binding, c, image);
        binding.preview_active[c] = false;
        rebound = true;
      }
      binding.edit_channels[c] = false;
      binding.gpu_stack_active[c] = false;
      binding.gpu_unbound_waiting[c] = false;
      if (c < 5)
        binding.gpu_uniform_values[c].reset();
    }
  if (rebound) {
    auto &tree = *binding.material->nodetree;
    BKE_ntree_update_after_single_tree_change(main, tree);
    DEG_id_tag_update_ex(&main, &binding.material->id, ID_RECALC_SHADING);
  }
}
// Finishing one dirty tile before upload prevents repeated 256x256 transfers
// when a 64x64 staging boundary falls on another frame.
constexpr int image_update_block = 256;
// One bounded owner-thread block; CPU staging still yields after 3 ms.
bool stage_block(Main &main,
                 Binding &binding,
                 const NativeCompositeResult &result,
                 int number,
                 int c,
                 int x0,
                 int y0)
{
  performance::Scope timing("composite_stage_block");
  property_upload_channel(c);
  auto *image = composite_image(binding, c);
  if (!image)
    return false;
  const auto &source = result.tiles.at(number)[c];
  if (source.empty())
    return false;
  ImageUser user{};
  user.tile = number;
  auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
  if (!buffer || !buffer->float_data() || source.size() != std::size_t(buffer->x) * buffer->y * 4)
  {
    if (buffer)
      BKE_image_release_ibuf(image, buffer, nullptr);
    throw std::runtime_error("Background composite dimensions changed");
  }
  const int x1 = std::min(x0 + image_update_block, buffer->x),
            y1 = std::min(y0 + image_update_block, buffer->y);
  auto *destination = buffer->float_data_for_write();
  bool dirty = false;
  for (int y = y0; y < y1 && !dirty; ++y) {
    const auto offset = (std::size_t(y) * buffer->x + x0) * 4;
    dirty = !std::equal(
        source.begin() + offset, source.begin() + offset + (x1 - x0) * 4, destination + offset);
  }
  if (c == 16) {
    binding.gpu_displacement_presence.reset();
    ++binding.displacement_cpu_epoch;
  }
  if (dirty) {
    if (c == 15)
      binding.mask_cached = false;
    for (int y = y0; y < y1; ++y) {
      const auto offset = (std::size_t(y) * buffer->x + x0) * 4;
      std::copy_n(source.data() + offset, (x1 - x0) * 4, destination + offset);
    }
    if (c == 16) {
      auto &presence = binding.displacement_presence[number];
      if (presence.empty())
        presence.resize(std::size_t(buffer->x) * buffer->y);
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
          const auto index = std::size_t(y) * buffer->x + x;
          const bool content = source[index * 4 + 3] > 0;
          if (content) {
            const float value = source[index * 4] / source[index * 4 + 3];
            binding.displacement_minimum = std::min(binding.displacement_minimum, value);
            binding.displacement_maximum = std::max(binding.displacement_maximum, value);
          }
          if (bool(presence[index]) != content) {
            if (content)
              ++binding.displacement_count;
            else
              --binding.displacement_count;
            presence[index] = content;
          }
        }
    }
    BKE_image_mark_dirty(image, buffer);
    BKE_image_update_gputexture_delayed(
        image, BKE_image_get_tile(image, number), buffer, x0, y0, x1 - x0, y1 - y0);
    DEG_id_tag_update_ex(&main, &image->id, ID_RECALC_SHADING);
  }
  BKE_image_release_ibuf(image, buffer, nullptr);
  return dirty;
}

