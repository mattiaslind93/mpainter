// SPDX-License-Identifier: GPL-3.0-or-later
// Brush strokes: the viewport's stroke_begin, stroke_stamps and stroke_end callbacks. Part of
// run_workspace_session() (app/WorkspaceSession.inc), included in order inside its body; it shares
// that function's locals and is not a standalone file.
bool stroke_spatial = false;
viewport->stroke_begin = [&] {
  if (skew && skew->painting())
    return skew->begin(brush);
  if (quick_editing && document && selected_material >= 0 && !quick_stroke_before) {
    quick_stroke_uid = snapshot.materials[selected_material].session_id;
    auto &mask = quick_masks[quick_stroke_uid];
    quick_stroke_before = mask;
    quick_stroke_pixels = {};
    quick_prefix = {};
    quick_tail_bounds = {};
    return true;
  }
  if (!document || selected_material < 0 || stroke_before ||
      (workspace && workspace->layer_stack()->selectedItems().size() != 1))
    return false;
  stroke_uid = snapshot.materials[selected_material].session_id;
  const auto &set = document->set(stroke_uid);
  if (bts::layer_is_cached(set, set.selected_layer)) {
    workspace->set_status("Layer is cached. Hide Pre-compose to paint on layers below.");
    return false;
  }
  if (bts::material_layering_main_readonly(set))
    return false;
  material_layering_brush.synchronize(brush, &set);
  if (brush.clone && !viewport->clone_source) {
    workspace->append_console("V+click to sample a clone source on the selected layer.");
    return false;
  }
  const auto &selected = bts::TextureDocument::layer(document->set(stroke_uid),
                                                     set.selected_layer);
  stroke_mask = set.selected_mask && selected.mask.has_value();
  if (bts::performance::enabled())
    std::printf("BTS_STROKE_CONTEXT target=%s tool=%s resolution=%d tiles=%zu\n",
                stroke_mask ? "mask" : "layer",
                brush.smudge ? "smudge" :
                brush.clone  ? "clone" :
                brush.erase  ? "erase" :
                               "paint",
                set.texture_resolution,
                set.tiles.size());
  if (!bts::TextureDocument::paint_effect(set) &&
      (!set.selected_effect.isNull() || (!stroke_mask && selected.kind != bts::LayerKind::Paint)))
    return false;
  stroke_adjustment.reset();
  if (!stroke_mask && !bts::TextureDocument::paint_effect(set) && (brush.smudge || brush.clone) &&
      bts::adjustment_layer(selected))
  {
    try {
      if (!gpu_authoring_enabled)
        throw std::runtime_error("Adjustment Smudge/Clone requires GPU authoring");
      for (int channel : {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16})
        if (bts::adjustment_channel(set, selected, channel) &&
            !bts::GpuLayerPreview::supported(set, channel))
          throw std::runtime_error("Adjustment source stack is unavailable on GPU");
      stroke_adjustment = host.begin_adjustment_source(set, selected.id, document->id());
      if (qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO") &&
          workspace->property("bmRefuseNextPage").toBool())
      {
        workspace->setProperty("bmRefuseNextPage", false);
        host.diagnostic_adjustment_budget(0);
        workspace->setProperty(
            "bmRefusal",
            QJsonObject{{"armed", false},
                        {"fired", true},
                        {"terminal", false},
                        {"reason", QString()},
                        {"needed_bytes", qint64(2 * bts::adjustment_page_bytes)},
                        {"available_bytes", 0},
                        {"stroke_cancelled", false},
                        {"acknowledged", false}});
      }
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      return false;
    }
  }
  stroke_before = std::make_unique<bts::TextureDocument>(*document);
  stroke_pixels = {};
  stroke_ao_pixels = {};
  stroke_users = {};
  stroke_opacity = brush.stroke_opacity / 100;
  stroke_erase = brush.erase;
  stroke_smudge = brush.smudge;
  stroke_clone = brush.clone;
  clone_paint.reset();
  clone_ao.reset();
  clone_users = {};
  smudge_paint.reset();
  smudge_ao.reset();
  smudge_users = {};
  stroke_changed = false;
  stroke_spatial = false;
  stroke_prefix = {};
  stroke_tail_bounds = {};
  refresh_history_actions();
  return true;
};
viewport->stroke_stamps = [&](const std::vector<bts::PaintPixels::Stamp> &display_stamps,
                              std::size_t stable_count) {
  std::vector<bts::PaintPixels::Stamp> legacy_stamps;
  if (document && stroke_before && !stroke_mask && document->set(stroke_uid).bump_offset() != 0) {
    legacy_stamps = display_stamps;
    for (auto &stamp : legacy_stamps)
      stamp.values.bump = document->set(stroke_uid).bump_storage(stamp.values.bump);
  }
  const auto &stamps = legacy_stamps.empty() ? display_stamps : legacy_stamps;
  if (skew && skew->stroke()) {
    skew->stamps(stamps, stable_count);
    return;
  }
  bts::EditTiming timing("BTS_STROKE_MS");
  const auto raster_start = bts::PropertyClock::now();
  bts::PreviewScope preview(interactive_composite);
  stable_count = std::min(stable_count, stamps.size());
  if (quick_stroke_before) {
    try {
      const bool recording = gpu_authoring_enabled &&
                             std::all_of(stamps.begin(), stamps.end(), [](auto stamp) {
                               stamp.coverage_mask.reset();
                               return bts::gpu_basic_brush_supported(stamp);
                             });
      bts::PaintRecordingScope record(recording);
      if (recording)
        std::printf("BTS_QUICK_MASK_GPU_RECORD stamps=%zu\n", stamps.size());
      auto &mask = quick_masks[quick_stroke_uid];
      quick_stroke_pixels = quick_prefix;
      auto dirty = quick_tail_bounds;
      bts::PaintPixels::Bounds tail{};
      const auto merge = [](auto &a, const auto &b) {
        if (b.empty())
          return;
        if (a.empty()) {
          a = b;
          return;
        }
        a = {std::min(a.x0, b.x0),
             std::min(a.y0, b.y0),
             std::max(a.x1, b.x1),
             std::max(a.y1, b.y1)};
      };
      for (std::size_t i = 0; i < stamps.size(); ++i) {
        auto stamp = stamps[i];
        stamp.coverage_mask.reset();
        stamp.channels = {false, false, true, false, false};
        stamp.values.roughness = mask.author_value(stamp.values.roughness);
        const auto bounds = quick_stroke_pixels.stamp_working(
            mask.resolution, stamp, document->set(quick_stroke_uid).tiles);
        merge(dirty, bounds);
        if (i >= stable_count)
          merge(tail, bounds);
        if (i + 1 == stable_count)
          quick_prefix = quick_stroke_pixels;
      }
      quick_tail_bounds = tail;
      mask.pixels.over_stroke(quick_stroke_before->pixels,
                              quick_stroke_pixels,
                              brush.stroke_opacity / 100,
                              dirty,
                              false,
                              true);
      refresh_quick_preview();
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      viewport->end_paint(true);
    }
    return;
  }

  if (!stroke_before || stroke_opacity == 0)
    return;
  bool gpu_recording = gpu_authoring_enabled &&
                       std::all_of(stamps.begin(), stamps.end(), bts::gpu_basic_brush_supported);
  if (gpu_recording)
    for (int channel : {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 16})
      gpu_recording &= bts::GpuLayerPreview::supported(document->set(stroke_uid), channel);
  if (stroke_adjustment && !gpu_recording) {
    workspace->append_console("Adjustment stroke is unsupported on GPU; no CPU fallback was used");
    viewport->end_paint(true);
    return;
  }
  bts::PaintRecordingScope record(gpu_recording);
  if (gpu_recording)
    std::printf("BTS_BRUSH_GPU_RECORD stamps=%zu tool=%s\n",
                stamps.size(),
                stroke_clone  ? "clone" :
                stroke_smudge ? "smudge" :
                                "paint");
  if (gpu_authoring_enabled && !gpu_recording)
    std::fprintf(stderr, "BTS_BRUSH_CPU_FALLBACK reason=unsupported-brush-or-stack\n");
  try {
    restore_stroke_prefix();
    auto candidate = std::make_unique<bts::TextureDocument>(*document);
    auto &set = candidate->set(stroke_uid);
    auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
    auto *effect = bts::TextureDocument::paint_effect(set);
    const int resolution = effect ?
                               (effect->resolution ? effect->resolution : set.texture_resolution) :
                               bts::TextureDocument::authoring_resolution(set, layer, stroke_mask);
    if (effect)
      effect->resolution = resolution;
    else if (stroke_mask)
      layer.mask->author_resolution = resolution;
    auto pending_stroke = stroke_pixels, pending_ao = stroke_ao_pixels;
    auto pending_users = stroke_users;
    const auto remap = [](bts::PaintPixels::Bounds b, int from, int to) {
      const double scale = double(to) / from;
      return bts::PaintPixels::Bounds{int(std::floor(b.x0 * scale)),
                                      int(std::floor(b.y0 * scale)),
                                      int(std::ceil(b.x1 * scale)),
                                      int(std::ceil(b.y1 * scale))};
    };
    bts::PaintPixels::Bounds dirty_rect{resolution, resolution, 0, 0}, new_tail_bounds{};
    const auto old_tail_bounds = stroke_tail_bounds;
    bool writing_tail = false;
    const auto include = [&](const bts::PaintPixels::Bounds &bounds) {
      if (bounds.empty())
        return;
      dirty_rect.x0 = std::min(dirty_rect.x0, bounds.x0);
      dirty_rect.y0 = std::min(dirty_rect.y0, bounds.y0);
      dirty_rect.x1 = std::max(dirty_rect.x1, bounds.x1);
      dirty_rect.y1 = std::max(dirty_rect.y1, bounds.y1);
      if (writing_tail) {
        if (new_tail_bounds.empty())
          new_tail_bounds = bounds;
        else
          new_tail_bounds = {std::min(new_tail_bounds.x0, bounds.x0),
                             std::min(new_tail_bounds.y0, bounds.y0),
                             std::max(new_tail_bounds.x1, bounds.x1),
                             std::max(new_tail_bounds.y1, bounds.y1)};
      }
    };
    include(old_tail_bounds);
    if (stroke_smudge) {
      const auto &base_set = stroke_before->set(stroke_uid);
      const auto &base_layer = bts::TextureDocument::layer(stroke_before->set(stroke_uid),
                                                           base_set.selected_layer);
      const auto *base_effect = bts::TextureDocument::paint_effect(base_set);
      const auto &base_pixels = base_effect ? base_effect->pixels :
                                stroke_mask ? base_layer.mask->pixels :
                                              base_layer.pixels;
      if (!smudge_paint)
        smudge_paint = std::make_unique<bts::SmudgeStroke>(
            base_pixels,
            resolution,
            bts::adjustment_main_source(base_set, base_layer, stroke_adjustment));
      for (std::size_t stamp_index = 0; stamp_index < stamps.size(); ++stamp_index) {
        std::optional<bts::performance::Scope> timing;
        if (viewport->measure_brush_dabs_)
          timing.emplace("stroke_raster_dab_cpu");
        auto stamp = stamps[stamp_index];
        writing_tail = stamp_index >= stable_count;
        stroke_spatial |= bool(stamp.surface);
        if (!stroke_mask)
          for (int c = 0; c < 5; ++c)
            stamp.channels[c] &= set.channels[c] && layer.channels[c];
        const float pickup = std::clamp(stamp.flow * brush.spacing / 100, 0.f, 1.f);
        include(smudge_paint->dab(stamp, set.loaded_tiles(), pickup));
        if (!stroke_mask && brush.ao && set.ao_channel && layer.ao_enabled) {
          if (!smudge_ao)
            smudge_ao = std::make_unique<bts::SmudgeStroke>(
                base_effect ? base_effect->ao : base_layer.ao_pixels,
                resolution,
                bts::adjustment_scalar_source(base_set, base_layer, 5, stroke_adjustment));
          auto scalar = stamp;
          scalar.channels = {false, false, true, false, false};
          include(smudge_ao->dab(scalar, set.loaded_tiles(), pickup));
        }
        if (!stroke_mask)
          for (int slot = 0; slot < 9; ++slot)
            if (scalar_enabled(set, layer, bool(effect), slot)) {
              auto &user = scalar_layer(layer, slot);
              const int size = effect ?
                                   resolution :
                                   bts::custom_authoring_resolution(user, set.texture_resolution);
              if (!smudge_users[slot])
                smudge_users[slot] = std::make_unique<bts::SmudgeStroke>(
                    base_effect ? scalar_pixels(*base_effect, slot) :
                                  scalar_layer(base_layer, slot).pixels,
                    size,
                    bts::adjustment_scalar_source(
                        base_set, base_layer, slot == 8 ? 16 : slot + 7, stroke_adjustment));
              auto scalar = stamp;
              scalar.channels = {false, false, true, false, false};
              include(remap(
                  smudge_users[slot]->dab(scalar, set.loaded_tiles(), pickup), size, resolution));
            }
        if (stamp_index + 1 == stable_count)
          save_stroke_prefix(pending_stroke, pending_ao, pending_users);
      }
      writing_tail = false;
      stroke_tail_bounds = new_tail_bounds;
      const auto primary = smudge_paint->publish(stroke_opacity);
      include(primary);
      (effect      ? effect->pixels :
       stroke_mask ? layer.mask->pixels :
                     layer.pixels) = smudge_paint->pixels();
      if (smudge_ao) {
        include(smudge_ao->publish(stroke_opacity));
        (effect ? effect->ao : layer.ao_pixels) = smudge_ao->pixels();
      }
      const bool main_changed = !dirty_rect.empty();
      for (int slot = 0; slot < 9; ++slot)
        if (smudge_users[slot]) {
          auto &user = scalar_layer(layer, slot);
          const int size = effect ? resolution :
                                    bts::custom_authoring_resolution(user, set.texture_resolution);
          const auto bounds = smudge_users[slot]->publish(stroke_opacity);
          include(remap(bounds, size, resolution));
          (effect ? scalar_pixels(*effect, slot) : user.pixels) = smudge_users[slot]->pixels();
          if (!effect && !bounds.empty())
            user.author_resolution = size;
        }
      if (dirty_rect.empty())
        return;
      if (!effect && !stroke_mask && main_changed)
        layer.author_resolution = resolution;
      const double raster_ms = bts::property_ms(raster_start);
      const auto composite_start = bts::PropertyClock::now();
      apply_document_layers(*candidate, stroke_uid, resolution, dirty_rect);
      if (qEnvironmentVariableIsSet("BTS_AD_STROKE_BENCH"))
        std::printf("BTS_AD_STROKE_RES resolution=%d tool=%s\n",
                    resolution,
                    stroke_clone  ? "clone" :
                    stroke_smudge ? "smudge" :
                    stroke_erase  ? "erase" :
                                    "paint");
      std::printf("BTS_STROKE_MS stamps=%zu raster_ms=%.3f composite_upload_ms=%.3f\n",
                  stamps.size(),
                  raster_ms,
                  bts::property_ms(composite_start));
      document = std::move(candidate);
      stroke_changed = true;
      return;
    }
    if (stroke_clone) {
      const auto &base = stroke_before->set(stroke_uid);
      const auto &source_layer = bts::TextureDocument::layer(stroke_before->set(stroke_uid),
                                                             base.selected_layer);
      const auto *source_effect = bts::TextureDocument::paint_effect(base);
      if (!clone_paint)
        clone_paint = std::make_unique<bts::CloneStroke>(
            source_effect ? source_effect->pixels :
            stroke_mask   ? source_layer.mask->pixels :
                            source_layer.pixels,
            resolution,
            *viewport->clone_source,
            brush.clone_absolute,
            bts::adjustment_main_source(base, source_layer, stroke_adjustment));
      if (!stroke_mask && brush.ao && set.ao_channel && layer.ao_enabled && !clone_ao)
        clone_ao = std::make_unique<bts::CloneStroke>(
            source_effect ? source_effect->ao : source_layer.ao_pixels,
            resolution,
            *viewport->clone_source,
            brush.clone_absolute,
            bts::adjustment_scalar_source(base, source_layer, 5, stroke_adjustment));
      if (!stroke_mask)
        for (int slot = 0; slot < 9; ++slot)
          if (scalar_enabled(set, layer, bool(effect), slot) && !clone_users[slot])
            clone_users[slot] = std::make_unique<bts::CloneStroke>(
                source_effect ? scalar_pixels(*source_effect, slot) :
                                scalar_layer(source_layer, slot).pixels,
                effect ? resolution :
                         bts::custom_authoring_resolution(scalar_layer(layer, slot),
                                                          set.texture_resolution),
                *viewport->clone_source,
                brush.clone_absolute,
                bts::adjustment_scalar_source(
                    base, source_layer, slot == 8 ? 16 : slot + 7, stroke_adjustment));
    }
    for (std::size_t stamp_index = 0; stamp_index < stamps.size(); ++stamp_index) {
      std::optional<bts::performance::Scope> timing;
      if (viewport->measure_brush_dabs_)
        timing.emplace("stroke_raster_dab_cpu");
      auto stamp = stamps[stamp_index];
      writing_tail = stamp_index >= stable_count;
      stroke_spatial |= bool(stamp.surface);
      if (!stroke_mask)
        for (int channel = 0; channel < 5; ++channel)
          stamp.channels[channel] &= set.channels[channel] && layer.channels[channel];
      const auto input = stamp;
      if (stroke_clone) {
        stamp = clone_paint->dab(stamp);
        viewport->clone_source_display = clone_paint->last_source();
      }
      include(pending_stroke.stamp_working(resolution, stamp, set.loaded_tiles()));
      if (!stroke_mask) {
        for (int slot = 0; slot < 9; ++slot)
          if (scalar_enabled(set, layer, bool(effect), slot)) {
            auto custom_stamp = stamp;
            custom_stamp.channels = {false, false, true, false, false};
            custom_stamp.values.roughness = scalar_value(slot);
            if (stroke_clone) {
              auto source = input;
              source.channels = custom_stamp.channels;
              custom_stamp = clone_users[slot]->dab(source);
            }
            if (custom_stamp.projected) {
              auto projection = std::make_shared<bts::ProjectionMapping>(*custom_stamp.projected);
              projection->scalar_override = scalar_value(slot);
              custom_stamp.projected = std::move(projection);
            }
            auto &user = scalar_layer(layer, slot);
            const int size = effect ?
                                 resolution :
                                 bts::custom_authoring_resolution(user, set.texture_resolution);
            const auto bounds = pending_users[slot].stamp_working(
                size, custom_stamp, set.loaded_tiles());
            if (!bounds.empty()) {
              if (!effect)
                user.author_resolution = size;
              include(remap(bounds, size, resolution));
            }
          }
      }
      if (!stroke_mask && brush.ao && set.ao_channel && layer.ao_enabled) {
        stamp.channels = {false, false, true, false, false};
        stamp.values.roughness = brush.values.ambient_occlusion;
        if (stroke_clone) {
          auto source = input;
          source.channels = stamp.channels;
          stamp = clone_ao->dab(source);
        }
        if (stamp.projected) {
          auto projection = std::make_shared<bts::ProjectionMapping>(*stamp.projected);
          projection->scalar_override = brush.values.ambient_occlusion;
          stamp.projected = std::move(projection);
        }
        include(pending_ao.stamp_working(resolution, stamp, set.loaded_tiles()));
      }
      if (stamp_index + 1 == stable_count)
        save_stroke_prefix(pending_stroke, pending_ao, pending_users);
    }
    writing_tail = false;
    stroke_tail_bounds = new_tail_bounds;
    if (!effect && !stroke_mask && (!pending_stroke.empty() || !pending_ao.empty()))
      layer.author_resolution = resolution;
    if (dirty_rect.empty())
      return;
    auto &starting_set = stroke_before->set(stroke_uid);
    const auto &starting_layer = bts::TextureDocument::layer(starting_set,
                                                             starting_set.selected_layer);
    auto &destination = effect ? effect->pixels : stroke_mask ? layer.mask->pixels : layer.pixels;
    const auto *starting_effect = bts::TextureDocument::paint_effect(starting_set);
    const auto &starting = starting_effect ? starting_effect->pixels :
                           stroke_mask     ? starting_layer.mask->pixels :
                                             starting_layer.pixels;
    const auto affected = dirty_rect;
    dirty_rect = {resolution, resolution, 0, 0};
    include(destination.over_stroke(
        starting, pending_stroke, stroke_opacity, affected, stroke_erase, true, true));
    if (!stroke_mask)
      include((effect ? effect->ao : layer.ao_pixels)
                  .over_stroke(starting_effect ? starting_effect->ao : starting_layer.ao_pixels,
                               pending_ao,
                               stroke_opacity,
                               affected,
                               stroke_erase,
                               true,
                               true));
    if (!stroke_mask)
      for (int slot = 0; slot < 9; ++slot)
        if (!pending_users[slot].empty() || !old_tail_bounds.empty()) {
          auto &user = scalar_layer(layer, slot);
          const int size = effect ? resolution :
                                    bts::custom_authoring_resolution(user, set.texture_resolution);
          const auto changed = (effect ? scalar_pixels(*effect, slot) : user.pixels)
                                   .over_stroke(starting_effect ?
                                                    scalar_pixels(*starting_effect, slot) :
                                                    scalar_layer(starting_layer, slot).pixels,
                                                pending_users[slot],
                                                stroke_opacity,
                                                remap(affected, resolution, size),
                                                stroke_erase,
                                                true,
                                                true);
          if (!changed.empty())
            include(remap(changed, size, resolution));
        }
    if (dirty_rect.empty()) {
      stroke_pixels = std::move(pending_stroke);
      stroke_ao_pixels = std::move(pending_ao);
      stroke_users = std::move(pending_users);
      return;
    }
    const double raster_ms = bts::property_ms(raster_start);
    const auto composite_start = bts::PropertyClock::now();
    apply_document_layers(*candidate, stroke_uid, resolution, dirty_rect);
    if (qEnvironmentVariableIsSet("BTS_AD_STROKE_BENCH"))
      std::printf("BTS_AD_STROKE_RES resolution=%d tool=%s\n",
                  resolution,
                  stroke_clone  ? "clone" :
                  stroke_smudge ? "smudge" :
                  stroke_erase  ? "erase" :
                                  "paint");
    std::printf("BTS_STROKE_MS stamps=%zu raster_ms=%.3f composite_upload_ms=%.3f\n",
                stamps.size(),
                raster_ms,
                bts::property_ms(composite_start));
    document = std::move(candidate);
    stroke_pixels = std::move(pending_stroke);
    stroke_ao_pixels = std::move(pending_ao);
    stroke_users = std::move(pending_users);
    stroke_changed = true;
    if (inspected_map >= 0)
      update_inspection(bts::InspectionMode::Roughness);
  }
  catch (const std::exception &error) {
    workspace->append_console(QString::fromUtf8(error.what()));
    viewport->end_paint(true);
  }
};
viewport->stroke_end = [&](bool cancel) {
  bts::PaintMetadataScope paint_metadata;
  if (skew && skew->stroke()) {
    skew->end(cancel);
    return;
  }
  if (quick_stroke_before) {
    if (cancel)
      quick_masks[quick_stroke_uid] = *quick_stroke_before;
    quick_stroke_before.reset();
    quick_stroke_pixels = {};
    quick_prefix = {};
    quick_tail_bounds = {};
    if (cancel)
      refresh_quick_preview();
    else {
      document->resolve_instances();
      for (const auto &set : document->texture_sets())
        finish_preview(set);
      viewport->update();
    }
    return;
  }
  if (!stroke_before)
    return;
  const auto completed_adjustment = stroke_adjustment;
  const bts::TextureDocument *adjustment_before = nullptr;
  if (cancel && stroke_changed) {
    apply_document_layers(*stroke_before, stroke_uid, int(stroke_before->settings().resolution));
    document = std::move(stroke_before);
  }
  else if (stroke_changed) {
    try {
      auto &set = document->set(stroke_uid);
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      auto &before_set = stroke_before->set(stroke_uid);
      const auto &before = bts::TextureDocument::layer(before_set, before_set.selected_layer);
      if (stroke_smudge) {
        bts::PaintRecordingScope history_record(gpu_authoring_enabled);
        if (auto *effect = bts::TextureDocument::paint_effect(set)) {
          const auto *source = bts::TextureDocument::paint_effect(before_set);
          if (source && source->strokes)
            effect->strokes = bts::append_smudge(source->strokes,
                                                 {source->pixels, source->ao},
                                                 {effect->pixels, effect->ao},
                                                 stroke_spatial);
          if (source)
            for (int slot = 0; slot < 9; ++slot)
              if (scalar_history(*source, slot) && smudge_users[slot])
                scalar_history(*effect,
                               slot) = bts::append_smudge(scalar_history(*source, slot),
                                                          {scalar_pixels(*source, slot), {}},
                                                          {scalar_pixels(*effect, slot), {}},
                                                          stroke_spatial);
        }
        else {
          if (stroke_mask)
            layer.mask->strokes = bts::append_smudge(before.mask->strokes,
                                                     {before.mask->pixels, {}},
                                                     {layer.mask->pixels, {}},
                                                     stroke_spatial);
          else {
            layer.strokes = bts::append_smudge(before.strokes,
                                               {before.pixels, before.ao_pixels},
                                               {layer.pixels, layer.ao_pixels},
                                               stroke_spatial);
            for (int slot = 0; slot < 9; ++slot)
              if (smudge_users[slot])
                scalar_layer(layer, slot).strokes = bts::append_smudge(
                    scalar_layer(before, slot).strokes,
                    {scalar_layer(before, slot).pixels, {}},
                    {scalar_layer(layer, slot).pixels, {}},
                    stroke_spatial);
          }
        }
      }
      else if (auto *effect = bts::TextureDocument::paint_effect(set)) {
        const auto *source = bts::TextureDocument::paint_effect(before_set);
        if (source && source->strokes && (!stroke_pixels.empty() || !stroke_ao_pixels.empty()))
          effect->strokes = bts::append_stroke(source->strokes,
                                               {source->pixels, source->ao},
                                               {effect->pixels, effect->ao},
                                               {stroke_pixels, stroke_ao_pixels},
                                               stroke_spatial,
                                               stroke_erase,
                                               stroke_opacity);
        if (source)
          for (int slot = 0; slot < 9; ++slot)
            if (scalar_history(*source, slot) && !stroke_users[slot].empty())
              scalar_history(*effect,
                             slot) = bts::append_stroke(scalar_history(*source, slot),
                                                        {scalar_pixels(*source, slot), {}},
                                                        {scalar_pixels(*effect, slot), {}},
                                                        {stroke_users[slot], {}},
                                                        stroke_spatial,
                                                        stroke_erase,
                                                        stroke_opacity);
      }
      else if (stroke_mask)
        layer.mask->strokes = bts::append_stroke(before.mask->strokes,
                                                 {before.mask->pixels, {}},
                                                 {layer.mask->pixels, {}},
                                                 {stroke_pixels, {}},
                                                 stroke_spatial,
                                                 stroke_erase,
                                                 stroke_opacity);
      else {
        if (!stroke_pixels.empty() || !stroke_ao_pixels.empty())
          layer.strokes = bts::append_stroke(before.strokes,
                                             {before.pixels, before.ao_pixels},
                                             {layer.pixels, layer.ao_pixels},
                                             {stroke_pixels, stroke_ao_pixels},
                                             stroke_spatial,
                                             stroke_erase,
                                             stroke_opacity);
        for (int slot = 0; slot < 9; ++slot)
          if (!stroke_users[slot].empty())
            scalar_layer(layer, slot).strokes = bts::append_stroke(
                scalar_layer(before, slot).strokes,
                {scalar_layer(before, slot).pixels, {}},
                {scalar_layer(layer, slot).pixels, {}},
                {stroke_users[slot], {}},
                stroke_spatial,
                stroke_erase,
                stroke_opacity);
      }
      maintain_history(*document);
      undo_history.push_back({std::move(stroke_before),
                              stroke_uid,
                              stroke_clone  ? "Clone stroke" :
                              stroke_smudge ? "Smudge stroke" :
                              stroke_erase  ? "Erase stroke" :
                                              "Paint stroke"});
      if (stroke_adjustment)
        adjustment_before = undo_history.back().state.get();
      if (undo_history.size() > 128)
        undo_history.erase(undo_history.begin());
      redo_history.clear();
    }
    catch (const std::exception &error) {
      if (stroke_before) {
        apply_document_layers(
            *stroke_before, stroke_uid, int(stroke_before->settings().resolution));
        document = std::move(stroke_before);
      }
      stroke_changed = false;
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  }
  if (!cancel && stroke_changed) {
    document->resolve_instances();
    for_affected(*document, stroke_uid, [&](const auto &set) { finish_preview(set); });
  }
  stroke_before.reset();
  stroke_adjustment.reset();
  stroke_prefix = {};
  stroke_tail_bounds = {};
  stroke_pixels = {};
  stroke_ao_pixels = {};
  stroke_users = {};
  smudge_paint.reset();
  smudge_ao.reset();
  smudge_users = {};
  clone_paint.reset();
  clone_ao.reset();
  clone_users = {};
  refresh_history_actions();
  if (stroke_changed)
    refresh_document_ui(false, stroke_uid);
  if (!cancel && completed_adjustment && adjustment_before)
    adjustment_transactions.record(completed_adjustment->id,
                                   document->id(),
                                   completed_adjustment->set,
                                   completed_adjustment->layer,
                                   document_revision,
                                   adjustment_before);
};
