// SPDX-License-Identifier: GPL-3.0-or-later
// GpuLayerPreview::Impl: the GPU resources, caches and programs behind the preview. Private to
// GpuLayerPreview.cc, which includes it inside namespace bts.
struct GpuLayerPreview::Impl {
  struct PolygonUndo {
    GpuPolygonRaster::Snapshot pixels;
    std::map<std::uint32_t, std::uint64_t> before, after;
    std::uint64_t used = 0;
  };
  struct Entry {
    Texture image{nullptr, GPU_texture_free};
    int width = 0, height = 0, row = 0, plane = -1;
    bool rejected = false, resource = false;
    std::uint64_t used = 0, patch_bytes = 0;
    std::shared_ptr<const void> residency;
    bool retained() const
    {
      return residency && residency.use_count() > 1;
    }
    std::map<std::uint32_t, std::uint64_t> pages;
    std::map<std::uint64_t, PolygonUndo> polygon_undo;
    std::uint64_t bytes() const
    {
      return (image ? std::uint64_t(width) * height * 16 : 0) + patch_bytes;
    }
  };
  std::map<std::string, Entry> inputs;
  performance::Clock::time_point cache_logged{};
  GpuPaintPages authoring_pages;
  AuthoringCacheBudget input_budget;
  struct EffectEntry {
    Texture image{nullptr, GPU_texture_free};
    std::vector<GpuEffectRow> rows;
  };
  std::map<std::string, EffectEntry> effect_parameters;
  gpu::Texture *effects(const std::string &key, const std::vector<GpuEffectRow> &rows)
  {
    if (rows.empty())
      return zero.get();
    auto &entry = effect_parameters[key];
    if (!entry.image || entry.rows.size() != rows.size()) {
      entry.image = texture("BTS GPU effect parameters",
                            int(GpuEffectRow{}.size()),
                            int(rows.size()),
                            gpu::TextureFormat::SFLOAT_32_32_32_32);
      entry.rows.clear();
    }
    if (!entry.image) {
      input_rejected = true;
      return nullptr;
    }
    if (entry.rows != rows) {
      GPU_texture_update(entry.image.get(), GPU_DATA_FLOAT, rows.data());
      entry.rows = rows;
    }
    return entry.image.get();
  }

  struct ParameterEntry {
    Texture image{nullptr, GPU_texture_free};
    GpuLayerParameters values;
  };
  std::map<std::string, ParameterEntry> layer_parameters;
  gpu::Texture *parameters(const std::string &key, const GpuLayerParameters &values)
  {
    auto &entry = layer_parameters[key];
    bool created = false;
    if (!entry.image) {
      entry.image = texture("BTS GPU layer parameters",
                            GpuLayerParameters{}.values.size(),
                            1,
                            gpu::TextureFormat::SFLOAT_32_32_32_32);
      created = true;
    }
    if (!entry.image)
      return nullptr;
    if (created || entry.values.values != values.values) {
      GPU_texture_update(entry.image.get(), GPU_DATA_FLOAT, values.values.data());
      entry.values = values;
    }
    return entry.image.get();
  }
  struct GeometryEntry {
    Texture image{nullptr, GPU_texture_free};
    std::shared_ptr<const FillSurfaceAtlas> owner;
    int tile = 0, row = 0, width = 0, height = 0, layers = 0;
  };
  std::map<std::pair<const FillSurfaceAtlas *, int>, GeometryEntry> geometry;
  Texture empty_geometry{nullptr, GPU_texture_free};
  gpu::Texture *surface(const std::shared_ptr<const FillSurfaceAtlas> &atlas, int tile)
  {
    performance::Scope timing("gpu_projection_source_upload");
    if (!atlas) {
      if (!empty_geometry) {
        const unsigned int data[4] = {};
        empty_geometry = Texture(GPU_texture_create_2d_array("BTS empty surface",
                                                             1,
                                                             1,
                                                             1,
                                                             1,
                                                             gpu::TextureFormat::UINT_32_32_32_32,
                                                             GPU_TEXTURE_USAGE_GENERAL,
                                                             nullptr),
                                 GPU_texture_free);
        if (empty_geometry)
          GPU_texture_clear(empty_geometry.get(), GPU_DATA_UINT, data);
      }
      return empty_geometry.get();
    }
    const auto &points = atlas->tiles.at(tile);
    const auto bytes = points.size() * sizeof(FillSurfacePoint);
    auto &entry = geometry[{atlas.get(), tile}];
    if (!entry.image) {
      entry.owner = atlas;
      entry.tile = tile;
      entry.width = 1024;
      entry.height = int(
          std::min<std::size_t>(8192, (bytes + entry.width * 16 - 1) / (entry.width * 16)));
      entry.layers = int((bytes + std::size_t(entry.width) * entry.height * 16 - 1) /
                         (std::size_t(entry.width) * entry.height * 16));
      entry.image = Texture(GPU_texture_create_2d_array("BTS shared projection surface",
                                                        entry.width,
                                                        entry.height,
                                                        entry.layers,
                                                        1,
                                                        gpu::TextureFormat::UINT_32_32_32_32,
                                                        GPU_TEXTURE_USAGE_GENERAL,
                                                        nullptr),
                            GPU_texture_free);
      if (!entry.image) {
        input_rejected = true;
        return nullptr;
      }
      std::printf("BTS_GPU_SURFACE_ALLOC tile=%d bytes=%zu shared_channels=1\n", tile, bytes);
    }
    const auto *data = reinterpret_cast<const unsigned char *>(points.data());
    const std::size_t row_bytes = entry.width * 16;
    while (std::size_t(entry.row) * row_bytes < bytes) {
      if (!upload_time_available())
        return nullptr;
      const int rows = std::min({64,
                                 entry.height - entry.row % entry.height,
                                 int(remaining / row_bytes),
                                 int((bytes - entry.row * row_bytes) / row_bytes)});
      if (!rows) {
        if (remaining < row_bytes)
          return nullptr;
        std::vector<unsigned char> tail(row_bytes);
        std::memcpy(tail.data(), data + entry.row * row_bytes, bytes - entry.row * row_bytes);
        GPU_texture_update_sub(entry.image.get(),
                               GPU_DATA_UINT,
                               tail.data(),
                               0,
                               entry.row % entry.height,
                               entry.row / entry.height,
                               entry.width,
                               1,
                               1);
        remaining -= row_bytes;
        ++entry.row;
        break;
      }
      GPU_texture_update_sub(entry.image.get(),
                             GPU_DATA_UINT,
                             data + entry.row * row_bytes,
                             0,
                             entry.row % entry.height,
                             entry.row / entry.height,
                             entry.width,
                             rows,
                             1);
      remaining -= rows * row_bytes;
      entry.row += rows;
    }
    return entry.image.get();
  }

  std::array<int, 2> oracle_position{24, 24};
  struct Query {
    std::string root;
    bool enabled = false;
    QUuid owner;
    std::set<std::string> nodes;
  };
  struct Stack {
    std::map<std::string, QUuid> node_owners;
    std::map<std::string, Query> queries;
    std::set<std::string> queried_sources;
    std::map<std::string, std::map<std::uint64_t, GpuStackPage>> node_versions;
    std::uint64_t node_clock = 0;
    Texture coverage{nullptr, GPU_texture_free}, height_image{nullptr, GPU_texture_free};
    gpu::Texture *head = nullptr;
    std::string head_id;
    GpuStackPlan plan;
    std::map<std::string, std::shared_ptr<gpu::Texture>> outputs;
    int width = 0, height = 0;
    std::uint64_t last_used = 0, revision = 0;
    std::set<int> output_dirty, height_dirty, last_changes;
  };
  std::map<std::string, GpuComparisonInput> comparison_memo;
  std::map<std::string, GpuAnchorInput> anchor_memo;
  std::map<std::string, Stack> stacks, mask_stacks;
  GpuRangeSummary ranges;
  GpuHistogram histograms;
  GpuMaterialSample material_samples;
  GpuThumbnail thumbnails;
  GpuLayerCapture layer_capture;
  std::map<std::uint32_t, std::uint64_t> displacement_epochs;
  std::uint64_t mask_dispatch_texels = 0;
  std::map<std::pair<std::uintptr_t, std::array<int, 3>>, std::string> target_owners;
  struct Margins {
    std::map<int, std::shared_ptr<const GpuMarginPlan>> extend, adjacent;
  };
  std::map<std::uint32_t, Margins> margin_plans;
  GpuMarginExecutor margins;
  GpuGeometryMask geometry_masks;
  struct DerivedEntry {
    GpuDerivedDirty tracking;
    Texture presence{nullptr, GPU_texture_free};
    std::map<int, Texture> dirty_images;
  };
  struct DerivedUpdate {
    GpuDerivedDirty tracking;
    GpuDerivedDirty::Chunks dirty;
    std::map<int, std::vector<std::uint32_t>> flags;
    bool needed = false;
  };
  std::map<std::string, DerivedEntry> derived;
  bool image_changed = false;
  std::size_t last_derived_chunks = 0, last_derived_total = 0;
  std::string derived_key(const TextureSet &set, int channel) const
  {
    return set.id.toString().toStdString() + ":derived:" + std::to_string(channel);
  }
  DerivedUpdate derived_changes(const TextureSet &set,
                                int channel,
                                gpu::Texture *target,
                                const std::map<int, std::array<int, 3>> &regions,
                                int output_tile = 0)
  {
    const auto key = derived_key(set, channel);
    auto &entry = derived[key];
    DerivedUpdate update;
    update.tracking = entry.tracking;
    std::string parameters;
    const auto append = [&](const auto &v) {
      parameters.append(reinterpret_cast<const char *>(&v), sizeof(v));
    };
    append(set.channels);
    append(set.ao_channel);
    append(set.ao_mixing);
    append(set.normal_mixing);
    append(set.normal_format);
    append(set.bump_method);
    append(set.bump_origin);
    append(set.padding_3d);
    for (int c : {3, 4, 5})
      append(set.precision[c]);
    append(gpu_channel_base(set, 4));
    append(gpu_channel_base(set, 5));
    append(gpu_channel_base(set, 6));
    append(reinterpret_cast<std::uintptr_t>(target));
    append(output_tile);
    GpuDerivedDirty::Sizes sizes;
    bool different_target = false;
    const auto &baked = set.mesh_maps[channel == 3 ? 0 : 3];
    for (const auto &[tile, offset] : regions) {
      sizes[tile] = {set.width(tile), set.rows(tile)};
      append(tile);
      append(offset);
      append(sizes[tile]);
      if (baked)
        if (auto found = baked->tiles.find(tile);
            found != baked->tiles.end() && found->second.pixels)
          parameters += found->second.pixels->hash;
      different_target |=
          target_owners[std::pair{reinterpret_cast<std::uintptr_t>(target), offset}] != key;
      if (auto found = margin_plans.find(set.material_session_uid); found != margin_plans.end()) {
        append(reinterpret_cast<std::uintptr_t>(found->second.extend.at(tile).get()));
        append(reinterpret_cast<std::uintptr_t>(found->second.adjacent.at(tile).get()));
      }
    }
    const bool full = different_target || parameters != entry.tracking.parameters;
    update.tracking.parameters = std::move(parameters);
    for (const auto &[tile, size] : sizes) {
      const int source = channel == 3 ? 6 : 5;
      auto &stack = stacks.at(set.id.toString().toStdString() + ":" + std::to_string(source) +
                              ":" + std::to_string(tile));
      auto dirty = update.tracking.changed(
          source, tile, stack.revision, stack.last_changes, size[0], size[1], full);
      update.dirty[tile].insert(dirty.begin(), dirty.end());
      if (channel == 3 && set.channels[4]) {
        auto &height = stacks.at(set.id.toString().toStdString() + ":4:" + std::to_string(tile));
        auto height_dirty = update.tracking.changed(
            4, tile, height.revision, height.last_changes, size[0], size[1], full);
        const GpuMarginPlan *plan = nullptr;
        if (auto found = margin_plans.find(set.material_session_uid); found != margin_plans.end())
          plan = (set.padding_3d ? found->second.adjacent : found->second.extend).at(tile).get();
        GpuDerivedDirty::normal_halo(
            update.dirty, tile, GpuDerivedDirty::padded(std::move(height_dirty), plan), sizes);
      }
    }
    for (const auto &[tile, size] : sizes) {
      auto &flags = update.flags[tile];
      flags.resize(((size[0] + 255) / 256) * ((size[1] + 255) / 256));
      for (int chunk : update.dirty[tile])
        flags[chunk] = 1;
      update.needed |= !update.dirty[tile].empty();
    }
    return update;
  }
  bool prepare_derived_masks(DerivedEntry &entry,
                             const DerivedUpdate &update,
                             const TextureSet &set)
  {
    if (!entry.presence) {
      entry.presence = texture(
          "BTS cached derived presence", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!entry.presence)
        return false;
      const float zero[4] = {};
      GPU_texture_clear(entry.presence.get(), GPU_DATA_FLOAT, zero);
    }
    for (const auto &[tile, flags] : update.flags) {
      const int w = (set.width(tile) + 255) / 256, h = (set.rows(tile) + 255) / 256;
      auto &image = entry.dirty_images.try_emplace(tile, nullptr, GPU_texture_free).first->second;
      if (!image || GPU_texture_width(image.get()) != w || GPU_texture_height(image.get()) != h)
        image = texture("BTS derived dirty chunks", w, h, gpu::TextureFormat::UINT_32);
      if (!image)
        return false;
      GPU_texture_update(image.get(), GPU_DATA_UINT, flags.data());
    }
    return true;
  }
  void commit_derived(const TextureSet &set,
                      int channel,
                      gpu::Texture *target,
                      const std::map<int, std::array<int, 3>> &regions,
                      DerivedUpdate update)
  {
    const auto key = derived_key(set, channel);
    derived[key].tracking = std::move(update.tracking);
    for (const auto &[tile, offset] : regions)
      target_owners[std::pair{reinterpret_cast<std::uintptr_t>(target), offset}] = key;
    image_changed = true;
    std::size_t dirty_chunks = 0;
    for (const auto &[tile, dirty] : update.dirty)
      dirty_chunks += dirty.size();
    last_derived_chunks = dirty_chunks;
    last_derived_total = 0;
    for (const auto &[tile, flags] : update.flags)
      last_derived_total += flags.size();
    std::printf("BTS_GPU_DERIVED_DIRTY channel=%d chunks=%zu tiles=%zu global_presence_on_gpu=1\n",
                channel,
                dirty_chunks,
                regions.size());
  }
  struct AdjustmentEntry {
    std::weak_ptr<const AdjustmentPaintSnapshot> source;
    TextureSet set;
    QUuid layer;
    std::vector<std::string> native_aliases;
    std::map<std::tuple<QUuid, int, int>, NativeMaterialGpuContext> contexts;
    std::string failure;
  };
  std::map<QUuid, AdjustmentEntry> adjustment_sources;
  GpuAdjustmentPages adjustment_pages;
  Texture export_final{nullptr, GPU_texture_free};
  Texture world_weight{nullptr, GPU_texture_free};
  Shader world_weight_shader{nullptr, GPU_shader_free};
  Shader world_apply_shader{nullptr, GPU_shader_free};
  std::array<Shader, 2> world_publish_shaders{Shader{nullptr, GPU_shader_free},
                                              Shader{nullptr, GPU_shader_free}};
  std::map<std::tuple<QUuid, QUuid, int>, std::shared_ptr<gpu::Texture>> native_normals;
  std::map<QUuid, std::string> native_normal_errors;
  std::uint64_t native_normal_revision = 0;
  std::string native_image_failure;
  NativeMaterialGpuLibrary native_materials;
  NativeMaterialPreparationQueue native_preparations{native_materials};
  NativeContextProvider native_context_provider;
  Texture native_source{nullptr, GPU_texture_free};
  std::uint64_t last_dispatch_texels = 0, last_cached_nodes = 0, last_active_nodes = 0;
  std::uint64_t frozen_prefix_copies = 0, frozen_prefix_copy_texels = 0;

  std::map<gpu::TextureFormat, Shader> uniform_shaders;
  bool uniform_failed = false;
  Texture scratch[2]{{nullptr, GPU_texture_free}, {nullptr, GPU_texture_free}},
      final_image{nullptr, GPU_texture_free}, zero{nullptr, GPU_texture_free};
  Shader coverage_shader{nullptr, GPU_shader_free}, presence_shader{nullptr, GPU_shader_free};
  Texture ao_presence{nullptr, GPU_texture_free};
  std::map<std::pair<gpu::TextureFormat, bool>, Shader> ao_shaders, normal_shaders;
  struct NeighborEntry {
    Texture image{nullptr, GPU_texture_free};
    std::array<std::array<float, 4>, 9> values{};
  };
  std::map<std::string, NeighborEntry> normal_neighbors;
  Shader layer_shader{nullptr, GPU_shader_free}, mask_shader{nullptr, GPU_shader_free},
      clear_shader{nullptr, GPU_shader_free};
  std::map<std::pair<gpu::TextureFormat, bool>, Shader> final_shaders, mask_output_shaders;
  gpu::Shader *final_shader = nullptr;
  int width = 0, height = 0;
  bool final_array = false;
  gpu::TextureFormat final_format = gpu::TextureFormat::SFLOAT_16_16_16_16;
  performance::Clock::time_point upload_start{};
  bool upload_time_available() const
  {
    return testing_stack || performance::ms(upload_start) < 4;
  }
  bool authoring_time_available() const
  {
    return testing_stack || performance::ms(upload_start) < 8;
  }
  std::uint64_t reported_authoring_pages = 0;
  std::set<std::string> adjustment_input_pins;
  std::uint64_t remaining = upload_budget, remaining_work = 32ULL * 1024 * 1024, used_bytes = 0,
                clock = 0, resource_allocations = 0;
  bool failed = false, input_rejected = false;
  using PreparedProgram =
      std::unique_ptr<GPUHostedShaderCompilation, decltype(&GPU_shader_hosted_compilation_free)>;
  PreparedProgram pending_layer{nullptr, GPU_shader_hosted_compilation_free},
      pending_mask{nullptr, GPU_shader_hosted_compilation_free};
  GpuProgramWarmup program_state;
  bool startup_prepared = false;
  struct DerivedProgram {
    PreparedProgram pending{nullptr, GPU_shader_hosted_compilation_free};
  };
  std::array<DerivedProgram, 10> derived_programs;
  GpuProgramWarmup derived_program_state{10};
  bool derived_programs_started = false;
  static std::pair<gpu::TextureFormat, bool> derived_key(int index)
  {
    return {index < 6 ? gpu::TextureFormat::SFLOAT_16_16_16_16 :
                        gpu::TextureFormat::SFLOAT_32_32_32_32,
            ((index - 2) / 2) % 2 != 0};
  }
  Shader &derived_output(int index)
  {
    if (index == 0)
      return coverage_shader;
    if (index == 1)
      return presence_shader;
    return (index % 2 ? normal_shaders : ao_shaders).at(derived_key(index));
  }
  bool poll_derived_program(int index)
  {
    auto &pending = derived_programs[index].pending;
    if (!pending || !GPU_shader_hosted_compilation_ready(pending.get()))
      return true;
    Shader completed{GPU_shader_hosted_compilation_finalize(pending.get()), GPU_shader_free};
    pending.reset();
    const bool okay = bool(completed);
    if (okay)
      derived_output(index) = std::move(completed);
    return okay;
  }
  bool programs_pending() const
  {
    return authoring_pages.programs_pending() || bool(pending_layer) || bool(pending_mask) ||
           std::any_of(derived_programs.begin(), derived_programs.end(), [](const auto &program) {
             return bool(program.pending);
           });
  }
  bool has_gpu() const
  {
    return adjustment_pages.has_gpu() || authoring_pages.programs_started() ||
           programs_pending() || native_materials.statistics().resources ||
           !uniform_shaders.empty() || bool(layer_shader) || bool(mask_shader) ||
           bool(coverage_shader) || bool(presence_shader) ||
           std::any_of(ao_shaders.begin(),
                       ao_shaders.end(),
                       [](const auto &entry) { return bool(entry.second); }) ||
           std::any_of(normal_shaders.begin(),
                       normal_shaders.end(),
                       [](const auto &entry) { return bool(entry.second); }) ||
           bool(scratch[0]) || !inputs.empty() || !stacks.empty();
  }
  bool poll_program(int index)
  {
    auto &pending = index == 0 ? pending_layer : pending_mask;
    auto &result = index == 0 ? layer_shader : mask_shader;
    if (!pending || !GPU_shader_hosted_compilation_ready(pending.get()))
      return true;
    Shader completed{GPU_shader_hosted_compilation_finalize(pending.get()), GPU_shader_free};
    pending.reset();  // Handle consumed before freeing its prepared sources.
    const bool okay = bool(completed);
    if (okay)
      result = std::move(completed);
    return okay;
  }
  void poll_programs()
  {
    program_state.drain([&](int index) { return poll_program(index); });
    if (derived_programs_started)
      derived_program_state.drain([&](int index) { return poll_derived_program(index); });
    authoring_pages.poll_programs();
  }
  void cancel_programs()
  {
    pending_layer.reset();
    pending_mask.reset();
    for (auto &program : derived_programs)
      program.pending.reset();
    authoring_pages.cancel_programs();
  }  // Terminal owner cleanup only.
  bool use_program_workers() const
  {
    const auto *opt_in = std::getenv("BTS_HOSTED_SHADER_WORKERS");
    // Fixed-frame diagnostic self-tests retain their existing synchronous contract.
    return opt_in && std::string_view(opt_in) == "1" && !testing_stack &&
           !GPU_use_main_context_workaround();
  }
  GpuPreviewUpdate prepare_programs()
  {
    // Poll first even when resource/compile admission has already failed.
    poll_programs();
    if (failed && !programs_pending())
      return GpuPreviewUpdate::Unsupported;
    const bool asynchronous = use_program_workers();
    const auto state = program_state.prepare(
        [&](int index) {
          const auto format = index == 0 ? gpu::TextureFormat::SFLOAT_32_32_32_32 :
                                           gpu::TextureFormat::SFLOAT_32;
          if (asynchronous) {
            auto &pending = index == 0 ? pending_layer : pending_mask;
            auto info = layer_info(format);
            gpu_derived::stabilize_hosted_resource_owners(info);
            pending.reset(
                GPU_shader_hosted_python_async(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
            if (!pending)
              throw std::runtime_error("Hosted GPU stack worker submission unavailable");
            std::printf(
                "BTS_GPU_STACK_PROGRAM_SUBMITTED output=%s route=THREAD python_preprocessed=1\n",
                index == 0 ? "RGBA" : "mask");
          }
          else {
            auto &result = index == 0 ? layer_shader : mask_shader;
            result = compile_layer(format);
          }
        },
        [&](int index) { return poll_program(index); },
        [&](int index) { return bool(index == 0 ? layer_shader : mask_shader); },
        [&](int index) { return bool(index == 0 ? pending_layer : pending_mask); });
    // Submit the ten measured derived programs alongside the layer/mask pair.
    // All prepared CreateInfos are owned by their existing compiler handles;
    // readiness and ordinary retirement never wait or cancel running workers.
    auto derived_state = GpuProgramWarmup::State::Ready;
    if (asynchronous || derived_programs_started) {
      if (!derived_programs_started) {
        for (int index = 2; index < 10; index += 2) {
          ao_shaders.try_emplace(derived_key(index), nullptr, GPU_shader_free);
          normal_shaders.try_emplace(derived_key(index), nullptr, GPU_shader_free);
        }
        derived_programs_started = true;
      }
      derived_state = derived_program_state.prepare(
          [&](int index) {
            auto info = [&] {
              if (index == 0)
                return gpu_derived::coverage_info();
              if (index == 1)
                return gpu_derived::presence_info();
              const auto [format, array] = derived_key(index);
              return index % 2 ? gpu_derived::normal_info(format, array, quant_source) :
                                 gpu_derived::ao_info(format, array, quant_source);
            }();
            auto &pending = derived_programs[index].pending;
            gpu_derived::stabilize_hosted_resource_owners(info);
            pending.reset(
                GPU_shader_hosted_python_async(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
            if (!pending)
              throw std::runtime_error("Hosted derived shader submission unavailable at slot " +
                                       std::to_string(index));
            std::printf(
                "BTS_GPU_DERIVED_PROGRAM_SUBMITTED slot=%d route=THREAD python_preprocessed=1\n",
                index);
          },
          [&](int index) { return poll_derived_program(index); },
          [&](int index) { return bool(derived_output(index)); },
          [&](int index) { return bool(derived_programs[index].pending); });
    }
    auto authoring_state = GpuProgramWarmup::State::Ready;
    if (asynchronous || authoring_pages.programs_started())
      authoring_state = authoring_pages.prepare_worker_programs();
    if (state == GpuProgramWarmup::State::Pending ||
        derived_state == GpuProgramWarmup::State::Pending ||
        authoring_state == GpuProgramWarmup::State::Pending)
      return GpuPreviewUpdate::Pending;
    if (state == GpuProgramWarmup::State::Failed ||
        derived_state == GpuProgramWarmup::State::Failed ||
        authoring_state == GpuProgramWarmup::State::Failed)
    {
      failed = true;
      return GpuPreviewUpdate::Unsupported;
    }
    return GpuPreviewUpdate::Updated;
  }
  GpuPreviewUpdate initialize(int w, int h, gpu::TextureFormat format, bool array)
  {
    performance::Scope timing("gpu_shader_prepare");
    if (const auto state = prepare_programs(); state != GpuPreviewUpdate::Updated)
      return state;
    if (!zero) {
      zero = texture("BTS preview zero", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!zero) {
        failed = true;
        return GpuPreviewUpdate::Unsupported;
      }
      const float empty[4] = {};
      GPU_texture_clear(zero.get(), GPU_DATA_FLOAT, empty);
      GPU_texture_filter_mode(zero.get(), true);
    }
    width = w;
    height = h;
    const auto key = std::pair{format, array};
    if (!final_shaders.contains(key))
      final_shaders.emplace(key, compile_final(format, array));
    final_shader = final_shaders.at(key).get();
    if (!final_shader) {
      failed = true;
      return GpuPreviewUpdate::Unsupported;
    }
    return GpuPreviewUpdate::Updated;
  }
  Entry *entry(const std::string &key,
               int w,
               int h,
               bool resource,
               const std::set<std::string> &protected_keys)
  {
    performance::Scope timing("gpu_source_allocate_or_find");
    if (auto found = inputs.find(key); found != inputs.end()) {
      if (found->second.width == w && found->second.height == h &&
          found->second.resource == resource && !found->second.rejected)
      {
        found->second.used = clock;
        return &found->second;
      }
      if (found->second.retained())
        throw std::logic_error("Retained GPU source identity changed");
      used_bytes -= found->second.bytes();
      inputs.erase(found);
    }
    const auto bytes = std::uint64_t(w) * h * 16;
    const auto cache_budget = input_budget.update(
        texture_vram_query_generation.load(),
        used_bytes,
        texture_free_vram.load(),
        texture_vram_query_valid.load() || texture_free_vram.load() != 0,
        texture_vram_query_valid.load() ? 0 : texture_available_ram_bytes());
    while (used_bytes + bytes > cache_budget) {
      auto victim = inputs.end();
      for (auto i = inputs.begin(); i != inputs.end(); ++i)
        if (!i->second.retained() && !protected_keys.contains(i->first) &&
            !adjustment_input_pins.contains(i->first) &&
            (victim == inputs.end() || i->second.used < victim->second.used))
          victim = i;
      if (victim == inputs.end()) {
        break;
      }
      used_bytes -= victim->second.bytes();
      inputs.erase(victim);
    }
    auto [found, inserted] = inputs.try_emplace(key);
    auto &e = found->second;
    e.width = w;
    e.height = h;
    e.resource = resource;
    e.used = clock;
    if (resource)
      e.residency = std::make_shared<const char>(0);
    e.image = texture("BTS cached preview source", w, h, gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!e.image) {
      texture_memory_refresh_needed = true;
      e.rejected = true;
      input_rejected = true;
      return &e;
    }
    used_bytes += bytes;
    if (resource) {
      ++resource_allocations;
      std::printf("BTS_GPU_RESOURCE_ALLOC key=%s bytes=%llu count=%llu\n",
                  key.c_str(),
                  (unsigned long long)bytes,
                  (unsigned long long)resource_allocations);
    }
    if (!resource) {
      performance::Scope timing("gpu_paint_clear");
      if (!clear_shader)
        clear_shader = compile_uniform(gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!clear_shader) {
        e.rejected = input_rejected = true;
        return &e;
      }
      const float empty[4] = {};
      GPU_shader_bind(clear_shader.get());
      GPU_shader_uniform_4fv(clear_shader.get(), "fill_value", empty);
      GPU_texture_image_bind(e.image.get(), 0);
      GPU_compute_dispatch(clear_shader.get(), (w + 15) / 16, (h + 15) / 16, 1);
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_TEXTURE_UPDATE);
      GPU_texture_image_unbind(e.image.get());
      GPU_shader_unbind();
    }
    GPU_texture_filter_mode(e.image.get(), true);
    GPU_texture_extend_mode(
        e.image.get(), resource ? GPU_SAMPLER_EXTEND_MODE_REPEAT : GPU_SAMPLER_EXTEND_MODE_EXTEND);
    return &e;
  }
  gpu::Texture *paint(const std::string &key,
                      const PaintPixels &pixels,
                      int channel,
                      const std::set<std::string> &protected_keys,
                      int polygon_depth = 0)
  {
    performance::Scope timing("gpu_paint_source_upload");
    PaintMetadataScope metadata;
    if (!pixels.page_count(channel))
      return zero.get();
    auto *e = entry(key, pixels.resolution(), pixels.resolution(), false, protected_keys);
    if (!e || e->rejected) {
      input_rejected = true;
      return nullptr;
    }
    e->plane = channel;
    PaintPixels::PageVersions versions_storage, base_storage, before_storage;
    const auto &wanted_versions = pixels.page_versions(versions_storage)[channel];
    if (e->pages == wanted_versions)
      return e->image.get();
    auto &polygon = authoring_pages.polygon_raster();
    for (auto &[revision, patch] : e->polygon_undo)
      if (patch.after == e->pages && patch.before == wanted_versions) {
        performance::Scope timing("gpu_polygon_undo");
        polygon.restore(e->image.get(), patch.pixels);
        e->pages = wanted_versions;
        patch.used = ++clock;
        std::printf("BTS_GPU_POLYGON_UNDO pages=%d bytes=%llu cpu_raster=0\n",
                    patch.pixels.count,
                    (unsigned long long)patch.pixels.bytes);
        return e->image.get();
      }
    const auto &root = pixels.recipe();
    const auto ink = root ? root->ink.recipe() : nullptr;
    if (polygon_depth < 32 && root && root->kind == PaintRecipe::Kind::Over &&
        root->complete_stroke && !root->erase && ink && ink->kind == PaintRecipe::Kind::Stamp &&
        ink->base.page_versions(base_storage)[channel].empty() &&
        (root->before.resolution() == 0 || root->before.resolution() == pixels.resolution()) &&
        GpuPolygonRaster::supported(ink->stamp))
    {
      const auto &before_versions = root->before.page_versions(before_storage)[channel];
      if (!before_versions.empty() && e->pages != before_versions) {
        auto protect = protected_keys;
        protect.insert(key);
        if (!paint(key, root->before, channel, protect, polygon_depth + 1))
          return nullptr;
      }
      if (!authoring_time_available())
        return nullptr;
      if (before_versions.empty() && !e->pages.empty()) {
        std::vector<std::uint32_t> stale;
        for (const auto &[page, version] : e->pages)
          stale.push_back(page);
        polygon.clear(e->image.get(), stale);
        e->pages.clear();
      }
      if (e->pages == before_versions) {
        const auto changed = GpuPolygonRaster::changed_pages(before_versions, wanted_versions);
        if (!before_versions.empty() && !changed.empty() &&
            !e->polygon_undo.contains(root->revision))
        {
          const auto columns = std::uint64_t(std::ceil(std::sqrt(double(changed.size()))));
          const auto bytes = columns * 64 * ((changed.size() + columns - 1) / columns) * 64 * 16 +
                             changed.size() * 4;
          const auto budget = source_cache_budget(used_bytes);
          while (used_bytes + bytes > budget) {
            Entry *victim = nullptr;
            std::uint64_t id = 0, oldest = ~std::uint64_t(0);
            for (auto &[name, entry] : inputs)
              for (auto &[rev, patch] : entry.polygon_undo)
                if (patch.used < oldest) {
                  victim = &entry;
                  id = rev;
                  oldest = patch.used;
                }
            if (!victim)
              break;
            const auto retired = victim->polygon_undo.at(id).pixels.bytes;
            victim->patch_bytes -= retired;
            used_bytes -= retired;
            victim->polygon_undo.erase(id);
          }
          if (used_bytes + bytes <= budget) {
            PolygonUndo patch;
            patch.pixels = polygon.capture(e->image.get(), changed);
            patch.before = before_versions;
            patch.after = wanted_versions;
            patch.used = ++clock;
            e->patch_bytes += patch.pixels.bytes;
            used_bytes += patch.pixels.bytes;
            e->polygon_undo.emplace(root->revision, std::move(patch));
          }
          else
            std::fprintf(
                stderr,
                "BTS_GPU_POLYGON_UNDO_CACHE skipped_bytes=%llu available_budget=%llu; reproducible GPU recipes retained\n",
                (unsigned long long)bytes,
                (unsigned long long)budget);
        }
        polygon.paint(
            e->image.get(), ink->stamp, channel, pixels.resolution(), changed, root->opacity);
        e->pages = wanted_versions;
        std::printf(
            "BTS_GPU_POLYGON_RASTER channel=%d pages=%zu triangles=%zu cpu_raster=0 full_image_copy=0\n",
            channel,
            changed.size(),
            ink->stamp.surface->triangles.size());
        return e->image.get();
      }
    }
    bool complete = true;
    std::set<std::uint32_t> wanted;
    if (pixels.recipe()) {
      for (const auto &[page, version] : wanted_versions) {
        wanted.insert(page);
        if (e->pages.contains(page) && e->pages.at(page) == version)
          continue;
        if (!authoring_time_available()) {
          complete = false;
          continue;
        }
        if (!authoring_pages.write(e->image.get(),
                                   pixels,
                                   channel,
                                   page,
                                   testing_stack ? performance::Clock::time_point::max() :
                                                   upload_start + std::chrono::milliseconds(8)))
        {
          complete = false;
          continue;
        }
        e->pages[page] = version;
      }
    }
    else
      pixels.visit_versioned_pages(
          channel, [&](auto page, auto version, std::span<const PaintPixels::Pixel> data) {
            wanted.insert(page);
            if (e->pages.contains(page) && e->pages.at(page) == version)
              return;
            const auto bytes = data.size_bytes();
            if (bytes > remaining) {
              complete = false;
              return;
            }
            const int x = int(page % 65536) * 64, y = int(page / 65536) * 64;
            GPU_texture_update_sub(
                e->image.get(), GPU_DATA_FLOAT, data.data(), x, y, 0, 64, 64, 1);
            remaining -= bytes;
            e->pages[page] = version;
          });
    static const std::array<PaintPixels::Pixel, 64 * 64> empty{};
    for (auto i = e->pages.begin(); i != e->pages.end();) {
      if (wanted.contains(i->first)) {
        ++i;
        continue;
      }
      if (sizeof(empty) > remaining) {
        complete = false;
        ++i;
        continue;
      }
      GPU_texture_update_sub(e->image.get(),
                             GPU_DATA_FLOAT,
                             empty.data(),
                             int(i->first % 65536) * 64,
                             int(i->first / 65536) * 64,
                             0,
                             64,
                             64,
                             1);
      remaining -= sizeof(empty);
      i = e->pages.erase(i);
    }
    return complete ? e->image.get() : nullptr;
  }
  gpu::Texture *resource(const FillTexture &source, const std::set<std::string> &protected_keys)
  {
    performance::Scope timing("gpu_resource_source_upload");
    auto *e = entry(resource_key(source), source.width, source.height, true, protected_keys);
    if (!e || e->rejected || !source.pixels) {
      input_rejected = true;
      return nullptr;
    }
    while (e->row < e->height) {
      if (!upload_time_available())
        return nullptr;
      const int rows = std::min(
          {64, e->height - e->row, int(remaining / (std::uint64_t(e->width) * 16))});
      if (!rows)
        return nullptr;
      // Decode workers already validate every component. Upload the immutable
      // shared storage directly, with no copy/finite scan/premultiply on the UI.
      const auto *values = source.pixels->data() + std::size_t(e->row) * e->width;
      GPU_texture_update_sub(
          e->image.get(), GPU_DATA_FLOAT, values, 0, e->row, 0, e->width, rows, 1);
      remaining -= std::uint64_t(e->width) * rows * sizeof(FillTexture::Pixel);
      e->row += rows;
    }
    return e->image.get();
  }
  gpu::Texture *baked(const MeshMapPlane *plane, const std::set<std::string> &protected_keys)
  {
    if (!plane || !plane->pixels)
      return zero.get();
    const auto &pixels = *plane->pixels;
    const auto key = "baked:" + pixels.hash + ":" + std::to_string(pixels.resolution) + ":" +
                     std::to_string(pixels.height);
    auto *e = entry(key, pixels.resolution, pixels.height, true, protected_keys);
    if (!e || e->rejected)
      return nullptr;
    GPU_texture_extend_mode(e->image.get(), GPU_SAMPLER_EXTEND_MODE_EXTEND);
    while (e->row < e->height) {
      if (!upload_time_available())
        return nullptr;
      const int rows = std::min(
          {64, e->height - e->row, int(remaining / (std::uint64_t(e->width) * 16))});
      if (!rows)
        return nullptr;
      GPU_texture_update_sub(e->image.get(),
                             GPU_DATA_FLOAT,
                             pixels.rgba.data() + std::size_t(e->row) * e->width * 4,
                             0,
                             e->row,
                             0,
                             e->width,
                             rows,
                             1);
      remaining -= std::uint64_t(e->width) * rows * 16;
      e->row += rows;
    }
    return e->image.get();
  }

  gpu::Texture *mask_prefix(const std::string &key,
                            const std::vector<GpuMaskStage> &stages,
                            const std::array<gpu::Texture *, 6> &maps,
                            int tile,
                            int w,
                            int h,
                            const std::set<std::string> &protected_keys,
                            const std::string &map_signature,
                            const GpuComparisonResolver &resolve,
                            const GpuAnchorResolver &resolve_anchor,
                            bool partial = false);
};
