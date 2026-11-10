// SPDX-License-Identifier: GPL-3.0-or-later
// struct MaterialInspection::Impl: bindings, the GPU preview and the native image queues.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
struct MaterialInspection::Impl {
  NativeCyclesImages cycles;
  NativeFragmentGeometryProvider world_geometry_provider;
  NativeFragmentObjectProvider world_object_provider;
  void invalidate_world_geometry(Binding &binding)
  {
    binding.world_context.invalidate_owners();
    binding.world_ao_lease.reset();
    binding.world_ao_tile = 0;
    if (binding.world_geometry_job)
      binding.world_geometry_job->cancelled = true;
    binding.world_geometry_job.reset();
    binding.world_geometry.reset();
    binding.world_published_signatures.clear();
    binding.world_tiles.clear();
    binding.world_set.reset();
    ++binding.world_geometry_revision;
  }
  bool observe_world_objects(Binding &binding, std::uint32_t material)
  {
    return binding.world_object_context.observe(world_object_provider, material);
  }
  bool cycles_entry_waiting = false, cycles_error_reported = false, cycles_entry_requested = false,
       cycles_preparation_failed = false;
  std::string cycles_preparation_error;
  bool subsurface_enabled = false, gpu_mips_pending = false;
  bool cpu_pending() const
  {
    return job.valid() || !queued.empty() || pending.has_value();
  }
  std::map<std::uint32_t, std::string> failures;
  std::map<std::uint32_t, CompositeChannels> failed_channels;
  std::unordered_map<std::uint32_t, Binding> bindings;
  std::vector<Image *> retired_visibility_images;
  struct Request {
    TextureSet set;
    std::shared_ptr<TexturePadding> padding;
    std::uint64_t generation;
    bool ao;
    CompositeChannels channels = complete_composite_channels;
    bool layer_edit = false;
    PropertyTrace trace;
    performance::Clock::time_point enqueued = performance::Clock::now();
    bool finalize = true;
  };
  std::map<std::uint32_t, std::uint64_t> generation;
  std::map<std::uint32_t, Request> queued;
  std::future<std::optional<NativeCompositeResult>> job;
  std::shared_ptr<std::atomic_bool> cancel;
  std::uint32_t running_uid = 0;
  std::optional<Request> running;
  struct Pending {
    NativeCompositeResult result;
    std::vector<int> tiles;
    std::size_t tile = 0;
    int channel = 0, x = 0, y = 0;
    bool layer_edit = false;
    Pending() = default;
    Pending(Pending &&) = default;
    Pending &operator=(Pending &&) = default;
    ~Pending()
    {
      // Plain CPU vectors only: their large virtual mappings need not be
      // unmapped inside the owner event when staging or cancellation finishes.
      if (!result.tiles.empty())
        QThreadPool::globalInstance()->start([pixels = std::move(result.tiles)]() mutable {
          performance::Scope timing("composite_free_cpu_buffers");
          pixels.clear();
        });
    }
  };
  std::optional<Pending> pending;
  struct GpuUniformRequest {
    std::uint64_t generation;
    std::array<std::optional<std::array<float, 4>>, 5> values;
    PropertyTrace trace;
    performance::Clock::time_point enqueued = performance::Clock::now();
  };
  std::map<std::uint32_t, GpuUniformRequest> gpu_queued;
  struct LayerCaptureJob {
    TextureSet set;
    LayerCaptureRequest request;
    std::uint64_t ticket;
  };
  std::optional<LayerCaptureJob> layer_capture_job;
  std::optional<LayerCaptureResult> layer_capture_failure;
  std::uint64_t layer_capture_ticket = 0;
  struct ThumbnailJob {
    TextureSet set;
    ThumbnailRequest request;
    std::uint64_t ticket;
  };
  std::optional<ThumbnailJob> thumbnail_job;
  std::optional<ThumbnailMetadata> thumbnail_failure;
  std::uint64_t thumbnail_ticket = 0;
  struct MaterialSampleJob {
    TextureSet set;
    MaterialSampleRequest request;
    std::uint64_t ticket;
  };
  std::optional<MaterialSampleJob> material_sample_job;
  std::optional<MaterialSample> material_sample_failure;
  std::uint64_t material_sample_ticket = 0;
  struct HistogramJob {
    TextureSet set;
    HistogramRequest request;
    std::uint64_t ticket;
  };
  std::optional<HistogramJob> histogram_job;
  std::optional<std::pair<QUuid, HistogramMetadata>> histogram_failure;
  std::uint64_t histogram_ticket = 0;
  std::vector<std::shared_ptr<const FillTexture>> preparing_textures;
  QUuid texture_preparation_owner;
  bool texture_upload_waiting = false;
  std::string texture_upload_error;
  GpuLayerPreview gpu_preview;
  MaterialLayeringSources layering_sources;
  bool layering_sources_pending = false;
  bool gpu_displacement_changed = false;
  std::map<std::uint32_t, Request> gpu_stack_queued;
  // Native chains stay vectors until explicit export. Each Fill is rasterized on
  // the actual mesh into target-UV tangent space, then the ordinary GPU layer
  // compositor applies masks, folders, blend modes and opacity.
  void queue_world_normal(Binding &binding, const TextureSet &source, bool view_only = false)
  {
    auto &library = gpu_preview.native_world_sources();
    TextureSet set = source;
    set.projection_tiles = binding.projection_cache;
    set.geometry_ownership = binding.ownership_cache;
    set.geometry_names = binding.geometry_names;
    if (!binding.projection_cache.empty())
      set.projection_geometry = binding.projection_cache.begin()->second;
    std::vector<QUuid> perturbed;
    for (const auto &layer : evaluation_layers(set))
      if (layer.fill.native_material && library.contains(layer.fill.material_resource) &&
          library.world_normal_perturbed(layer.fill.material_resource))
        perturbed.push_back(layer.id);
    const auto plan = native_world_normal_plan(set, perturbed);
    // Retire the former single-layer world override, including restored projects.
    if (binding.world_normal_enabled) {
      auto &socket = output(*binding.world_normal_enabled, "Value");
      if (static_cast<const bNodeSocketValueFloat *>(socket.default_value)->value != 0) {
        scalar(*binding.material->nodetree, socket, 0);
        BKE_ntree_update_after_single_tree_change(*binding.owner_main,
                                                  *binding.material->nodetree);
        DEG_id_tag_update_ex(binding.owner_main, &binding.material->id, ID_RECALC_SHADING);
      }
    }
    if (binding.world_images_full) {
      for (int side = 0; side < 2; ++side) {
        native_world_normal_placeholder(*binding.world_normal_images[side]);
      }
      binding.world_images_full = false;
      binding.world_scratch = {};
    }
    gpu_preview.clear_native_normals(set.id);
    binding.world_layers_ready.clear();
    binding.world_tiles.clear();
    binding.world_set.reset();
    binding.world_error.clear();
    binding.world_tile = 0;
    if (!plan.eligible) {
      if (binding.world_geometry_job)
        binding.world_geometry_job->cancelled = true;
      binding.world_geometry_job.reset();
      binding.world_error = plan.reason.toStdString();
      if (!binding.world_error.empty())
        gpu_preview.set_native_normal_error(source.id, binding.world_error);
      return;
    }
    try {
      if (!world_geometry_provider)
        throw std::runtime_error("Native Normal geometry owner is not ready");
      NativeFragmentGeometryRequest request;
      request.material_uid = set.material_session_uid;
      request.tiles.assign(set.tiles.begin(), set.tiles.end());
      request.target_uv = {NativeFragmentUvKind::DefaultRender, {}};
      request.uv_requests = {{NativeFragmentUvKind::DefaultRender, {}},
                             {NativeFragmentUvKind::DisplayActive, {}}};
      std::set<std::string> uvs;
      bool needs_ao = false;
      for (const auto &part : plan.contributions) {
        if (library.world_normal_context_state(part.resource) ==
            NativeWorldNormalContextState::Unsupported)
          throw std::runtime_error(library.world_normal_error(part.resource).toStdString());
        const auto uv = library.world_normal_uv(part.resource);
        if (!uv.empty() && uvs.insert(uv).second)
          request.uv_requests.push_back({NativeFragmentUvKind::Named, uv});
        request.generated_stream |= library.world_normal_requires_generated(part.resource);
        needs_ao |= library.world_normal_requires_baked_ao(part.resource);
      }
      request.allow_original_orco = request.generated_stream;
      std::string layout = std::to_string(request.generated_stream);
      for (const auto &uv : uvs)
        layout += ';' + uv;
      const bool objects_changed = observe_world_objects(binding, set.material_session_uid);
      if (!binding.world_object_context.error().empty())
        throw std::runtime_error(binding.world_object_context.error());
      if (objects_changed || layout != binding.world_source_uv) {
        if (binding.world_geometry_job)
          binding.world_geometry_job->cancelled = true;
        binding.world_geometry_job.reset();
        binding.world_geometry.reset();
        ++binding.world_geometry_revision;
      }
      binding.world_source_uv = layout;
      binding.world_generated = request.generated_stream;
      request.revision = binding.world_geometry_revision;
      binding.world_ao = NativeMaterialBakedAoContext::capture(
          set.mesh_maps, request.tiles, needs_ao);
      binding.world_ao_lease.reset();
      binding.world_ao_tile = 0;
      if (!binding.world_geometry && !binding.world_geometry_job) {
        const std::atomic_bool cancel = false;
        binding.world_geometry_job = NativeWorldNormalGeometryJob::start(
            world_geometry_provider(std::move(request), cancel));
      }
      binding.world_set = std::move(set);
      binding.world_tiles.assign(source.tiles.begin(), source.tiles.end());
      // Force derived shading refresh also for edits to source-only parameters.
      auto found = gpu_stack_queued.find(source.material_session_uid);
      if (found != gpu_stack_queued.end()) {
        found->second.channels[3] = true;
        found->second.channels[6] = true;
      }
      else {
        Request refresh{source,
                        binding.padding,
                        generation[source.material_session_uid],
                        bool(binding.ao_image)};
        refresh.channels = {};
        refresh.channels[3] = refresh.channels[6] = true;
        gpu_stack_queued.emplace(source.material_session_uid, std::move(refresh));
      }
    }
    catch (const std::exception &error) {
      binding.world_error = error.what();
      pending_composite_error = "Native Normal unavailable: " + binding.world_error;
      if (binding.mask_snapshot)
        gpu_preview.set_native_normal_error(binding.mask_snapshot->id, pending_composite_error);
      binding.world_set.reset();
      binding.world_tiles.clear();
      if (binding.world_geometry_job)
        binding.world_geometry_job->cancelled = true;
      binding.world_geometry_job.reset();
    }
  }
  bool pump_world_normal(Binding &binding)
  {
    const auto uid = binding.material->id.session_uid;
    const bool changed = binding.world_objects_dirty ||
                         binding.world_queued_generation != generation[uid] ||
                         binding.world_registry_generation !=
                             gpu_preview.native_world_sources().source_generation();
    if (binding.mask_snapshot && (changed || binding.world_view_dirty)) {
      binding.world_view_dirty = false;
      binding.world_objects_dirty = false;
      binding.world_queued_generation = generation[uid];
      binding.world_registry_generation = gpu_preview.native_world_sources().source_generation();
      queue_world_normal(binding, *binding.mask_snapshot);
    }
    if (!binding.world_set)
      return false;
    try {
      if (binding.world_geometry_job) {
        auto job = binding.world_geometry_job;
        if (!job->ready.load(std::memory_order_acquire))
          return false;
        if (!job->error.empty())
          throw std::runtime_error(job->error);
        if (!job->geometry)
          throw std::runtime_error("Native Normal geometry cancelled");
        binding.world_geometry = std::make_unique<NativeMaterialFragmentGpuGeometry>(
            job->geometry);
        binding.world_geometry_job.reset();
      }
      if (!binding.world_geometry || binding.world_tile >= binding.world_tiles.size())
        return false;
      auto &set = *binding.world_set;
      auto &library = gpu_preview.native_world_sources();
      std::vector<QUuid> ids;
      const auto &layers = evaluation_layers(set);
      for (const auto &layer : layers)
        if (layer.fill.native_material && library.contains(layer.fill.material_resource) &&
            library.world_normal_perturbed(layer.fill.material_resource))
          ids.push_back(layer.id);
      const auto plan = native_world_normal_plan(set, ids);
      const int tile = binding.world_tiles[binding.world_tile];
      // Render all contributions for this tile; the compositor waits for every source.
      for (const auto &part : plan.contributions) {
        if (binding.world_layers_ready.contains({part.layer, tile}))
          continue;
        const auto layer = std::find_if(
            layers.begin(), layers.end(), [&](const auto &l) { return l.id == part.layer; });
        std::optional<NativeMaterialViewContext> view;
        if (library.world_normal_requires_view(part.resource))
          view = gpu_preview.native_world_view(set, *layer, tile);
        const auto *ao_context = library.world_normal_requires_baked_ao(part.resource) ?
                                     &binding.world_ao :
                                     nullptr;
        if (!library.prepare_world_normal(part.resource,
                                          &binding.world_geometry->source(),
                                          view ? &*view : nullptr,
                                          ao_context,
                                          true))
        {
          if (library.world_normal_context_state(part.resource) ==
              NativeWorldNormalContextState::Unsupported)
            throw std::runtime_error(library.world_normal_error(part.resource).toStdString());
          return false;
        }
        std::optional<NativeBakedAoInput> ao;
        if (ao_context) {
          const auto &plane = binding.world_ao.plane(tile);
          if (binding.world_ao_tile != tile) {
            binding.world_ao_lease.reset();
            binding.world_ao_tile = tile;
          }
          const auto ready = gpu_preview.prepare_baked_resource(
              tile, plane, binding.world_ao_lease);
          if (ready == GpuPreviewUpdate::Pending)
            return false;
          if (ready != GpuPreviewUpdate::Updated)
            throw std::runtime_error("Native Normal AO upload failed");
          ao = NativeBakedAoInput{tile, &plane, &binding.world_ao_lease};
        }
        const auto atlas = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                                 set.projection_geometry;
        if (fill_surface_projection(layer->fill) && !atlas)
          return false;
        const auto projection = native_material_projection(layer->fill, atlas.get(), set);

        std::shared_ptr<gpu::Texture> target(
            GPU_texture_create_2d("Native tangent Normal layer",
                                  set.width(tile),
                                  set.rows(tile),
                                  1,
                                  gpu::TextureFormat::SFLOAT_32_32_32_32,
                                  GPU_TEXTURE_USAGE_GENERAL,
                                  nullptr),
            GPU_texture_free);
        if (!target)
          throw std::bad_alloc();
        library.render_world_normal(part.resource,
                                    *layer->fill.native_material,
                                    *binding.world_geometry,
                                    tile,
                                    target.get(),
                                    nullptr,
                                    library.world_normal_uv(part.resource),
                                    view ? &*view : nullptr,
                                    ao ? &*ao : nullptr,
                                    true,
                                    projection.get());
        gpu_preview.set_native_normal(set.id, part.layer, tile, std::move(target));
        binding.world_layers_ready.insert({part.layer, tile});
        ++binding.world_rendered_tiles;
      }
      binding.world_geometry_diagnostics = binding.world_geometry->diagnostics();
      ++binding.world_tile;
      if (binding.world_tile == binding.world_tiles.size())
        ++binding.world_publication_generation;
      return true;
    }
    catch (const std::exception &error) {
      binding.world_error = error.what();
      pending_composite_error = "Native Normal unavailable: " + binding.world_error;
      if (binding.mask_snapshot)
        gpu_preview.set_native_normal_error(binding.mask_snapshot->id, pending_composite_error);
      binding.world_set.reset();
      binding.world_tiles.clear();
      binding.world_geometry_job.reset();
      return false;
    }
  }

  bool prepare_layer_sources(bool allow_upload, bool frame_started);
  void collect_projection_geometry()
  {
    for (auto &[uid, binding] : bindings)
      if (binding.margin_job && binding.margin_job->ready.load(std::memory_order_acquire)) {
        auto job = std::move(binding.margin_job);
        if (job->cancelled || job->source != binding.padding || job->sizes != binding.tile_sizes)
          continue;
        if (!job->error.empty()) {
          failures[uid] = job->error;
          pending_composite_error = "Margin geometry failed: " + job->error;
          gpu_stack_queued.erase(uid);
          std::fprintf(stderr, "BTS_GPU_MARGIN_FAILED uid=%u %s\n", uid, job->error.c_str());
          continue;
        }
        gpu_preview.set_padding_geometry(uid, job->extend, job->adjacent);
        binding.margins_ready = true;
        std::printf("BTS_GPU_MARGIN_READY uid=%u tiles=%zu layer_composite_cpu=0\n",
                    uid,
                    job->extend.size());
      }
    for (auto &[uid, binding] : bindings)
      if (binding.projection_job && binding.projection_job->ready.load(std::memory_order_acquire))
      {
        auto job = std::move(binding.projection_job);
        if (job->cancelled || job->source != binding.padding || job->sizes != binding.tile_sizes)
          continue;
        if (!job->error.empty()) {
          failures[uid] = job->error;
          pending_composite_error = "Projection geometry failed: " + job->error;
          gpu_stack_queued.erase(uid);
          std::fprintf(stderr, "BTS_GPU_GEOMETRY_FAILED uid=%u %s\n", uid, job->error.c_str());
          continue;
        }
        binding.projection_cache = std::move(job->result);
        binding.ownership_cache = std::move(job->ownership);
        binding.geometry_names = std::move(job->names);
        binding.geometry_upload_pending = true;
        const auto adopt = [&](const TextureSet &set) {
          set.projection_tiles = binding.projection_cache;
          set.geometry_ownership = binding.ownership_cache;
          set.geometry_names = binding.geometry_names;
          if (!binding.projection_cache.empty())
            set.projection_geometry = binding.projection_cache.begin()->second;
        };
        if (auto request = gpu_stack_queued.find(uid); request != gpu_stack_queued.end())
          adopt(request->second.set);
        if (auto request = queued.find(uid); request != queued.end())
          adopt(request->second.set);
        if (binding.gpu_stack_set)
          adopt(*binding.gpu_stack_set);
        if (binding.mask_snapshot)
          adopt(*binding.mask_snapshot);
        if (binding.world_set)
          adopt(*binding.world_set);
        binding.world_queued_generation = ~std::uint64_t(0);
        std::printf("BTS_GPU_GEOMETRY_READY uid=%u tiles=%zu layer_composite_cpu=0\n",
                    uid,
                    binding.projection_cache.size());
      }
  }
  void invalidate(std::uint32_t uid)
  {
    gpu_queued.erase(uid);
    gpu_stack_queued.erase(uid);
    if (auto found = bindings.find(uid); found != bindings.end()) {
      found->second.mask_async_layer = QUuid();
      found->second.gpu_uniform_values = {};
      found->second.gpu_stack_active = {};
      found->second.gpu_unbound_waiting = {};
      found->second.gpu_stack_set.reset();
    }
    if (pending && pending->result.uid == uid)
      pending.reset();
    ++generation[uid];
    queued.erase(uid);
    if (job.valid() && running_uid == uid && cancel)
      cancel->store(true);
  }
};
