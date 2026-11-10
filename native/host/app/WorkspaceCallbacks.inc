// SPDX-License-Identifier: GPL-3.0-or-later
// The workspace callbacks (panels, layers, masks, assets, bakes) and the bare-viewport fallback.
// Part of run_workspace_session() (app/WorkspaceSession.inc), included in order inside its body;
// it shares that function's locals and is not a standalone file.
if (use_workspace) {
  viewport->enable_layouts();
  select_material = [&](int index) {
    bts::performance::Scope timing_all("select_material");
    if (index != selected_material && shader)
      shader->finish();
    if (index != selected_material && skew)
      skew->finish_edit();
    if (uniform_before && finish_uniform_edit)
      finish_uniform_edit(false);
    if (viewport->path_active && !path_guard && path_brush_changed)
      path_brush_changed();
    if (!layer_selection_refresh) {
      bts::performance::Scope timing("select_environment");
      const auto environment = host.environment_state();
      workspace->set_environment(environment);
    }
    selected_material = index >= 0 && index < int(snapshot.materials.size()) ? index : -1;
    const bts::TextureSet *set = nullptr;
    if (document && selected_material >= 0) {
      for (const auto &candidate : document->texture_sets())
        if (candidate.material_session_uid == snapshot.materials[selected_material].session_id)
          set = &candidate;
    }
    host.set_material_layering_view(
        set ? set->material_session_uid : 0,
        set && set->material_layering.enabled,
        set && set->material_layering.enabled ? set->material_layering.active_mask : -1);
    material_layering_brush.synchronize(brush, set);
    if (set && !set->selected_mask &&
        !(set->material_layering.enabled && set->material_layering.active_mask >= 0))
    {
      const auto found = std::find_if(
          set->layers.begin(), set->layers.end(), [&](const auto &layer) {
            return layer.id == set->selected_layer;
          });
      if (found != set->layers.end() && found->kind != bts::LayerKind::Folder) {
        for (int c = 0; c < 5; ++c)
          brush.channels[c] = c != 3 && set->channels[c] && found->channels[c];
        brush.ao = set->ao_channel && found->ao_enabled;
        brush.displacement = set->displacement.channel && found->displacement.enabled;
        for (int slot = 0; slot < bts::custom_channel_count; ++slot)
          brush.users[slot] = set->users[slot].present && found->users[slot].enabled;
      }
    }
    if (id_picker_active && (!set || set->material_session_uid != id_picker_material ||
                             set->selected_effect != id_picker_effect))
      change_id_picker(false);
    if (set) {
      bool geometry_ready = !set->geometry_names.empty();
      for (const auto &[uid, name] : set->geometry_names)
        if (!snapshot.object_names.contains(uid) || snapshot.object_names.at(uid) != name)
          geometry_ready = false;
      if (!geometry_ready)
        bts::TextureDocument::refresh_geometry_names(*set, snapshot);
    }
    for (int role = 0; role < 7; ++role)
      viewport->available_mesh_maps[role] = set && set->mesh_maps[role].has_value();
    viewport->inspection_channels = set ? set->channels : std::array<bool, 5>{};
    viewport->inspection_displacement = set && set->displacement.channel;
    viewport->inspection_mask = set && set->selected_mask;
    viewport->inspection_ao = set && set->ao_channel;
    for (int slot = 0; slot < bts::custom_channel_count; ++slot)
      viewport->inspection_users[slot] = set && set->users[slot].present ?
                                             QString::fromStdString(set->users[slot].name) :
                                             QString{};
    if (int(host.inspection_mode()) >= 7 && int(host.inspection_mode()) < 15 &&
        viewport->inspection_users.at(int(host.inspection_mode()) - 7).isEmpty())
      update_inspection(bts::InspectionMode::Material);
    if (host.inspection_mode() == bts::InspectionMode::Displacement &&
        !viewport->inspection_displacement)
      update_inspection(bts::InspectionMode::Material);
    if (host.inspection_mode() == bts::InspectionMode::AmbientOcclusion &&
        !viewport->inspection_ao)
      update_inspection(bts::InspectionMode::Material);
    if ((host.inspection_mode() == bts::InspectionMode::Mask && !viewport->inspection_mask) ||
        (int(host.inspection_mode()) >= 0 && int(host.inspection_mode()) < 5 &&
         inspected_map < 0 && !viewport->inspection_channels[int(host.inspection_mode())]))
      update_inspection(bts::InspectionMode::Material);
    if (host.inspection_mode() == bts::InspectionMode::Mask)
      update_inspection(bts::InspectionMode::Mask);
    if (!(skew && skew->painting()) &&
        (!set || (!set->selected_effect.isNull() && !bts::TextureDocument::paint_effect(*set)) ||
         (!set->selected_mask &&
          bts::TextureDocument::layer(document->set(set->material_session_uid),
                                      set->selected_layer)
                  .kind != bts::LayerKind::Paint &&
          !bts::TextureDocument::paint_effect(*set)) ||
         !set->uniform_editable || workspace->layer_stack()->selectedItems().size() != 1))
    {
      viewport->polygon_active = false;
      workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    }
    const bool can_hide = !(skew && skew->mode() > 0) && set && set->uniform_editable &&
                          !set->selected_geometry &&
                          bts::TextureDocument::layer(document->set(set->material_session_uid),
                                                      set->selected_layer)
                              .geometry.active();
    if (!layer_selection_refresh || hide_excluded_geometry) {
      bts::performance::Scope timing("select_geometry_visibility");
      host.set_geometry_visibility(hide_excluded_geometry && can_hide ? set : nullptr);
    }
    viewport->hidden_uv_tiles.clear();
    if (set)
      for (int tile : set->tiles)
        if (!set->tile_loaded(tile))
          viewport->hidden_uv_tiles.insert(tile);
    if (hide_excluded_geometry && can_hide) {
      const auto &mask = bts::TextureDocument::layer(document->set(set->material_session_uid),
                                                     set->selected_layer)
                             .geometry;
      if (mask.type == 1)
        for (int tile : set->tiles)
          if (!mask.includes_tile(tile))
            viewport->hidden_uv_tiles.insert(tile);
    }
    {
      auto *button = workspace->findChild<QToolButton *>("hideExcludedGeometry");
      const QSignalBlocker block(button);
      button->setChecked(hide_excluded_geometry);
      button->setEnabled(set && set->uniform_editable);
    }
    viewport->geometry_tile_editing = false;
    viewport->geometry_excluded_tiles.clear();
    viewport->geometry_hatch_shapes.clear();
    if (set && set->selected_geometry) {
      const auto &layer = bts::TextureDocument::layer(document->set(set->material_session_uid),
                                                      set->selected_layer);
      if (layer.geometry.type == 1) {
        viewport->geometry_tile_editing = true;
        for (int tile : set->tiles)
          if (!layer.geometry.includes_tile(tile))
            viewport->geometry_excluded_tiles.insert(tile);
        for (const auto &triangle : snapshot.uv_triangles)
          if (snapshot.materials[triangle.material].session_id == set->material_session_uid)
            for (int tile : viewport->geometry_excluded_tiles) {
              auto shape = bts::geometry_tile_polygon(triangle, tile);
              if (shape.size() >= 3)
                viewport->geometry_hatch_shapes.push_back(std::move(shape));
            }
      }
    }
    if (viewport->fill_projection && set) {
      viewport->fill_projection->cm_per_native = set->cm_per_native;
      viewport->fill_projection->native_per_uv = set->native_per_uv;
    }
    workspace->set_instance_clipboard_available(instance_clipboard && document &&
                                                instance_clipboard_document == document->id());
    workspace->set_quick_mask_state(quick_enabled, quick_editing);
    workspace->set_path_active(viewport->path_active);
    const bool residency_editable = set && !set->loaded_tiles().empty() &&
                                    (!workspace->active_tile() ||
                                     set->tile_loaded(workspace->active_tile()));
    workspace->findChild<QTabWidget *>("layerSettingsTabs")
        ->widget(0)
        ->setEnabled(residency_editable);
    workspace->findChild<QWidget *>("propertiesSlot")->setEnabled(residency_editable);
    if (!residency_editable) {
      viewport->paint_enabled = false;
      viewport->polygon_active = false;
      viewport->fill_projection.reset();
    }
    workspace->setProperty(
        "materialEditabilityReason",
        selected_material >= 0 && selected_material < int(snapshot.materials.size()) ?
            QString::fromStdString(
                snapshot.materials[selected_material].roughness_unavailable_reason) :
            QString{});
    if (set && !layer_selection_refresh) {
      bts::performance::Scope timing("select_warp_projections");
      host.prepare_warp_projections(*set);
    }
    const auto next_paint_target = set ? QString::number(set->material_session_uid) +
                                             set->selected_layer.toString() +
                                             set->selected_effect.toString() +
                                             (set->selected_mask ? "mask" : "material") :
                                         QString{};
    if (set && set->selected_mask && next_paint_target != viewport->paint_target && !quick_editing)
    {
      const auto &layer = bts::TextureDocument::layer(document->set(set->material_session_uid),
                                                      set->selected_layer);
      if (layer.mask)
        brush.mask_value = bts::default_mask_ink(layer.mask->background);
    }
    {
      bts::performance::Scope timing("select_show_texture_set");
      workspace->show_texture_set(set, document ? &document->settings() : nullptr, &brush);
    }
    if (shader && !layer_selection_refresh) {
      bts::performance::Scope timing("select_shader_sync");
      shader->sync();
    }
    viewport->fill_projection.reset();
    if (set && !set->selected_geometry && !set->selected_mask && set->selected_effect.isNull() &&
        workspace->layer_stack()->selectedItems().size() == 1)
    {
      const auto &layer = bts::TextureDocument::layer(document->set(set->material_session_uid),
                                                      set->selected_layer);
      if (layer.kind == bts::LayerKind::Fill && !layer.instance) {
        viewport->fill_projection = layer.fill;
        viewport->fill_projection->native_per_uv = set->native_per_uv;
        viewport->fill_projection->cm_per_native = set->cm_per_native;
      }
    }
    if (set && set->selected_mask) {
      const auto &layer = bts::TextureDocument::layer(document->set(set->material_session_uid),
                                                      set->selected_layer);
      if (layer.mask)
        if (const auto *effect = bts::find_mask_effect(layer.mask->effects, set->selected_effect);
            effect && effect->kind == bts::MaskEffectKind::Fill && effect->anchor_source.isNull())
          viewport->fill_projection = effect->mapping.projection;
    }
    viewport->set_paint_target(
        set ? QString::number(set->material_session_uid) + set->selected_layer.toString() +
                  set->selected_effect.toString() + (set->selected_mask ? "mask" : "material") :
              QString{});
    viewport->sync_warp_projection();
    viewport->paint_mask = set && (set->selected_mask || quick_editing);
    viewport->quick_mask_edit = quick_editing;
    viewport->quick_mask_gate.reset();
    if (quick_enabled && !quick_editing && set) {
      const auto found = quick_masks.find(set->material_session_uid);
      if (found != quick_masks.end())
        viewport->quick_mask_gate = std::make_shared<bts::QuickMaskField>(found->second);
    }
    viewport->paint_enabled =
        set && !set->loaded_tiles().empty() &&
        (!workspace->active_tile() || set->tile_loaded(workspace->active_tile())) &&
        !bts::layer_is_cached(*set, set->selected_layer) && !set->selected_geometry &&
        !bts::TextureDocument::layer(document->set(set->material_session_uid), set->selected_layer)
             .instance &&
        (set->selected_effect.isNull() || bts::TextureDocument::paint_effect(*set)) &&
        workspace->layer_stack()->selectedItems().size() == 1 && set->uniform_editable &&
        (bts::TextureDocument::paint_effect(*set) || viewport->paint_mask ||
         bts::TextureDocument::layer(document->set(set->material_session_uid), set->selected_layer)
                 .kind == bts::LayerKind::Paint);
    if (set && bts::material_layering_main_readonly(*set)) {
      viewport->paint_enabled = false;
      viewport->polygon_active = false;
      viewport->fill_projection.reset();
      workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    }
    if (viewport->fill_projection) {
      fill_display_symmetry = viewport->fill_projection->symmetry ?
                                  viewport->fill_projection->symmetry->constraint :
                                  bts::FillSymmetry{}.constraint;
      if (!bts::fill_surface_projection(*viewport->fill_projection))
        fill_display_symmetry.enabled = false;
      viewport->symmetry = &fill_display_symmetry;
      workspace->set_symmetry(fill_display_symmetry);
    }
    else {
      viewport->symmetry = &symmetry;
      workspace->set_symmetry(symmetry);
    }
    const bool symmetry_available = set && set->uniform_editable && !set->disabled &&
                                    (viewport->fill_projection ?
                                         bts::fill_surface_projection(*viewport->fill_projection) :
                                         viewport->paint_enabled);
    workspace->findChild<QToolButton *>("symmetryToggle")->setEnabled(symmetry_available);
    workspace->findChild<QToolButton *>("symmetryMenuButton")->setEnabled(symmetry_available);
    workspace->findChild<QToolButton *>("symmetryManipulator")->setEnabled(symmetry_available);
    if (material_layering_ui)
      material_layering_ui->synchronize();
    if (viewport->paint_enabled && !layer_selection_refresh)
      host.prepare_paint_texture(*set);
    if (refresh_paths)
      refresh_paths();
    const bool can_pick_fill =
        set && set->uniform_editable && !set->selected_mask && !set->selected_geometry &&
        set->selected_effect.isNull() &&
        bts::TextureDocument::layer(document->set(set->material_session_uid), set->selected_layer)
                .kind == bts::LayerKind::Fill &&
        !bts::TextureDocument::layer(document->set(set->material_session_uid), set->selected_layer)
             .instance &&
        !bts::TextureDocument::layer(document->set(set->material_session_uid), set->selected_layer)
             .fill.native_material;
    if (!viewport->paint_enabled && !can_pick_fill && !id_picker_active && !color_picker_pending) {
      viewport->picker_active = false;
      workspace->set_picker_active(false);
    }
    if (selected_material < 0) {
      viewport->select_material(std::nullopt);
      workspace->set_roughness(0);
      workspace->set_roughness_editable(false,
                                        document ? "No material selected." : "No project open");
      return;
    }
    const auto &material = snapshot.materials[selected_material];
    const bool folder = set && bts::TextureDocument::layer(
                                   document->set(set->material_session_uid), set->selected_layer)
                                       .kind == bts::LayerKind::Folder;
    if (!layer_selection_refresh)
      viewport->select_material(material.session_id);
    else
      viewport->update();
    workspace->set_roughness(set                         ? (viewport->paint_enabled ?
                                                                brush.values.roughness :
                                                                bts::TextureDocument::property_values(*set).roughness) :
                             material.constant_roughness ? material.roughness :
                                                           0);
    workspace->set_roughness_editable(
        set ? (set->uniform_editable && !bts::material_layering_main_readonly(*set) && !folder &&
               !set->selected_mask && workspace->layer_stack()->selectedItems().size() == 1 &&
               set->channels[2]) :
              material.roughness_editable,
        QString::fromStdString(material.roughness_unavailable_reason));
    if (skew)
      skew->sync();
  };
  bts::WorkspaceShell::Callbacks callbacks;
  callbacks.trackball_changed = [&](bool enabled) {
    viewport->set_trackball(enabled);
    bts::save_trackball_navigation(enabled);
  };
  callbacks.view_layout_changed = [&](bts::ViewLayoutState layout) {
    viewport->set_layout(layout);
  };
  callbacks.projection_changed = [&](bts::OrbitCamera::Projection projection) {
    viewport->set_projection(projection);
  };
  callbacks.reference_image_toggled = [&](bool enabled) {
    viewport->end_paint(false);
    viewport->reference_image_enabled = enabled;
    if (workspace)
      workspace->set_reference_image_enabled(enabled);
    if (enabled && viewport->view_layout().mode == bts::ViewLayout::SceneOnly) {
      auto layout = viewport->view_layout();
      layout.mode = bts::ViewLayout::Split;
      viewport->set_layout(layout);
      if (workspace)
        workspace->set_view_layout(layout);
    }
    if (enabled && viewport->reference_image.valid()) {
      viewport->frame_reference_image();
    }
    sync_reference_image_state();
    if (document) {
      dirty = true;
      ++document_revision;
      last_document_edit = bts::performance::Clock::now();
    }
    viewport->update();
  };
  callbacks.open_reference_image_requested = [&] {
    viewport->end_paint(false);
    const QString path = bts::FileDialog::getOpenFileName(
        workspace,
        "Open Reference Image",
        viewport->reference_image.path,
        "Images (*.png *.jpg *.jpeg *.webp *.tif *.tiff *.tga *.bmp *.exr *.hdr)");
    if (path.isEmpty())
      return;
    if (!load_reference_image_file(path, true))
      return;
    viewport->reference_image_enabled = true;
    if (workspace)
      workspace->set_reference_image_enabled(true);
    if (viewport->view_layout().mode == bts::ViewLayout::SceneOnly) {
      auto layout = viewport->view_layout();
      layout.mode = bts::ViewLayout::Split;
      viewport->set_layout(layout);
      if (workspace)
        workspace->set_view_layout(layout);
    }
    viewport->frame_reference_image();
    sync_reference_image_state();
    if (document) {
      dirty = true;
      ++document_revision;
      last_document_edit = bts::performance::Clock::now();
    }
    viewport->update();
  };
  viewport->reference_view_changed = [&] {
    if (document) {
      auto state = bts::document_reference_image(*document);
      state.enabled = viewport->reference_image_enabled;
      state.path = viewport->reference_image.path.toStdString();
      state.center = {viewport->ref_center_.x(), viewport->ref_center_.y()};
      state.vertical_extent = viewport->ref_vertical_extent_;
      bts::set_document_reference_image(*document, std::move(state));
    }
  };
  viewport->reference_color_picked = [&](const bts::PickerColor &sampled) {
    if (!workspace)
      return;
    const bool use_secondary = secondary_picker_pending || viewport->secondary_active_;
    auto *picker = static_cast<bts::ColorPicker *>(workspace->findChild<QWidget *>(
        use_secondary ? "secondaryColorPicker" : "materialColorPicker"));
    if (picker)
      picker->accept_sample(sampled);
    color_picker_pending = false;
    secondary_picker_pending = false;
  };
  callbacks.residency_changed = [&](std::uint32_t uid, int tile, bool loaded) {
    if (!document || !uid)
      return;
    const auto serial = ++residency_requests[{uid, tile}];
    if ((tile ? document->set(uid).tile_loaded(tile) : document->set(uid).loaded) == loaded)
      return;
    try {
      viewport->end_paint(false);
      if (uniform_before)
        finish_uniform_edit(false);
      if (shader)
        shader->finish();
      if (host.texture_updates_pending() || host.save_project_pending() ||
          (texture_loader && texture_loader->pending()) ||
          (native_material_loader && native_material_loader->pending()))
      {
        auto *pending = new QTimer(workspace);
        pending->setInterval(100);
        const auto project_id = document->id();
        QObject::connect(pending,
                         &QTimer::timeout,
                         workspace,
                         [&, pending, project_id, uid, tile, loaded, serial] {
                           if (!document || document->id() != project_id ||
                               residency_requests[{uid, tile}] != serial)
                           {
                             pending->stop();
                             pending->deleteLater();
                             return;
                           }
                           if (host.texture_updates_pending() || host.save_project_pending() ||
                               (texture_loader && texture_loader->pending()) ||
                               (native_material_loader && native_material_loader->pending()))
                             return;
                           pending->stop();
                           pending->deleteLater();
                           workspace->request_texture_residency(uid, tile, loaded);
                         });
        pending->start();
        return;
      }
      // Finishing a gesture may replace the document; resolve the set afterward.
      auto &set = document->set(uid);
      const bool old_loaded = set.loaded;
      const auto old_tiles = set.unloaded_tiles;
      if (tile) {
        if (!set.tiles.contains(tile))
          throw std::invalid_argument("Unknown UV tile");
        if (loaded)
          set.unloaded_tiles.erase(tile);
        else
          set.unloaded_tiles.insert(tile);
      }
      else
        set.loaded = loaded;
      try {
        host.change_texture_residency(set);
      }
      catch (...) {
        set.loaded = old_loaded;
        set.unloaded_tiles = old_tiles;
        throw;
      }
      for (auto *history : {&undo_history, &redo_history})
        for (auto &entry : *history)
          if (entry.state)
            for (const auto &old : entry.state->texture_sets())
              if (old.id == set.id) {
                auto &target = entry.state->set(old.material_session_uid);
                target.loaded = set.loaded;
                target.unloaded_tiles = set.unloaded_tiles;
              }
      refresh_document_ui(false, uid);
      schedule_residency_spill();
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      refresh_texture_set_list();
      select_material(selected_material);
    }
  };
  callbacks.texture_set_selected = [&](int index, const QString &) { select_material(index); };
  callbacks.display_transform_changed = [&](const QString &name) {
    try {
      host.set_display_transform(name.toStdString());
      dirty = bool(document);
      viewport->update();
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  callbacks.apply_smart_material = [&] {
    edit_material(
        [&](bts::TextureSet &set) { bts::apply_worn_metal(set, set.texture_resolution); });
  };
  callbacks.bake_mesh_maps = [&] {
    if (!document)
      return;
    try {
      viewport->end_paint(false);
      if (skew)
        skew->finish_edit();
      bts::apply_mesh_bake_template(document->id(), document->bake_configuration());
      // Missing tile entries mean the dialog's current enabled-set defaults.
      const auto effective_choice = [&](QJsonObject choice) {
        QJsonObject tiles;
        const auto saved = choice["tiles"].toObject();
        for (const auto &set : document->texture_sets()) {
          const auto key = set.id.toString();
          const auto selected = saved[key].toArray();
          QJsonArray values;
          if (!set.disabled)
            for (int tile : set.tiles)
              if (!saved.contains(key) || selected.contains(tile))
                values.append(tile);
          tiles[key] = values;
        }
        choice["tiles"] = tiles;
        return choice;
      };
      const auto old_choice = effective_choice(bts::mesh_bake_template(document->id()));
      const auto previous_updates = viewport->updatesEnabled();
      viewport->setProperty("bakeRenderingSuspended", true);
      viewport->setUpdatesEnabled(false);
      const auto resume_bake_view = qScopeGuard([&] {
        viewport->setProperty("bakeRenderingSuspended", false);
        viewport->setUpdatesEnabled(previous_updates);
        viewport->update();
      });
      host.release_bake_scratch();
      const auto baked = bts::choose_mesh_bake(
          workspace,
          *document,
          host.scene_snapshot(true),
          [&](const std::string &path, const bts::BakeSourceOptions &options) {
            return host.load_bake_source(path, options);
          },
          [&](const std::function<void()> &fn) { host.with_gpu(fn); });
      auto recipe = bts::mesh_bake_template(document->id());
      // Compare dialog choices before restoring document-only mode/quality keys.
      if (!baked && old_choice == effective_choice(recipe))
        return;
      for (auto *key : {"ui_mode", "auto_role", "map_options"})
        if (document->bake_configuration().contains(key))
          recipe[key] = document->bake_configuration()[key];
      auto candidate = std::make_unique<bts::TextureDocument>(*document);
      candidate->set_bake_configuration(recipe);
      if (baked)
        for (const auto &result : *baked) {
          auto &set = candidate->set(result.material_uid);
          if (result.maps[0] && set.precision[3] != bts::ChannelPrecision::RGB32F)
            set.precision[3] = bts::ChannelPrecision::RGB32F;
          if (result.maps[3] && !set.ao_channel && set.precision[5] == bts::ChannelPrecision::L8)
            set.precision[5] = bts::ChannelPrecision::L32F;
          for (int role = 0; role < 7; ++role)
            if (result.maps[role]) {
              if (!set.mesh_maps[role])
                set.mesh_maps[role] = bts::MeshMap{};
              auto &map = *set.mesh_maps[role];
              map.name = result.maps[role]->name;
              for (const auto &[tile, plane] : result.maps[role]->tiles)
                map.tiles[tile] = plane;
            }
        }
      if (candidate->edit_state() == document->edit_state())
        return;
      const auto uid = selected_material >= 0 ?
                           snapshot.materials[selected_material].session_id :
                           document->texture_sets().front().material_session_uid;
      auto history = bts::prepare_document_commit(undo_history, uid);
      history.label = baked ? "Bake mesh maps" : "Edit bake parameters";
      if (baked)
        queue_mesh_map_changes(*document, *candidate, bts::NativeMappingEditOrigin::Authoring);
      bts::publish_document_commit(
          document, candidate, undo_history, redo_history, std::move(history), []() noexcept {});
      if (undo_history.size() > 128)
        undo_history.erase(undo_history.begin());
      if (baked)
        refresh_document_ui(false, uid);
      else {
        if (skew)
          skew->sync_panel();
        mark_document_changed();
        refresh_history_actions();
      }
    }
    catch (const std::exception &error) {
      if (document)
        bts::apply_mesh_bake_template(document->id(), document->bake_configuration());
      workspace->append_console(QString::fromUtf8(error.what()));
      QMessageBox::warning(
          workspace, "Mesh maps could not be baked", QString::fromUtf8(error.what()));
    }
  };
  callbacks.remove_mesh_map = [&](int role) {
    edit_material([=](bts::TextureSet &set) { set.mesh_maps.at(role).reset(); });
  };
  callbacks.inspect_mesh_map = viewport->mesh_map_requested;
  callbacks.baked_maps = [&] {
    std::vector<bts::BakedShelfItem> items;
    if (!document)
      return items;
    for (const auto &set : document->texture_sets())
      for (int role = 0; role < 7; ++role)
        if (set.mesh_maps[role] && !set.mesh_maps[role]->tiles.empty())
          items.push_back(bts::present_baked_map(
              set.id, QString::fromStdString(set.name), role, *set.mesh_maps[role]));
    return items;
  };
  callbacks.baked_asset = baked_asset_from_key;
  callbacks.brush_preview_requested = [&](const bts::BrushSettings &settings,
                                          std::function<void(QImage)> ready) {
    if (!brush_thumbnails) {
      ready({});
      return;
    }
    static std::uint64_t preview_revision = 0;
    bts::StudioAsset asset;
    asset.id = QUuid("{cc6cdb96-706f-45fc-9a72-b37fb7a997a3}");
    asset.name = "Current brush property preview";
    asset.usages = {"brush"};
    asset.scope = bts::AssetScope::Session;
    asset.revision = ++preview_revision;
    asset.brush_preset = settings;
    brush_thumbnails->request(asset, std::move(ready));
  };
  callbacks.save_brush_preset = [&] {
    if (brush_presets)
      brush_presets->save();
  };
  callbacks.brush_preset_action = [&](const QString &operation, const QUuid &id) {
    if (brush_presets)
      brush_presets->action(operation, id);
  };
  const auto import_resource = [&](const QString &path) {
    const auto imported = bts::choose_resource_import(workspace, bool(document), path);
    if (!imported)
      return;
    try {
      if (imported->scope == bts::AssetScope::User) {
        workspace->setProperty("resourceImporting", true);
        workspace->set_status("Installing resource…");
        auto prepared = std::make_shared<std::future<bts::PreparedUserAsset>>(std::async(
            std::launch::async, [asset = *imported, directory = assets.directory()]() mutable {
              return bts::AssetCatalog::prepare_user(std::move(asset), directory);
            }));
        auto *poll = new QTimer(workspace);
        poll->setInterval(16);
        QObject::connect(poll, &QTimer::timeout, workspace, [&, prepared, poll] {
          if (prepared->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
          poll->stop();
          poll->deleteLater();
          workspace->setProperty("resourceImporting", false);
          try {
            assets.add_prepared(prepared->get());
            workspace->refresh_alpha_shelf();
            workspace->set_status("Resource imported");
          }
          catch (const std::exception &error) {
            workspace->set_status(
                QString("Resource import failed: %1").arg(QString::fromUtf8(error.what())));
          }
        });
        poll->start();
        return;
      }
      assets.add(*imported);
      workspace->refresh_alpha_shelf();
      if (imported->scope == bts::AssetScope::Project) {
        dirty = true;
        (++document_revision, last_document_edit = bts::performance::Clock::now());
        window->setWindowTitle((document_path.isEmpty() ?
                                    QString("Untitled") :
                                    QFileInfo(document_path).completeBaseName()) +
                               " * — mPainter");
      }
    }
    catch (const std::exception &error) {
      QMessageBox::warning(
          workspace, "Resource could not be imported", QString::fromUtf8(error.what()));
    }
  };
  callbacks.resource_import_requested = [import_resource] { import_resource({}); };
  callbacks.texture_assets_import_requested = [import_resource](const QStringList &paths) {
    for (const auto &path : paths)
      import_resource(path);
  };
  callbacks.asset_export_requested = [&](const bts::StudioAsset &source) {
    const auto asset = source;
    if (asset.bytes.isEmpty())
      return;
    const auto extension = asset.brush_preset         ? QString(".btsbrush") :
                           bts::is_spmsk_asset(asset) ? QString(".spmsk") :
                           bts::is_spsm_asset(asset)  ? QString(".spsm") :
                           asset.material_preset      ? QString(".btsmaterial") :
                                                        QString(".btsmask");
    auto *dialog = bts::FileDialog::borrow(
        workspace,
        "Export preset",
        {},
        (bts::is_spsm_archive_asset(asset) ?
             "Original preset source (*.spsm *.spmsk)" :
             "Studio presets (*.btsmaterial *.btsmask *.btsbrush)"));
    dialog->setObjectName("presetExportFileDialog");
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setDefaultSuffix(extension.mid(1));
    auto safe_name = asset.name;
    safe_name.replace('/', '_');
    safe_name.replace(QChar(92), '_');
    dialog->selectFile(safe_name + extension);
    if (dialog->exec() != QDialog::Accepted || dialog->selectedFiles().isEmpty())
      return;
    const auto path = dialog->selectedFiles().first();
    bts::FileDialog::remember_folder_async(QFileInfo(path).absolutePath());
    workspace->setProperty("resourceExporting", true);
    workspace->set_status("Exporting preset…");
    auto writer = std::make_shared<std::future<void>>(std::async(
        std::launch::async,
        [bytes = asset.bytes,
         path,
         brush_asset = asset.brush_preset,
         brush_name = asset.name]() mutable {
          if (brush_asset)
            bytes = bts::write_brush_preset({brush_name, *brush_asset});
          QSaveFile output(path);
          if (!output.open(QIODevice::WriteOnly))
            throw std::runtime_error(output.errorString().toStdString());
          for (qsizetype offset = 0; offset < bytes.size();) {
            const auto count = std::min<qsizetype>(4 * 1024 * 1024, bytes.size() - offset);
            if (output.write(bytes.constData() + offset, count) != count)
              throw std::runtime_error(output.errorString().toStdString());
            offset += count;
          }
          if (!output.commit())
            throw std::runtime_error(output.errorString().toStdString());
        }));
    auto *poll = new QTimer(workspace);
    poll->setInterval(16);
    QObject::connect(poll, &QTimer::timeout, workspace, [&, writer, poll] {
      if (writer->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
      poll->stop();
      poll->deleteLater();
      workspace->setProperty("resourceExporting", false);
      try {
        writer->get();
        workspace->set_status("Preset exported");
      }
      catch (const std::exception &error) {
        workspace->set_status(
            QString("Preset export failed: %1").arg(QString::fromUtf8(error.what())));
      }
    });
    poll->start();
  };
  const auto native_material_error = [&](const QString &error) {
    if (asset_progress)
      asset_progress->fail(error);
    workspace->set_error_status("Native material: " + error);
    workspace->append_console("Native material: " + error);
  };
  const auto apply_native_asset = [&, native_material_error](bts::StudioAsset source,
                                                             QUuid target,
                                                             bool above,
                                                             bool replace) {
    if (!document || selected_material < 0)
      throw std::invalid_argument("Create or open a project before applying a material");
    const auto project = document->id();
    const auto uid = snapshot.materials[selected_material].session_id;
    const auto serial = ++native_apply_serial;
    const auto revision = document_revision;
    if (replace && target.isNull())
      target = document->set(uid).selected_layer;
    native_material_loader->ensure(
        source,
        assets.assets(),
        [&, project, uid, serial, revision, target, above, replace, native_material_error](
            const bts::StudioAsset &ready) {
          const QScopedValueRollback<bool> publication(asset_publication, true);
          if (!document || opening_project || document->id() != project ||
              native_apply_serial != serial || document_revision != revision)
          {
            native_material_error("Loading cancelled: project changed");
            return;
          }
          host.validate_native_material_target(ready.id, document->set(uid));
          const auto retained_id = assets.retain_project(ready);
          host.alias_native_material(ready.id, retained_id);
          const auto found = std::find_if(
              assets.assets().begin(), assets.assets().end(), [&](const auto &asset) {
                return asset.id == retained_id;
              });
          if (found == assets.assets().end())
            throw std::logic_error("Retained native material vanished");
          const auto retained = *found;
          // Keep asynchronous completion attached to the requested texture set.
          if (selected_material < 0 || snapshot.materials[selected_material].session_id != uid) {
            native_material_error("Texture set changed during loading; select the material again");
            return;
          }
          edit_material([&](bts::TextureSet &set) {
            bts::apply_native_material_asset(set, retained, target, above, replace);
          });
          workspace->refresh_alpha_shelf();
          viewport->update();
        },
        native_material_error);
  };
  const auto apply_smart_mask_asset =
      [&](const bts::StudioAsset &asset, QUuid target, bool replace) {
        bts::performance::Scope timing("smart_mask_apply");
        if (!document || selected_material < 0 || !asset.smart_mask)
          throw std::invalid_argument("Create or open a project before applying a mask generator");
        const auto &set = document->set(snapshot.materials[selected_material].session_id);
        if (target.isNull())
          target = set.selected_layer;
        if (!set.uniform_editable || set.disabled)
          throw std::invalid_argument(
              "Select an editable Texture Set before applying a mask generator");
        if (bts::mask_texture_layer(set, target).instance)
          throw std::invalid_argument("Edit instance masks at their source");
        const auto preset = asset.smart_mask;
        const auto previous_assets = assets.project_assets();
        const auto before = document.get();
        try {
          assets.retain_project(asset);
          edit_material([&](bts::TextureSet &edited) {
            bts::TextureDocument::apply_mask_preset(edited, target, *preset, replace);
          });
          if (document.get() == before && !last_material_error.isEmpty())
            throw std::runtime_error(last_material_error.toStdString());
          if (document.get() != before && !undo_history.empty())
            undo_history.back().label = "Apply mask generator";
        }
        catch (...) {
          if (document.get() == before)
            assets.replace_project(previous_assets);
          throw;
        }
      };
  const auto apply_spsm_asset = [&,
                                 apply_smart_mask_asset,
                                 serial = std::make_shared<std::uint64_t>(0),
                                 preparation_serial = std::make_shared<std::uint64_t>(0)](
                                    bts::StudioAsset source,
                                    QUuid target,
                                    bool above,
                                    bool replace = false) {
    if (!document || selected_material < 0)
      throw std::invalid_argument("Create or open a project before applying a material preset");
    const auto project = document->id();
    const auto uid = snapshot.materials[selected_material].session_id;
    const auto selected = document->set(uid).selected_layer;
    const auto revision = document_revision;
    const auto ticket = ++*serial;
    workspace->set_status("Preparing SPSM source…");
    const auto valid = [&, serial, ticket, project, uid, selected, revision] {
      return *serial == ticket && document && !opening_project && document->id() == project &&
             document_revision == revision && selected_material >= 0 &&
             snapshot.materials[selected_material].session_id == uid &&
             document->set(uid).selected_layer == selected && !viewport->painting_ &&
             !uniform_before;
    };
    bts::prepare_spsm_application(
        workspace,
        std::move(source),
        valid,
        [&, target, above, replace, apply_smart_mask_asset](bts::StudioAsset ready) {
          const QScopedValueRollback<bool> publication(asset_publication, true);
          const auto prepared = bts::prepared_spsm_source(ready);
          if (!prepared)
            throw std::runtime_error("Prepared SPSM source is no longer available");
          // Publish a cached recipe only into the same catalog identity/hash.
          const auto hydrated = assets.hydrate_preset(ready.id, ready.hash());
          if (!hydrated)
            throw std::runtime_error("SPSM source changed or was removed during loading");
          // Retention can grow the catalog vector; keep a value across publication.
          const bts::StudioAsset current = *hydrated;
          if (bts::is_spmsk_asset(current)) {
            apply_smart_mask_asset(current, target, replace);
            workspace->refresh_alpha_shelf();
            workspace->append_console(bts::spsm_source_summary(ready));
            workspace->set_status("Mask generator applied — original source preserved");
            viewport->update();
            return;
          }
          const auto previous_assets = assets.project_assets();
          const auto *before = document.get();
          try {
            const auto retained = assets.retain_project(current);
            edit_material([&](bts::TextureSet &set) {
              const auto material = bts::spsm_material_for_destination(prepared->prepared, set);
              if (!target.isNull()) {
                bts::TextureDocument::layer(set, target);
                set.selected_layer = target;
              }
              set.selected_mask = false;
              set.selected_geometry = false;
              set.selected_effect = QUuid();
              const auto added = bts::apply_material_preset(set, material);
              set.channels[3] |= material.set.channels[3];
              auto &folder = bts::TextureDocument::layer(set, added);
              folder.name = current.name.toStdString();
              folder.fill.material_resource =
                  retained.toString(QUuid::WithoutBraces).toStdString();
              if (!target.isNull())
                bts::TextureDocument::move_layer_relative(set, added, target, above);
            });
            if (!last_material_error.isEmpty())
              throw std::runtime_error(last_material_error.toStdString());
            if (document.get() == before)
              throw std::runtime_error("SPSM application made no document change");
            if (!undo_history.empty())
              undo_history.back().label = "Apply SPSM material preset";
          }
          catch (...) {
            if (document.get() == before)
              assets.replace_project(previous_assets);
            throw;
          }
          workspace->refresh_alpha_shelf();
          workspace->append_console(bts::spsm_source_summary(ready));
          workspace->set_status("SPSM material applied — original source preserved");
          viewport->update();
        },
        [&](const QString &error) {
          if (asset_progress)
            asset_progress->fail(error);
          workspace->set_error_status(error);
          workspace->append_console(error);
        },
        [&, preparation_serial, ticket](const bts::StudioAsset &ready) {
          std::vector<std::shared_ptr<const bts::FillTexture>> textures;
          std::set<const bts::FillTexture *> seen;
          const auto retain = [&](const auto &texture) {
            if (texture && seen.insert(texture.get()).second)
              textures.push_back(texture);
          };
          if (ready.smart_mask) {
            for (const auto &effect : bts::mask_effects_recursive(ready.smart_mask->effects))
              retain(effect.mapping.projection.texture);
          }
          else if (ready.material_preset)
            for (const auto &layer : ready.material_preset->set.layers) {
              for (int channel = 0; channel < 15; ++channel)
                retain(bts::fill_channel_texture(layer.fill, channel));
              if (layer.mask)
                for (const auto &effect : bts::mask_effects_recursive(layer.mask->effects))
                  retain(effect.mapping.projection.texture);
            }
          if (textures.empty())
            return true;
          if (texture_loader && texture_loader->pending())
            return false;
          *preparation_serial = ticket;
          const bool prepared = host.prepare_texture_resources(textures);
          if (!prepared) {
            workspace->set_status("Preparing SPSM textures…");
            viewport->update();
          }
          return prepared;
        },
        [&, preparation_serial, ticket] {
          if (*preparation_serial == ticket) {
            host.cancel_texture_preparation();
            *preparation_serial = 0;
          }
        });
  };
  const auto project_material_asset = [&](bts::StudioAsset asset) {
    if (!asset.filename.endsWith(".btsuniform"))
      return asset;
    const auto id = assets.retain_project(asset);
    for (const auto &retained : assets.assets())
      if (retained.id == id)
        return retained;
    throw std::logic_error("Retained material vanished");
  };
  callbacks.asset_dropped =
      [&, project_material_asset, apply_native_asset, apply_spsm_asset, apply_smart_mask_asset](
          const bts::StudioAsset &source_asset, QUuid target, bool above, bool replace) {
        try {
          if (document && selected_material >= 0 &&
              document->set(snapshot.materials[selected_material].session_id)
                  .material_layering.enabled)
            throw std::runtime_error(
                "File/Shelf resources are unavailable in uniform four-material mode");
          if (bts::is_spsm_archive_asset(source_asset)) {
            apply_spsm_asset(source_asset, target, above, replace);
            return;
          }
          if (bts::is_native_material_asset(source_asset)) {
            apply_native_asset(source_asset, target, above, false);
            return;
          }
          const auto asset = project_material_asset(source_asset);
          if (asset.usages.contains("texture")) {
            try {
              auto load_target = texture_target(workspace->layer_channel_context(), true);
              const auto &set = document->set(snapshot.materials[selected_material].session_id);
              if (asset_drop_mesh || (!set.selected_mask && set.selected_effect.isNull())) {
                load_target.effect = QUuid();
                load_target.create_mask_fill = false;
                load_target.create_fill = true;
                load_target.insertion = QUuid::createUuid();
                load_target.channel = workspace->layer_channel_context();
                if (!target.isNull())
                  load_target.layer = target;
              }
              texture_loader->from_asset(load_target, asset);
            }
            catch (const std::exception &error) {
              texture_error(QString::fromUtf8(error.what()));
            }
            return;
          }
          if (asset.projection_weave) {
            workspace->append_console("Use this resource in Projection.");
            return;
          }
          if (asset.smart_mask) {
            apply_smart_mask_asset(asset, target, replace);
            return;
          }
          const auto source = asset;
          const auto material_resource = source.filename.endsWith(".btsuniform") ?
                                             assets.retain_project(source) :
                                             source.id;
          bool applied = false;
          edit_material([&](bts::TextureSet &set) {
            if (!target.isNull()) {
              set.selected_layer = target;
              set.selected_mask = false;
            }
            QUuid added;
            if (source.smart_material) {
              added = source.material_preset ?
                          bts::apply_material_preset(set, *source.material_preset) :
                          bts::apply_worn_metal(set, set.texture_resolution);
              if (source.material_preset)
                bts::TextureDocument::layer(set, added).name = source.name.toStdString();
            }
            else if (source.material) {
              bts::TextureDocument::add_fill(set);
              added = set.selected_layer;
              auto &layer = bts::TextureDocument::layer(set, added);
              layer.values = *source.material;
              layer.fill = bts::asset_fill_projection(source,
                                                      document->settings().units.auto_physical);
              if (source.filename.endsWith(".btsuniform")) {
                bts::apply_blend_material_channels(set,
                                                   layer,
                                                   *source.material,
                                                   source.fill_preset ? &*source.fill_preset :
                                                                        nullptr,
                                                   true);
                layer.fill.material_resource =
                    material_resource.toString(QUuid::WithoutBraces).toStdString();
              }
              layer.name = source.name.toStdString();
            }
            applied = !added.isNull();
            if (!target.isNull() && !added.isNull())
              bts::TextureDocument::move_layer_relative(set, added, target, above);
          });
          if (applied && source.material_preset)
            assets.retain_project(source);
        }
        catch (const std::exception &error) {
          if (asset_progress)
            asset_progress->fail(QString::fromUtf8(error.what()));
          workspace->set_error_status(
              QString("Material could not be applied: %1").arg(QString::fromUtf8(error.what())));
          workspace->append_console(QString::fromUtf8(error.what()));
        }
      };
  callbacks.asset_selected = [&,
                              project_material_asset,
                              apply_native_asset,
                              apply_spsm_asset,
                              apply_smart_mask_asset](const bts::StudioAsset &source_asset) {
    try {
      if (brush_presets && source_asset.usages.contains("brush")) {
        const auto source = source_asset;
        if (document && source.scope != bts::AssetScope::Builtin)
          assets.retain_project(source);
        brush_presets->select(source);
        return;
      }
      if (material_layering_ui && material_layering_ui->consume_shelf(source_asset))
        return;
      if (document && selected_material >= 0 &&
          document->set(snapshot.materials[selected_material].session_id)
              .material_layering.enabled &&
          (source_asset.material || source_asset.smart_material || source_asset.smart_mask ||
           source_asset.usages.contains("texture") ||
           bts::is_native_material_asset(source_asset) ||
           bts::is_spsm_archive_asset(source_asset)))
        throw std::runtime_error(
            "File/Shelf material resources are unavailable in uniform four-material mode");
      if (native_image_ui && native_image_ui->consume_shelf(source_asset))
        return;
      if (bts::is_spsm_archive_asset(source_asset)) {
        apply_spsm_asset(
            source_asset, {}, true, bool(QApplication::keyboardModifiers() & Qt::ControlModifier));
        return;
      }
      if (bts::is_native_material_asset(source_asset)) {
        const bool replace = workspace->property("fillMaterialPick").toBool();
        workspace->setProperty("fillMaterialPick", false);
        apply_native_asset(source_asset, {}, true, replace);
        return;
      }
      const auto asset = project_material_asset(source_asset);
      if (asset.usages.contains("texture")) {
        apply_fill_texture(asset);
      }
      else if (brush.projected && asset.material && !asset.filename.endsWith(".btsuniform")) {
        if (asset.fill_preset) {
          workspace->append_console("This fill resource is unavailable for Projection.");
          return;
        }
        viewport->end_paint(false);
        brush.values = *asset.material;
        brush.projected->weave = asset.projection_weave;
        brush.projected->name = asset.name.toStdString();
        brush.channels = {true, true, true, !asset.projection_weave, true};
        select_material(selected_material);
        viewport->update();
      }
      else if (asset.projection_weave) {
        workspace->append_console("Select Projection to use this material.");
      }
      else if (asset.smart_mask) {
        apply_smart_mask_asset(asset, {}, false);
      }
      else if (asset.smart_material && document && selected_material >= 0) {
        const auto source = asset;
        bool applied = false;
        edit_material([&](bts::TextureSet &set) {
          if (source.material_preset) {
            const auto id = bts::apply_material_preset(set, *source.material_preset);
            bts::TextureDocument::layer(set, id).name = source.name.toStdString();
          }
          else
            bts::apply_worn_metal(set, set.texture_resolution);
          applied = true;
        });
        if (applied && source.material_preset)
          assets.retain_project(source);
      }
      else if (asset.usages.contains("alpha") && viewport->paint_enabled &&
               !viewport->polygon_active)
      {
        auto alpha = asset.alpha;
        if (!alpha && !asset.bytes.isEmpty()) {
          bts::BrushAlpha bitmap;
          bitmap.kind = bts::AlphaKind::Bitmap;
          bitmap.bitmap = bts::read_bitmap_alpha_bytes(asset.bytes, asset.name);
          alpha = bitmap;
        }
        if (alpha) {
          viewport->end_paint(false);
          brush.alpha = *alpha;
          select_material(selected_material);
          viewport->update();
        }
      }
      else if (asset.material && document && selected_material >= 0) {
        const bool pick_material = workspace->property("fillMaterialPick").toBool();
        if (pick_material)
          workspace->setProperty("fillMaterialPick", false);
        const auto value = *asset.material;
        const auto material_resource = asset.filename.endsWith(".btsuniform") ?
                                           assets.retain_project(asset) :
                                           asset.id;
        edit_material([&](bts::TextureSet &set) {
          auto &current = bts::TextureDocument::layer(set, set.selected_layer);
          if (pick_material && current.kind == bts::LayerKind::Fill && !current.instance &&
              !set.selected_mask && !asset.smart_material &&
              (!asset.fill_preset || asset.filename.endsWith(".btsuniform")) &&
              !asset.projection_weave)
          {
            if (asset.filename.endsWith(".btsuniform"))
              bts::apply_blend_material_channels(
                  set, current, value, asset.fill_preset ? &*asset.fill_preset : nullptr, false);
            else {
              current.values = value;
              current.fill.texture.reset();
              current.fill.channel_textures.reset();
              current.fill.atom = false;
            }
            current.fill.material_resource =
                material_resource.toString(QUuid::WithoutBraces).toStdString();
            return;
          }
          bts::TextureDocument::add_fill(set);
          bts::TextureDocument::edit_values(set) = value;
          bts::TextureDocument::layer(set, set.selected_layer).name = asset.name.toStdString();
          bts::TextureDocument::layer(set, set.selected_layer).fill = bts::asset_fill_projection(
              asset, document->settings().units.auto_physical);
          if (asset.filename.endsWith(".btsuniform")) {
            auto &created = bts::TextureDocument::layer(set, set.selected_layer);
            bts::apply_blend_material_channels(
                set, created, value, asset.fill_preset ? &*asset.fill_preset : nullptr, true);
            created.fill.material_resource =
                material_resource.toString(QUuid::WithoutBraces).toStdString();
          }
        });
      }
      else if (asset.usages.contains("environment") && !asset.bytes.isEmpty())
        change_environment_resource(assets.materialize(asset));
      else if (asset.usages.contains("procedural") && document && selected_material >= 0) {
        edit_material([&](bts::TextureSet &set) {
          auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
          if (layer.kind == bts::LayerKind::Fill && !layer.instance && !set.selected_mask) {
            layer.fill.atom = true;
            layer.fill.texture.reset();
            layer.fill.asset_size_cm = {10, 10, 10};
            layer.fill.custom_size = false;
            if (document->settings().units.auto_physical)
              layer.fill.physical = true;
            layer.fill.middle_dot = asset.name != "Atom Simple";
            layer.fill.border_dots = asset.name != "Atom Simple";
          }
        });
      }
    }
    catch (const std::exception &error) {
      if (asset_progress)
        asset_progress->fail(QString::fromUtf8(error.what()));
      workspace->set_error_status(QString::fromUtf8(error.what()));
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  callbacks.shelf_preferences_requested = [&] {
    bts::show_preferences(workspace,
                          assets.directory(),
                          bool(document),
                          viewport->viewport_clipping(),
                          [&](auto value) {
                            viewport->set_viewport_clipping(value);
                            viewport->update();
                          });
  };
  callbacks.environment_resource_requested = [&] {
    const auto path = bts::FileDialog::getOpenFileName(
        workspace, "Select environment", QString(), "Environment images (*.hdr *.exr)");
    if (path.isEmpty())
      return;
    try {
      change_environment_resource(path);
    }
    catch (const std::exception &error) {
      QMessageBox::warning(
          workspace, "Environment could not be loaded", QString::fromUtf8(error.what()));
    }
  };
  callbacks.environment_changed = [&](const QString &key, double value) {
    try {
      const auto before = host.environment_state();
      if (key.startsWith("subsurface.")) {
        host.edit_subsurface_preview(key.mid(11), value);
        workspace->set_environment(host.environment_state());
      }
      else if (key.startsWith("post.")) {
        host.edit_display_post_effect(key.mid(5), value);
        workspace->set_environment(host.environment_state());
      }
      else if (key == "rotation")
        host.set_environment_rotation(value);
      else if (key == "alignment")
        host.set_environment_alignment(value == 0);
      else
        host.set_environment_appearance(key.toStdString(), value);
      workspace->set_environment(host.environment_state());
      if (before == host.environment_state())
        return;
      if (document) {
        const QString label = "Environment " + key;
        const bool merge = key != "alignment" && environment_edit_clock.isValid() &&
                           environment_edit_clock.elapsed() < 250 && !undo_history.empty() &&
                           !undo_history.back().environment.isEmpty() &&
                           undo_history.back().label == label;
        if (!merge)
          undo_history.push_back(
              {nullptr,
               selected_material >= 0 ? snapshot.materials[selected_material].session_id : 0,
               label,
               {},
               before});
        if (undo_history.size() > 128)
          undo_history.erase(undo_history.begin());
        redo_history.clear();
        (++document_revision, last_document_edit = bts::performance::Clock::now());
        refresh_history_actions();
      }
      environment_edit_clock.restart();
      dirty = bool(document);
      viewport->update();
      if (dirty)
        window->setWindowTitle((document_path.isEmpty() ?
                                    QString("Untitled") :
                                    QFileInfo(document_path).completeBaseName()) +
                               " * — mPainter");
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  viewport->environment_rotation_changed = [change = callbacks.environment_changed](double value) {
    change("rotation", value);
  };
  callbacks.layer_blend_changed = [&](QUuid id, int channel, bts::LayerBlend mode) {
    edit_material([=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, id);
      if (layer.instance)
        throw std::invalid_argument("Instance blend is inherited from its source");
      if (channel == 14)
        layer.displacement.blending = mode;
      else if (channel >= 6)
        layer.users.at(channel - 6).blending = mode;
      else if (channel == 5)
        layer.ao_blending = mode;
      else
        layer.blending.at(channel) = mode;
    });
  };
  callbacks.layer_opacity_changed = [&](QUuid id, int channel, double value) {
    bts::property_event("opacity");
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    bts::CompositeChannels flags{};
    const int image = channel == 14 ? 16 : channel >= 6 ? channel + 1 : channel;
    if (image >= 0 && image < 17)
      flags[image] = true;
    if (flags[3])
      flags[6] = true;
    if (flags[4])
      flags[3] = true;
    queued_property_channels = flags;
    edit_property(
        [=](bts::TextureSet &candidate) {
          auto &layer = bts::TextureDocument::layer(candidate, id);
          if (channel == 14)
            layer.displacement.opacity = float(value);
          else if (channel >= 6)
            layer.users.at(channel - 6).opacity = float(value);
          else if (channel == 5)
            layer.ao_opacity = float(value);
          else
            layer.opacity.at(channel) = float(value);
        },
        "Layer opacity");
  };
  callbacks.layer_selection_changed = [&] {
    if (!document || selected_material < 0)
      return;
    const auto selected = workspace->layer_stack()->selectedItems();
    if (selected.size() == 1) {
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      set.selected_geometry = false;
      const QUuid id(selected.front()->data(Qt::UserRole).toString());
      if (set.selected_layer != id)
        set.selected_mask = false;
      set.selected_layer = id;
      set.selected_effect = QUuid(selected.front()->data(Qt::UserRole + 10).toString());
      if (!set.selected_effect.isNull())
        set.selected_mask = selected.front()->data(Qt::UserRole + 12).toBool();
    }
    const QScopedValueRollback<bool> selection_scope(layer_selection_refresh, true);
    workspace->setProperty("layerSelectionOnly", true);
    const auto selection_property = qScopeGuard(
        [&] { workspace->setProperty("layerSelectionOnly", false); });
    select_material(selected_material);
  };
  callbacks.add_levels = [&](int channel) {
    edit_material([=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (set.selected_mask && layer.mask) {
        bts::MaskEffect effect;
        effect.kind = bts::MaskEffectKind::Levels;
        effect.levels.id = effect.id;
        effect.levels.channel = 2;
        layer.mask->effects.push_back(effect);
        set.selected_effect = effect.id;
        return;
      }
      bts::LevelsEffect effect;
      effect.channel = channel;
      layer.effects.push_back(effect);
      set.selected_mask = false;
      set.selected_effect = effect.id;
    });
  };
  callbacks.effect_selected = [&](QUuid parent, QUuid effect) {
    workspace->set_instance_hierarchy_target();
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    const auto &layer = bts::TextureDocument::layer(set, parent);
    const bool mask = layer.mask && bts::find_mask_effect(layer.mask->effects, effect) != nullptr;
    if (!mask && std::none_of(layer.effects.begin(), layer.effects.end(), [&](const auto &value) {
          return value.id == effect && value.channel != 3;
        }))
      return;
    set.selected_geometry = false;
    set.selected_layer = parent;
    set.selected_mask = mask;
    set.selected_effect = effect;
    select_material(selected_material);
  };
  callbacks.effect_component_selected = [&](QUuid id, int component) {
    if (!document || selected_material < 0 || component < 0 || component > 3)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
    if (set.selected_mask && layer.mask) {
      if (auto *effect = bts::edit_mask_effect(
              layer.mask->effects, id, bts::MaskEffectKind::Levels);
          effect)
        effect->levels.component = 0;
    }
    else
      for (auto &effect : layer.effects)
        if (effect.id == id)
          effect.component = bts::rgb_levels_channel(effect.channel) ? component : 0;
    select_material(selected_material);
  };
  callbacks.effect_changed = [&](const bts::LevelsEffect &value) {
    auto edit = [=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (set.selected_mask && layer.mask) {
        if (auto *effect = bts::edit_mask_effect(
                layer.mask->effects, value.id, bts::MaskEffectKind::Levels);
            effect)
        {
          effect->levels = value;
          effect->levels.channel = 2;
          effect->levels.id = effect->id;
          return;
        }
        throw std::invalid_argument("Mask Levels effect missing");
      }
      auto &effects = layer.effects;
      for (auto &effect : effects)
        if (effect.id == value.id) {
          if (value.kind == bts::ContentEffectKind::Generator)
            for (int slot = 0; slot < 2; ++slot)
              if (effect.input_anchors[slot] != value.input_anchors[slot] &&
                  !value.input_anchors[slot].source.isNull())
              {
                const auto problem = bts::TextureDocument::anchor_reference_problem(
                    set, layer.id, value.input_anchors[slot]);
                if (!problem.isEmpty())
                  throw std::invalid_argument(problem.toStdString());
              }
          effect = value;
          return;
        }
      throw std::invalid_argument("Effect is missing");
    };
    if (value.kind == bts::ContentEffectKind::Levels)
      edit_property(std::move(edit), "Levels");
    else
      edit_material(std::move(edit));
  };
  callbacks.effect_removed = [&](QUuid parent, QUuid id) {
    edit_material(
        [=](bts::TextureSet &set) { bts::TextureDocument::remove_effects(set, {{parent, id}}); });
  };
  callbacks.delete_selection = [&](const std::vector<std::pair<QUuid, QUuid>> &effects,
                                   const std::vector<QUuid> &masks,
                                   const std::vector<QUuid> &layers) {
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::remove_effects(set, effects);
      bts::TextureDocument::remove_masks(set, masks);
      bts::TextureDocument::delete_layers(set, layers);
    });
  };
  callbacks.effect_visibility_changed = [&](QUuid parent, QUuid id, bool enabled) {
    edit_material([=](bts::TextureSet &set) {
      for (auto &effect : bts::TextureDocument::layer(set, parent).effects)
        if (effect.id == id)
          effect.enabled = enabled;
      auto &layer = bts::TextureDocument::layer(set, parent);
      if (layer.mask)
        if (auto *effect = bts::edit_mask_effect(layer.mask->effects, id))
          effect->enabled = enabled;
    });
  };
  callbacks.copy_mask = [&](QUuid id) {
    if (!document || selected_material < 0)
      return;
    mask_clipboard = bts::TextureDocument::copy_mask(
        document->set(snapshot.materials[selected_material].session_id), id);
    workspace->set_mask_clipboard_available(true);
    workspace->set_status("Mask content copied");
  };
  callbacks.paste_mask = [&](QUuid id) {
    if (mask_clipboard)
      edit_material([&](bts::TextureSet &set) {
        bts::TextureDocument::paste_mask(set, id, *mask_clipboard);
      });
  };
  callbacks.create_smart_mask = [&](QUuid id) {
    if (!document || selected_material < 0)
      return;
    try {
      const auto mask = bts::TextureDocument::copy_mask(
          document->set(snapshot.materials[selected_material].session_id), id);
      QInputDialog dialog(workspace);
      dialog.setObjectName("smartMaskNameDialog");
      dialog.setWindowTitle("Create mask generator");
      dialog.setLabelText("Name");
      dialog.setInputMode(QInputDialog::TextInput);
      dialog.setTextValue("Mask generator");
      dialog.findChild<QLineEdit *>()->setMaxLength(std::numeric_limits<int>::max());
      if (dialog.exec() != QDialog::Accepted)
        return;
      const auto name = bts::smart_preset_name(dialog.textValue());
      bts::StudioAsset asset;
      asset.name = name;
      asset.filename = asset.id.toString(QUuid::WithoutBraces) + ".btsmask";
      asset.usages = {"smartmask"};
      asset.scope = bts::AssetScope::User;
      host.begin_mask_preset(mask);
      workspace->setProperty("smartMaskSaving", true);
      workspace->set_status("Saving mask generator…");
      auto *poll = new QTimer(workspace);
      poll->setInterval(16);
      auto prepared = std::make_shared<std::future<bts::PreparedUserAsset>>();
      QObject::connect(
          poll,
          &QTimer::timeout,
          workspace,
          [&, poll, prepared, asset = std::move(asset)]() mutable {
            try {
              if (!prepared->valid()) {
                auto bytes = host.poll_mask_preset();
                if (!bytes)
                  return;
                asset.bytes = std::move(*bytes);
                *prepared = std::async(
                    std::launch::async,
                    [asset = std::move(asset), directory = assets.directory()]() mutable {
                      return bts::AssetCatalog::prepare_user(std::move(asset), directory);
                    });
                return;
              }
              if (prepared->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
              assets.add_prepared(prepared->get());
              workspace->setProperty("smartMaskSaving", false);
              poll->stop();
              poll->deleteLater();
              workspace->refresh_alpha_shelf();
              workspace->set_status("Mask generator saved to the user library");
            }
            catch (const std::exception &error) {
              workspace->setProperty("smartMaskSaving", false);
              poll->stop();
              poll->deleteLater();
              workspace->set_status(
                  QString("Mask generator save failed: %1").arg(QString::fromUtf8(error.what())));
            }
          });
      poll->start();
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      workspace->set_status(
          QString("Mask generator save failed: %1").arg(QString::fromUtf8(error.what())));
    }
  };
  callbacks.create_smart_material = [&](QUuid id) {
    if (!document || selected_material < 0)
      return;
    try {
      const auto source = document->set(snapshot.materials[selected_material].session_id);
      QInputDialog dialog(workspace);
      dialog.setObjectName("smartMaterialNameDialog");
      dialog.setWindowTitle("Create material preset");
      dialog.setLabelText("Name");
      dialog.setInputMode(QInputDialog::TextInput);
      dialog.setTextValue("Material preset");
      dialog.findChild<QLineEdit *>()->setMaxLength(std::numeric_limits<int>::max());
      if (dialog.exec() != QDialog::Accepted)
        return;
      const auto name = bts::smart_preset_name(dialog.textValue());
      bts::StudioAsset asset;
      asset.name = name;
      asset.filename = asset.id.toString(QUuid::WithoutBraces) + ".btsmaterial";
      asset.usages = {"smartmaterial"};
      asset.scope = bts::AssetScope::User;
      const auto recipe_count = bts::PaintPixels::recipe_materializations();
      host.begin_material_preset(source, id);
      workspace->setProperty("smartMaterialSaving", true);
      workspace->set_status("Saving material preset…");
      auto *poll = new QTimer(workspace);
      poll->setInterval(16);
      auto prepared = std::make_shared<std::future<bts::PreparedUserAsset>>();
      QObject::connect(
          poll,
          &QTimer::timeout,
          workspace,
          [&, poll, prepared, recipe_count, asset = std::move(asset)]() mutable {
            try {
              if (!prepared->valid()) {
                auto bytes = host.poll_material_preset();
                if (!bytes)
                  return;
                asset.bytes = std::move(*bytes);
                *prepared = std::async(
                    std::launch::async,
                    [asset = std::move(asset), directory = assets.directory()]() mutable {
                      return bts::AssetCatalog::prepare_user(std::move(asset), directory);
                    });
                return;
              }
              if (prepared->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
              assets.add_prepared(prepared->get());
              std::printf("BTS_SMART_MATERIAL_PRESET_SAVED cpu_materializations=%llu\n",
                          static_cast<unsigned long long>(
                              bts::PaintPixels::recipe_materializations() - recipe_count));
              std::fflush(stdout);
              workspace->setProperty("smartMaterialSaving", false);
              poll->stop();
              poll->deleteLater();
              workspace->refresh_alpha_shelf();
              workspace->set_status("Material preset saved to the user library");
            }
            catch (const std::exception &error) {
              workspace->setProperty("smartMaterialSaving", false);
              poll->stop();
              poll->deleteLater();
              workspace->set_status(
                  QString("Material preset save failed: %1").arg(QString::fromUtf8(error.what())));
            }
          });
      poll->start();
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      workspace->set_status(
          QString("Material preset save failed: %1").arg(QString::fromUtf8(error.what())));
    }
  };
  callbacks.create_folder_material =
      [&, create_smart_material = callbacks.create_smart_material](QUuid id) {
        if (!document || selected_material < 0)
          return;
        try {
          const auto &source = document->set(snapshot.materials[selected_material].session_id);
          auto preset = bts::capture_material_folder(source, id);
          if (bts::material_preset_has_generators(preset)) {
            create_smart_material(id);
            return;
          }
          preset.set.mesh_maps = source.mesh_maps;
          preset.set.projection_geometry = source.projection_geometry;
          QInputDialog dialog(workspace);
          dialog.setObjectName("folderMaterialNameDialog");
          dialog.setWindowTitle("Save folder as Material");
          dialog.setLabelText("Name");
          dialog.setInputMode(QInputDialog::TextInput);
          dialog.setTextValue(preset.set.layers.empty() ?
                                  QStringLiteral("Folder material") :
                                  QString::fromUtf8(preset.set.layers.front().name.c_str()));
          dialog.findChild<QLineEdit *>()->setMaxLength(std::numeric_limits<int>::max());
          if (dialog.exec() != QDialog::Accepted)
            return;
          const auto name = bts::smart_preset_name(dialog.textValue());
          auto bundle = bts::build_folder_material_bundle(preset.set, name);
          for (const auto &tex : bundle.textures)
            assets.add(tex);
          assets.add(bundle.material);
          workspace->refresh_alpha_shelf();
          workspace->set_status("Material saved to the user library");
        }
        catch (const std::exception &error) {
          workspace->append_console(QString::fromUtf8(error.what()));
          workspace->set_status(
              QString("Material save failed: %1").arg(QString::fromUtf8(error.what())));
        }
      };
  callbacks.remove_mask = [&](QUuid id) {
    edit_material([=](bts::TextureSet &set) { bts::TextureDocument::remove_masks(set, {id}); });
  };
  viewport->effect_move_requested = [&](int offset) {
    if (!document || selected_material < 0 ||
        document->set(snapshot.materials[selected_material].session_id).selected_effect.isNull())
      return false;
    edit_material([=](bts::TextureSet &set) { bts::TextureDocument::move_effect(set, offset); });
    return true;
  };
  callbacks.move_effect = [&](int offset) {
    edit_material([=](bts::TextureSet &set) { bts::TextureDocument::move_effect(set, offset); });
  };
  callbacks.add_mask_fill = [&] {
    edit_material([](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (!set.selected_mask || !layer.mask)
        return;
      bts::MaskEffect effect;
      effect.kind = bts::MaskEffectKind::Fill;
      layer.mask->effects.push_back(effect);
      set.selected_effect = effect.id;
    });
  };
  callbacks.paint_effect_mix_changed = [&](float opacity, bts::LayerBlend blending) {
    edit_material([=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (set.selected_mask && layer.mask) {
        if (auto *found = bts::edit_mask_effect(
                layer.mask->effects, set.selected_effect, bts::MaskEffectKind::Paint);
            found)
        {
          auto &effect = *found;
          effect.opacity = opacity;
          effect.blending = blending;
          return;
        }
      }
      else
        for (auto &effect : layer.effects)
          if (effect.id == set.selected_effect && effect.kind == bts::ContentEffectKind::Paint) {
            effect.opacity = opacity;
            effect.blending = blending;
            return;
          }
    });
  };
  callbacks.hide_excluded_changed = [&](bool enabled) {
    viewport->end_paint(false);
    hide_excluded_geometry = enabled;
    try {
      select_material(selected_material);
      viewport->update();
    }
    catch (const std::exception &error) {
      hide_excluded_geometry = false;
      select_material(selected_material);
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  callbacks.copy_layer_reference = [&] {
    if (!document || selected_material < 0)
      return;
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    if (!set.selected_effect.isNull())
      return;
    const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                    set.selected_layer);
    instance_clipboard = bts::LayerInstance{set.id, layer.id};
    instance_clipboard_document = document->id();
    instance_clipboard_name = layer.name;
    workspace->set_instance_clipboard_available(true);
    select_material(selected_material);
  };
  callbacks.paste_layer_instance = [&] {
    if (!document || !instance_clipboard || instance_clipboard_document != document->id())
      return;
    const auto reference = *instance_clipboard;
    const auto name = instance_clipboard_name;
    workspace->set_instance_hierarchy_target();
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::add_instance(set, reference.source_set, reference.source_layer, name);
    });
  };
  callbacks.instance_selected = [&](QUuid id) {
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    if (!set.instance_hierarchies.contains(id))
      return;
    set.selected_layer = id;
    set.selected_mask = false;
    set.selected_geometry = false;
    set.selected_effect = QUuid();
    workspace->set_instance_hierarchy_target(set.id, id);
    select_material(selected_material);
  };
  callbacks.instance_navigate = [&](QUuid set_id, QUuid layer_id) {
    if (!document)
      return;
    for (const auto &description : document->texture_sets())
      if (description.id == set_id && !description.disabled) {
        for (int index = 0; index < int(snapshot.materials.size()); ++index)
          if (snapshot.materials[index].session_id == description.material_session_uid) {
            auto &set = document->set(description.material_session_uid);
            const auto found = std::find_if(
                set.layers.begin(), set.layers.end(), [&](const auto &layer) {
                  return layer.id == layer_id;
                });
            if (found == set.layers.end())
              return;
            workspace->texture_sets()->setCurrentRow(index);
            set.selected_layer = layer_id;
            set.selected_mask = false;
            set.selected_geometry = false;
            set.selected_effect = QUuid();
            workspace->set_instance_hierarchy_target(set.id, layer_id);
            select_material(index);
            return;
          }
      }
  };
  callbacks.geometry_selected = [&](QUuid id) {
    workspace->set_instance_hierarchy_target();
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    if (bts::TextureDocument::layer(set, id).instance)
      return;
    set.selected_layer = id;
    set.selected_mask = false;
    set.selected_effect = QUuid();
    set.selected_geometry = true;
    select_material(selected_material);
  };
  callbacks.geometry_type_changed = [&](QUuid id, int type) {
    edit_geometry([=](bts::TextureSet &set) {
      if (type < 0 || type > 1)
        throw std::invalid_argument("Invalid geometry selection type");
      bts::TextureDocument::layer(set, id).geometry.type = type;
    });
  };
  callbacks.geometry_tile_changed = [&](QUuid id, int tile, bool include) {
    edit_geometry([=](bts::TextureSet &set) {
      if (!set.tiles.contains(tile))
        throw std::invalid_argument("Tile is not in this Texture Set");
      bts::TextureDocument::layer(set, id).geometry.set_tile(tile, include);
    });
  };
  callbacks.geometry_mesh_changed = [&](QUuid id, const QString &name, bool include) {
    edit_geometry([=](bts::TextureSet &set) {
      bts::TextureDocument::set_geometry_mesh(set, id, name.toStdString(), include);
    });
  };
  callbacks.geometry_all_changed = [&](QUuid id, bool include) {
    edit_geometry(
        [=](bts::TextureSet &set) { bts::TextureDocument::set_geometry_all(set, id, include); });
  };
  callbacks.copy_geometry = [&](QUuid id) {
    if (!document || selected_material < 0)
      return;
    geometry_clipboard = bts::TextureDocument::layer(
                             document->set(snapshot.materials[selected_material].session_id), id)
                             .geometry;
    workspace->set_geometry_clipboard_available(true);
  };
  callbacks.paste_geometry = [&](QUuid id) {
    if (geometry_clipboard)
      edit_geometry([&](bts::TextureSet &set) {
        bts::TextureDocument::layer(set, id).geometry = *geometry_clipboard;
      });
  };
  callbacks.anchor_requested = [&](QUuid id) {
    workspace->set_instance_hierarchy_target();
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    for (const auto &anchor : bts::TextureDocument::mask_anchors(set))
      if (anchor.id == id) {
        set.selected_geometry = false;
        set.selected_layer = anchor.layer;
        set.selected_mask = anchor.mask;
        set.selected_effect = anchor.id;
        select_material(selected_material);
        return;
      }
  };
  callbacks.add_mask_anchor = [&] {
    edit_material([](bts::TextureSet &set) { bts::TextureDocument::add_mask_anchor(set); });
  };
  callbacks.anchor_renamed = [&](QUuid id, const QString &name) {
    edit_material(
        [=](bts::TextureSet &set) { bts::TextureDocument::rename_mask_anchor(set, id, name); });
  };
  callbacks.add_filter_effect = [&](int channel) {
    edit_material(
        [=](bts::TextureSet &set) { bts::TextureDocument::add_filter_effect(set, channel); });
  };
  callbacks.filter_changed = [&](const bts::FilterSettings &filter,
                                 int channel,
                                 float opacity,
                                 bts::LayerBlend blending) {
    edit_property(
        [=](bts::TextureSet &set) {
          auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
          if (set.selected_mask && layer.mask) {
            if (auto *found = bts::edit_mask_effect(
                    layer.mask->effects, set.selected_effect, bts::MaskEffectKind::Filter);
                found)
            {
              auto &effect = *found;
              effect.filter = filter;
              effect.opacity = opacity;
              effect.blending = blending;
              return;
            }
          }
          else
            for (auto &effect : layer.effects)
              if (effect.id == set.selected_effect &&
                  effect.kind == bts::ContentEffectKind::Filter) {
                effect.filter = filter;
                effect.channel = channel;
                effect.opacity = opacity;
                effect.blending = blending;
                return;
              }
        },
        "Filter");
  };
  callbacks.add_paint_effect = [&] {
    edit_material([](bts::TextureSet &set) { bts::TextureDocument::add_paint_effect(set); });
  };
  callbacks.material_id_requested = [&](QUuid id) {
    if (!document || selected_material < 0)
      return;
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    const auto &layer = bts::TextureDocument::layer(
        document->set(snapshot.materials[selected_material].session_id), id);
    const auto color = bts::choose_material_id(
        workspace, layer.material_id.value_or(bts::material_id_palette[0]));
    if (color)
      edit_material([=](bts::TextureSet &set) {
        auto &layer = bts::TextureDocument::layer(set, id);
        layer.material_id = *color;
        layer.material_id_override_children = layer.kind == bts::LayerKind::Folder;
      });
  };
  callbacks.add_compare_mask = [&] {
    edit_material([](bts::TextureSet &set) { bts::TextureDocument::add_compare_mask(set); });
  };
  callbacks.add_mask_generator = [&] {
    const int channel = workspace->layer_channel_context();
    edit_material([=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (set.selected_mask) {
        if (!layer.mask)
          return;
        bts::MaskEffect effect;
        effect.kind = bts::MaskEffectKind::Generator;
        effect.surface_fallback = effect.triplanar_grunge = true;
        layer.mask->effects.push_back(effect);
        set.selected_effect = effect.id;
      }
      else {
        bts::LevelsEffect effect;
        effect.kind = bts::ContentEffectKind::Generator;
        effect.surface_fallback = effect.triplanar_grunge = true;
        effect.channel = channel == 3 ? 0 : channel;
        layer.effects.push_back(effect);
        set.selected_effect = effect.id;
      }
    });
  };
  callbacks.generator_input_requested = [&](int role, int tile) {
    if (!document || selected_material < 0 || role < 0 || role >= 7 ||
        workspace->property("generatorInputLoading").toBool())
      return;
    try {
      const auto uid = snapshot.materials[selected_material].session_id;
      const auto set_id = document->set(uid).id;
      std::shared_ptr<const bts::MeshMapPixels> before;
      if (const auto &map = document->set(uid).mesh_maps[role]; map)
        if (const auto member = map->tiles.find(tile); member != map->tiles.end())
          before = member->second.pixels;
      auto *dialog = bts::FileDialog::borrow(
          workspace,
          "Replace generator input",
          {},
          "Images (*.png *.jpg *.jpeg *.tif *.tiff *.tga *.exr *.hdr *.bmp)");
      dialog->setObjectName("generatorInputDialog");
      dialog->setFileMode(QFileDialog::ExistingFile);
      dialog->setAcceptMode(QFileDialog::AcceptOpen);
      if (dialog->exec() != QDialog::Accepted || dialog->selectedFiles().isEmpty())
        return;
      const auto path = QFileInfo(dialog->selectedFiles().first()).absoluteFilePath();
      bts::FileDialog::remember_folder_async(QFileInfo(path).absolutePath());
      workspace->setProperty("generatorInputLoading", true);
      workspace->findChild<QLabel *>("generatorInputResource")
          ->setText("Loading " + QFileInfo(path).fileName() + "…");
      workspace->findChild<QPushButton *>("generatorReplaceInput")->setEnabled(false);
      auto result = std::make_shared<std::future<bts::MeshMapPlane>>(std::async(
          std::launch::async, [path] { return bts::load_mesh_map_image(path.toStdString()); }));
      auto *poll = new QTimer(workspace);
      poll->setInterval(16);
      QObject::connect(
          poll,
          &QTimer::timeout,
          workspace,
          [&, poll, result, path, uid, set_id, role, tile, before] {
            if (result->wait_for(std::chrono::seconds(0)) != std::future_status::ready ||
                uniform_before || stroke_before || path_drag_before)
              return;
            poll->stop();
            poll->deleteLater();
            workspace->setProperty("generatorInputLoading", false);
            workspace->findChild<QPushButton *>("generatorReplaceInput")->setEnabled(true);
            try {
              auto plane = result->get();
              if (!document || document->set(uid).id != set_id)
                throw std::runtime_error("Generator input target project has changed");
              std::shared_ptr<const bts::MeshMapPixels> current;
              if (const auto &map = document->set(uid).mesh_maps[role]; map)
                if (const auto member = map->tiles.find(tile); member != map->tiles.end())
                  current = member->second.pixels;
              if (current != before)
                throw std::runtime_error(
                    "Generator input changed while loading; choose the input again");
              edit_texture_set(uid, [&](bts::TextureSet &set) {
                if (!set.tiles.contains(tile))
                  throw std::invalid_argument("Generator input tile is missing");
                if (!set.mesh_maps[role])
                  set.mesh_maps[role] = bts::MeshMap{};
                set.mesh_maps[role]->name = QFileInfo(path).completeBaseName().toStdString();
                set.mesh_maps[role]->tiles[tile] = std::move(plane);
              });
            }
            catch (const std::exception &error) {
              workspace->append_console(QString::fromUtf8(error.what()));
              workspace->set_error_status(QString::fromUtf8(error.what()));
              if (document && selected_material >= 0)
                select_material(selected_material);
            }
          });
      poll->start();
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      workspace->set_error_status(QString::fromUtf8(error.what()));
    }
  };
  callbacks.add_color_selection = [&] {
    edit_material([](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (!layer.mask)
        bts::TextureDocument::add_mask(set, 0);
      bts::MaskEffect effect;
      layer.mask->effects.push_back(effect);
      set.selected_mask = true;
      set.selected_effect = effect.id;
    });
  };
  callbacks.thumbnail_requested = [&](const bts::TextureSet &set,
                                      const bts::ThumbnailRequest &request) {
    const auto ticket = host.request_thumbnail(set, request);
    viewport->update();
    return ticket;
  };
  callbacks.thumbnail_result = [&] { return host.thumbnail_result(); };
  callbacks.histogram_requested = [&](const bts::TextureSet &set,
                                      const bts::HistogramRequest &request) {
    const auto ticket = host.request_histogram(set, request);
    viewport->update();
    return ticket;
  };
  callbacks.histogram_result = [&](const QUuid &id) { return host.histogram_result(id); };
  callbacks.mask_effect_changed = [&](const bts::MaskEffect &value) {
    if (!document || selected_material < 0)
      return;
    const auto layer_id =
        document->set(snapshot.materials[selected_material].session_id).selected_layer;
    const auto edit = [=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, layer_id);
      if (layer.mask)
        if (auto *found = bts::edit_mask_effect(layer.mask->effects, value.id, value.kind); found)
        {
          auto &effect = *found;
          if (value.kind == bts::MaskEffectKind::Generator)
            for (int slot = 0; slot < 2; ++slot)
              if (effect.input_anchors[slot] != value.input_anchors[slot] &&
                  !value.input_anchors[slot].source.isNull())
              {
                const auto problem = bts::TextureDocument::mask_effect_anchor_reference_problem(
                    set, layer.id, value.id, value.input_anchors[slot]);
                if (!problem.isEmpty())
                  throw std::invalid_argument(problem.toStdString());
              }
          if (value.kind == bts::MaskEffectKind::Fill &&
              effect.anchor_reference() != value.anchor_reference())
          {
            const auto problem = bts::TextureDocument::mask_effect_anchor_reference_problem(
                set, layer_id, value.id, value.anchor_reference());
            if (!problem.isEmpty())
              throw std::invalid_argument(problem.toStdString());
          }
          effect = value;
          return;
        }
      throw std::invalid_argument("Mask effect is missing");
    };
    if (value.kind == bts::MaskEffectKind::Fill)
      edit_property(edit, "Mask Fill");
    else
      edit_material(edit);
  };
  callbacks.layer_context_changed = [&] { select_material(selected_material); };
  callbacks.add_mask = [&](float background) {
    edit_material([=](bts::TextureSet &set) { bts::TextureDocument::add_mask(set, background); });
    brush.mask_value = 1 - background;
    select_material(selected_material);
  };
  callbacks.add_bitmap_mask = [&] {
    edit_material([](bts::TextureSet &set) { bts::add_bitmap_mask_target(set); });
    if (last_material_error.isEmpty())
      if (auto *resource = workspace->findChild<QComboBox *>("maskFillResource"))
        resource->setFocus(Qt::OtherFocusReason);
  };
  callbacks.mask_selected = [&](QUuid id, bool mask) {
    workspace->set_instance_hierarchy_target();
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    auto &layer = bts::TextureDocument::layer(set, id);
    if (mask && !layer.mask)
      return;
    set.selected_geometry = false;
    set.selected_layer = id;
    set.selected_mask = mask;
    set.selected_effect = QUuid();
    select_material(selected_material);
    viewport->update();
  };
  callbacks.mask_isolated = [&](QUuid id) {
    workspace->set_instance_hierarchy_target();
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    if (!bts::TextureDocument::layer(set, id).mask)
      return;
    set.selected_geometry = false;
    set.selected_layer = id;
    set.selected_mask = true;
    set.selected_effect = QUuid();
    select_material(selected_material);
    update_inspection(bts::InspectionMode::Mask);
  };
  callbacks.mask_background_inverted = [&](QUuid id) {
    edit_material([=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, id);
      if (layer.mask)
        layer.mask->background = 1 - layer.mask->background;
    });
  };
  callbacks.mask_bypassed = [&](QUuid id) {
    edit_material([=](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, id);
      if (layer.mask)
        layer.mask->enabled = !layer.mask->enabled;
    });
  };
  callbacks.mask_value_changed = [&](double value) { brush.mask_value = float(value); };
  callbacks.add_folder = [&] {
    edit_material([](bts::TextureSet &set) { bts::TextureDocument::add_folder(set); });
  };
  callbacks.group_layers = [&] {
    std::vector<QUuid> ids;
    for (const auto *item : workspace->layer_stack()->selectedItems())
      if (item->data(Qt::UserRole + 10).toString().isEmpty())
        ids.emplace_back(item->data(Qt::UserRole).toString());
    edit_material([=](bts::TextureSet &set) { bts::TextureDocument::group_layers(set, ids); });
  };
  callbacks.flatten_layer = [&] {
    try {
      if (!document || selected_material < 0)
        return;
      viewport->end_paint(false);
      if (uniform_before)
        finish_uniform_edit(false);
      document->resolve_instances();
      const auto uid = snapshot.materials[selected_material].session_id;
      const auto &set = document->set(uid);
      if (!set.uniform_editable || set.selected_mask || set.selected_geometry ||
          !set.selected_effect.isNull())
        throw std::invalid_argument("Select a layer or group to flatten");
      const auto selected = set.selected_layer;
      if (auto ready = bts::choose_layer_flatten(workspace,
                                                 host.prepare_layer_capture(set),
                                                 selected,
                                                 {[&](const auto &source, const auto &request) {
                                                    const auto ticket = host.request_layer_capture(
                                                        source, request);
                                                    viewport->update();
                                                    return ticket;
                                                  },
                                                  [&] { return host.layer_capture_result(); }}))
      {
        for (auto &asset : ready->assets)
          assets.add(std::move(asset));
        edit_material([&](bts::TextureSet &target) {
          bts::install_flattened_layer(target, std::move(ready->bake.layer), selected);
        });
        workspace->refresh_alpha_shelf();
        workspace->append_console(
            "Created embedded flattened maps; source retained and disabled.");
      }
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  callbacks.precompose_layer = [&](bool update) {
    try {
      if (!document || selected_material < 0)
        return;
      viewport->end_paint(false);
      if (uniform_before)
        finish_uniform_edit(false);
      document->resolve_instances();
      const auto uid = snapshot.materials[selected_material].session_id;
      const auto &set = document->set(uid);
      bts::require_uncached_layer(set, set.selected_layer);
      const auto found = std::find_if(set.layers.begin(), set.layers.end(), [&](const auto &l) {
        return l.id == set.selected_layer;
      });
      if (found == set.layers.end() || found->depth != 0 || !set.uniform_editable)
        throw std::invalid_argument("Select a top-level layer for Pre-compose");
      const int index = int(found - set.layers.begin()) + (update ? 0 : 1);
      if (update && !found->precompose_below)
        throw std::invalid_argument("Select a Pre-compose cache to update");
      const auto revision = document_revision;
      auto capture_set = host.prepare_layer_capture(set);
      const auto &expanded = bts::evaluation_layers(capture_set);
      const auto capture_root = std::find_if(expanded.begin(), expanded.end(), [&](const auto &l) {
        return l.id == set.selected_layer;
      });
      if (capture_root == expanded.end())
        throw std::invalid_argument("Pre-compose source is unavailable");
      auto source = bts::precompose_capture_source(
          capture_set, int(capture_root - expanded.begin()) + (update ? 0 : 1));
      const auto group = source.selected_layer;
      if (auto ready = bts::choose_layer_flatten(workspace,
                                                 std::move(source),
                                                 group,
                                                 {[&](const auto &input, const auto &request) {
                                                    const auto ticket = host.request_layer_capture(
                                                        input, request);
                                                    viewport->update();
                                                    return ticket;
                                                  },
                                                  [&] { return host.layer_capture_result(); }}))
      {
        if (document_revision != revision)
          throw std::runtime_error("Layers changed during GPU capture; update Pre-compose again.");
        const QScopedValueRollback<bool> control(precompose_control_edit, true);
        edit_texture_set(
            uid,
            [&](bts::TextureSet &target) {
              bts::install_precompose_layer(target, std::move(ready->bake.layer), index, update);
            },
            {},
            [&] {
              for (auto &asset : ready->assets)
                assets.add(std::move(asset));
            });
        workspace->refresh_alpha_shelf();
        workspace->set_status(
            "Pre-compose cache ready. Layers below are frozen; hide its eye to edit the originals.");
      }
    }
    catch (const std::exception &error) {
      workspace->set_error_status(QString::fromUtf8(error.what()));
    }
  };
  callbacks.release_precompose = [&] {
    const QScopedValueRollback<bool> control(precompose_control_edit, true);
    edit_material([](bts::TextureSet &set) {
      bts::require_uncached_layer(set, set.selected_layer);
      auto &cache = bts::TextureDocument::layer(set, set.selected_layer);
      if (!cache.precompose_below)
        throw std::invalid_argument("Select a Pre-compose layer");
      cache.visible = false;
    });
  };
  callbacks.export_flattened = [&](bool mask_only) {
    try {
      if (!document || selected_material < 0)
        return;
      viewport->end_paint(false);
      if (uniform_before)
        finish_uniform_edit(false);
      document->resolve_instances();
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      if (!set.uniform_editable || !set.selected_effect.isNull())
        throw std::invalid_argument("Select layers/groups or a mask to export");
      std::vector<QUuid> ids;
      if (mask_only)
        ids.push_back(set.selected_layer);
      else
        for (const auto *item : workspace->layer_stack()->selectedItems())
          if (item->data(Qt::UserRole + 10).toString().isEmpty())
            ids.emplace_back(item->data(Qt::UserRole).toString());
      const auto tiles = host.export_source(*document, {set.id});
      if (!tiles.empty())
        bts::choose_flattened_export(workspace,
                                     host.prepare_layer_capture(set),
                                     ids,
                                     tiles.front().deferred_padding,
                                     mask_only,
                                     {[&](const auto &source, const auto &request) {
                                        const auto ticket = host.request_layer_capture(source,
                                                                                       request);
                                        viewport->update();
                                        return ticket;
                                      },
                                      [&] { return host.layer_capture_result(); }});
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  callbacks.tile_selected = [&](int) {
    viewport->end_paint(false);
    select_material(selected_material);
  };
  callbacks.tile_resolution_changed = [&](int tile, int resolution) {
    try {
      viewport->end_paint(false);
      edit_material([&](bts::TextureSet &set) {
        bts::TextureDocument::set_tile_resolution(set, tile, resolution);
      });
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      refresh_document_ui();
    }
  };
  callbacks.texture_resolution_changed = [&](int resolution) {
    try {
      viewport->end_paint(false);
      edit_material(
          [&](bts::TextureSet &set) { bts::TextureDocument::set_resolution(set, resolution); });
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      refresh_document_ui();
    }
  };
  callbacks.texture_height_changed = [&](int height) {
    try {
      viewport->end_paint(false);
      edit_material([&](bts::TextureSet &set) {
        bts::TextureDocument::set_dimensions(set, set.texture_resolution, height, false);
      });
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      refresh_document_ui();
    }
  };
  callbacks.texture_size_linked = [&](bool linked) {
    try {
      viewport->end_paint(false);
      edit_material([&](bts::TextureSet &set) {
        bts::TextureDocument::set_dimensions(
            set, set.texture_resolution, linked ? set.texture_resolution : set.rows(), linked);
      });
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
      refresh_document_ui();
    }
  };
  callbacks.add_paint = [&] {
    edit_material([](bts::TextureSet &set) { bts::TextureDocument::add_paint(set); });
  };
  callbacks.add_adjustment = [&] {
    edit_material([](bts::TextureSet &set) {
      bts::TextureDocument::add_paint(set);
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      bts::set_adjustment_layer(layer, true);
      layer.name = "Adjustment Paint";
    });
  };
  callbacks.layer_adjustment_changed = [&](QUuid id, bool enabled) {
    edit_material([=](bts::TextureSet &set) {
      bts::set_adjustment_layer(bts::TextureDocument::layer(set, id), enabled);
    });
  };
  callbacks.brush_pressure_changed = [&](int which, bool enabled) {
    (which == 0 ? brush.pressure_size : brush.pressure_flow) = enabled;
    select_material(selected_material);
    viewport->update();
  };
  callbacks.brush_minimum_changed = [&](int which, double value) {
    (which == 0 ? brush.minimum_size : brush.minimum_flow) = float(value);
    select_material(selected_material);
    viewport->update();
  };
  callbacks.fill_color_space_changed = [&](const QString &space) {
    try {
      const auto target = texture_target(0);
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (!layer.fill.texture)
        return;
      const auto found = std::find_if(
          assets.assets().begin(), assets.assets().end(), [&](const auto &asset) {
            return asset.id.toString(QUuid::WithoutBraces).toStdString() ==
                   layer.fill.texture->asset;
          });
      if (found == assets.assets().end())
        throw std::runtime_error("Texture resource is missing");
      texture_loader->from_asset(target, *found, space.toStdString());
    }
    catch (const std::exception &error) {
      texture_error(QString::fromUtf8(error.what()));
    }
  };
  callbacks.fill_texture_file_requested = [&] {
    auto *dialog = bts::FileDialog::borrow(
        workspace,
        "Fill texture",
        {},
        "Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp)");
    dialog->setObjectName("fillTextureFileDialog");
    dialog->setFileMode(QFileDialog::ExistingFile);
    if (dialog->exec() != QDialog::Accepted || dialog->selectedFiles().isEmpty())
      return;
    const auto files = dialog->selectedFiles();
    load_texture_files(workspace->fill_resource_channel(), files, false);
  };
  callbacks.layer_texture_file_requested = [&] {
    const auto path = bts::FileDialog::getOpenFileName(
        workspace,
        "Load image as fill layer",
        {},
        "Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp)");
    if (!path.isEmpty())
      load_texture_files(-1, {path}, true);
  };
  callbacks.fill_texture_files_dropped = [&](int channel, const QStringList &paths) {
    load_texture_files(channel, paths, false);
  };
  callbacks.layer_texture_files_dropped = [&](const QStringList &paths) {
    load_texture_files(-1, paths, true);
  };
  callbacks.fill_texture_asset_dropped = [&](int channel, const QString &id) {
    try {
      if (id.startsWith("baked:")) {
        texture_loader->from_asset(texture_target(channel), baked_asset_from_key(id));
        return;
      }
      const auto found = std::find_if(assets.assets().begin(),
                                      assets.assets().end(),
                                      [&](const auto &asset) { return asset.id == QUuid(id); });
      if (found == assets.assets().end() || !found->usages.contains("texture"))
        throw std::runtime_error("Choose a Shelf resource with Usage Texture");
      texture_loader->from_asset(texture_target(channel), *found);
    }
    catch (const std::exception &error) {
      texture_error(QString::fromUtf8(error.what()));
    }
  };
  callbacks.fill_texture_cancelled = [&](int channel) {
    try {
      texture_loader->cancel(texture_target(channel));
    }
    catch (...) {
    }
  };
  callbacks.cancel_texture_loads = [&] {
    texture_loader->cancel_all();
#ifdef BTS_WITH_MATERIALX
    if (material_importer)
      material_importer->cancel();
#endif
  };
  callbacks.fill_channel_enabled = [&](int channel, bool enabled) {
    if (!document || selected_material < 0)
      return;
    viewport->end_paint(false);
    const auto uid = snapshot.materials[selected_material].session_id;
    auto &before_set = document->set(uid);
    const auto previous = bts::TextureDocument::layer(before_set, before_set.selected_layer);
    const auto revision = document_revision;
    if (!enabled) {
      try {
        texture_loader->cancel(texture_target(channel));
      }
      catch (...) {
      }
    }
    edit_material([&](bts::TextureSet &set) {
      bts::set_layer_channel(set, set.selected_layer, channel, enabled);
    });
    if (document_revision != revision) {
      document->resolve_instances();
      try {
        host.retire_layer_channel(*document, document->set(uid), previous, channel);
      }
      catch (const std::exception &error) {
        workspace->append_console("Channel cache retirement: " + QString::fromUtf8(error.what()));
      }
      viewport->update();
    }
    else
      select_material(selected_material);
  };
  callbacks.fill_projection_changed = [&](const bts::FillProjection &p) {
    bts::validate_fill_projection(p);
    if (!document || selected_material < 0)
      return;
    const auto &target = document->set(snapshot.materials[selected_material].session_id);
    const auto layer_id = target.selected_layer, effect_id = target.selected_effect;
    const bool mask = target.selected_mask,
               auto_physical = document->settings().units.auto_physical;
    edit_property(
        [p, layer_id, effect_id, mask, auto_physical](bts::TextureSet &set) {
          auto &layer = bts::TextureDocument::layer(set, layer_id);
          if (mask && layer.mask) {
            if (auto *found = bts::edit_mask_effect(
                    layer.mask->effects, effect_id, bts::MaskEffectKind::Fill);
                found)
            {
              auto &effect = *found;
              effect.mapping.projection = p;
              return;
            }
          }
          if (layer.kind == bts::LayerKind::Fill && !mask) {
            for (int c = 0; c < 15; ++c)
              if (p.anchors[c] != layer.fill.anchors[c] && !p.anchors[c].source.isNull()) {
                const auto reason = bts::TextureDocument::anchor_reference_problem(
                    set, layer_id, p.anchors[c]);
                if (!reason.isEmpty())
                  throw std::invalid_argument(reason.toStdString());
              }
            auto mapping = p;
            if (p.atom && !layer.fill.atom) {
              mapping.asset_size_cm = {10, 10, 10};
              mapping.custom_size = false;
              if (auto_physical)
                mapping.physical = true;
            }
            layer.fill = mapping;
          }
        },
        "Fill mapping");
  };
  callbacks.warp_selection_changed = [&](std::vector<int> ids) {
    viewport->warp_editor.set_selection(std::move(ids));
    viewport->update();
  };
  callbacks.warp_split_requested = [&](int mode) {
    viewport->warp_editor.set_split_mode(mode);
    viewport->update();
  };
  callbacks.warp_space_changed = [&](bool local) {
    viewport->warp_editor.set_local_space(local);
    viewport->update();
  };
  viewport->warp_editor.changed = [&](const bts::FillProjection &visible) {
    const auto p = bts::rebase_warp_view_edit(visible);
    workspace->edit_fill_projection(p);
    viewport->fill_projection = p;
    viewport->sync_warp_projection();
    viewport->update();
  };
  viewport->warp_editor.started = [&] {
    begin_property_edit("Fill mapping");
    property_dragging = true;
    property_idle_timer.stop();
  };
  viewport->warp_editor.finished = [&](bool cancel) {
    property_dragging = false;
    finish_uniform_edit(cancel);
  };
  viewport->fill_gizmo.started = viewport->warp_editor.started;
  viewport->fill_gizmo.finished = viewport->warp_editor.finished;
  viewport->fill_gizmo.error = [&](const QString &error) {
    workspace->set_status("Fill mapping: " + error);
    workspace->append_console("Fill mapping: " + error);
  };
  viewport->fill_gizmo.changed = [&](const bts::FillProjection &p) {
    workspace->edit_fill_projection(p);
    viewport->fill_projection = p;
    viewport->update();
  };
  viewport->warp_editor.selection_changed = [&](std::vector<int> ids) {
    workspace->set_warp_selection(std::move(ids));
  };
  viewport->warp_editor.split_cancelled = [&] { workspace->cancel_warp_split(); };
  viewport->warp_editor.error = [&](const QString &error) {
    workspace->append_console("Warp: " + error);
  };
  callbacks.polygon_tool_changed = [&](bool active) {
    if (quick_editing)
      return;
    if (leave_path)
      leave_path();
    viewport->end_paint(false);
    viewport->polygon_active = active && viewport->paint_enabled;
    if (viewport->polygon_active && viewport->paint_mask)
      viewport->polygon_value = brush.mask_value;
    viewport->polygon_drag = false;
    viewport->picker_active = false;
    workspace->set_picker_active(false);
    workspace->set_polygon_fill(
        viewport->polygon_active, int(viewport->polygon_mode), viewport->polygon_value);
    select_material(selected_material);
    viewport->update();
  };
  callbacks.polygon_fill_changed = [&](int mode, double value) {
    viewport->polygon_mode = bts::PolygonFillMode(mode);
    viewport->polygon_value = float(value);
    workspace->set_polygon_fill(viewport->polygon_active, mode, value);
    viewport->update();
  };
  viewport->polygon_tool_changed = callbacks.polygon_tool_changed;
  viewport->polygon_apply = [&](const std::vector<std::size_t> &selection) {
    bts::performance::Scope timing("polygon_fill_apply");
    auto stamp = bts::polygon_fill_stamp(snapshot, selection, viewport->polygon_value);
    if (skew && skew->painting()) {
      skew->polygon(stamp);
      return;
    }
    stamp.values = bts::temporary_ink(
        brush.values, viewport->secondary_color, viewport->secondary_active_);
    stamp.channels = brush.channels;
    stamp.hardness = 1;
    stamp.flow = 1;
    if (viewport->paint_mask) {
      stamp.values.roughness = bts::temporary_mask_ink(viewport->polygon_value,
                                                       viewport->secondary_active_);
      stamp.channels = {false, false, true, false, false};
    }
    const QScopedValueRollback<bool> erase(brush.erase, false), smudge(brush.smudge, false),
        clone(brush.clone, false);
    if (!viewport->stroke_begin())
      return;
    try {
      viewport->stroke_stamps({stamp}, 1);
      stroke_spatial = false;
      viewport->stroke_end(false);
    }
    catch (...) {
      viewport->stroke_end(true);
      throw;
    }
  };
  change_id_picker = [&](bool active) {
    if (active) {
      if (!document || selected_material < 0)
        return;
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      if (!set.selected_mask || set.selected_effect.isNull() || !set.mesh_maps[2])
        return;
      id_picker_previous_mode = host.inspection_mode();
      id_picker_previous_map = inspected_map;
      id_picker_material = set.material_session_uid;
      id_picker_effect = set.selected_effect;
      id_picker_active = true;
      viewport->picker_active = true;
      viewport->mesh_map_requested(2);
    }
    else if (id_picker_active) {
      id_picker_active = false;
      viewport->picker_active = false;
      if (id_picker_previous_map >= 0)
        viewport->mesh_map_requested(id_picker_previous_map);
      else {
        inspected_map = -1;
        viewport->mesh_map_active = -1;
        update_inspection(id_picker_previous_mode);
      }
    }
    workspace->set_id_picker_active(id_picker_active);
    viewport->update();
  };
  callbacks.id_pick_changed = change_id_picker;
  callbacks.picker_changed = [&](bool active) {
    if (!active) {
      color_picker_pending = false;
      secondary_picker_pending = false;
    }
    if (id_picker_active) {
      if (!active)
        change_id_picker(false);
      return;
    }
    viewport->end_paint(false);
    bool can_pick = viewport->paint_enabled;
    if (!can_pick && document && selected_material >= 0) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      if (set.uniform_editable && !set.selected_mask && !set.selected_geometry &&
          set.selected_effect.isNull())
      {
        const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                        set.selected_layer);
        can_pick = layer.kind == bts::LayerKind::Fill && !layer.instance &&
                   !layer.fill.native_material;
      }
    }
    viewport->picker_active = active && can_pick;
    workspace->set_picker_active(viewport->picker_active);
    viewport->update();
  };
  viewport->picker_changed = callbacks.picker_changed;
  QObject::connect(&material_pick_timer, &QTimer::timeout, viewport, [&] {
    if (!pending_material_pick) {
      material_pick_timer.stop();
      return;
    }
    const auto &pending = *pending_material_pick;
    if (!document || document->id() != pending.document || document_revision != pending.revision ||
        selected_material < 0 || viewport->painting_ || brush.values != pending.brush_values)
    {
      pending_material_pick.reset();
      viewport->setProperty("gpuMaterialPickPending", false);
      material_pick_timer.stop();
      return;
    }
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    if (set.id != pending.set || set.selected_layer != pending.layer ||
        set.selected_effect != pending.effect || set.selected_mask != pending.selected_mask)
    {
      pending_material_pick.reset();
      viewport->setProperty("gpuMaterialPickPending", false);
      material_pick_timer.stop();
      return;
    }
    const auto sampled = host.material_sample_result();
    if (!sampled || sampled->ticket != pending.ticket)
      return;
    const auto completed = pending;
    pending_material_pick.reset();
    viewport->setProperty("gpuMaterialPickPending", false);
    material_pick_timer.stop();
    if (!sampled->valid) {
      workspace->append_console("Material pick failed: GPU channel data unavailable");
      window->statusBar()->showMessage("Material pick failed", 4000);
      return;
    }
    if (completed.color) {
      auto *picker = static_cast<bts::ColorPicker *>(workspace->findChild<QWidget *>(
          completed.secondary ? "secondaryColorPicker" : "materialColorPicker"));
      if (picker)
        picker->accept_sample(sampled->material.base_color);
    }
    else {
      bts::apply_material_sample(completed.request, *sampled, brush);
      select_material(selected_material);
    }
    std::printf("BTS_GPU_MATERIAL_PICK ticket=%llu packet_bytes=96 cpu_composition=0\n",
                (unsigned long long)completed.ticket);
    viewport->update();
  });
  viewport->picker_sample = [&](QPointF uv) {
    if (!document || selected_material < 0)
      return false;
    if (id_picker_active) {
      const auto &set = document->set(id_picker_material);
      if (uv.x() < 0 || uv.x() >= 10 || uv.y() < 0 || uv.y() >= 100)
        return false;
      const int tile = 1001 + int(uv.x()) + 10 * int(uv.y());
      if (!set.tiles.contains(tile))
        return false;
      const auto sample = bts::sample_id_map(set.mesh_maps[2] ? &*set.mesh_maps[2] : nullptr,
                                             uv.x() - std::floor(uv.x()),
                                             uv.y() - std::floor(uv.y()),
                                             tile);
      if (sample[3] <= 1e-8f)
        return false;
      std::array<float, 3> color;
      for (int c = 0; c < 3; ++c) {
        if (sample[c] < 0 || sample[c] > 1)
          return false;
        color[c] = sample[c];
      }
      edit_material([&](bts::TextureSet &candidate) {
        auto &mask = bts::TextureDocument::layer(candidate, candidate.selected_layer).mask;
        if (!mask)
          return;
        if (auto *found = bts::edit_mask_effect(mask->effects, id_picker_effect);
            found && found->kind == bts::MaskEffectKind::ColorSelection)
        {
          auto &effect = *found;
          if (std::find(effect.colors.begin(), effect.colors.end(), color) == effect.colors.end())
            effect.colors.push_back(color);
        }
      });
      return true;
    }
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    const bool pick_color_only = color_picker_pending || !viewport->paint_enabled;
    const auto request = bts::material_sample_request(set, uv.x(), uv.y(), brush, pick_color_only);
    if (!request)
      return false;
    pending_material_pick = PendingMaterialPick{*request,
                                                host.request_material_sample(set, *request),
                                                document_revision,
                                                document->id(),
                                                set.id,
                                                set.selected_layer,
                                                set.selected_effect,
                                                set.selected_mask,
                                                pick_color_only,
                                                secondary_picker_pending,
                                                brush.values};
    viewport->setProperty("gpuMaterialPickPending", true);
    material_pick_timer.start();
    viewport->update();
    return true;
  };
  callbacks.lazy_mouse_changed = [&](bool enabled, double distance) {
    if (viewport->path_active)
      return;
    viewport->end_paint(false);
    brush.lazy_mouse = enabled;
    brush.lazy_distance = float(distance);
    select_material(selected_material);
    viewport->update();
  };
  callbacks.brush_angle_changed = [&](double angle) {
    viewport->end_paint(false);
    brush.angle = float(angle);
    select_material(selected_material);
    viewport->update();
  };
  callbacks.brush_follow_path_changed = [&](bool follow) {
    viewport->end_paint(false);
    brush.follow_path = follow;
    select_material(selected_material);
    viewport->update();
  };
  callbacks.brush_jitter_changed = [&](int field, double value) {
    viewport->end_paint(false);
    auto next = brush.jitter;
    switch (field) {
      case 0:
        next.size = value;
        break;
      case 1:
        next.flow = value;
        break;
      case 2:
        next.angle = value;
        break;
      case 3:
        next.position = value;
        break;
      default:
        throw std::invalid_argument("Invalid jitter control");
    }
    next.validate();
    brush.jitter = next;
    select_material(selected_material);
    viewport->update();
  };
  callbacks.brush_spacing_changed = [&](double value) {
    brush.spacing = float(value);
    select_material(selected_material);
  };
  callbacks.symmetry_changed = [&](const bts::SymmetrySettings &settings) {
    if (document && selected_material >= 0 && viewport->fill_projection) {
      if (!bts::fill_surface_projection(*viewport->fill_projection))
        return;
      settings.validate();
      viewport->end_paint(false);
      const auto &selected = document->set(snapshot.materials[selected_material].session_id);
      const auto layer_id = selected.selected_layer, effect_id = selected.selected_effect;
      const bool mask = selected.selected_mask;
      auto options = std::make_shared<bts::FillSymmetry>(viewport->fill_projection->symmetry ?
                                                             *viewport->fill_projection->symmetry :
                                                             bts::FillSymmetry{});
      options->constraint = settings;
      edit_material([=](bts::TextureSet &set) {
        auto &layer = bts::TextureDocument::layer(set, layer_id);
        if (mask && layer.mask) {
          if (auto *found = bts::edit_mask_effect(
                  layer.mask->effects, effect_id, bts::MaskEffectKind::Fill);
              found)
          {
            auto &effect = *found;
            effect.mapping.projection.symmetry = options;
            return;
          }
        }
        else if (layer.kind == bts::LayerKind::Fill)
          layer.fill.symmetry = options;
      });
      return;
    }
    viewport->end_paint(false);
    host.set_symmetry(settings);
    symmetry = settings;
    workspace->set_symmetry(symmetry);
    dirty = bool(document);
    if (dirty)
      window->setWindowTitle((document_path.isEmpty() ?
                                  QString("Untitled") :
                                  QFileInfo(document_path).completeBaseName()) +
                             " * — mPainter");
    viewport->update();
  };
  viewport->symmetry_changed = callbacks.symmetry_changed;
  callbacks.ao_mixing_changed = [&](int mode) {
    edit_material([=](bts::TextureSet &set) { set.ao_mixing = bts::AoMixing(mode); });
  };
  callbacks.ao_changed = [&](double value) {
    if (viewport->paint_enabled) {
      brush.values.ambient_occlusion = float(value);
      if (viewport->path_active && path_brush_changed)
        path_brush_changed();
      return;
    }
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::edit_values(set).ambient_occlusion = float(value);
    });
  };
  callbacks.bump_method_changed = [&](int method) {
    edit_material([=](bts::TextureSet &set) { set.bump_method = bts::BumpMethod(method); });
  };
  callbacks.bump_changed = [&](double value) {
    if (viewport->paint_enabled) {
      brush.values.bump = float(value);
      if (viewport->path_active && path_brush_changed)
        path_brush_changed();
      return;
    }
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::edit_values(set).bump = set.bump_storage(float(value));
    });
  };
  callbacks.normal_changed = [&](int component, double value) {
    if (viewport->paint_enabled) {
      brush.values.normal.at(component) = float(value);
      select_material(selected_material);
      return;
    }
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::edit_values(set).normal.at(component) = float(value);
    });
  };
  callbacks.normal_mixing_changed = [&](int mode) {
    edit_material([=](bts::TextureSet &set) { set.normal_mixing = bts::NormalMixing(mode); });
  };
  callbacks.padding_changed = [&](bool adjacent) {
    edit_material([=](bts::TextureSet &set) { set.padding_3d = adjacent; });
  };
  callbacks.brush_projection_changed = [&](int mode, bool cull, double angle) {
    viewport->end_paint(false);
    brush.projection.alignment = bts::BrushAlignment(mode);
    brush.projection.cull = cull;
    brush.projection.angle = float(angle);
    select_material(selected_material);
    viewport->update();
  };
  callbacks.stencil_changed = [&](const bts::BrushStencil &settings) {
    settings.validate();
    viewport->end_paint(false);
    if (brush.projected)
      brush.projected->transform = settings;
    else
      brush.stencil = settings;
    select_material(selected_material);
    viewport->update();
  };
  viewport->stencil_changed = callbacks.stencil_changed;
  callbacks.stencil_asset_requested = [&](QUuid id) {
    try {
      for (const auto &asset : assets.assets())
        if (asset.id == id) {
          auto alpha = asset.alpha;
          if (!alpha && !asset.bytes.isEmpty()) {
            bts::BrushAlpha image;
            image.kind = bts::AlphaKind::Bitmap;
            image.bitmap = bts::read_bitmap_alpha_bytes(asset.bytes, asset.name);
            alpha = image;
          }
          if (!alpha)
            return false;
          auto settings = brush.stencil;
          settings.resource = *alpha;
          viewport->stencil_changed(settings);
          return true;
        }
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
    return false;
  };
  callbacks.stencil_import_requested = [&] {
    bts::FileDialog dialog(workspace,
                           "Import stencil image",
                           {},
                           "Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp)");
    dialog.setObjectName("stencilImportDialog");
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::ExistingFile);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
      return;
    try {
      bts::BrushAlpha alpha;
      alpha.kind = bts::AlphaKind::Bitmap;
      alpha.bitmap = bts::read_bitmap_alpha(dialog.selectedFiles().front());
      auto settings = brush.stencil;
      settings.resource = alpha;
      viewport->stencil_changed(settings);
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  callbacks.dynamic_stroke_changed = [&](const bts::DynamicStrokeParameters &value) {
    if (brush.alpha.kind != bts::AlphaKind::DynamicOutline)
      return;
    value.validate();
    viewport->end_paint(false);
    brush.alpha.dynamic = value;
    select_material(selected_material);
    viewport->update();
  };
  callbacks.alpha_selected = [&](const bts::BrushAlpha &alpha) {
    viewport->end_paint(false);
    brush.alpha = alpha;
    select_material(selected_material);
    viewport->update();
  };
  callbacks.alpha_parameters_changed = [&](bool inverted, double uniform, double hardness) {
    viewport->end_paint(false);
    brush.alpha.inverted = inverted;
    brush.alpha.uniform = float(uniform);
    brush.hardness = float(hardness);
    select_material(selected_material);
    viewport->update();
  };
  callbacks.alpha_import_requested = [&] {
    bts::FileDialog dialog(window.get(),
                           "Import alpha for this session",
                           {},
                           "Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp)");
    dialog.setObjectName("alphaImportDialog");
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::ExistingFile);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
      return;
    try {
      bts::BrushAlpha alpha;
      alpha.kind = bts::AlphaKind::Bitmap;
      alpha.bitmap = bts::read_bitmap_alpha(dialog.selectedFiles().front());
      viewport->end_paint(false);
      brush.alpha = alpha;
      workspace->add_alpha_resource(alpha);
      select_material(selected_material);
      viewport->update();
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  callbacks.tool_selected = [&](bool erase) {
    if (skew && skew->painting()) {
      viewport->end_paint(false);
      if (brush.erase)
        skew_eraser = brush;
      else
        skew_brush = brush;
      brush = erase ? skew_eraser : skew_brush;
      viewport->polygon_active = false;
      select_material(selected_material);
      return;
    }
    if (quick_editing)
      return;
    if (leave_path)
      leave_path();
    viewport->polygon_active = false;
    viewport->polygon_drag = false;
    workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    viewport->picker_active = false;
    workspace->set_picker_active(false);
    if (!viewport->paint_enabled)
      return;
    if (!brush.projected && !brush.smudge && !brush.clone && brush.erase == erase) {
      select_material(selected_material);
      return;
    }
    viewport->end_paint(false);
    if (brush.clone)
      clone_brush = brush;
    else if (brush.projected)
      projection_brush = brush;
    else if (brush.smudge)
      smudge_brush = brush;
    else if (brush.erase)
      eraser_brush = brush;
    else
      paint_brush = brush;
    brush = erase ? eraser_brush : paint_brush;
    brush.erase = erase;
    select_material(selected_material);
    viewport->update();
  };
  viewport->tool_requested = callbacks.tool_selected;
  callbacks.projection_selected = [&] {
    if (skew && skew->mode() > 0)
      return;
    if (quick_editing)
      return;
    if (leave_path)
      leave_path();
    if (!viewport->paint_enabled)
      return;
    viewport->end_paint(false);
    viewport->polygon_active = false;
    viewport->polygon_drag = false;
    workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    viewport->picker_active = false;
    workspace->set_picker_active(false);
    if (!brush.projected) {
      if (brush.clone)
        clone_brush = brush;
      else if (brush.smudge)
        smudge_brush = brush;
      else if (brush.erase)
        eraser_brush = brush;
      else
        paint_brush = brush;
      brush = projection_brush;
    }
    select_material(selected_material);
    viewport->update();
  };
  viewport->projection_tool_requested = callbacks.projection_selected;
  callbacks.smudge_selected = [&] {
    if (skew && skew->mode() > 0)
      return;
    if (quick_editing)
      return;
    if (leave_path)
      leave_path();
    if (!viewport->paint_enabled)
      return;
    viewport->end_paint(false);
    viewport->polygon_active = false;
    viewport->polygon_drag = false;
    workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    viewport->picker_active = false;
    workspace->set_picker_active(false);
    if (!brush.smudge) {
      if (brush.clone)
        clone_brush = brush;
      else if (brush.projected)
        projection_brush = brush;
      else if (brush.erase)
        eraser_brush = brush;
      else
        paint_brush = brush;
      brush = smudge_brush;
    }
    select_material(selected_material);
    viewport->update();
  };
  viewport->smudge_tool_requested = callbacks.smudge_selected;

  callbacks.clone_selected = [&] {
    if (skew && skew->mode() > 0)
      return;
    if (quick_editing)
      return;
    if (leave_path)
      leave_path();
    if (!viewport->paint_enabled)
      return;
    viewport->end_paint(false);
    viewport->polygon_active = false;
    viewport->polygon_drag = false;
    workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    viewport->picker_active = false;
    workspace->set_picker_active(false);
    if (!brush.clone) {
      if (brush.projected)
        projection_brush = brush;
      else if (brush.smudge)
        smudge_brush = brush;
      else if (brush.erase)
        eraser_brush = brush;
      else
        paint_brush = brush;
      brush = clone_brush;
    }
    select_material(selected_material);
    viewport->update();
  };
  viewport->clone_tool_requested = callbacks.clone_selected;
  callbacks.quick_mask_action = [&](int action) {
    if (!document || selected_material < 0)
      return;
    if (leave_path)
      leave_path();
    viewport->end_paint(true);
    const auto uid = snapshot.materials[selected_material].session_id;
    bool next_edit = quick_editing;
    if (action == 0) {
      if (quick_enabled) {
        quick_enabled = false;
        next_edit = false;
      }
      else {
        quick_enabled = true;
        next_edit = true;
        quick_masks.try_emplace(uid);
      }
    }
    else if (action == 1 && quick_enabled)
      next_edit = !quick_editing;
    else if (action == 2 && quick_enabled)
      quick_masks[uid].inverted = !quick_masks[uid].inverted;
    else if (action == 3) {
      quick_enabled = false;
      next_edit = false;
      quick_masks.clear();
    }
    if (next_edit != quick_editing) {
      if (next_edit) {
        quick_return_brush = brush;
        brush = quick_brush;
      }
      else {
        quick_brush = brush;
        brush = quick_return_brush;
      }
      quick_editing = next_edit;
    }
    viewport->polygon_active = false;
    workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    select_material(selected_material);
    refresh_quick_preview();
  };
  viewport->quick_mask_requested = callbacks.quick_mask_action;
  callbacks.path_selected = [&] {
    if (skew && skew->mode() > 0)
      return;
    if (!viewport->paint_enabled || quick_editing)
      return;
    viewport->end_paint(false);
    viewport->polygon_active = false;
    viewport->polygon_drag = false;
    viewport->picker_active = false;
    workspace->set_picker_active(false);
    workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
    if (!viewport->path_active) {
      path_return_brush = brush;
      path_return_symmetry = symmetry;
      if (brush.projected)
        projection_brush = brush;
      else if (brush.smudge)
        smudge_brush = brush;
      else if (brush.clone)
        clone_brush = brush;
      else if (brush.erase)
        eraser_brush = brush;
      else
        paint_brush = brush;
      brush = paint_brush;
      viewport->path_active = true;
      viewport->path_editing = false;
      viewport->selected_path.clear();
      viewport->pending_path_point.reset();
    }
    select_material(selected_material);
    viewport->update();
  };
  callbacks.clone_mode_changed = [&](bool absolute) {
    viewport->end_paint(false);
    brush.clone_absolute = absolute;
    select_material(selected_material);
    viewport->update();
  };
  callbacks.projection_material_changed = [&](const bts::ProjectionMaterial &source) {
    if (!brush.projected)
      return;
    source.validate();
    viewport->end_paint(false);
    brush.projected = source;
    select_material(selected_material);
    viewport->update();
  };
  callbacks.projection_resource_selected = [&](bool weave) {
    if (!brush.projected)
      return;
    viewport->end_paint(false);
    brush.projected->weave = weave;
    brush.projected->name = weave ? "Blue Weave" : "uniform color";
    brush.values = bts::ProjectionMaterial{}.values;
    if (weave) {
      brush.values.base_color = {.025f, .06f, .16f, 1};
      brush.values.roughness = .8824f;
    }
    brush.channels = {true, true, true, !weave, true};
    select_material(selected_material);
    viewport->update();
  };

  callbacks.brush_opacity_changed = [&](double value) {
    brush.stroke_opacity = float(value);
    select_material(selected_material);
  };
  callbacks.brush_flow_changed = [&](double value) {
    brush.flow = float(value);
    select_material(selected_material);
  };
  callbacks.brush_size_changed = [&](double value) {
    brush.diameter = float(value);
    select_material(selected_material);
    viewport->update();
  };
  callbacks.brush_channel_changed =
      [&, change_channel = callbacks.fill_channel_enabled](int channel, bool enabled) {
        change_channel(channel, enabled);
      };
  callbacks.add_fill = [&] {
    const QScopedValueRollback<bool> insertion(known_fill_insertion, true);
    edit_material([](bts::TextureSet &set) { bts::TextureDocument::add_fill(set); });
  };
  callbacks.layer_selected = [&](QUuid id) {
    bts::performance::Scope timing("layer_selected");
    workspace->set_instance_hierarchy_target();
    if (!document || selected_material < 0)
      return;
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    bts::TextureDocument::layer(set, id);
    if (set.selected_layer != id)
      set.selected_mask = false;
    set.selected_geometry = false;
    set.selected_layer = id;
    set.selected_effect = QUuid();
    const QScopedValueRollback<bool> selection_scope(layer_selection_refresh, true);
    workspace->setProperty("layerSelectionOnly", true);
    const auto selection_property = qScopeGuard(
        [&] { workspace->setProperty("layerSelectionOnly", false); });
    select_material(selected_material);
    if (bts::layer_is_cached(set, id))
      workspace->set_status(
          "Layer is cached. Hide Pre-compose or use Unfreeze / Release Pre-compose to edit it.");
  };
  const auto rename_metadata = [&](const QString &label, const std::function<void()> &apply) {
    if (!document || selected_material < 0)
      return;
    viewport->end_paint(false);
    auto before = std::make_unique<bts::TextureDocument>(*document);
    try {
      apply();
      undo_history.push_back(
          {std::move(before), snapshot.materials[selected_material].session_id, label});
      if (undo_history.size() > 128)
        undo_history.erase(undo_history.begin());
      redo_history.clear();
      refresh_texture_set_list();
      workspace->refresh_alpha_shelf();
      const QScopedValueRollback<bool> selection(layer_selection_refresh, true);
      const auto previous = workspace->property("layerSelectionOnly");
      workspace->setProperty("layerSelectionOnly", true);
      {
        const auto restore = qScopeGuard(
            [&] { workspace->setProperty("layerSelectionOnly", previous); });
        select_material(selected_material);
      }
      mark_document_changed();
      refresh_history_actions();
    }
    catch (const std::exception &error) {
      workspace->set_error_status(QString::fromUtf8(error.what()));
    }
  };
  callbacks.layer_item_renamed =
      [&, rename_metadata](QUuid layer, QUuid effect, const QString &value) {
        if (!document || selected_material < 0)
          return;
        const auto uid = snapshot.materials[selected_material].session_id;
        rename_metadata("Rename layer item", [&] {
          bts::require_uncached_layer(document->set(uid), layer);
          bts::rename_layer_item(document->set(uid), layer, effect, value);
        });
      };
  callbacks.texture_set_renamed = [&, rename_metadata](std::uint32_t uid, const QString &value) {
    if (!document)
      return;
    rename_metadata("Rename texture set", [&] {
      const auto name = bts::checked_name(value);
      const auto actual = host.rename_material(uid, name.toStdString());
      document->set(uid).name = actual;
      for (auto &material : snapshot.materials)
        if (material.session_id == uid)
          material.name = actual;
    });
  };
  callbacks.asset_renamed = [&, rename_metadata](QUuid id, const QString &value) {
    QString before;
    for (const auto &asset : assets.assets())
      if (asset.id == id)
        before = asset.name;
    if (before.isEmpty() || before == value.trimmed())
      return;
    const auto revision = document_revision;
    rename_metadata("Rename resource", [&] { assets.rename(id, bts::checked_name(value)); });
    if (document_revision != revision)
      undo_history.back().asset_name = std::pair{id, before};
    workspace->refresh_alpha_shelf();
  };
  callbacks.layer_visibility_changed = [&](QUuid id, bool visible) {
    if (document && selected_material >= 0 &&
        bts::layer_is_cached(document->set(snapshot.materials[selected_material].session_id), id))
    {
      workspace->set_status("Layer is cached. Hide Pre-compose to edit layers below.");
      return;
    }
    const QScopedValueRollback<bool> control(precompose_control_edit, true);
    edit_material(
        [=](bts::TextureSet &set) { bts::TextureDocument::layer(set, id).visible = visible; });
  };
  callbacks.layer_hierarchy_dropped =
      [&](const std::vector<QUuid> &ids, QUuid target, bool into, bool above) {
        edit_material([&](bts::TextureSet &set) {
          bts::drop_layer_subtrees(set, ids, target, into, above);
        });
      };
  callbacks.layer_dropped = [&](QUuid id, int target_row) {
    auto *list = workspace->layer_stack();
    if (target_row < 0 || target_row >= list->count())
      return;
    const QUuid target(list->item(target_row)->data(Qt::UserRole).toString());
    int source_row = -1;
    for (int row = 0; row < list->count(); ++row)
      if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == id &&
          list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
        source_row = row;
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::move_layer_relative(set, id, target, target_row < source_row);
    });
  };
  callbacks.layer_moved = [&](QUuid id, int offset) {
    edit_material(
        [=](bts::TextureSet &set) { bts::TextureDocument::move_layer(set, id, offset); });
  };
  callbacks.scattering_added = [&] {
    edit_material(
        [&](bts::TextureSet &set) { bts::TextureDocument::add_scattering_channel(set); });
  };
  callbacks.displacement_channel_changed = [&, change_channel = callbacks.fill_channel_enabled](
                                               bool enabled) { change_channel(14, enabled); };
  callbacks.displacement_changed = [&](const bts::DisplacementSettings &state) {
    if (!document || selected_material < 0)
      return;
    if (uniform_before && finish_uniform_edit)
      finish_uniform_edit(false);
    bts::EditTiming timing("BTS_EDIT_MS");
    const auto uid = snapshot.materials[selected_material].session_id;
    if (document->set(uid).displacement == state)
      return;
    auto candidate = std::make_unique<bts::TextureDocument>(*document);
    candidate->set(uid).displacement = state;
    state.validate();
    host.preview_displacement(candidate->set(uid));
    undo_history.push_back({std::move(document), uid, "Edit displacement"});
    if (undo_history.size() > 128)
      undo_history.erase(undo_history.begin());
    redo_history.clear();
    document = std::move(candidate);
    refresh_document_ui(false, uid);
  };
  callbacks.displacement_brush_changed = [&, change_channel = callbacks.fill_channel_enabled](
                                             bool enabled) { change_channel(14, enabled); };
  callbacks.displacement_value_changed = [&](double value) {
    if (viewport->paint_enabled) {
      brush.displacement_value = float(value);
      if (viewport->path_active && path_brush_changed)
        path_brush_changed();
    }
    else
      edit_material([&](bts::TextureSet &set) {
        bts::TextureDocument::layer(set, set.selected_layer).displacement.value = float(value);
      });
  };
  callbacks.user_channel_changed = [&](int slot, bool enabled) {
    edit_material([&](bts::TextureSet &set) {
      if (enabled)
        bts::TextureDocument::add_user_channel(set, slot);
      else
        set.users.at(slot).present = false;
    });
  };
  callbacks.user_renamed = [&](int slot, const QString &name) {
    edit_material([&](bts::TextureSet &set) {
      bts::TextureDocument::rename_user_channel(set, slot, name.toStdString());
    });
  };
  callbacks.user_precision_changed = [&](int slot, int precision) {
    edit_material([&](bts::TextureSet &set) {
      if (precision < 0 || precision > 8)
        throw std::invalid_argument("Invalid custom precision");
      set.users.at(slot).precision = bts::ChannelPrecision(precision);
    });
  };
  callbacks.user_brush_changed =
      [&, change_channel = callbacks.fill_channel_enabled](int slot, bool enabled) {
        change_channel(slot + 6, enabled);
      };
  callbacks.user_value_changed = [&](int slot, double value) {
    if (viewport->paint_enabled) {
      brush.user_values.at(slot) = float(value);
      return;
    }
    bts::CompositeChannels channels{};
    channels.at(7 + slot) = true;
    queued_property_channels = channels;
    edit_property(
        [slot, value](bts::TextureSet &set) {
          bts::TextureDocument::layer(set, set.selected_layer).users.at(slot).value = float(value);
        },
        QString("User channel %1 value").arg(slot + 1));
  };
  callbacks.channel_precision_changed = [&](int channel, int format) {
    viewport->end_paint(false);
    edit_material([&](bts::TextureSet &set) {
      if (channel < 0 || channel > 5 || format < 0 || format > 8)
        throw std::invalid_argument("Invalid channel precision");
      set.precision[channel] = bts::ChannelPrecision(format);
    });
  };
  callbacks.channel_changed = [&](int channel, bool enabled) {
    edit_material([=](bts::TextureSet &set) {
      if (channel == 5) {
        set.ao_channel = enabled;
        return;
      }
      set.channels.at(channel) = enabled;
      if (channel == 3 && !enabled)
        set.normal_mixing = bts::NormalMixing::Combine;
      if (channel == 4 && !enabled)
        set.bump_method = bts::BumpMethod::SmoothSobel;
    });
  };
  callbacks.metallic_changed = [&](double value) {
    if (viewport->paint_enabled) {
      brush.values.metallic = float(value);
      if (viewport->path_active && path_brush_changed)
        path_brush_changed();
      return;
    }
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::edit_values(set).metallic = float(value);
    });
  };
  callbacks.base_color_changed = [&](int component, double value) {
    if (viewport->paint_enabled) {
      brush.values.base_color.at(component) = float(value);
      if (viewport->path_active && path_brush_changed)
        path_brush_changed();
      return;
    }
    edit_material([=](bts::TextureSet &set) {
      bts::TextureDocument::edit_values(set).base_color.at(component) = float(value);
    });
  };
  queue_property_pixels =
      [&](const bts::TextureSet &set, bts::CompositeChannels channels, bool finalize) {
        std::optional<bts::TextureSet> visible;
        const auto mask = quick_masks.find(set.material_session_uid);
        if (quick_enabled && (quick_editing || mask != quick_masks.end()))
          visible = bts::quick_mask_preview(
              set, mask == quick_masks.end() ? nullptr : &mask->second, quick_editing);
        if (!host.preview_layer_edit(visible ? *visible : set, channels, finalize)) {
          const auto started = bts::PropertyClock::now();
          apply_preview(set, set.texture_resolution);
          auto trace = bts::property_current;
          trace.preview = !finalize;
          bts::property_uploaded(trace, bts::property_ms(started));
        }
      };
  flush_property_edit = [&] {
    bts::PaintMetadataScope paint_metadata;
    bts::PaintRecordingScope paint_record(gpu_authoring_enabled);
    bool native_candidate_admission = false;
    try {
      property_frame_timer.stop();
      if (!pending_property_edit || !uniform_before || uniform_kind != UniformKind::Property)
        return;
      bts::EditTiming timing("BTS_EDIT_MS");
      const auto known_channels = std::move(pending_property_channels);
      pending_property_channels.reset();
      std::optional<bts::TextureSet> previous_copy;
      QJsonObject previous_state;
      if (!known_channels) {
        previous_state = document->edit_state();
        previous_copy = document->set(uniform_uid);
      }
      auto edit = std::move(pending_property_edit);
      pending_property_edit = {};
      auto &current = document->set(uniform_uid);
      const bool native_fill =
          property_label == "Fill mapping" && !current.selected_mask &&
          bts::TextureDocument::layer(current, current.selected_layer).kind ==
              bts::LayerKind::Fill &&
          bool(bts::TextureDocument::layer(current, current.selected_layer).fill.native_material);
      if (native_fill || current.material_layering.enabled) {
        // Native Mapping and uniform layering widgets use the coalesced property path, not
        // queue_document_edit. Admit every affected set before live preview.
        native_candidate_admission = true;
        last_material_error.clear();
        auto candidate = std::make_unique<bts::TextureDocument>(*document);
        edit(candidate->set(uniform_uid));
        candidate->resolve_instances();
        bts::require_material_layering_edit_target(current);
        if (current.material_layering.enabled) {
          bts::require_uniform_material_layering(candidate->set(uniform_uid));
          bts::validate_material_substacks(candidate->set(uniform_uid));
        }
        for_affected(*candidate, uniform_uid, [&](const auto &set) {
          host.validate_native_mapping_edit(document->set(set.material_session_uid), set);
        });
        native_candidate_admission = false;
        document = std::move(candidate);
      }
      else
        edit(document->set(uniform_uid));
      document->resolve_instances();
      const auto &edited_set = document->set(uniform_uid);
      const auto &edited_layer = bts::TextureDocument::layer(document->set(uniform_uid),
                                                             edited_set.selected_layer);
      host.prepare_warp_projections(edited_set);
      if (!edited_set.selected_mask && edited_layer.kind == bts::LayerKind::Fill) {
        viewport->fill_projection = edited_layer.fill;
        viewport->fill_projection->native_per_uv = edited_set.native_per_uv;
        viewport->fill_projection->cm_per_native = edited_set.cm_per_native;
      }
      else if (edited_set.selected_mask && edited_layer.mask)
        if (const auto *effect = bts::find_mask_effect(edited_layer.mask->effects,
                                                       edited_set.selected_effect);
            effect && effect->kind == bts::MaskEffectKind::Fill)
          viewport->fill_projection = effect->mapping.projection;
      viewport->sync_warp_projection();
      const auto flags = known_channels ? *known_channels :
                                          bts::layer_edit_channels(
                                              *previous_copy,
                                              document->set(uniform_uid),
                                              bts::edit_set_state(previous_state,
                                                                  document->set(uniform_uid).id),
                                              bts::edit_set_state(document->edit_state(),
                                                                  document->set(uniform_uid).id));
      bool any = false;
      for (int c = 0; c < 17; ++c) {
        property_channels[c] = property_channels[c] || flags[c];
        any |= flags[c];
      }
      if (any) {
        uniform_previewed = true;
        for_affected(*document, uniform_uid, [&](const auto &set) {
          queue_property_pixels(set, flags, false);
        });
      }
      viewport->update();
    }
    catch (const std::exception &error) {
      last_material_error = QString::fromUtf8(error.what());
      workspace->append_console(last_material_error);
      if (native_candidate_admission) {
        // A rejected first value has never touched live state or queues.
        // If an earlier value previewed, cancel that gesture explicitly.
        property_frame_timer.stop();
        property_idle_timer.stop();
        property_dragging = false;
        pending_property_edit = {};
        pending_property_channels.reset();
        const bool restore_preview = uniform_previewed;
        if (restore_preview)
          document = std::move(uniform_before);
        else
          uniform_before.reset();
        if (restore_preview) {
          document->resolve_instances();
          for_affected(*document, uniform_uid, [&](const auto &set) {
            host.validate_native_mapping_edit(
                set, set, bts::NativeMappingEditOrigin::HistoryRestore);
          });
          for_affected(*document, uniform_uid, [&](const auto &set) {
            queue_property_pixels(set, property_channels, true);
          });
        }
        uniform_brush_before.reset();
        uniform_previewed = false;
        property_channels = {};
        auto &set = document->set(uniform_uid);
        workspace->show_texture_set(&set, &document->settings(), &brush);
        viewport->fill_projection = bts::TextureDocument::layer(set, set.selected_layer).fill;
        viewport->fill_projection->native_per_uv = set.native_per_uv;
        viewport->fill_projection->cm_per_native = set.cm_per_native;
        viewport->sync_warp_projection();
        refresh_history_actions();
        viewport->update();
      }
      else
        finish_uniform_edit(true);
      workspace->set_error_status(last_material_error);
    }
  };
  begin_property_edit = [&](const QString &label) {
    if (document && selected_material >= 0) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      if (bts::layer_is_cached(set, set.selected_layer)) {
        workspace->set_status("Layer is cached. Hide Pre-compose to edit its properties.");
        return;
      }
    }
    if (!document || selected_material < 0)
      return;
    if (bts::material_layering_main_readonly(
            document->set(snapshot.materials[selected_material].session_id)))
    {
      workspace->set_status("Main is retained read-only in four-material mode");
      return;
    }
    if (uniform_before && uniform_kind == UniformKind::Property && property_label == label)
      return;
    if (uniform_before)
      finish_uniform_edit(false);
    viewport->end_paint(false);
    uniform_kind = UniformKind::Property;
    uniform_uid = snapshot.materials[selected_material].session_id;
    uniform_before = std::make_unique<bts::TextureDocument>(*document);
    uniform_brush_before.reset();
    uniform_previewed = false;
    property_label = label;
    property_before_state = document->edit_state();
    property_channels = {};
    refresh_history_actions();
  };
  edit_property = [&](std::function<void(bts::TextureSet &)> edit, const QString &label) {
    const auto channels = std::move(queued_property_channels);
    queued_property_channels.reset();
    begin_property_edit(label);
    if (!uniform_before)
      return;
    pending_property_edit = std::move(edit);
    pending_property_channels = channels;
    if (!property_frame_timer.isActive())
      property_frame_timer.start();
    if (!property_dragging)
      property_idle_timer.start();
    viewport->update();
  };
  viewport->prepare_frame = [&] {
    if (pending_property_edit && flush_property_edit)
      flush_property_edit();
  };
  QObject::connect(
      &property_frame_timer, &QTimer::timeout, viewport, [&] { flush_property_edit(); });
  QObject::connect(&property_idle_timer, &QTimer::timeout, viewport, [&] {
    if (uniform_before && uniform_kind == UniformKind::Property && !property_dragging)
      finish_uniform_edit(false);
  });
  callbacks.property_edit_started = [&](const QString &label) {
    begin_property_edit(label);
    property_dragging = true;
    property_idle_timer.stop();
  };
  callbacks.property_edit_finished = [&] {
    property_dragging = false;
    if (uniform_before && uniform_kind == UniformKind::Property)
      finish_uniform_edit(false);
  };
  finish_uniform_edit = [&](bool cancel) {
    if (!uniform_before)
      return;
    if (uniform_kind == UniformKind::Property) {
      property_frame_timer.stop();
      property_idle_timer.stop();
      property_dragging = false;
      if (cancel) {
        pending_property_edit = {};
        pending_property_channels.reset();
      }
      else
        flush_property_edit();
      if (!uniform_before)
        return;
      const bool changed = document->edit_state() != property_before_state;
      if (cancel)
        document = std::move(uniform_before);
      else if (changed) {
        undo_history.push_back({std::move(uniform_before), uniform_uid, property_label});
        if (undo_history.size() > 128)
          undo_history.erase(undo_history.begin());
        redo_history.clear();
      }
      else
        uniform_before.reset();
      if (uniform_previewed) {
        document->resolve_instances();
        for_affected(*document, uniform_uid, [&](const auto &set) {
          queue_property_pixels(set, property_channels, true);
        });
      }
      uniform_brush_before.reset();
      pending_property_edit = {};
      pending_property_channels.reset();
      uniform_previewed = false;
      if (cancel)
        refresh_document_ui(false, uniform_uid);
      else if (changed)
        defer_property_ui(uniform_uid);
      refresh_history_actions();
      return;
    }
    const bool color = uniform_kind != UniformKind::Roughness,
               brush_color = uniform_kind == UniformKind::ColorBrush;
    const auto &before_values = brush_color ? uniform_brush_before->values :
                                              bts::TextureDocument::edit_values(
                                                  uniform_before->set(uniform_uid));
    const auto &after_values = brush_color ?
                                   brush.values :
                                   bts::TextureDocument::edit_values(document->set(uniform_uid));
    const bool changed = color ? before_values.base_color != after_values.base_color :
                                 before_values.roughness != after_values.roughness;
    if (cancel) {
      document = std::move(uniform_before);
      if (brush_color)
        brush.values.base_color = uniform_brush_before->values.base_color;
    }
    else if (changed) {
      undo_history.push_back({std::move(uniform_before),
                              uniform_uid,
                              color ? "Edit color" : "Edit roughness",
                              uniform_brush_before ?
                                  std::optional{uniform_brush_before->values.base_color} :
                                  std::nullopt});
      if (undo_history.size() > 128)
        undo_history.erase(undo_history.begin());
      redo_history.clear();
    }
    else
      uniform_before.reset();
    uniform_brush_before.reset();
    if (!brush_color && uniform_previewed) {
      document->resolve_instances();
      for_affected(*document, uniform_uid, [&](const auto &set) {
        if (cancel) {
          bts::CompositeChannels flags{};
          flags[color ? 0 : 2] = true;
          queue_property_pixels(set, flags, false);
        }
        bts::CompositeChannels flags{};
        flags[color ? 0 : 2] = true;
        queue_property_pixels(set, flags, true);
      });
    }
    if (cancel)
      refresh_document_ui(false, uniform_uid);
    else if (changed)
      defer_property_ui(uniform_uid);
  };
  callbacks.secondary_color_changed = [&](const std::array<float, 4> &value) {
    viewport->secondary_color = value;
    viewport->update();
  };
  callbacks.color_started = [&] {
    if (!document || selected_material < 0)
      return;
    viewport->end_paint(false);
    if (uniform_before)
      finish_uniform_edit(false);
    uniform_kind = viewport->paint_enabled ? UniformKind::ColorBrush : UniformKind::ColorFill;
    uniform_previewed = false;
    uniform_uid = snapshot.materials[selected_material].session_id;
    uniform_before = std::make_unique<bts::TextureDocument>(*document);
    uniform_brush_before = viewport->paint_enabled ? std::optional{brush} : std::nullopt;
    if (!viewport->paint_enabled)
      host.prepare_paint_texture(document->set(uniform_uid));
  };
  callbacks.color_changed = [&](const std::array<float, 4> &value) {
    if (!uniform_before)
      return;
    if (uniform_kind == UniformKind::ColorBrush) {
      brush.values.base_color = value;
      return;
    }
    auto &target = bts::TextureDocument::edit_values(document->set(uniform_uid)).base_color;
    if (target == value)
      return;
    target = value;
    uniform_previewed = true;
    document->resolve_instances();
    for_affected(*document, uniform_uid, [&](const auto &set) {
      bts::CompositeChannels flags{};
      flags[0] = true;
      queue_property_pixels(set, flags, false);
    });
    viewport->update();
  };
  callbacks.color_finished = [&] {
    if (viewport->path_active && uniform_before && uniform_kind == UniformKind::ColorBrush) {
      uniform_before.reset();
      uniform_brush_before.reset();
      uniform_previewed = false;
      if (path_brush_changed)
        path_brush_changed();
      refresh_history_actions();
    }
    else
      finish_uniform_edit(false);
  };
  callbacks.secondary_pick_requested = [&] {
    if (!document || selected_material < 0)
      return;
    viewport->end_paint(false);
    color_picker_pending = true;
    secondary_picker_pending = true;
    viewport->picker_active = true;
    workspace->set_picker_active(true);
    viewport->update();
  };
  callbacks.color_pick_requested = [&] {
    if (!document || selected_material < 0)
      return;
    viewport->end_paint(false);
    color_picker_pending = true;
    secondary_picker_pending = false;
    viewport->picker_active = true;
    workspace->set_picker_active(true);
    viewport->update();
  };
  callbacks.roughness_started = [&] {
    if (!document || selected_material < 0 || viewport->paint_enabled)
      return;
    viewport->end_paint(false);
    if (uniform_before)
      finish_uniform_edit(false);
    uniform_kind = UniformKind::Roughness;
    uniform_previewed = false;
    uniform_brush_before.reset();
    uniform_uid = snapshot.materials[selected_material].session_id;
    uniform_before = std::make_unique<bts::TextureDocument>(*document);
  };
  callbacks.roughness_finished = [&] {
    if (finish_uniform_edit)
      finish_uniform_edit(false);
  };
  callbacks.roughness_changed = [&](double value) {
    if (!viewport->paint_enabled && uniform_before && uniform_kind != UniformKind::Roughness)
      finish_uniform_edit(false);
    bts::EditTiming timing("BTS_EDIT_MS");
    if (uniform_before) {
      auto &set = document->set(uniform_uid);
      auto &roughness = bts::TextureDocument::edit_values(set).roughness;
      if (roughness == float(value))
        return;
      roughness = float(value);
      uniform_previewed = true;
      document->resolve_instances();
      for_affected(*document, uniform_uid, [&](const auto &target) {
        bts::CompositeChannels flags{};
        flags[2] = true;
        queue_property_pixels(target, flags, false);
      });
      viewport->update();
      return;
    }
    if (viewport->paint_enabled) {
      brush.values.roughness = float(value);
      if (viewport->path_active && path_brush_changed)
        path_brush_changed();
      return;
    }
    if (document) {
      edit_material([=](bts::TextureSet &set) {
        bts::TextureDocument::edit_values(set).roughness = float(value);
      });
      return;
    }
    if (selected_material < 0 || selected_material >= int(snapshot.materials.size())) {
      workspace->set_roughness_editable(false,
                                        document ? "No material selected." : "No project open");
      return;
    }
    auto &material = snapshot.materials[selected_material];
    try {
      host.set_material_roughness(material.session_id, float(value));
      material.roughness = float(value);
      dirty = true;
      (++document_revision, last_document_edit = bts::performance::Clock::now());
      viewport->update();
    }
    catch (const std::exception &error) {
      material.roughness_editable = false;
      material.roughness_unavailable_reason = error.what();
      workspace->set_roughness(material.roughness);
      workspace->set_roughness_editable(false, QString::fromUtf8(error.what()));
      workspace->append_console(QString::fromUtf8(error.what()));
      qWarning("BTS_MATERIAL_EDIT_FAILED: %s", error.what());
    }
  };
  // Defer dispatch one event turn so the progress bar/cursor can paint before
  // owner-thread publication. IO/decoding use existing workers; GPU staging
  // advances on the owning render frames.
  const auto begin_asset_apply = [&](bts::StudioAsset asset,
                                     std::function<void(const bts::StudioAsset &)> apply,
                                     std::optional<bts::AssetMeshHit> mesh = std::nullopt,
                                     QUuid relative = QUuid{},
                                     bool above = true) {
    if (!document || selected_material < 0) {
      workspace->set_error_status("Create or open a project before applying a material");
      return;
    }
    if (!asset_progress || asset_progress->active() ||
        (native_material_loader && native_material_loader->pending()) ||
        (texture_loader && texture_loader->pending()))
    {
      workspace->set_status("A material is still being applied");
      return;
    }
    const auto project = document->id();
    const auto revision = document_revision;
    const auto uid = snapshot.materials[selected_material].session_id;
    asset_apply_project = project;
    asset_drop_mesh = std::move(mesh);
    asset_texture_relative = asset.usages.contains("texture") ? relative : QUuid{};
    asset_texture_above = above;
    asset_apply_uid = uid;
    asset_progress->begin(asset.name);
    QTimer::singleShot(
        16,
        workspace,
        [&, asset = std::move(asset), apply = std::move(apply), project, revision, uid] {
          if (!document || document->id() != project || document_revision != revision ||
              selected_material < 0 || snapshot.materials[selected_material].session_id != uid)
          {
            asset_progress->fail("Destination changed before loading");
            return;
          }
          last_material_error.clear();
          try {
            const QScopedValueRollback<bool> publication(asset_publication, true);
            apply(asset);
            if (!last_material_error.isEmpty())
              asset_progress->fail(last_material_error);
          }
          catch (const std::exception &error) {
            asset_progress->fail(QString::fromUtf8(error.what()));
          }
          asset_progress->dispatched();
          viewport->update();
        });
  };
  const auto drop_asset = callbacks.asset_dropped;
  callbacks.asset_dropped =
      [begin_asset_apply,
       drop_asset](const bts::StudioAsset &asset, QUuid target, bool above, bool replace) {
        begin_asset_apply(
            asset,
            [=](const bts::StudioAsset &ready) { drop_asset(ready, target, above, replace); },
            std::nullopt,
            target,
            above);
      };
  const auto select_asset = callbacks.asset_selected;
  callbacks.asset_selected = [begin_asset_apply, select_asset](const bts::StudioAsset &asset) {
    if (asset.material || asset.smart_material || asset.usages.contains("texture") ||
        bts::is_spsm_archive_asset(asset))
      begin_asset_apply(asset, select_asset);
    else
      select_asset(asset);
  };
  auto shell = std::make_unique<bts::WorkspaceShell>(viewport, std::move(callbacks));
  workspace = shell.get();
  workspace->set_trackball(viewport->trackball_enabled());
  initialize_texture_loader();
  asset_progress = new bts::AssetApplyProgress(workspace);
  workspace->statusBar()->addPermanentWidget(asset_progress);
  asset_progress->completed = [&](const QString &error, bool success) {
    asset_drop_mesh.reset();
    asset_texture_relative = QUuid();
    if (success)
      workspace->set_status("Material applied");
    else
      workspace->set_error_status(error);
  };
  asset_progress->pending = [&] {
    if (!document || opening_project || document->id() != asset_apply_project) {
      asset_progress->fail("Material loading cancelled: project changed");
      return true;
    }
    if (viewport->shader_failed) {
      asset_progress->fail("Shader compilation failed; see the error panel");
      return true;
    }
    if ((native_material_loader && native_material_loader->pending()) ||
        (texture_loader && texture_loader->pending()))
      return true;
    for (auto *timer : workspace->findChildren<QTimer *>("spsmApplicationPoll"))
      if (timer->isActive())
        return true;
    if (host.texture_updates_pending()) {
      asset_progress->stage("Composite", 3);
      viewport->update();
      return true;
    }
    return false;
  };
  auto *asset_view_drop = new bts::AssetViewportDrop(viewport);
  asset_view_drop->accepts = [&](const QString &id) {
    if (!document || asset_progress->active())
      return false;
    for (const auto &asset : assets.assets())
      if (asset.id == QUuid(id))
        return !asset.projection_weave &&
               (asset.material || asset.smart_material || asset.smart_mask ||
                asset.usages.contains("texture") || bts::is_spsm_asset(asset) ||
                bts::is_spmsk_asset(asset));
    return false;
  };
  asset_view_drop->pick = [&](QPointF point) -> std::optional<bts::AssetMeshHit> {
    if (!viewport->pane_rect(bts::ViewKind::Scene).contains(point))
      return {};
    const auto [origin, direction] = viewport->path_ray(point);
    auto hit = bts::pick_asset_mesh(snapshot, origin, direction);
    if (!hit || hit->name.empty() || hit->material >= snapshot.materials.size())
      return {};
    const auto &sets = document->texture_sets();
    const auto uid = snapshot.materials[hit->material].session_id;
    const auto set = std::find_if(sets.begin(), sets.end(), [uid](const auto &value) {
      return value.material_session_uid == uid;
    });
    if (set == sets.end() || !set->uniform_editable || set->disabled ||
        set->material_layering.enabled)
      return {};
    return hit;
  };
  asset_view_drop->clip = [&] { return viewport->pane_rect(bts::ViewKind::Scene); };
  asset_view_drop->silhouette = [&](const bts::AssetMeshHit &hit) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const auto &triangle : snapshot.uv_triangles)
      if (std::uint32_t(triangle.triangle_id >> 32) == hit.object &&
          triangle.material == hit.material)
      {
        QPolygonF polygon;
        for (const auto &p : triangle.positions)
          polygon << viewport->scene_point({p[0], p[1], p[2]});
        path.addPolygon(polygon);
        path.closeSubpath();
      }
    return path;
  };
  asset_view_drop->apply = [&, begin_asset_apply, drop_asset](const QString &id,
                                                              const bts::AssetMeshHit &hit) {
    for (const auto &source : assets.assets())
      if (source.id == QUuid(id)) {
        const auto asset = source;
        viewport->end_paint(false);
        select_material(int(hit.material));
        auto &set = document->set(snapshot.materials[hit.material].session_id);
        set.selected_mask = false;
        set.selected_geometry = false;
        set.selected_effect = QUuid();
        // A viewport drop adds above the entire stack, independently of the
        // previously selected layer, and constrains coverage to the hit mesh.
        const QUuid top = set.layers.empty() ? QUuid{} : set.layers.back().id;
        // A mask attaches to a layer that already exists, so a mask drop needs
        // one; the material paths create their own first layer. Mask generators
        // then reach apply_smart_mask_asset through asset_dropped, exactly as a
        // drop on the layer stack does.
        if (top.isNull() && (asset.smart_mask || bts::is_spmsk_asset(asset))) {
          workspace->set_error_status(
              "Add a layer to this Texture Set before dropping a mask generator");
          return;
        }
        begin_asset_apply(
            asset,
            [drop_asset, top](const bts::StudioAsset &ready) {
              drop_asset(ready, top, true, false);
            },
            hit,
            top,
            true);
        return;
      }
  };

  // One check, a few seconds after the window is up so it never competes
  // with loading. Nothing is shown unless a newer release is confirmed.
  auto *updates = new bts::UpdateNotice(workspace);
  updates->available = [workspace](const QString &release, const QUrl &page) {
    if (workspace)
      workspace->show_update_available(release, page);
  };
  QTimer::singleShot(5000, workspace, [updates] {
    updates->check(bts::product_version(),
                   QUrl("https://api.github.com/repos/mattiaslind93/mpainter/releases/latest"));
  });

  {
    const auto ref_prefs = bts::load_reference_image_preferences();
    if (!ref_prefs.path.isEmpty() && QFileInfo::exists(ref_prefs.path)) {
      load_reference_image_file(ref_prefs.path, true);
    }
    viewport->reference_image_enabled = ref_prefs.enabled && viewport->reference_image.valid();
    workspace->set_reference_image_enabled(viewport->reference_image_enabled);
    workspace->set_reference_image_path(viewport->reference_image.path);
  }
  brush_presets = new bts::BrushPresetUi(
      workspace,
      assets,
      [&] { return brush; },
      [&](const bts::BrushSettings &next) {
        viewport->end_paint(false);
        brush = next;
        if (brush.clone)
          clone_brush = brush;
        else if (brush.projected)
          projection_brush = brush;
        else if (brush.smudge)
          smudge_brush = brush;
        else if (brush.erase)
          eraser_brush = brush;
        else
          paint_brush = brush;
        select_material(selected_material);
        viewport->update();
      },
      [&] { workspace->refresh_alpha_shelf(); },
      [&](QString message) {
        workspace->set_status(message);
        workspace->append_console(message);
      });
  brush_thumbnails = std::make_unique<bts::BrushPresetThumbnailJobs>(
      host,
      [view = QPointer<Viewport>(viewport)] {
        return view && (view->property("headlessGpuReady").toBool() || view->isValid());
      },
      [owner = QPointer<bts::WorkspaceShell>(workspace)](const QString &error) {
        if (owner)
          owner->append_console("Brush preview: " + error);
      });
  native_material_loader = std::make_unique<bts::NativeMaterialSourceLoader>(
      host,
      nullptr,
      [&] { viewport->update(); },
      [&](const QString &message) {
        workspace->set_status(message);
        if (asset_progress && asset_progress->active())
          asset_progress->stage(message);
      });
  smart_mask_thumbnails = std::make_unique<bts::SmartMaskThumbnailJobs>(
      host,
      [view = QPointer<Viewport>(viewport)] {
        return view && (view->property("headlessGpuReady").toBool() || view->isValid());
      },
      [owner = QPointer<bts::WorkspaceShell>(workspace)](const QString &error) {
        if (owner)
          owner->append_console("Native mask preview: " + error);
      });
  viewport->gpu_startup_ready = [&] {
    if (smart_mask_thumbnails)
      smart_mask_thumbnails->prepare_startup();
    if (brush_thumbnails)
      brush_thumbnails->prepare_startup();
  };
  bts::asset_thumbnail_native_generator(
      workspace, [&](const bts::StudioAsset &asset, std::function<void(QImage)> ready) {
        if (asset.brush_preset && brush_thumbnails) {
          brush_thumbnails->request(asset, std::move(ready));
          return;
        }
        if (asset.smart_mask && smart_mask_thumbnails) {
          const QPointer<bts::WorkspaceShell> owner = workspace;
          const auto name = asset.name;
          smart_mask_thumbnails->request(
              asset, [owner, name, ready = std::move(ready)](QImage image) {
                if (!owner)
                  return;
                if (qEnvironmentVariableIsSet("BTS_BN_SMART_MASK") && !image.isNull()) {
                  auto packets = owner->property("bnThumbnailPackets").toJsonObject();
                  packets[name] = QString::fromLatin1(
                      QCryptographicHash::hash(
                          QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
                                         image.sizeInBytes()),
                          QCryptographicHash::Sha256)
                          .toHex());
                  owner->setProperty("bnThumbnailPackets", packets);
                }
                ready(std::move(image));
              });
          return;
        }
        if (!native_material_loader)
          return;
        native_material_loader->ensure(
            asset,
            assets.assets(),
            [&, ready](const bts::StudioAsset &completed) {
              if (completed.search_terms.contains("preview:color-metallic-roughness-sphere"))
                workspace->append_console(
                    "Native material preview: generated Color / Metallic / Roughness sphere; normal, bump and displacement not shown");
              ready(bts::blend_material_preview(completed));
            },
            [&, ready](const QString &error) {
              workspace->append_console("Native material preview: " + error);
              ready({});
            },
            true);
      });
  initialize_native_image_ui();
#ifdef BTS_WITH_MATERIALX
  initialize_material_importer();
#endif
  bts::FileDialog::prepare_directory_thread();
  workspace->set_fill_color_spaces(bts::fill_texture_color_spaces());
  workspace->set_asset_catalog(&assets);
  if (!library_error.isEmpty())
    workspace->append_console(library_error);
  QObject::connect(
      workspace->findChild<QAction *>("configureChannels"), &QAction::triggered, workspace, [&] {
        if (!document || selected_material < 0)
          return;
        viewport->end_paint(false);
        if (uniform_before && finish_uniform_edit)
          finish_uniform_edit(false);
        const auto uid = snapshot.materials[selected_material].session_id;
        const auto source = document->set(uid).id;
        try {
          const auto edit = bts::choose_channel_configuration(
              workspace, *document, document->set(uid));
          if (!edit)
            return;
          bts::EditTiming timing("BTS_EDIT_MS");
          auto candidate = std::make_unique<bts::TextureDocument>(*document);
          const auto changed = bts::apply_channel_configuration(*candidate, source, *edit);
          if (changed.empty())
            return;
          const auto before = document->edit_state(), after = candidate->edit_state();
          for (auto changed_uid : changed)
            queue_document_edit(*document, *candidate, changed_uid, before, after);
          undo_history.push_back({std::move(document), uid, "Configure channels"});
          if (undo_history.size() > 128)
            undo_history.erase(undo_history.begin());
          redo_history.clear();
          document = std::move(candidate);
          refresh_document_ui(false);
          if (python_event)
            for (auto changed_uid : changed)
              python_event(
                  "LayerStacksModelDataChanged",
                  {{"texture_set", document->set(changed_uid).id.toString(QUuid::WithoutBraces)},
                   {"operation", "configure_channels"}});
        }
        catch (const std::exception &error) {
          workspace->append_console(QString::fromUtf8(error.what()));
        }
      });
  viewport->fill_gizmo_requested = [&](int mode) {
    workspace->findChild<QToolButton *>(QString("fillGizmoMode%1").arg(mode))->click();
  };
  viewport->polygon_toggle_requested = [&] {
    workspace->findChild<QAction *>("selectPolygonFill")->trigger();
  };
  viewport->polygon_invert_requested = [&] {
    workspace->findChild<QDoubleSpinBox *>("polygonGrayscale0")
        ->setValue(1 - viewport->polygon_value);
  };
  viewport->picker_toggle_requested = [&] {
    workspace->findChild<QAction *>("selectMaterialPicker")->trigger();
  };
  viewport->lazy_toggle_requested = [&] {
    workspace->findChild<QAction *>("toggleLazyMouse")->trigger();
  };
  viewport->symmetry_toggle_requested = [&] {
    workspace->findChild<QAction *>("toggleSymmetry")->trigger();
  };
  window = std::move(shell);
  {
    bts::MaterialLayeringHooks hooks;
    hooks.current = [&]() -> std::optional<bts::MaterialLayeringTarget> {
      if (!document || selected_material < 0 ||
          selected_material >= int(snapshot.materials.size()))
        return std::nullopt;
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      bts::MaterialLayeringTarget target{document->id(),
                                         set.id,
                                         set.material_session_uid,
                                         document_revision,
                                         &set,
                                         host.color_state()["working_space"].toString(),
                                         {}};
      if (opening_project)
        target.unavailable = "Wait for the project to finish loading before editing materials.";
      else if (quick_enabled)
        target.unavailable = "Leave Quick Mask before editing four materials";
      else if (!set.uniform_editable || set.disabled)
        target.unavailable = "Requires an editable canonical ordinary Principled material";
      return target;
    };
    const auto require_current = [&](const bts::MaterialLayeringTarget &target) {
      if (!document || document->id() != target.project || document_revision != target.revision ||
          document->set(target.uid).id != target.set)
        throw std::runtime_error("Four-material target changed; reopen the editor");
      if (opening_project || quick_enabled)
        throw std::runtime_error(
            "Wait for the project to finish loading and exit Quick Mask before editing materials.");
    };
    hooks.validate = [&, require_current](const auto &target) {
      require_current(target);
      host.validate_material_layering(document->set(target.uid));
    };
    hooks.begin_parameter_export = [&, require_current](const auto &target,
                                                        const QString &directory) {
      require_current(target);
      host.begin_material_parameter_export(document->set(target.uid), assets.assets(), directory);
      viewport->update();
    };
    hooks.poll_parameter_export = [&](bool current) {
      const bool ready = host.poll_material_parameter_export(current);
      if (!ready)
        viewport->update();
      return ready;
    };
    hooks.cancel_parameter_export = [&] { host.cancel_material_parameter_export(); };
    hooks.begin_parameter_import = [&, require_current](const auto &target,
                                                        const QString &directory) {
      require_current(target);
      if (!parameter_import)
        throw std::logic_error("Parameter Import is not initialized");
      viewport->end_paint(false);
      viewport->finish_path_drag(false);
      if (uniform_before && finish_uniform_edit)
        finish_uniform_edit(false);
      require_current(target);  // Focus/modal completion may have committed an edit.
      bts::MaterialParameterImportTarget saved;
      saved.project = target.project;
      saved.set = target.set;
      saved.uid = target.uid;
      saved.revision = target.revision;
      const auto color = host.color_state();
      saved.working_space = color["working_space"].toString();
      const auto rows = color["scene_linear_to_xyz"].toArray();
      if (rows.size() != 3)
        throw std::runtime_error("Parameter Import working-space matrix is unavailable");
      for (int row = 0; row < 3; ++row) {
        const auto values = rows[row].toArray();
        if (values.size() != 3)
          throw std::runtime_error("Parameter Import working-space matrix is invalid");
        for (int column = 0; column < 3; ++column) {
          if (!values[column].isDouble())
            throw std::runtime_error("Parameter Import working-space matrix is invalid");
          saved.scene_linear_to_xyz[row * 3 + column] = values[column].toDouble();
        }
      }
      saved.signature = document->edit_state();
      parameter_import->start(std::move(saved), *document, directory);
    };
    hooks.parameter_import_status = [&] {
      return parameter_import ? parameter_import->status() : bts::MaterialParameterImportStatus{};
    };
    hooks.cancel_parameter_import = [&] {
      if (parameter_import)
        parameter_import->cancel();
    };
    hooks.validate_source = [](const bts::FillTexture &source) {
      bts::material_layer_color_source_transforms(source);
    };
    hooks.load_file = [&](const auto &target, const QString &path, const auto &space) {
      texture_loader->from_file(target, path, space);
    };
    hooks.load_asset = [&](const auto &target, const auto &asset, const auto &space) {
      texture_loader->from_asset(target, asset, space);
    };
    hooks.cancel_load = [&](const auto &target) {
      if (texture_loader)
        texture_loader->cancel(target);
    };
    hooks.shelf = [&] { workspace->show_texture_shelf(); };
    hooks.release = [&] {
      if (host.project_gpu_available())
        host.cancel_texture_preparation();
    };
    hooks.apply_inputs = [&, require_current](const auto &target,
                                              bool enabled,
                                              const auto &inputs,
                                              const auto &buffered_assets) {
      require_current(target);
      viewport->end_paint(false);
      if (uniform_before && finish_uniform_edit)
        finish_uniform_edit(false);
      if (viewport->path_active && leave_path)
        leave_path();
      if (document_revision != target.revision)
        throw std::runtime_error("Finish the previous edit, then reopen the four-material editor");
      host.validate_material_layering(document->set(target.uid));
      const auto edited = bts::prepare_material_layering_inputs(
          document->set(target.uid), enabled, inputs);
      if (enabled)
        host.validate_material_layering(edited);
      std::vector<std::shared_ptr<const bts::FillTexture>> resources;
      if (enabled)
        for (const auto &input : inputs)
          for (int channel : {0, 1, 2, 4, 5})
            if (const auto &source = input.textures[channel]) {
              if (!source->pixels || (source->tiles && std::any_of(source->tiles->begin(),
                                                                   source->tiles->end(),
                                                                   [](const auto &entry) {
                                                                     return !entry.second->pixels;
                                                                   })))
                throw std::invalid_argument("Material source pixels are not ready");
              if (channel == 0)
                bts::material_layer_color_source_transforms(*source);
              resources.push_back(source);
            }
      // Prepare/validate catalog storage without publishing any resource.
      auto catalog = assets;
      std::set<QUuid> needed;
      for (const auto &input : inputs)
        for (int channel : {0, 1, 2, 4, 5})
          if (const auto &source = input.textures[channel])
            needed.insert(QUuid(QString::fromStdString(source->asset)));
      for (const auto &asset : buffered_assets)
        if (needed.contains(asset.id)) {
          const auto found = std::find_if(
              catalog.assets().begin(), catalog.assets().end(), [&](const auto &current) {
                return current.id == asset.id;
              });
          if (found == catalog.assets().end())
            catalog.add(asset);
          else if (found->hash() != asset.hash())
            throw std::invalid_argument("Material source resource changed before Apply");
        }
      for (const auto &input : inputs)
        for (int channel : {0, 1, 2, 4, 5})
          if (const auto &source = input.textures[channel]) {
            const auto found = std::find_if(
                catalog.assets().begin(), catalog.assets().end(), [&](const auto &asset) {
                  return asset.id.toString(QUuid::WithoutBraces).toStdString() == source->asset;
                });
            if (found == catalog.assets().end() || found->hash().toStdString() != source->hash)
              throw std::invalid_argument("Material source original is missing or changed");
            bts::require_material_layer_udim_originals(*source, found->texture_files);
          }
      if (enabled && !resources.empty() && !host.prepare_texture_resources(resources)) {
        viewport->update();
        return false;
      }
      static_assert(std::is_nothrow_move_assignable_v<bts::AssetCatalog>);
      const auto *before = document.get();
      const QScopedValueRollback<bool> mode_edit(material_layering_edit, true);
      edit_texture_set(
          target.uid,
          [&](auto &set) { set = edited; },
          {},
          [&]() noexcept { assets = std::move(catalog); });
      const bool committed = document.get() != before;
      if (!last_material_error.isEmpty() && !committed)
        throw std::runtime_error(last_material_error.toStdString());
      if (committed) {
        undo_history.back().label = "Edit material inputs";
        workspace->refresh_alpha_shelf();
      }
      return true;
    };
    hooks.select = [&, require_current](const auto &target, int which) {
      require_current(target);
      viewport->end_paint(false);
      if (uniform_before && finish_uniform_edit)
        finish_uniform_edit(false);
      if (document_revision != target.revision)
        throw std::runtime_error(
            "Material changed while finishing input; select the opacity stack again");
      bts::select_material_substack(document->set(target.uid), which);
      select_material(selected_material);
    };
    hooks.error = [&](const QString &error) {
      workspace->set_error_status(error);
      workspace->append_console(error);
    };
    auto *layout = qobject_cast<QBoxLayout *>(workspace->layer_stack()->parentWidget()->layout());
    if (!layout)
      throw std::logic_error("Layers has no owned box layout");
    material_layering_ui = std::make_unique<bts::MaterialLayeringUi>(
        *workspace, *layout, std::move(hooks));
  }

  {
    bts::SkewHooks hooks;
    hooks.with_gpu = [&](const std::function<void()> &work) { host.with_gpu(work); };
    hooks.document = [&] { return document.get(); };
    hooks.scene = [&]() -> const bts::SceneSnapshot & { return snapshot; };
    hooks.bake_scene = [&] { return host.scene_snapshot(true); };
    hooks.material = [&] {
      return document && selected_material >= 0 ?
                 snapshot.materials[selected_material].session_id :
                 0;
    };
    hooks.load_source = [&](const std::string &path, const bts::BakeSourceOptions &options) {
      return host.load_bake_source(path, options);
    };
    hooks.log = [&](const QString &text) { workspace->append_console("Skew: " + text); };
    hooks.end_input = [&] {
      viewport->end_paint(false);
      if (uniform_before && finish_uniform_edit)
        finish_uniform_edit(false);
      if (viewport->path_active && leave_path)
        leave_path();
    };
    hooks.refresh = [&] { refresh_document_ui(false); };
    hooks.configure = [&] { workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger(); };
    hooks.history = [&](std::unique_ptr<bts::TextureDocument> before,
                        const QString &label,
                        std::uint32_t uid) {
      if (!document || !uid)
        return;
      if (before && before->edit_state() != document->edit_state()) {
        undo_history.push_back({std::move(before), uid, label});
        if (undo_history.size() > 128)
          undo_history.erase(undo_history.begin());
        redo_history.clear();
      }
      refresh_document_ui(false, uid);
    };
    hooks.install = [&](std::uint32_t uid, int role, bts::MeshMap map) {
      if (!document)
        return;
      auto &set = document->set(uid);
      set.mesh_maps[role] = std::move(map);
      bts::CompositeChannels channels = role == 0 ? bts::CompositeChannels{} :
                                                    bts::complete_composite_channels;
      if (role == 0)
        channels[3] = channels[6] = true;
      if (!host.preview_layer_edit(set, channels))
        host.apply_texture_set(set, set.texture_resolution);
      refresh_document_ui(false, uid);
    };
    hooks.preview_role = [&] { return inspected_map; };
    hooks.preview = [&](int role) {
      if (role < 0) {
        inspected_map = -1;
        viewport->mesh_map_active = -1;
        update_inspection(bts::InspectionMode::Material);
      }
      else if (document && selected_material >= 0 &&
               document->set(snapshot.materials[selected_material].session_id).mesh_maps[role])
        viewport->mesh_map_requested(role);
      else
        workspace->append_console("Bake this mesh map before previewing it.");
    };
    hooks.view = [&](bool editing,
                     std::optional<bts::SkewField> field,
                     const std::vector<bts::SkewSurfaceVector> &vectors,
                     std::shared_ptr<const bts::SkewGeometry> geometry) {
      viewport->skew_editing = editing;
      viewport->skew_field = std::move(field);
      viewport->skew_vectors = vectors;
      viewport->skew_geometry = std::move(geometry);
      const bool baking = skew && skew->mode() > 0;
      if (editing && !skew_profile_active) {
        skew_return_brush = brush;
        brush = skew_brush;
        skew_profile_active = true;
      }
      else if (!editing && skew_profile_active) {
        if (brush.erase)
          skew_eraser = brush;
        else
          skew_brush = brush;
        brush = skew_return_brush;
        skew_profile_active = false;
      }
      if (baking) {
        viewport->fill_projection.reset();
        viewport->paint_enabled = editing;
        viewport->paint_mask = editing;
        viewport->path_active = false;
        viewport->quick_mask_edit = false;
        viewport->quick_mask_gate.reset();
        quick_editing = quick_enabled = false;
        workspace->layer_stack()->setEnabled(false);
        for (auto *name : {"addPaintLayer", "addFillLayer"})
          if (auto *button = workspace->findChild<QToolButton *>(name))
            button->setEnabled(false);
      }
      else
        workspace->layer_stack()->setEnabled(true);
      workspace->set_baking_tools(baking, editing, brush);
      if (auto *properties = workspace->findChild<QWidget *>("properties"))
        properties->setVisible(!baking);
      viewport->update();
    };
    bts::ShaderHooks shader_hooks;
    shader_hooks.document = [&] { return document.get(); };
    shader_hooks.material = [&] {
      return selected_material >= 0 && selected_material < int(snapshot.materials.size()) ?
                 snapshot.materials[selected_material].session_id :
                 0;
    };
    shader_hooks.apply = [&](const auto &set) { host.set_shader_parameters(set); };
    shader_hooks.end_input = [&] {
      viewport->end_paint(false);
      if (uniform_before && finish_uniform_edit)
        finish_uniform_edit(false);
    };
    shader_hooks.repaint = [&] { viewport->update(); };
    shader_hooks.log = [&](const QString &text) { workspace->append_console(text); };
    shader_hooks.dirty = [&] {
      dirty = true;
      (++document_revision, last_document_edit = bts::performance::Clock::now());
      window->setWindowTitle((document_path.isEmpty() ?
                                  QString("Untitled") :
                                  QFileInfo(document_path).completeBaseName()) +
                             " * — mPainter");
    };
    shader = std::make_unique<bts::ShaderController>(workspace, std::move(shader_hooks));
    bts::RenderHooks render_hooks;
    render_hooks.document = [&] { return bool(document) && (!skew || skew->mode() == 0); };
    render_hooks.end_input = [&] {
      viewport->end_paint(false);
      if (uniform_before && finish_uniform_edit)
        finish_uniform_edit(false);
      if (shader)
        shader->finish();
    };
    render_hooks.backend = [&](bool enabled, bool cpu) { host.set_render_preview(enabled, cpu); };
    render_hooks.state = [&] { return host.render_state(); };
    render_hooks.quality = [&](int samples, bool denoise) {
      host.set_render_quality(samples, denoise);
    };
    render_hooks.layout = [&] { return viewport->view_layout(); };
    render_hooks.set_layout = [&](auto layout) { viewport->set_layout(layout); };
    render_hooks.rendering = [&](bool enabled) {
      viewport->rendering = enabled;
      viewport->paint_enabled = false;
      if (!enabled)
        select_material(selected_material);
    };
    render_hooks.repaint = [&] { viewport->update(); };
    render_hooks.log = [&](const QString &text) { workspace->append_console(text); };
    render_hooks.dirty = [&] {
      dirty = bool(document);
      (++document_revision, last_document_edit = bts::performance::Clock::now());
    };
    render_controller = std::make_unique<bts::RenderController>(workspace,
                                                                std::move(render_hooks));

    skew_owner = std::make_unique<bts::SkewController>(workspace, std::move(hooks));
    skew = skew_owner.get();
    viewport->skew_invert = [&] {
      if (skew)
        skew->invert();
    };
  }
  {
    bts::ResourceMonitorHooks hooks;
    QObject::connect(&resource_ui_timer, &QTimer::timeout, workspace, [&] {
      if (!document)
        return;
      select_material(selected_material);
      workspace->refresh_alpha_shelf();
      viewport->update();
    });
    hooks.project = [&] { return document ? document->id().toString() : QString{}; };
    hooks.busy = [&] {
      return viewport->painting_ || bool(uniform_before) || bool(path_drag_before) ||
             bool(QApplication::activeModalWidget());
    };
    hooks.log = [&](const QString &text) { workspace->append_console("Resources: " + text); };
    hooks.sources = [&](bool library, bool project) {
      std::set<std::string> used;
      if (project && document)
        for (const auto &set : document->texture_sets())
          for (const auto &layer : set.layers)
            for (int c = 0; c < 15; ++c) {
              const auto &texture = bts::fill_channel_texture(layer.fill, c);
              if (texture)
                used.insert(texture->asset);
            }
      std::vector<bts::StudioAsset> result;
      for (const auto &asset : assets.assets())
        if ((library && asset.scope != bts::AssetScope::Project &&
             asset.scope != bts::AssetScope::Builtin) ||
            (project && asset.scope == bts::AssetScope::Project &&
             used.contains(asset.id.toString(QUuid::WithoutBraces).toStdString())))
          result.push_back(asset);
      return result;
    };
    hooks.apply = [&](const bts::PreparedResourceChange &ready) {
      const auto &old = ready.change.previous;
      const auto &next = ready.change.current;
      const auto hash = old.hash();
      const auto found = std::find_if(assets.assets().begin(),
                                      assets.assets().end(),
                                      [&](const auto &asset) { return asset.id == old.id; });
      if (found == assets.assets().end() || found->hash() != hash)
        return;
      if (old.scope != bts::AssetScope::Project) {
        if (assets.replace(next, hash)) {
          workspace->refresh_alpha_shelf();
          workspace->append_console("Resources: refreshed library asset " + next.name);
        }
        return;
      }
      if (!document)
        return;
      auto candidate = std::make_unique<bts::TextureDocument>(*document);
      std::set<std::uint32_t> changed;
      for (const auto &description : candidate->texture_sets()) {
        auto &set = candidate->set(description.material_session_uid);
        for (auto &layer : set.layers)
          for (int c = 0; c < 15; ++c) {
            auto &texture = bts::fill_channel_texture(layer.fill, c);
            if (texture && texture->asset == old.id.toString(QUuid::WithoutBraces).toStdString() &&
                texture->hash == hash.toStdString())
            {
              texture = c == 0 ? ready.color : ready.raw;
              changed.insert(set.material_session_uid);
            }
          }
      }
      if (changed.empty())
        return;
      candidate->resolve_instances();
      assets.add(next);
      const auto before_state = document->edit_state(), after_state = candidate->edit_state();
      for (const auto uid : changed) {
        const auto &set = candidate->set(uid);
        const auto channels = bts::layer_edit_channels(document->set(uid),
                                                       set,
                                                       bts::edit_set_state(before_state, set.id),
                                                       bts::edit_set_state(after_state, set.id));
        if (!host.preview_layer_edit(set, channels))
          apply_preview(set, set.texture_resolution);
      }
      const auto uid = selected_material >= 0 ? snapshot.materials[selected_material].session_id :
                                                *changed.begin();
      undo_history.push_back({std::move(document), uid, "Update project resources"});
      if (undo_history.size() > 128)
        undo_history.erase(undo_history.begin());
      redo_history.clear();
      document = std::move(candidate);
      dirty = true;
      (++document_revision, last_document_edit = bts::performance::Clock::now());
      refresh_history_actions();
      window->setWindowTitle((document_path.isEmpty() ?
                                  QString("Untitled") :
                                  QFileInfo(document_path).completeBaseName()) +
                             " * — mPainter");
      if (!resource_ui_timer.isActive())
        resource_ui_timer.start();
      workspace->append_console("Resources: updated project texture " + next.name +
                                " (previous version retained for Undo)");
    };
    resource_monitor = std::make_unique<bts::ResourceMonitor>(workspace, std::move(hooks));
  }
  QStringList display_choices;
  for (const auto &name : host.display_transforms())
    display_choices.append(QString::fromStdString(name));
  workspace->set_display_transforms(display_choices,
                                    host.color_state()["view_transform"].toString());
  viewport->layout_changed = [&](bts::ViewLayoutState layout) {
    workspace->set_view_layout(layout);
  };
  viewport->projection_changed = [&](bts::OrbitCamera::Projection projection) {
    workspace->set_projection(projection);
  };
  for (int key :
       {Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F4, Qt::Key_F, Qt::Key_F5, Qt::Key_F6})
  {
    auto *action = new QAction(window.get());
    action->setShortcut(QKeySequence(key));
    window->addAction(action);
    QObject::connect(action, &QAction::triggered, window.get(), [viewport, key] {
      if (!viewport->handle_layout_key(key)) {
        viewport->handle_navigation_key(key);
      }
    });
  }
  for (const auto &shortcut :
       {QKeySequence(Qt::Key_C), QKeySequence(Qt::SHIFT | Qt::Key_C), QKeySequence(Qt::Key_M)})
  {
    auto *action = new QAction(window.get());
    action->setShortcut(shortcut);
    window->addAction(action);
    QObject::connect(action, &QAction::triggered, window.get(), [viewport, shortcut] {
      viewport->handle_inspection_key(shortcut[0].key(),
                                      shortcut[0].keyboardModifiers() == Qt::ShiftModifier);
    });
  }
  workspace->set_texture_sets({}, -1);
  select_material(-1);
  viewport->set_empty_project(true);
  viewport->empty_project_view->new_requested = [&] {
    if (auto *action = workspace->findChild<QAction *>("newProject"))
      action->trigger();
  };
  viewport->empty_project_view->open_requested = [&] {
    if (auto *action = workspace->findChild<QAction *>("openProject"))
      action->trigger();
  };
}
else {
  window->setWindowTitle("mPainter");
  window->setCentralWidget(viewport);
  auto *toolbar = window->addToolBar("Material");
  toolbar->setMovable(false);
  toolbar->addWidget(new QLabel(" Roughness "));
  roughness = new QSlider(Qt::Horizontal);
  roughness->setRange(0, 100);
  roughness->setValue(18);
  roughness->setFixedWidth(220);
  toolbar->addWidget(roughness);
  auto *roughness_value = new QLabel("0.18");
  toolbar->addWidget(roughness_value);
  QObject::connect(
      roughness, &QSlider::valueChanged, window.get(), [&, roughness_value](int value) {
        try {
          host.set_roughness(float(value) / 100);
          roughness_value->setText(QString::number(double(value) / 100, 'f', 2));
          viewport->update();
        }
        catch (const std::exception &error) {
          roughness->setEnabled(false);
          roughness->setToolTip(QString::fromUtf8(error.what()));
          roughness_value->setText("Unavailable");
          qWarning("BTS_MATERIAL_EDIT_FAILED: %s", error.what());
        }
      });
}
