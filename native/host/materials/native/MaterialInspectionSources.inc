// SPDX-License-Identifier: GPL-3.0-or-later
// GPU channel resume, layer sources, staging-image uploads and Cycles image sync.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
void MaterialInspection::resume_gpu_channels()
{
  // Called on the GPU owner after drawing: a view switch may have caused EEVEE
  // to bind a previously invisible channel for the first time. Keep its latest
  // authored snapshot and resume GPU work; never build a hidden CPU composite.
  for (auto &[uid, binding] : impl_->bindings) {
    if (!binding.gpu_stack_set)
      continue;
    for (int c = 0; c < 17; ++c)
      if (binding.gpu_unbound_waiting[c]) {
        auto *image = composite_image(binding, c);
        if (!image)
          continue;
        const auto native = BKE_image_gpu_override_texture(image);
        if (!native.texture || !*native.texture)
          continue;
        auto found = impl_->gpu_stack_queued.find(uid);
        if (found == impl_->gpu_stack_queued.end()) {
          Impl::Request request{*binding.gpu_stack_set,
                                binding.padding,
                                impl_->generation[uid],
                                bool(binding.ao_image)};
          request.channels = {};
          request.layer_edit = true;
          request.finalize = false;
          found = impl_->gpu_stack_queued.emplace(uid, std::move(request)).first;
        }
        found->second.channels[c] = true;
        binding.gpu_unbound_waiting[c] = false;
        std::printf("BTS_GPU_CHANNEL_RESUME channel=%d cpu_composite=0\n", c);
      }
  }
}
void MaterialInspection::validate_material_layer_sources(const TextureDocument &document,
                                                         bool require_ready) const
{
  for (const auto &set : document.texture_sets())
    if (set.material_layering.enabled) {
      bool sourced = false;
      for (const auto &input : set.material_layering.materials)
        for (const auto &source : input.textures)
          sourced |= bool(source);
      if (!sourced)
        continue;
      if (set.disabled || !set.uniform_editable)
        throw std::invalid_argument(
            "Packed reusable sources require an enabled editable Texture Set");
      const auto found = impl_->bindings.find(set.material_session_uid);
      if (found == impl_->bindings.end() || !found->second.owner_main || !found->second.material ||
          !found->second.resolution)
        throw std::invalid_argument(
            "Packed reusable source Texture Set has no prepared native channel binding");
      const auto &model = found->second.material_layering;
      if (!model.enabled)
        throw std::invalid_argument("Packed reusable source model was not restored");
      for (int input = 0; input < 4; ++input)
        for (int channel = 0; channel < 6; ++channel)
          if (model.materials[input].textures[channel] !=
              set.material_layering.materials[input].textures[channel])
            throw std::invalid_argument(
                "Packed reusable source binding differs from its typed document");
      if (require_ready && !impl_->layering_sources.images(set.material_layering))
        throw std::invalid_argument("Packed reusable source GPU aliases are incomplete");
    }
}
bool MaterialInspection::Impl::prepare_layer_sources(bool allow_upload, bool frame_started)
{
  std::vector<const MaterialLayering *> models;
  Main *main = nullptr;
  for (const auto &[uid, binding] : this->bindings)
    if (binding.material_layering.enabled && !binding.resolution)
      for (const auto &input : binding.material_layering.materials)
        for (const auto &source : input.textures)
          if (source)
            throw std::invalid_argument(
                "Reusable source maps require prepared native channel targets before source staging");
  for (auto &[uid, binding] : this->bindings)
    if (binding.resolution || !binding.material_layering.enabled) {
      models.push_back(&binding.material_layering);
      if (main && main != binding.owner_main)
        throw std::logic_error("Reusable sources span different Main owners");
      main = binding.owner_main;
    }
  bool ready = std::all_of(models.begin(), models.end(), [&](const auto *model) {
    return bool(this->layering_sources.images(*model));
  });
  if (!ready && main && allow_upload) {
    if (!frame_started)
      this->gpu_preview.begin_frame();
    ready = this->layering_sources.prepare(*main, this->gpu_preview, models);
  }
  this->layering_sources_pending = !ready;
  if (ready) {
    for (auto &[uid, binding] : this->bindings)
      if (binding.resolution || !binding.material_layering.enabled) {
        const auto images = this->layering_sources.images(binding.material_layering);
        binding.layered_shader.apply(*binding.owner_main,
                                     *binding.material,
                                     binding.material_layering,
                                     binding.shader_parameters.normal_intensity,
                                     binding.shader_parameters.ao_intensity,
                                     binding.layering_directx,
                                     *images);
      }
    this->layering_sources.retain_only(models);
  }
  return ready;
}
bool MaterialInspection::prepare_material_layer_sources(bool allow_upload)
{
  return impl_->prepare_layer_sources(allow_upload, false);
}
void MaterialInspection::upload_staging_images(bool allow_gpu_compute, bool input_active)
{
  performance::Scope timing_all("upload_staging_images");
  impl_->collect_projection_geometry();
  if (allow_gpu_compute && !input_active)
    impl_->gpu_preview.step_native_preparations();
  // Disabled private world inputs are neutral placeholders, not completed
  // authored images. The full-resolution layered normal is mirrored through
  // composite channel 3; never register these 1x1 sentinels as Cycles products.
  for (auto &[uid, binding] : impl_->bindings)
    for (int side = 0; side < 2; ++side)
      if (auto *image = binding.world_normal_images[side]) {
        const auto target = BKE_image_gpu_override_texture(image);
        if (!target.texture || !*target.texture) {
          native_world_normal_placeholder(*image);
          binding.world_images_full = false;
          if (binding.world_normal_enabled)
            scalar(*binding.material->nodetree, output(*binding.world_normal_enabled, "Value"), 0);
        }
      }
  for (auto &[uid, binding] : impl_->bindings) {
    const bool busy = input_active || impl_->queued.contains(uid) ||
                      impl_->gpu_queued.contains(uid) || impl_->gpu_stack_queued.contains(uid) ||
                      (impl_->pending && impl_->pending->result.uid == uid) ||
                      (impl_->job.valid() && impl_->running_uid == uid);
    for (int c = 0; c < 17; ++c)
      if (auto *image = composite_image(binding, c))
        BKE_image_gpu_suspend_mipmaps(image, busy);
  }
  for (auto &[uid, binding] : impl_->bindings) {
    for (auto *image : binding.images)
      prepare_native_gpu_image(image);
    prepare_native_gpu_image(binding.ao_image);
    prepare_native_gpu_image(binding.displacement_image);
    if (binding.displayed_channel == int(InspectionMode::Normal))
      prepare_native_gpu_image(binding.authored_normal_image);
    if (binding.displayed_channel == int(InspectionMode::Mask))
      prepare_native_gpu_image(binding.mask_image);
    for (auto *image : binding.user_images)
      prepare_native_gpu_image(image);
  }
  // Ordinary frame budgets must reset even when no reusable sources exist.
  impl_->gpu_preview.begin_frame();
  // Isolated Open starts its own frame; this upload has already started one.
  impl_->prepare_layer_sources(allow_gpu_compute && !input_active, true);
  if (allow_gpu_compute) {

    for (auto &[uid, request] : impl_->gpu_queued) {
      auto found = impl_->bindings.find(uid);
      if (found == impl_->bindings.end() || impl_->generation[uid] != request.generation)
        continue;
      auto &binding = found->second;
      performance::record({performance::flow(),
                           "gpu_uniform_queue",
                           0,
                           0,
                           performance::ms(request.enqueued),
                           true});
      bool rebound = false;
      int channels = 0;
      const auto started = PropertyClock::now();
      performance::Scope timing("gpu_uniform_submit");
      for (int c : {0, 1, 2, 4})
        if (request.values[c]) {
          const auto lookup_start = PropertyClock::now();
          auto *image = binding.images[c];
          auto textures = BKE_image_gpu_override_texture(image);
          auto *texture = textures.texture ? *textures.texture : nullptr;
          const double lookup_ms = property_ms(lookup_start);
          if (texture && GpuProjectReadback::protect_for_write(textures.texture))
            texture = *textures.texture;
          if (!texture || (!BKE_image_gpu_residency_limit(image) &&
                           (GPU_texture_width(texture) != binding.tile_sizes.at(1001).first ||
                            GPU_texture_height(texture) != binding.tile_sizes.at(1001).second)))
          {
            binding.gpu_uniform_values[c].reset();
            continue;
          }
          const auto format = GPU_texture_format(texture);
          auto value = *request.values[c];
          if (format == gpu::TextureFormat::SFLOAT_16_16_16_16) {
            for (auto &v : value)
              v = math::half_to_float(math::float_to_half_make_finite(v));
          }
          else if (format != gpu::TextureFormat::SFLOAT_32_32_32_32) {
            binding.gpu_uniform_values[c].reset();
            continue;
          }
          if (binding.preview_active[c]) {
            image_binding(*binding.owner_main, binding, c, image);
            binding.preview_active[c] = false;
            rebound = true;
          }
          const auto clear_start = PropertyClock::now();
          if (!impl_->gpu_preview.uniform(texture, value)) {
            binding.gpu_uniform_values[c].reset();
            continue;
          }
          BKE_image_gpu_override_updated(image);
          impl_->cycles.changed(*image, uid, c);
          const double clear_ms = property_ms(clear_start);
          const double mip_ms = 0;
          GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
          property_upload_channel(c);
          ++channels;
          if (lookup_ms + clear_ms + mip_ms > 8 || std::getenv("BTS_GPU_FILL_DETAILS"))
            std::printf(
                "BTS_GPU_FILL_PHASE channel=%d lookup_ms=%.3f broadcast_ms=%.3f mip_ms=%.3f\n",
                c,
                lookup_ms,
                clear_ms,
                mip_ms);
          // Test only: production presentation has no GPU-to-CPU readback.
          static std::set<std::pair<std::uint32_t, int>> verified;
          if (std::getenv("BTS_VERIFY_GPU_UNIFORM") && verified.insert({uid, c}).second) {
            auto *pixels = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
            const auto count = std::size_t(GPU_texture_width(texture)) *
                               GPU_texture_height(texture);
            bool exact = true;
            for (std::size_t i = 0; i < count && exact; ++i)
              for (int k = 0; k < 4; ++k)
                exact &= std::isfinite(pixels[i * 4 + k]) &&
                         std::abs(pixels[i * 4 + k] - value[k]) <=
                             .001f * std::max(1.f, std::abs(value[k]));
            MEM_delete(pixels);
            if (!exact)
              throw std::runtime_error("GPU uniform preview differs from CPU reference");
            std::printf("BTS_GPU_UNIFORM_VERIFIED channel=%d texels=%zu\n", c, count);
          }
        }
      if (rebound) {
        auto &tree = *binding.material->nodetree;
        BKE_ntree_update_after_single_tree_change(*binding.owner_main, tree);
        DEG_id_tag_update_ex(binding.owner_main, &binding.material->id, ID_RECALC_SHADING);
      }
      if (channels) {
        auto trace = request.trace;
        trace.preview = true;
        property_uploaded(trace, property_ms(started));
        std::printf("BTS_GPU_FILL channels=%d cpu_submit_ms=%.3f host_upload_bytes=0\n",
                    channels,
                    property_ms(started));
      }
    }
    impl_->gpu_queued.clear();
  }
  else if (!impl_->gpu_queued.empty())
    performance::record({performance::flow(), "gpu_compute_deferred", 0, 0, 0, true});

  if (allow_gpu_compute)
    for (auto &[uid, binding] : impl_->bindings)
      if (binding.gpu_inspection_waiting &&
          binding.displayed_channel == int(InspectionMode::Normal) && binding.gpu_stack_set)
      {
        if (auto found = impl_->gpu_stack_queued.find(uid); found != impl_->gpu_stack_queued.end())
          found->second.channels[6] = true;
        else {
          Impl::Request request{*binding.gpu_stack_set,
                                binding.padding,
                                impl_->generation[uid],
                                bool(binding.ao_image)};
          request.channels = {};
          request.channels[6] = true;
          request.layer_edit = true;
          request.finalize = false;
          impl_->gpu_stack_queued.emplace(uid, std::move(request));
        }
      }
  if (allow_gpu_compute)
    for (auto i = impl_->gpu_stack_queued.begin(); i != impl_->gpu_stack_queued.end();) {
      const auto uid = i->first;
      auto &request = i->second;
      auto found = impl_->bindings.find(uid);
      if (found == impl_->bindings.end() || impl_->generation[uid] != request.generation) {
        i = impl_->gpu_stack_queued.erase(i);
        continue;
      }
      auto &binding = found->second;
      bool pending = false, rebound = false, changed = false;
      const auto started = PropertyClock::now();
      if (!binding.gpu_stack_set || binding.gpu_snapshot_generation != request.generation) {
        binding.gpu_stack_set = request.set;
        binding.gpu_snapshot_generation = request.generation;
      }
      performance::record(
          {performance::flow(), "gpu_stack_queue", 0, 0, performance::ms(request.enqueued), true});
      for (int c = 0; c < 17; ++c)
        if (request.channels[c]) {
          auto textures = BKE_image_gpu_override_texture(composite_image(binding, c));
          auto *texture = textures.texture ? *textures.texture : nullptr;
          if (texture && GpuProjectReadback::protect_for_write(textures.texture))
            texture = *textures.texture;
          // Displacement needs presence metadata before its shader is connected.
          // Allocate its canonical target once, without a CPU composite placeholder.
          if (!texture && c == 16) {
            ImageUser user{};
            user.tile = *request.set.tiles.begin();
            textures = BKE_image_get_gpu_material_texture(
                composite_image(binding, c), &user, true);
            texture = textures.texture ? *textures.texture : nullptr;
          }
          // Unbound inspection images need no low-resolution replacement and no
          // node-tree retag. When selected, let EEVEE allocate once, then fill it.
          if (!texture && c == 6) {
            if (binding.displayed_channel == int(InspectionMode::Normal)) {
              pending = true;
              continue;
            }
            binding.gpu_stack_active[c] = true;
            binding.gpu_inspection_waiting = true;
            binding.edit_channels[c] = false;
            request.channels[c] = false;
            continue;
          }
          if (!texture) {
            binding.gpu_unbound_waiting[c] = true;
            binding.gpu_stack_active[c] = true;
            binding.edit_channels[c] = false;
            request.channels[c] = false;
            std::printf(
                "BTS_GPU_CHANNEL_DEFER channel=%d reason=not-shader-bound cpu_composite=0\n", c);
            continue;
          }
          auto result = GpuPreviewUpdate::Updated;
          if (c == 16)
            impl_->gpu_preview.set_displacement_epoch(uid, binding.displacement_cpu_epoch);
          if (texture) {
            std::map<int, std::array<int, 3>> regions;
            for (int tile : request.set.tiles) {
              ImageGpuTileRegion region;
              if (!BKE_image_gpu_override_tile_region(composite_image(binding, c), tile, region) ||
                  region.width != request.set.width(tile) ||
                  region.height != request.set.rows(tile))
              {
                result = GpuPreviewUpdate::Unsupported;
                break;
              }
              regions[tile] = {region.x, region.y, region.layer};
            }
            if (result != GpuPreviewUpdate::Unsupported) {
              auto active = request.set;
              if (active.residency_view) {
                active.tiles = active.loaded_tiles();
                std::erase_if(regions,
                              [&](const auto &entry) { return !active.tile_loaded(entry.first); });
              }
              if (!active.tiles.empty())
                result = impl_->gpu_preview.update_tiles(active, c, texture, regions);
            }
          }
          if (result == GpuPreviewUpdate::Pending) {
            pending = true;
            continue;
          }
          request.channels[c] = false;
          if (result != GpuPreviewUpdate::Updated) {
            const auto native_error = impl_->gpu_preview.native_image_error();
            pending_composite_error =
                !native_error.empty() ?
                    native_error :
                pending_adjustment_failure ?
                    pending_adjustment_failure->message :
                    "GPU layer target allocation failed; retaining the previous image (CPU fallback disabled)";
            impl_->failures[uid] = pending_composite_error;
            impl_->failed_channels[uid][c] = true;
            std::fprintf(
                stderr,
                "BTS_GPU_STACK_ERROR uid=%u channel=%d reason=unsupported-target-or-allocation detail=%s cpu_fallback=0\n",
                uid,
                c,
                pending_composite_error.c_str());
            continue;
          }
          if (impl_->gpu_preview.wrote_image()) {
            BKE_image_gpu_override_updated(composite_image(binding, c));
            if (cycles_shader_channel(binding, c))
              impl_->cycles.changed(*composite_image(binding, c), uid, c);
          }
          binding.gpu_stack_active[c] = true;
          binding.edit_channels[c] = false;
          if (c == 6)
            binding.gpu_inspection_waiting = false;
          if (binding.preview_active[c]) {
            image_binding(*binding.owner_main, binding, c, composite_image(binding, c));
            binding.preview_active[c] = false;
            rebound = true;
          }
          if (impl_->gpu_preview.wrote_image()) {
            property_upload_channel(c);
            changed = true;
            if (performance::enabled())
              std::printf("BTS_GPU_CHANNEL_PUBLISH channel=%d generation=%llu queue_ms=%.3f\n",
                          c,
                          (unsigned long long)request.generation,
                          performance::ms(request.enqueued));
          }
        }
      if (rebound) {
        auto &tree = *binding.material->nodetree;
        BKE_ntree_update_after_single_tree_change(*binding.owner_main, tree);
        DEG_id_tag_update_ex(binding.owner_main, &binding.material->id, ID_RECALC_SHADING);
      }
      if (changed) {
        auto trace = request.trace;
        trace.preview = true;
        property_uploaded(trace, property_ms(started));
      }
      if (pending)
        ++i;
      else {
        if (request.finalize) {
          binding.final_generation = request.generation;
          auto trace = request.trace;
          trace.preview = false;
          property_uploaded(trace, 0);
        }
        i = impl_->gpu_stack_queued.erase(i);
      }
    }
  if (allow_gpu_compute)
    for (auto &[uid, binding] : impl_->bindings)
      impl_->pump_world_normal(binding);
  if (allow_gpu_compute)
    for (auto &[uid, binding] : impl_->bindings)
      if (binding.geometry_upload_pending)
        binding.geometry_upload_pending = !impl_->gpu_preview.prefetch_geometry(
            binding.projection_cache);
  if (allow_gpu_compute && impl_->material_sample_job) {
    auto &job = *impl_->material_sample_job;
    auto found = impl_->bindings.find(job.set.material_session_uid);
    if (found == impl_->bindings.end()) {
      MaterialSample failed;
      failed.ticket = job.ticket;
      failed.valid = false;
      impl_->material_sample_failure = failed;
      impl_->material_sample_job.reset();
    }
    else {
      auto &binding = found->second;
      job.set.projection_tiles = binding.projection_cache;
      job.set.geometry_ownership = binding.ownership_cache;
      job.set.geometry_names = binding.geometry_names;
      if (!binding.projection_cache.empty())
        job.set.projection_geometry = binding.projection_cache.begin()->second;
      const auto result = impl_->gpu_preview.sample_material(job.set, job.request, job.ticket);
      if (result != GpuPreviewUpdate::Pending) {
        if (result == GpuPreviewUpdate::Unsupported) {
          MaterialSample failed;
          failed.ticket = job.ticket;
          failed.valid = false;
          impl_->material_sample_failure = failed;
          std::fprintf(stderr,
                       "BTS_GPU_MATERIAL_PICK_FAILED ticket=%llu\n",
                       (unsigned long long)job.ticket);
        }
        impl_->material_sample_job.reset();
      }
    }
  }
  if (allow_gpu_compute && impl_->layer_capture_job) {
    auto &job = *impl_->layer_capture_job;
    auto found = impl_->bindings.find(job.set.material_session_uid);
    if (found == impl_->bindings.end()) {
      impl_->layer_capture_failure = LayerCaptureResult{job.ticket, false, {}};
      impl_->layer_capture_job.reset();
    }
    else {
      auto &binding = found->second;
      if (binding.padding) {
        std::map<int, std::pair<int, int>> sizes;
        for (int tile : job.set.tiles)
          sizes[tile] = {job.set.width(tile), job.set.rows(tile)};
        if (binding.tile_sizes.empty())
          binding.tile_sizes = sizes;
        if (!binding.margin_job && !binding.margins_ready)
          binding.margin_job = MarginGeometryJob::start(binding.padding, sizes);
        bool complete = binding.ownership_cache.size() == sizes.size();
        for (const auto &[tile, size] : sizes) {
          const auto it = binding.projection_cache.find(tile);
          complete &= it != binding.projection_cache.end() &&
                      it->second->resolution == size.first && it->second->height == size.second;
        }
        if (!complete && !binding.projection_job)
          binding.projection_job = ProjectionGeometryJob::start(binding.padding, sizes);
      }
      if (!binding.projection_cache.empty()) {
        job.set.projection_tiles = binding.projection_cache;
        job.set.projection_geometry = binding.projection_cache.begin()->second;
      }
      if (!binding.ownership_cache.empty())
        job.set.geometry_ownership = binding.ownership_cache;
      if (!binding.geometry_names.empty())
        job.set.geometry_names = binding.geometry_names;
      const auto result = impl_->gpu_preview.capture_layer(job.set, job.request, job.ticket);
      if (result != GpuPreviewUpdate::Pending) {
        if (result == GpuPreviewUpdate::Unsupported) {
          impl_->layer_capture_failure = LayerCaptureResult{job.ticket, false, {}};
          std::fprintf(stderr,
                       "BTS_GPU_LAYER_CAPTURE_FAILED layer=%s\n",
                       job.request.layer.toString().toStdString().c_str());
        }
        impl_->layer_capture_job.reset();
      }
    }
  }
  if (allow_gpu_compute && impl_->thumbnail_job) {
    auto &job = *impl_->thumbnail_job;
    auto found = impl_->bindings.find(job.set.material_session_uid);
    if (found == impl_->bindings.end()) {
      ThumbnailMetadata failed;
      failed.ticket = job.ticket;
      failed.valid = false;
      impl_->thumbnail_failure = failed;
      impl_->thumbnail_job.reset();
    }
    else {
      auto &binding = found->second;
      job.set.projection_tiles = binding.projection_cache;
      job.set.geometry_ownership = binding.ownership_cache;
      job.set.geometry_names = binding.geometry_names;
      if (!binding.projection_cache.empty())
        job.set.projection_geometry = binding.projection_cache.begin()->second;
      const auto result = impl_->gpu_preview.thumbnail(job.set, job.request, job.ticket);
      if (result != GpuPreviewUpdate::Pending) {
        if (result == GpuPreviewUpdate::Unsupported) {
          ThumbnailMetadata failed;
          failed.ticket = job.ticket;
          failed.valid = false;
          impl_->thumbnail_failure = failed;
          std::fprintf(stderr,
                       "BTS_GPU_THUMBNAIL_FAILED layer=%s\n",
                       job.request.layer.toString().toStdString().c_str());
        }
        impl_->thumbnail_job.reset();
      }
    }
  }
  if (allow_gpu_compute && impl_->histogram_job) {
    auto &job = *impl_->histogram_job;
    auto found = impl_->bindings.find(job.set.material_session_uid);
    if (found == impl_->bindings.end())
      impl_->histogram_job.reset();
    else {
      auto &binding = found->second;
      job.set.projection_tiles = binding.projection_cache;
      job.set.geometry_ownership = binding.ownership_cache;
      job.set.geometry_names = binding.geometry_names;
      if (!binding.projection_cache.empty())
        job.set.projection_geometry = binding.projection_cache.begin()->second;
      const auto result = impl_->gpu_preview.histogram(job.set, job.request, job.ticket);
      if (result != GpuPreviewUpdate::Pending) {
        if (result == GpuPreviewUpdate::Unsupported) {
          HistogramMetadata failed;
          failed.ticket = job.ticket;
          failed.valid = false;
          impl_->histogram_failure = std::pair{job.request.effect, failed};
          std::fprintf(stderr,
                       "BTS_GPU_HISTOGRAM_FAILED effect=%s\n",
                       job.request.effect.toString().toStdString().c_str());
        }
        impl_->histogram_job.reset();
      }
    }
  }
  // Interactive edits and diagnostics keep priority over a not-yet-assigned resource.
  if (allow_gpu_compute && !input_active && impl_->gpu_queued.empty() &&
      impl_->gpu_stack_queued.empty() && impl_->texture_upload_waiting &&
      !impl_->preparing_textures.empty())
  {
    performance::Scope timing("gpu_texture_prepare");
    std::vector<const FillTexture *> inputs;
    for (const auto &texture : impl_->preparing_textures)
      inputs.push_back(texture.get());
    const auto result = impl_->gpu_preview.prepare_texture_resources(inputs);
    if (result == GpuPreviewUpdate::Unsupported) {
      impl_->texture_upload_error =
          "Insufficient GPU memory to prepare the material input group at full resolution";
      impl_->texture_upload_waiting = false;
    }
    else if (result == GpuPreviewUpdate::Updated) {
      impl_->texture_upload_waiting = false;
      std::printf("BTS_TEXTURE_GPU_READY name=%s full_resolution=1\n",
                  (impl_->preparing_textures.size() == 1 ?
                       impl_->preparing_textures.front()->name.c_str() :
                       "material input group"));
    }
  }
  for (const auto &summary : impl_->gpu_preview.displacement_summaries())
    if (auto found = impl_->bindings.find(summary.uid); found != impl_->bindings.end()) {
      auto &binding = found->second;
      if (summary.epoch != binding.displacement_cpu_epoch)
        continue;
      const bool old_presence = binding.gpu_displacement_presence.value_or(
          binding.displacement_count > 0);
      binding.gpu_displacement_presence = summary.present;
      binding.displacement_minimum = summary.minimum;
      binding.displacement_maximum = summary.maximum;
      const bool shader_changed = displacement_shader(*binding.owner_main, binding);
      if (shader_changed || old_presence != summary.present) {
        impl_->gpu_displacement_changed = true;
        if (binding.displacement_image)
          impl_->cycles.changed(*binding.displacement_image, summary.uid, 16);
      }
      std::printf(
          "BTS_GPU_DISPLACEMENT_RANGE uid=%u present=%d minimum=%.9g maximum=%.9g async_metadata_bytes=16 cpu_composite=0\n",
          summary.uid,
          int(summary.present),
          summary.minimum,
          summary.maximum);
    }
  poll_gpu_programs();
  if (!impl_->pending || !impl_->pending->layer_edit)
    return;
  const auto found = impl_->bindings.find(impl_->pending->result.uid);
  if (found == impl_->bindings.end())
    return;
  auto &binding = found->second;
  for (int c = 0; c < 17; ++c)
    if (impl_->pending->result.channels[c])
      if (auto *image = composite_image(binding, c)) {
        BKE_image_get_gpu_material_texture_try(image, nullptr, false);
        if (image->source == IMA_SRC_TILED)
          BKE_image_get_gpu_material_texture_try(image, nullptr, true);
      }
}
bool MaterialInspection::sync_cycles_images(Main &main, bool active)
{
  impl_->cycles_entry_waiting = false;
  const auto mirror = [&](bool enabled) {
    return native_cycles_recorded_error_boundary(
        impl_->cycles_error_reported,
        [&] { return impl_->cycles.update(main, enabled); },
        [&] { return impl_->cycles.error(); },
        [](std::string_view detail) noexcept {
          std::fputs("BTS_CYCLES_MIRROR_PAUSED ", stderr);
          std::fwrite(detail.data(), 1, detail.size(), stderr);
          std::fputc('\n', stderr);
        });
  };
  if (!active) {
    impl_->cycles_entry_requested = false;
    impl_->cycles_preparation_failed = false;
    impl_->cycles_preparation_error.clear();
    impl_->cycles_error_reported = false;
    return mirror(false);
  }
  // Packing a generated composite changes its persisted source to FILE.
  // Restore only exact, local binding-owned packed targets; imported texture
  // inputs (including Coat images) are not part of this registry. Keep the
  // existing CPU cache and GPU allocation, which already contain the pixels.
  for (auto &[uid, binding] : impl_->bindings)
    for (int c = 0; c < 17; ++c) {
      if (!cycles_shader_channel(binding, c))
        continue;
      auto *image = composite_image(binding, c);
      if (!image || image->source != IMA_SRC_FILE || !image->packedfiles.first ||
          image->alpha_mode != IMA_ALPHA_CHANNEL_PACKED ||
          !ELEM(image->type, IMA_TYPE_IMAGE, IMA_TYPE_UV_TEST))
        continue;
      if (binding.owner_main != &main || BLI_findindex(&main.images, image) < 0 ||
          ID_IS_LINKED(&image->id) || ID_IS_OVERRIDE_LIBRARY(&image->id) ||
          (image->id.tag &
           (ID_TAG_NO_MAIN | ID_TAG_LOCALIZED | ID_TAG_COPIED_ON_EVAL | ID_TAG_MISSING)))
        continue;
      const auto *node = composite_texture(binding, c);
      const auto *tile = static_cast<const ImageTile *>(image->tiles.first);
      if (!node || node->id != &image->id || BLI_listbase_count(&image->tiles) != 1 || !tile ||
          tile->tile_number != 1001 || tile->gen_x < 1 || tile->gen_y < 1)
        continue;
      image->source = IMA_SRC_GENERATED;
      DEG_id_tag_update_ex(&main, &image->id, ID_RECALC_SOURCE);
      std::printf(
          "BTS_CYCLES_RESTORE_PACKED_TARGET uid=%u channel=%d image=%s width=%d height=%d retained_storage=1\n",
          uid,
          c,
          image->id.name + 2,
          tile->gen_x,
          tile->gen_y);
    }
  if (!impl_->cycles_entry_requested) {
    impl_->cycles_entry_requested = true;
    impl_->cycles_error_reported = false;
    for (auto &[uid, binding] : impl_->bindings)
      for (int c = 0; c < 17; ++c) {
        if (!cycles_shader_channel(binding, c))
          continue;
        if (auto *image = composite_image(binding, c))
          impl_->cycles.changed(*image, uid, c);
      }
  }
  // A failed entry may still own a fence/worker from an earlier live revision.
  // Drain that coordinator without retrying allocation until an explicit re-entry.
  if (impl_->cycles_preparation_failed) {
    mirror(false);
    return false;
  }
  const auto producers_pending = [&] {
    return impl_->gpu_preview.programs_pending() || !impl_->gpu_queued.empty() ||
           !impl_->gpu_stack_queued.empty() || impl_->texture_upload_waiting ||
           impl_->layering_sources_pending || impl_->gpu_preview.native_preparations_pending() ||
           std::any_of(impl_->bindings.begin(), impl_->bindings.end(), [](const auto &entry) {
             return entry.second.world_view_dirty || entry.second.world_objects_dirty ||
                    bool(entry.second.margin_job) || bool(entry.second.projection_job) ||
                    NativeWorldNormalContextRoute::producer_pending(
                        entry.second.world_set.has_value(),
                        bool(entry.second.world_geometry_job)) ||
                    (entry.second.world_set &&
                     entry.second.world_tile < entry.second.world_tiles.size());
           });
  };
  if (producers_pending()) {
    impl_->cycles_entry_waiting = true;
    return false;
  }
  for (auto &[uid, binding] : impl_->bindings)
    for (int c = 0; c < 17; ++c) {
      if (!cycles_shader_channel(binding, c))
        continue;
      auto *image = composite_image(binding, c);
      if (!image)
        continue;
      const auto existing = BKE_image_gpu_override_texture(image);
      if (existing.texture && *existing.texture)
        continue;
      try {
        performance::Scope timing("cycles_gpu_target_prepare");
        // Runtime-only native ownership is lost by packed Open. Adopt only the
        // validated binding's authoritative GPU stack target, never a bake/input.
        const bool gpu_authoritative = binding.gpu_stack_set && binding.gpu_stack_active[c];
        std::uint64_t width = 1, height = 1, count = 0;
        for (const ImageTile &tile : image->tiles) {
          width = std::max(width, std::uint64_t(tile.gen_x));
          height = std::max(height, std::uint64_t(tile.gen_y));
          ++count;
        }
        auto bytes = resource_sum(resource_product(
                                      resource_product(
                                          resource_product(resource_product(width, height), count),
                                          image->flag & IMA_HIGH_BITDEPTH ? 16 : 8),
                                      4),
                                  2) /
                     3;
        if (gpu_authoritative) {
          if (!ELEM(image->source, IMA_SRC_GENERATED, IMA_SRC_TILED) ||
              !ELEM(image->type, IMA_TYPE_IMAGE, IMA_TYPE_UV_TEST) ||
              !GpuLayerPreview::supported(*binding.gpu_stack_set, c))
            throw std::runtime_error("Required owned image has no authoritative GPU producer");
          const bool was_native = native_gpu_image(image);
          const auto flags = image->flag;
          image->runtime->gpuflag |= native_generated_gpu_bit;
          native_image_precision(image, gpu_channel_precision(*binding.gpu_stack_set, c));
          try {
            prepare_native_gpu_image(image);
          }
          catch (...) {
            if (!was_native)
              image->runtime->gpuflag &= ~native_generated_gpu_bit;
            image->flag = flags;
            throw;
          }
          // New zero storage is never a completed image. Recompose the complete
          // latest source snapshot before admitting the first/live Cycles mirror.
          binding.gpu_unbound_waiting[c] = true;
        }
        else {
          // Genuine CPU-owned legacy images retain Blender's normal upload path.
          // No layer evaluation, author-page readback or CPU composition occurs here.
          ResourceAvailability budget;
          int total_kb = 0, free_kb = 0;
          if (!GPU_type_matches(GPU_DEVICE_SOFTWARE, GPU_OS_ANY, GPU_DRIVER_ANY) &&
              GPU_mem_stats_supported())
            GPU_mem_stats_get(&total_kb, &free_kb);
          budget.vram_known = coherent_vram_report(total_kb, free_kb);
          budget.available_vram = budget.vram_known ? std::uint64_t(free_kb) * 1024 : 0;
          BKE_image_get_gpu_material_texture(image, nullptr, false);
          if (image->source == IMA_SRC_TILED)
            BKE_image_get_gpu_material_texture(image, nullptr, true);
        }
        const auto allocated = BKE_image_gpu_override_texture(image);
        if (!allocated.texture || !*allocated.texture)
          throw std::runtime_error("Native allocation returned no full-resolution GPU storage");
        bytes = resource_sum(resource_product(
                                 resource_product(
                                     resource_product(
                                         resource_product(GPU_texture_width(*allocated.texture),
                                                          GPU_texture_height(*allocated.texture)),
                                         std::max(1, GPU_texture_layer_count(*allocated.texture))),
                                     GPU_texture_format(*allocated.texture) ==
                                             gpu::TextureFormat::SFLOAT_32_32_32_32 ?
                                         16 :
                                         8),
                                 4),
                             2) /
                3;
        std::printf(
            "BTS_CYCLES_GPU_TARGET_PREPARE uid=%u channel=%d image=%llx image_uid=%u tiles=%llu bytes=%llu gpu_authoritative=%d cpu_initial_upload=%d cpu_composite=0\n",
            uid,
            c,
            (unsigned long long)reinterpret_cast<std::uintptr_t>(image),
            image->id.session_uid,
            (unsigned long long)count,
            (unsigned long long)bytes,
            int(gpu_authoritative),
            int(!gpu_authoritative));
        impl_->cycles.changed(*image, uid, c);
        resume_gpu_channels();
        impl_->cycles_entry_waiting = true;
        return false;
      }
      catch (const std::exception &error) {
        impl_->cycles_preparation_failed = true;
        try {
          impl_->cycles_preparation_error = "Cannot prepare Cycles channel " + std::to_string(c) +
                                            " (" + std::string(image->id.name + 2) +
                                            "): " + error.what();
        }
        catch (...) {
          impl_->cycles_preparation_error.clear();
        }
        const auto detail = cycles_images_error();
        std::fputs("BTS_CYCLES_MIRROR_PAUSED ", stderr);
        std::fwrite(detail.data(), 1, detail.size(), stderr);
        std::fputc('\n', stderr);
        mirror(false);
        return false;
      }
    }
  resume_gpu_channels();
  // Hidden allocations are not sufficient: all latest producer writes must be
  // complete before publishing a full required-image entry or live revision.
  if (producers_pending()) {
    impl_->cycles_entry_waiting = true;
    return false;
  }
  return mirror(true);
}
