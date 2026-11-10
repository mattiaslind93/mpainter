// SPDX-License-Identifier: GPL-3.0-or-later
// Project, texture-set and export menus. Part of run_workspace_session() (app/WorkspaceSession.inc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
    if (workspace) {
      auto *file_menu = new QMenu("&File", workspace);file_menu->setObjectName("projectFileMenu");
      const auto existing = workspace->menuBar()->actions();
      workspace->menuBar()->insertMenu(existing.isEmpty() ? nullptr : existing.front(), file_menu);
      auto *blend_material_action=file_menu->addAction("Import Blender material…");blend_material_action->setObjectName("importBlendMaterialAction");

      auto *native_material_action=file_menu->addAction("Import native Blender shader…");native_material_action->setObjectName("importNativeBlendMaterialAction");
      const auto install_native_blend_material=[&](bts::PreparedBlendMaterial prepared){
        const auto textures=prepared.textures;
        native_material_loader->prepared(std::move(prepared),[&,textures](const bts::StudioAsset &asset){auto candidate=assets;for(const auto &image:textures)candidate.add(image);candidate.add(asset);assets=std::move(candidate);workspace->refresh_alpha_shelf();workspace->append_console("Native shader prepared: "+asset.name);if(asset.fill_preset&&asset.fill_preset->native_material)for(const auto &warning:asset.fill_preset->native_material->warnings)workspace->append_console(warning);workspace->set_status("Imported "+asset.name+(asset.fill_preset&&asset.fill_preset->native_material&&!asset.fill_preset->native_material->warnings.isEmpty()?" with warnings — see Console":" — select it in Shelf to add a fill layer"));},[&](const QString &error){workspace->set_status("Native material import failed: "+error);workspace->append_console(error);});
      };
      const auto install_blend_material=[&,install_native_blend_material](bts::PreparedBlendMaterial prepared){if(prepared.native_graph){install_native_blend_material(std::move(prepared));return;}if(prepared.preview_only)throw std::invalid_argument("A preview cannot be installed as a material");for(auto &image:prepared.textures)assets.add(std::move(image));const auto name=prepared.material.name;assets.add(std::move(prepared.material));workspace->refresh_alpha_shelf();workspace->append_console("Imported Principled channel preset: "+name+" (Color → ACEScg; scalar images → Non-Color/raw)");workspace->statusBar()->showMessage("Imported "+name+" — select it in Shelf to add a fill layer",8000);};
      QObject::connect(native_material_action,&QAction::triggered,workspace,[&,install_native_blend_material]{bts::choose_blend_material(workspace,[&](const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancel){return host.import_native_material(path,name,std::move(cancel));},install_native_blend_material,true);});
      QObject::connect(blend_material_action,&QAction::triggered,workspace,[&,install_blend_material]{bts::choose_blend_material(workspace,[&](const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancel){return host.import_blend_material(path,name,std::move(cancel));},install_blend_material);});
      auto *cancel_native=file_menu->addAction("Cancel native material loading");cancel_native->setObjectName("cancelNativeMaterialLoading");QObject::connect(cancel_native,&QAction::triggered,workspace,[&]{++native_apply_serial;native_material_loader->cancel_all();if(asset_progress)asset_progress->fail("Material loading cancelled");workspace->set_status("Native material loading cancelled");});
      auto *library_action=file_menu->addAction("Browse Blender Asset Library…");library_action->setObjectName("browseBlendAssetLibraryAction");QObject::connect(library_action,&QAction::triggered,workspace,[&,install_native_blend_material]{bts::BlendAssetBrowserHooks hooks;hooks.native_shader=true;hooks.scan=[&](const QString &root,std::shared_ptr<std::stop_source> stop,std::function<void(const QString &)> progress){return host.inspect_asset_library(root,std::move(stop),std::move(progress));};hooks.prepare=[&](const QString &file,const QString &name,std::shared_ptr<std::atomic_bool> cancel,bool preview){if(preview)throw std::logic_error("Native library browsing cannot prepare shaders");return host.import_native_material(file,name,std::move(cancel));};hooks.install=install_native_blend_material;hooks.base_color_preview=[&](const QString &file,const QString &name,std::shared_ptr<std::atomic_bool> cancel){return host.preview_native_material(file,name,std::move(cancel));};bts::browse_blend_asset_library(workspace,std::move(hooks));});
      auto *channel_library_action=file_menu->addAction("Browse Blender channel-preset library…");channel_library_action->setObjectName("browseBlendChannelLibraryAction");QObject::connect(channel_library_action,&QAction::triggered,workspace,[&,install_blend_material]{bts::BlendAssetBrowserHooks hooks;hooks.scan=[&](const QString &root,std::shared_ptr<std::stop_source> stop,std::function<void(const QString &)> progress){return host.inspect_asset_library(root,std::move(stop),std::move(progress));};hooks.prepare=[&](const QString &file,const QString &name,std::shared_ptr<std::atomic_bool> cancel,bool preview){return host.import_blend_material(file,name,std::move(cancel),preview);};hooks.install=install_blend_material;bts::browse_blend_asset_library(workspace,std::move(hooks));});
      if(auto *button=workspace->findChild<QToolButton *>("importResources")){auto *menu=new QMenu(button);menu->addAction(blend_material_action);menu->addAction(native_material_action);menu->addAction(library_action);menu->addAction(channel_library_action);button->setMenu(menu);button->setPopupMode(QToolButton::MenuButtonPopup);button->setFixedSize(38,24);}
      auto *edit_menu = new QMenu("&Edit", workspace);
      workspace->menuBar()->insertMenu(existing.isEmpty() ? nullptr : existing.front(), edit_menu);
      edit_menu->addAction(workspace->findChild<QAction *>("shelfPreferencesAction"));
      auto *input_menu=workspace->findChild<QMenu *>("viewportMenu");
      auto *pressure_overlay=input_menu->addAction("Show pen pressure");pressure_overlay->setObjectName("showPenPressure");pressure_overlay->setCheckable(true);
      QSettings pointer_preferences("mPainter","Preferences");pressure_overlay->setChecked(pointer_preferences.value("Viewport/showPenPressure",false).toBool());viewport->pressure_overlay_=pressure_overlay->isChecked();
      QObject::connect(pressure_overlay,&QAction::toggled,workspace,[&](bool enabled){viewport->pressure_overlay_=enabled;QSettings settings("mPainter","Preferences");settings.setValue("Viewport/showPenPressure",enabled);viewport->update();});
      edit_menu->addSeparator();
      auto *undo = edit_menu->addAction("&Undo");
      undo_action=undo;
      undo->setObjectName("undoDocument");
      undo->setShortcut(QKeySequence::Undo);
      QObject::connect(undo, &QAction::triggered, workspace, [&] { history_step(false); });
      auto *redo = edit_menu->addAction("&Redo");
      redo_action=redo;
      redo->setObjectName("redoDocument");
      redo->setShortcut(QKeySequence::Redo);
      QObject::connect(redo, &QAction::triggered, workspace, [&] { history_step(true); });
      for (const auto &[name,action]:{std::pair{"undoToolbar",undo},std::pair{"redoToolbar",redo}}) {
        auto *button=workspace->findChild<QToolButton *>(name);action->setIcon(button->icon());button->setDefaultAction(action);
      }
      refresh_history_actions();
      auto *configure=edit_menu->addAction("Project Configuration…");configure->setObjectName("projectConfiguration");QObject::connect(configure,&QAction::triggered,workspace,[&]{if(opening_project){workspace->set_status("Finish or cancel project Open before reimport");return;}if(!document)return;viewport->end_paint(false);bts::configure_project_mesh(workspace,*document,[&](const QString &path,bool preserve_world,bts::NormalMapFormat format,bool per_fragment,bool import_cameras,const bts::AutoUnwrapOptions &auto_unwrap,const bts::UsdImportOptions &usd,const bts::UnitSettings &units,const bts::BlendMeshSelection &blend){if(path.isEmpty()){auto candidate=std::make_unique<bts::TextureDocument>(*document);candidate->set_normal_map_format(format);candidate->set_tangent_per_fragment(per_fragment);candidate->set_auto_unwrap(auto_unwrap);candidate->set_units(units);try{host.rebind_texture_sets(*candidate);}catch(...){host.rebind_texture_sets(*document);throw;}document=std::move(candidate);refresh_document_ui();return;}auto replacement=host.prepare_reimport(*document,QFileInfo(path).absoluteFilePath().toStdString(),preserve_world,import_cameras,auto_unwrap,usd,units,blend);replacement.authored->set_normal_map_format(format);replacement.authored->set_tangent_per_fragment(per_fragment);try{checkpoint(document_path,true);}catch(const std::exception &error){workspace->append_console("Pre-reimport checkpoint unavailable: "+QString::fromUtf8(error.what()));}host.commit_reimport(*document,replacement);viewport->stroke_anchor.reset();undo_history.clear();redo_history.clear();refresh_document_ui();});});
      auto *new_action = file_menu->addAction("&New…");
      new_action->setObjectName("newProject");new_action->setShortcut(QKeySequence::New);
      QObject::connect(new_action, &QAction::triggered, workspace, [&] {
        cancel_pending_new(); // Opening another New replaces any waiting intent.
        auto *dialog = new bts::NewProjectDialog(workspace);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(dialog, &QDialog::accepted, workspace, [&, dialog] {
          if(owner_programs_pending()){
            const auto options=dialog->options();if(!options)return;
            cancel_pending_new();
            const auto project=document?document->id().toString(QUuid::WithoutBraces).toStdString():std::string{};
            pending_new->replace(*options,project,document_revision);const auto generation=pending_new->generation();
            auto *progress=new QProgressDialog("Preparing graphics before New…","Cancel",0,0,workspace);
            progress->setObjectName("newProjectProgramProgress");progress->setProperty("newGeneration",qulonglong(generation));progress->setWindowModality(Qt::NonModal);progress->setMinimumDuration(0);pending_new_progress=progress;
            auto *timer=new QTimer(progress);timer->setInterval(16);pending_new_poll=timer;
            QObject::connect(progress,&QProgressDialog::canceled,progress,[&,intent=pending_new,generation]{intent->cancel_generation(generation);clear_pending_new_ui(generation);});
            QObject::connect(timer,&QTimer::timeout,progress,[&,intent=pending_new,retry=QPointer<QTimer>(timer),generation]{
              try{
                const auto project_id=[&]{return document?document->id().toString(QUuid::WithoutBraces).toStdString():std::string{};};
                auto request=intent->take_ready(generation,owner_programs_pending(),project_id(),document_revision);
                if(!request){if(!intent->pending())clear_pending_new_ui(generation);return;}
                if(retry)retry->stop(); // No reentrant timeout during Save dialogs.
                if(!discard_or_save()){intent->cancel_generation(generation);clear_pending_new_ui(generation);return;}
                // Save may pump events; a newer request or edit invalidates this one.
                if(!intent->current(*request,project_id(),document_revision)){clear_pending_new_ui(generation);workspace->set_status("New cancelled because the project changed");return;}
                if(owner_programs_pending()){if(intent->restore(std::move(*request))&&retry)retry->start();return;}
                clear_pending_new_ui(generation);
                if(qEnvironmentVariableIsSet("BTS_GPU_EARLY_PROJECT_TEST_MESH")){
                  workspace->setProperty("newProgramDeferredExecutions",workspace->property("newProgramDeferredExecutions").toInt()+1);
                  workspace->setProperty("newProgramExecutionCompilerPending",blender::GPU_shader_compiler_has_pending_work());
                  workspace->setProperty("newProgramExecutionMonotonicMs",std::chrono::duration<double,std::milli>(bts::performance::Clock::now().time_since_epoch()).count());
                }
                create_project(request->options);
              }catch(const std::exception &error){
                intent->cancel_generation(generation);clear_pending_new_ui(generation);
                workspace->append_console(QString::fromUtf8(error.what()));QMessageBox::critical(workspace,"New project failed",QString::fromUtf8(error.what()));
              }catch(...){
                intent->cancel_generation(generation);clear_pending_new_ui(generation);
                workspace->append_console("New project failed with an unknown error");QMessageBox::critical(workspace,"New project failed","Unknown native error");
              }
            });
            timer->start();progress->show();return;
          }
          if(!discard_or_save())return;
          if (const auto options = dialog->options()) {
            try { create_project(*options); }
            catch (const std::exception &error) {
              workspace->append_console(QString::fromUtf8(error.what()));
              QMessageBox::critical(workspace, "New project failed", QString::fromUtf8(error.what()));
            }
          }
        });
        dialog->open();
      });
    }
    if(workspace) {
      workspace->close_requested=[&] {
        if(harness.is_set("brush-presets-demo")||harness.is_set("gui-fix-demo")||harness.is_set("new-demo-dir")||harness.is_set("demo-dir")||harness.is_set("layout-demo-dir")||harness.is_set("navigation-demo-dir")||harness.is_set("open-demo-dir"))return true;
        return discard_or_save();
      };
      auto *menu=workspace->findChild<QMenu *>("projectFileMenu");
      auto *open=menu->addAction("&Open…");open->setObjectName("openProject");open->setShortcut(QKeySequence::Open);
      QObject::connect(open,&QAction::triggered,workspace,[&]{
        const auto path=bts::FileDialog::getOpenFileName(workspace,"Open project",document_path,"Blender projects (*.blend)");
        if(path.isEmpty()||!discard_or_save())return;
        begin_open_document(path);
      });
      auto *recover=menu->addAction("Recover snapshot…");recover->setObjectName("recoverProject");QObject::connect(recover,&QAction::triggered,workspace,[&]{if(!discard_or_save())return;const auto path=bts::choose_recovery(workspace,recovery);if(path.isEmpty())return;begin_open_document(path,true);});if(!recovery.entries().empty())workspace->append_console("Recovery snapshots available: File → Recover snapshot…");
      menu->addSeparator();auto *save=menu->addAction("&Save");save->setShortcut(QKeySequence::Save);
      QObject::connect(save,&QAction::triggered,workspace,[&]{choose_save(false);});
      auto *save_as=menu->addAction("Save &As…");save_as->setShortcut(QKeySequence::SaveAs);
      QObject::connect(save_as,&QAction::triggered,workspace,[&]{choose_save(true);});
    }
    if(workspace){auto *menu=workspace->findChild<QMenu *>("projectFileMenu");auto *action=menu->addAction("Save As Template…");action->setObjectName("saveProjectTemplate");QObject::connect(action,&QAction::triggered,workspace,[&]{if(!document)return;viewport->end_paint(false);try{const auto env=host.environment_state();const auto uid=selected_material>=0?snapshot.materials[selected_material].session_id:document->texture_sets().front().material_session_uid;const auto data=bts::make_project_template(*document,uid,host.color_state(),env,bts::pack_environment_resource(env["resource"].toString()),bts::mesh_bake_template(document->id()));QDir().mkpath(bts::project_template_directory());bts::FileDialog picker(workspace,"Save template",bts::project_template_directory(),"BTS project templates (*.btst)");picker.setObjectName("saveTemplatePicker");picker.setAcceptMode(QFileDialog::AcceptSave);picker.setOption(QFileDialog::DontUseNativeDialog);picker.setDefaultSuffix("btst");if(picker.exec()==QDialog::Accepted&&!picker.selectedFiles().isEmpty()){bts::write_project_template(picker.selectedFiles().front(),data);workspace->append_console("Project template saved: "+picker.selectedFiles().front());}}catch(const std::exception &error){workspace->append_console(QString::fromUtf8(error.what()));QMessageBox::warning(workspace,"Template could not be saved",QString::fromUtf8(error.what()));}});}
    if(workspace){auto *settings=workspace->findChild<QToolButton *>("textureSetListSettings");auto *menu=new QMenu(settings);auto *action=menu->addAction("Reassign Texture Sets…");action->setObjectName("reassignTextureSets");settings->setMenu(menu);settings->setPopupMode(QToolButton::InstantPopup);QObject::connect(action,&QAction::triggered,workspace,[&]{if(!document)return;viewport->end_paint(false);try{const auto assigned=bts::choose_texture_set_assignment(workspace,*document,snapshot);if(!assigned)return;auto candidate=std::make_unique<bts::TextureDocument>(*document);candidate->stage_texture_sets(*assigned);if(candidate->to_json()==document->to_json())return;try{checkpoint(document_path,true);}catch(const std::exception &error){workspace->append_console("Pre-assignment checkpoint unavailable: "+QString::fromUtf8(error.what()));}try{host.rebind_texture_sets(*candidate);}catch(...){host.rebind_texture_sets(*document);throw;}document=std::move(candidate);undo_history.clear();redo_history.clear();viewport->stroke_anchor.reset();refresh_document_ui();}catch(const std::exception &error){workspace->append_console(QString::fromUtf8(error.what()));QMessageBox::warning(workspace,"Texture Set reassignment failed",QString::fromUtf8(error.what()));}});}
    if(workspace&&qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO")){auto *refuse=new QAction(workspace);refuse->setObjectName("guiBmRefuseNextPage");QObject::connect(refuse,&QAction::triggered,workspace,[workspace]{workspace->setProperty("bmRefuseNextPage",true);workspace->setProperty("bmRefusal",QJsonObject{{"armed",true},{"fired",false},{"terminal",false},{"reason",QString()},{"needed_bytes",qint64(2*bts::adjustment_page_bytes)},{"available_bytes",0},{"stroke_cancelled",false},{"acknowledged",false}});});workspace->addAction(refuse);auto *ack=new QAction(workspace);ack->setObjectName("guiBmAcknowledgeRefusal");QObject::connect(ack,&QAction::triggered,workspace,[workspace]{auto refusal=workspace->property("bmRefusal").toJsonObject();if(!refusal["fired"].toBool()||!refusal["terminal"].toBool())return;const auto reason=refusal["reason"].toString();if(auto *label=workspace->findChild<QLabel *>("workspaceErrorText");label&&label->text()==reason)workspace->set_error_status({});if(QString::fromStdString(bts::pending_composite_error)==reason)bts::pending_composite_error.clear();refusal["acknowledged"]=true;workspace->setProperty("bmRefusal",refusal);});workspace->addAction(ack);}
    if(workspace){auto *menu=workspace->findChild<QMenu *>("projectFileMenu");menu->addSeparator();auto *export_action=menu->addAction("Export textures…");export_action->setObjectName("exportTextures");export_action->setShortcut(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_E));QObject::connect(export_action,&QAction::triggered,workspace,[&]{if(!document)return;viewport->end_paint(false);if(bts::choose_texture_export(workspace,*document,[&](const auto &ids){return host.export_source(*document,ids);},[&](bool displaced){return host.prepare_mesh_export(*document,displaced,true);},host.mesh_export_displacement_available(*document),[&](const bts::ExportRequest &request,std::shared_ptr<const std::atomic_bool> cancelled,std::function<void(std::vector<bts::ExportTile>)> complete,std::function<void(std::string)> failed){new bts::GpuTextureExportSource(workspace,host,[viewport]{viewport->update();},*document,request,std::move(cancelled),std::move(complete),std::move(failed));}))refresh_document_ui();schedule_residency_spill();update_inspection(host.inspection_mode());viewport->update();});}
    if(workspace){auto *menu=workspace->findChild<QMenu *>("projectFileMenu");auto *action=menu->addAction("Export Mesh…");action->setObjectName("exportMesh");QObject::connect(action,&QAction::triggered,workspace,[&]{if(!document){workspace->append_console("No project open");return;}viewport->end_paint(false);bts::choose_mesh_export(workspace,QString::fromStdString(document->settings().absolute_mesh_path),host.mesh_export_displacement_available(*document),[&](bool displaced,bool normals){return host.prepare_mesh_export(*document,displaced,normals);});});}
