// SPDX-License-Identifier: GPL-3.0-or-later
// The viewport and the document helpers its callbacks use: previews, inspection, edits, undo. Part of run_workspace_session() (app/WorkspaceSession.inc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
    viewport = new Viewport(host, window.get());
    viewport->texture_interaction=[&]{return bool(uniform_before)||bool(stroke_before)||bool(path_drag_before)||property_dragging;};
    // Proximity is delivered to QApplication; leaving range must terminate a
    // stranded contact, including removal/suspend with no matching release.
    class ProximityGuard final:public QObject{public:std::function<void(const QPointingDevice *)> leave;bool eventFilter(QObject *,QEvent *e)override{if(e->type()==QEvent::TabletLeaveProximity&&leave)if(auto *tablet=dynamic_cast<QTabletEvent *>(e))leave(tablet->pointingDevice());return false;}};
    auto *proximity_guard=new ProximityGuard;proximity_guard->setParent(viewport);proximity_guard->leave=[&](const QPointingDevice *device){if(viewport->tablet_device_==device)viewport->cancel_tablet_contact();};app.installEventFilter(proximity_guard);
    QObject::connect(&app,&QGuiApplication::applicationStateChanged,viewport,[&](Qt::ApplicationState state){if(state!=Qt::ApplicationActive)viewport->cancel_tablet_contact();});
    viewport->brush=&brush;viewport->symmetry=&symmetry;viewport->symmetry_scene=&snapshot;viewport->capture_spline=qEnvironmentVariableIsSet("BTS_STROKE_SPLINE_DEMO")||qEnvironmentVariableIsSet("BTS_TABLET_REGRESSION");viewport->input_spline_enabled_=!qEnvironmentVariableIsSet("BTS_TEST_LINEAR_INPUT");
    bool interactive_composite=false;
    const bool gpu_authoring_enabled=qEnvironmentVariable("BTS_GPU_BRUSH_AUTHORING",QStringLiteral("1"))!="0";
    const auto apply_preview=[&](const bts::TextureSet &set,int resolution,bts::PaintPixels::Bounds bounds=bts::PaintPixels::Bounds{}){ bts::performance::Scope timing("apply_preview");
      const auto found=quick_masks.find(set.material_session_uid);
      if(quick_enabled&&(quick_editing||found!=quick_masks.end())){auto preview=bts::quick_mask_preview(set,found==quick_masks.end()?nullptr:&found->second,quick_editing);host.apply_texture_set(preview,resolution,bounds,interactive_composite);}
      else host.apply_texture_set(set,resolution,bounds,interactive_composite);
    };
    const auto finish_preview=[&](const bts::TextureSet &set){const auto mask=quick_masks.find(set.material_session_uid);if(quick_enabled&&(quick_editing||mask!=quick_masks.end())){auto preview=bts::quick_mask_preview(set,mask==quick_masks.end()?nullptr:&mask->second,quick_editing);host.finish_texture_set(preview);}else host.finish_texture_set(set);};
    const auto refresh_quick_preview=[&]{if(document){document->resolve_instances();for(const auto &set:document->texture_sets()){const auto found=quick_masks.find(set.material_session_uid);auto value=quick_enabled&&(quick_editing||found!=quick_masks.end())?bts::quick_mask_preview(set,found==quick_masks.end()?nullptr:&found->second,quick_editing):set;bts::CompositeChannels channels{};channels[0]=true;if(!host.preview_layer_edit(value,channels,true)){std::fprintf(stderr,"BTS_GPU_CPU_FALLBACK reason=quick-mask-uninitialized-images\n");apply_preview(set,set.texture_resolution);}}}viewport->update();};
    const auto update_inspection = [&](bts::InspectionMode mode,std::optional<std::uint32_t> changed_uid=std::nullopt) {
      if (!document) return;
      if(mode!=bts::InspectionMode::Roughness){inspected_map=-1;viewport->mesh_map_active=-1;}
      std::vector<bts::InspectionMaterial> materials;
      for (const auto &set : document->texture_sets()) {
        if(set.disabled||!set.uniform_editable||(changed_uid&&*changed_uid!=set.material_session_uid))continue;
        // Native channel images are composed on the GPU. Inspection routing
        // needs the document base and selected view, not a CPU layer summary.
        bts::InspectionMaterial material{set.material_session_uid,set.base_values,{},selected_material>=0&&set.material_session_uid==snapshot.materials[selected_material].session_id&&set.selected_mask?&set:nullptr,set.texture_resolution};if(inspected_map>=0){material.mesh_inspection=true;material.mask_set=&set;material.mesh_map=set.mesh_maps[inspected_map]?&*set.mesh_maps[inspected_map]:nullptr;}materials.push_back(material);
      }
      host.set_inspection(mode,materials);
      viewport->update();
    };
    viewport->inspection_requested = [&](bts::InspectionMode mode) {
      try { inspected_map=-1;viewport->mesh_map_active=-1;update_inspection(mode); }
      catch (const std::exception &error) { if (workspace) workspace->append_console(QString::fromUtf8(error.what())); }
    };
    viewport->mesh_map_requested=[&](int role){if(!document||role<0||role>=7)return;inspected_map=role;viewport->mesh_map_active=viewport->last_mesh_map=role;update_inspection(bts::InspectionMode::Roughness);};
    const auto refresh_texture_set_list=[&]{if(!workspace||!document)return;const QSignalBlocker block(workspace->texture_sets());QStringList names;for(const auto &material:snapshot.materials)names.append(QString::fromStdString(document->set(material.session_id).name));workspace->set_texture_sets(names,names.isEmpty()?-1:std::clamp(selected_material,0,int(names.size())-1));for(int row=0;row<int(snapshot.materials.size());++row){auto *item=workspace->texture_sets()->item(row);const auto &set=document->set(snapshot.materials[row].session_id);item->setData(Qt::UserRole,set.material_session_uid);item->setData(Qt::CheckStateRole,set.loaded?Qt::Checked:Qt::Unchecked);item->setForeground(QColor(set.loaded?bts::theme::Text:bts::theme::Disabled));}for(const auto &set:document->texture_sets())if(set.disabled){auto *item=new QListWidgetItem(QString::fromStdString(set.name),workspace->texture_sets());item->setFlags(Qt::ItemIsUserCheckable);item->setCheckState(set.loaded?Qt::Checked:Qt::Unchecked);item->setForeground(QColor(bts::theme::Muted));item->setToolTip("Unused Texture Set: use Settings → Reassign Texture Sets");}};
    const auto mark_document_commit = [&]()noexcept {
      dirty=true;++document_revision;last_document_edit=bts::performance::Clock::now();
    };
    const auto mark_document_changed = [&] {
      mark_document_commit();
      window->setWindowTitle((document_path.isEmpty()?QString("Untitled"):QFileInfo(document_path).completeBaseName())+" * — mPainter");
    };
    const auto refresh_document_ui = [&](bool geometry_changed=true,std::optional<std::uint32_t> changed_uid=std::nullopt,bool mark_changed=true) { bts::performance::Scope timing("refresh_document_ui");
      if(restore_path_profile)restore_path_profile();
      refresh_history_actions();
      update_inspection(host.inspection_mode(),changed_uid);
      if(geometry_changed){snapshot = host.scene_snapshot();viewport->set_scene_cameras(snapshot.cameras);}
      refresh_texture_set_list();select_material(selected_material);
      if(workspace){QString signature;if(document)for(const auto &set:document->texture_sets()){signature+=set.id.toString()+QString::fromStdString(set.name);for(int role=0;role<7;++role)if(set.mesh_maps[role])for(const auto &[tile,plane]:set.mesh_maps[role]->tiles)signature+=QString::number(role)+":"+QString::number(tile)+":"+QString::fromStdString(plane.hash);}if(workspace->property("bakedShelfSignature").toString()!=signature){workspace->setProperty("bakedShelfSignature",signature);if(!workspace->property("bakedShelfRefreshPending").toBool()){workspace->setProperty("bakedShelfRefreshPending",true);QTimer::singleShot(0,workspace,[workspace]{workspace->setProperty("bakedShelfRefreshPending",false);workspace->refresh_alpha_shelf();});}}}
      if(mark_changed)mark_document_changed();
      viewport->update();
    };
    const auto schedule_residency_spill=[&]{
      if(!workspace||!document||std::all_of(document->texture_sets().begin(),document->texture_sets().end(),[](const auto &set){return set.fully_loaded();}))return;
      auto *poll=new QTimer(workspace);poll->setInterval(100);const auto project_id=document->id();
      QObject::connect(poll,&QTimer::timeout,workspace,[&,poll,project_id]{
        if(!document||document->id()!=project_id){poll->stop();poll->deleteLater();return;}
        if(QApplication::activeModalWidget()||host.texture_updates_pending()||host.save_project_pending()||(texture_loader&&texture_loader->pending())||(native_material_loader&&native_material_loader->pending()))return;
        poll->stop();poll->deleteLater();
        try{for(const auto &set:document->texture_sets())if(!set.disabled&&!set.uniform_editable&&!set.fully_loaded())host.change_texture_residency(set);host.evict_unloaded_resources(*document);for(auto *history:{&undo_history,&redo_history})for(auto &entry:*history)if(entry.state)bts::spill_unloaded_textures(*entry.state,false);bts::spill_unloaded_textures(*document);}catch(const std::exception &error){workspace->set_error_status(QString("Texture cache: ")+QString::fromUtf8(error.what()));}
      });poll->start();
    };
    // History/dirty state is immediate. Broad panel synchronization follows the
    // first newly composed frame and never closes a subsequent active drag.
    const auto defer_property_ui = [&](std::uint32_t uid) {
      mark_document_changed();refresh_history_actions();
      property_ui_refresh=std::pair{document->id(),uid};viewport->update();
    };
    const auto for_affected=[&](bts::TextureDocument &model,std::uint32_t uid,const auto &apply){std::set<QUuid> affected{model.set(uid).id};bool changed=true;while(changed){changed=false;for(const auto &set:model.texture_sets())for(const auto &layer:set.layers)if(layer.instance&&affected.contains(layer.instance->source_set))changed=affected.insert(set.id).second||changed;}for(const auto &set:model.texture_sets())if(affected.contains(set.id))apply(set);};
    const auto apply_document_layers=[&](bts::TextureDocument &model,std::uint32_t uid,int resolution,bts::PaintPixels::Bounds bounds=bts::PaintPixels::Bounds{}){model.resolve_instances();for_affected(model,uid,[&](const auto &set){apply_preview(set,set.material_session_uid==uid?resolution:int(model.settings().resolution),set.material_session_uid==uid?bounds:bts::PaintPixels::Bounds{});});};
    const auto queue_document_edit=[&](bts::TextureDocument &before,bts::TextureDocument &after,std::uint32_t uid,const QJsonObject &old_state,const QJsonObject &new_state,bts::NativeMappingEditOrigin origin=bts::NativeMappingEditOrigin::Authoring){bts::performance::Scope timing("queue_document_edit");after.resolve_instances();for_affected(after,uid,[&](const auto &set){host.validate_native_mapping_edit(before.set(set.material_session_uid),set,origin);});for_affected(after,uid,[&](const auto &set){const auto old_set_state=bts::edit_set_state(old_state,set.id),new_set_state=bts::edit_set_state(new_state,set.id);if(!quick_enabled&&bts::layer_edit_compatible(old_set_state,new_set_state)&&host.preview_layer_edit(set,set.material_session_uid==uid?bts::layer_edit_channels(before.set(set.material_session_uid),set,old_set_state,new_set_state):bts::complete_composite_channels))return;apply_preview(set,set.texture_resolution);});};
    // Bake changes may span independent sets. Preflight their entire dependent
    // union before any queue writes; recipe-only edits require no map work.
    const auto queue_mesh_map_changes=[&](bts::TextureDocument &before,bts::TextureDocument &after,bts::NativeMappingEditOrigin origin){
      std::set<std::uint32_t> affected;
      for(const auto &set:after.texture_sets())if(!set.disabled&&bts::changed_mesh_map_roles(before.set(set.material_session_uid).mesh_maps,set.mesh_maps))
        for_affected(after,set.material_session_uid,[&](const auto &dependent){if(!dependent.disabled)affected.insert(dependent.material_session_uid);});
      if(affected.empty())return;
      after.resolve_instances();
      for(auto uid:affected)host.validate_native_mapping_edit(before.set(uid),after.set(uid),origin);
      // As with ordinary queued edits, later GPU allocation failure is visible;
      // this admission ordering is not a guarantee of atomic GPU OOM rollback.
      for(auto uid:affected){const auto &set=after.set(uid);if(!quick_enabled&&host.preview_layer_edit(set,bts::complete_composite_channels))continue;apply_preview(set,set.texture_resolution);}
    };
    QTimer history_spill_poll;history_spill_poll.setInterval(50);
    bool history_spill_reported=false;QString history_spill_error;
    QObject::connect(&history_spill_poll,&QTimer::timeout,qApp,[&]{
      if(auto result=bts::poll_history_spill()){
        if(!result->error.empty()){const auto error=QString::fromStdString(result->error);if(error!=history_spill_error){history_spill_error=error;workspace->set_status(error);workspace->append_console(error);}}
        else if(result->written&&!history_spill_reported){history_spill_reported=true;const auto message=QString("Stroke history cached to disk (%1 MiB). All strokes retained.").arg(result->written/1048576);workspace->set_status(message);workspace->append_console(message);}
      }
      if(!bts::history_spill_pending())history_spill_poll.stop();
    });
    const auto maintain_history=[&](bts::TextureDocument &value){if(bts::limit_stroke_history(value)&&!history_spill_poll.isActive())history_spill_poll.start();};
    bts::AssetApplyProgress *asset_progress=nullptr;
    std::optional<bts::AssetMeshHit> asset_drop_mesh;
    QUuid asset_apply_project,asset_texture_relative;bool asset_texture_above=true;
    std::uint32_t asset_apply_uid=0;bool asset_publication=false;
    QString last_material_error;
    bool last_material_published=false;
    bool precompose_control_edit=false;
    const auto edit_texture_set = [&](std::uint32_t uid,std::function<void(bts::TextureSet &)> edit,std::function<void()> before_publish={},std::function<void()> publish_resources={}) { bts::PaintMetadataScope paint_metadata; bts::PaintRecordingScope paint_record(gpu_authoring_enabled); bts::performance::Scope timing_all("edit_material");
      last_material_error.clear();last_material_published=false;
      if(uniform_before&&finish_uniform_edit)finish_uniform_edit(false);
      bts::EditTiming timing("BTS_EDIT_MS");
      if (!document) return;
      auto candidate = std::make_unique<bts::TextureDocument>(*document);
      try {
        if(document->set(uid).loaded_tiles().empty()||(workspace&&workspace->active_tile()&&!document->set(uid).tile_loaded(workspace->active_tile())))throw std::invalid_argument("Load the Texture Set / UV tile before editing it");
        if(!precompose_control_edit)bts::require_uncached_layer(document->set(uid),document->set(uid).selected_layer);
        if(!material_layering_edit)bts::require_material_layering_edit_target(document->set(uid));
        edit(candidate->set(uid));
        if(asset_publication&&asset_progress&&asset_progress->active()&&uid==asset_apply_uid){
          bts::constrain_asset_insertion(candidate->set(uid),document->set(uid),asset_drop_mesh?asset_drop_mesh->name:std::string{},asset_texture_relative,asset_texture_above);
          asset_progress->stage("Apply layers",2);
        }
        if(candidate->set(uid).material_layering.enabled){bts::normalize_uniform_material_selection(candidate->set(uid));bts::require_uniform_material_layering(candidate->set(uid));}
        bts::validate_material_substacks(candidate->set(uid));
        if(!known_fill_insertion)maintain_history(*candidate);
        // Failure injection is scoped to this native publication only; ordinary
        // Fill edits and production sessions cannot trigger these checkpoints.
        const auto native_failure=[&](const char *stage){if(before_publish&&qEnvironmentVariableIsSet("BTS_NATIVE_IMAGE_UI_DEMO")&&window->property("nativeImageFailure").toString()==QString::fromLatin1(stage)){window->setProperty("nativeImageFailure",QVariant());throw std::runtime_error(std::string("Injected native image publication failure: ")+stage);}};
        native_failure("before-reserve");
        auto prepared_history=bts::prepare_document_commit(undo_history,uid);
        std::optional<bts::NativeSourceEditBatch> native_queue;
        if(known_fill_insertion){
          // Adding a fill is a guaranteed structural change. Its new coverage
          // invalidates the stack directly; serializing both complete documents
          // cannot reveal any additional information and grows with every set.
          if(candidate->set(uid).layers.size()!=document->set(uid).layers.size()+1)throw std::logic_error("Fill insertion did not add exactly one layer");
          apply_document_layers(*candidate,uid,candidate->set(uid).texture_resolution);
        }else{
          QJsonObject old_state,new_state;{bts::performance::Scope timing("edit_state_compare");old_state=document->edit_state();new_state=candidate->edit_state();}if(new_state==old_state)return;
          if(!precompose_control_edit){const auto &previous=document->set(uid);const int first=bts::precompose_start(previous.layers);if(first){const auto &after=candidate->set(uid).layers;const auto cache=std::find_if(after.begin(),after.end(),[&](const auto &l){return l.id==previous.layers[first].id;});if(cache!=after.end()&&cache->visible&&int(cache-after.begin())!=first)throw std::invalid_argument("Release Pre-compose before moving its cache boundary or inserting layers below it.");const auto old_layers=bts::edit_set_state(old_state,previous.id)["layers"].toArray(),new_layers=bts::edit_set_state(new_state,previous.id)["layers"].toArray();for(int i=0;i<first;++i)if(i>=new_layers.size()||old_layers[i]!=new_layers[i])throw std::invalid_argument("Layers below Pre-compose are frozen. Hide the cache or use Unfreeze / Release Pre-compose before editing or reordering them.");}}

          if(before_publish){
            if(quick_enabled)throw std::runtime_error("Leave Quick Mask before replacing a native material image");
            candidate->resolve_instances();std::vector<bts::NativeSourceEdit> edits;
            for_affected(*candidate,uid,[&](const auto &set){
              auto old_set=bts::edit_set_state(old_state,set.id),new_set=bts::edit_set_state(new_state,set.id);auto old_config=old_set,new_config=new_set;old_config.remove("layers");new_config.remove("layers");
              if(old_config!=new_config)throw std::runtime_error("Native image replacement must preserve material target settings");
              for(const auto &layer:bts::evaluation_layers(set))if(layer.kind==bts::LayerKind::Fill&&layer.fill.native_material)host.validate_native_material_target(QUuid(QString::fromStdString(layer.fill.material_resource)),set);
              edits.push_back({&document->set(set.material_session_uid),&set,set.material_session_uid==uid?bts::layer_edit_channels(document->set(uid),set,old_set,new_set):bts::complete_composite_channels});
            });
            native_queue.emplace(host.prepare_native_source_edits(edits));
            native_failure("queue-prepared");
          }else queue_document_edit(*document,*candidate,uid,old_state,new_state);
        }
        bts::publish_document_commit(document,candidate,undo_history,redo_history,std::move(prepared_history),[&]()noexcept{if(native_queue)native_queue->commit();if(before_publish)before_publish();if(publish_resources)publish_resources();});
        last_material_published=true;
        if(asset_publication&&asset_progress&&asset_progress->active())asset_progress->stage("Composite",3);
        // Persistence identity belongs to the commit, before any throwing UI
        // synchronization. A refresh warning must never leave a clean project.
        mark_document_commit();
        if(qEnvironmentVariableIsSet("BTS_BK_FOLDER_MASK")&&window->property("bkTextureRefreshFailure").toBool()){window->setProperty("bkTextureRefreshFailure",false);throw std::runtime_error("Injected BK postcommit texture refresh failure");}
        native_failure("committed-refresh");
        window->setWindowTitle((document_path.isEmpty()?QString("Untitled"):QFileInfo(document_path).completeBaseName())+" * — mPainter");
        if (undo_history.size() > 128) undo_history.erase(undo_history.begin());
        if(known_fill_insertion){
          const auto previous=workspace->property("layerRowsOnly");workspace->setProperty("layerRowsOnly",true);
          {const auto restore=qScopeGuard([&]{workspace->setProperty("layerRowsOnly",previous);});workspace->show_texture_set(&document->set(uid),&document->settings(),&brush);}
          viewport->paint_enabled=false;refresh_history_actions();property_ui_refresh=std::pair{document->id(),uid};viewport->update();
        }else if(defer_material_ui){refresh_history_actions();property_ui_refresh=std::pair{document->id(),uid};viewport->update();}else refresh_document_ui(false,uid,false);if(python_event)python_event("LayerStacksModelDataChanged",{{"texture_set",document->set(uid).id.toString(QUuid::WithoutBraces)}});
      }
      catch (const std::exception &error) {
        last_material_error=QString::fromUtf8(error.what());
        if(asset_publication&&asset_progress)asset_progress->fail(last_material_error);
        try{workspace->append_console(last_material_error);if(!before_publish)select_material(selected_material);workspace->set_error_status(last_material_error);}catch(const std::exception &refresh){std::fprintf(stderr,"BTS_DOCUMENT_EDIT_REFRESH_WARNING %s\n",refresh.what());}
      }
    };
    edit_material = [&](std::function<void(bts::TextureSet &)> edit){if(document&&selected_material>=0)edit_texture_set(snapshot.materials[selected_material].session_id,std::move(edit));};
    const auto edit_geometry = [&](std::function<void(bts::TextureSet &)> edit){const auto *before=document.get();edit_material(std::move(edit));if(document.get()!=before&&!undo_history.empty()){undo_history.back().label="Edit geometry mask";refresh_history_actions();}};
    const auto history_step = [&](bool redo) {
      if(skew&&skew->stroke()){viewport->end_paint(true);return;}if(skew&&skew->cancel_transient())return;
      if(uniform_before&&finish_uniform_edit){finish_uniform_edit(true);return;}
      if(path_drag_before){viewport->finish_path_drag(true);return;}
      if(stroke_before){viewport->end_paint(true);return;}
      auto &from = redo ? redo_history : undo_history;
      auto &to = redo ? undo_history : redo_history;
      if (!document || from.empty()) return;
      const auto uid = from.back().material_uid;
      environment_edit_clock.invalidate();
      if(!from.back().environment.isEmpty()){
        const auto current=host.environment_state(),wanted=from.back().environment;
        if(wanted.contains("subsurface"))host.set_subsurface_preview(wanted["subsurface"].toObject());
        if(wanted.contains("post_effects"))host.set_display_post_effects(wanted["post_effects"].toObject());
        host.set_environment_alignment(!wanted["lighting_follows_view"].toBool());host.set_environment_rotation(wanted["rotation"].toDouble());
        for(auto key:{"exposure","opacity","blur"})host.set_environment_appearance(key,wanted[key].toDouble());
        to.push_back({nullptr,uid,from.back().label,{},current});from.pop_back();
        workspace->set_environment(host.environment_state());dirty=true;(++document_revision,last_document_edit=bts::performance::Clock::now());refresh_document_ui(false,uid);viewport->update();return;
      }
      if(from.back().label.startsWith("Rename ")){try{auto entry=std::move(from.back());from.pop_back();std::optional<std::pair<QUuid,QString>> inverse;if(entry.asset_name){for(const auto &asset:assets.assets())if(asset.id==entry.asset_name->first){inverse=std::pair{asset.id,asset.name};break;}assets.rename(entry.asset_name->first,entry.asset_name->second);}to.push_back({std::move(document),uid,entry.label,{},{},inverse});document=std::move(entry.state);for(const auto &set:document->texture_sets())if(!set.disabled){const auto actual=host.rename_material(set.material_session_uid,set.name);document->set(set.material_session_uid).name=actual;for(auto &material:snapshot.materials)if(material.session_id==set.material_session_uid)material.name=actual;}refresh_texture_set_list();workspace->refresh_alpha_shelf();select_material(selected_material);mark_document_changed();refresh_history_actions();return;}catch(const std::exception &error){workspace->set_error_status(QString::fromUtf8(error.what()));return;}}
      try {
        if(shader)shader->finish();
        // This operation imports a complete local shader assignment alongside
        // its layers; its inverse must restore the retained document library.
        // Ordinary paint/layer history still keeps independent shader edits.
        if(from.back().label!="Reconstruct material parameters")from.back().state->preserve_shader_instances(document->shader_instances());
        from.back().state->set_export_configuration(document->export_configuration());
        auto bake_recipe=(from.back().label=="Edit bake parameters"||from.back().label=="Sync bake parameters"||from.back().label=="Bake mesh maps")?from.back().state->bake_configuration():document->bake_configuration();for(const auto *key:{"ui_mode","auto_role"}){const auto wanted=from.back().state->bake_configuration();if(wanted.contains(key))bake_recipe[key]=wanted[key];else bake_recipe.remove(key);}from.back().state->set_bake_configuration(bake_recipe);
        from.back().state->set_normal_map_format(document->settings().normal_map_format);
        from.back().state->set_tangent_per_fragment(document->settings().tangent_per_fragment);
        from.back().state->set_auto_unwrap(document->settings().auto_unwrap);
        from.back().state->set_usd_options(document->settings().usd);
        from.back().state->set_units(document->settings().units);
        if(from.back().label=="Edit geometry mask"){const auto &current=document->set(uid);auto &restored=from.back().state->set(uid);restored.selected_layer=current.selected_layer;restored.selected_mask=current.selected_mask;restored.selected_geometry=current.selected_geometry;restored.selected_effect=current.selected_effect;}
        // Trusted Undo/Redo restores retained rows; it does not author a new
        // Mapping override. Validate every set before any history queue changes.
        from.back().state->resolve_instances();for(const auto &set:from.back().state->texture_sets())if(!set.disabled)host.validate_native_mapping_edit(document->set(set.material_session_uid),set,bts::NativeMappingEditOrigin::HistoryRestore);
        if(from.back().label=="Bake mesh maps"){queue_mesh_map_changes(*document,*from.back().state,bts::NativeMappingEditOrigin::HistoryRestore);}else if(from.back().label=="Configure channels"){const auto before=document->edit_state(),after=from.back().state->edit_state();for(const auto &set:from.back().state->texture_sets())if(!set.disabled&&bts::channel_configuration(set)!=bts::channel_configuration(document->set(set.material_session_uid)))queue_document_edit(*document,*from.back().state,set.material_session_uid,before,after,bts::NativeMappingEditOrigin::HistoryRestore);}else if(from.back().label=="Update project resources"){for(const auto &set:from.back().state->texture_sets())if(!set.disabled)host.preview_layer_edit(set,bts::complete_composite_channels);}else if(from.back().label.contains("skew",Qt::CaseInsensitive)||from.back().label=="Switch baking mode"||from.back().label=="Automatic Normal bake"||from.back().label=="Edit bake parameters"||from.back().label=="Sync bake parameters"){bts::CompositeChannels channels=bts::complete_composite_channels;host.preview_layer_edit(from.back().state->set(uid),channels);}else if(from.back().label=="Edit displacement")host.preview_displacement(from.back().state->set(uid));else queue_document_edit(*document,*from.back().state,uid,document->edit_state(),from.back().state->edit_state(),bts::NativeMappingEditOrigin::HistoryRestore);
        to.push_back({std::move(document), uid,from.back().label,from.back().brush?std::optional{brush.values.base_color}:std::nullopt});
        if(from.back().brush)brush.values.base_color=*from.back().brush;
        const bool all_channel_sets=from.back().label=="Configure channels";
        document = std::move(from.back().state);
        from.pop_back();
        if(document->bake_configuration()!=to.back().state->bake_configuration())bts::apply_mesh_bake_template(document->id(),document->bake_configuration());
        refresh_document_ui(false,all_channel_sets?std::nullopt:std::optional<std::uint32_t>(uid));
      }
      catch (const std::exception &error) { workspace->append_console(QString::fromUtf8(error.what())); }
    };
