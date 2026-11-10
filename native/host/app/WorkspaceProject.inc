// SPDX-License-Identifier: GPL-3.0-or-later
// Project lifecycle: new project, save, recovery, open and the autosave timer. Part of
// WorkspaceSession::run() (app/WorkspaceSession.inc), included in order inside its body; it shares
// that function's locals and is not a standalone file.
const auto create_project = [&](const bts::NewProjectOptions &options) {
  if (parameter_import)
    parameter_import->cancel();
  if (opening_project)
    throw std::logic_error(
        "Cancel the pending Open and wait for it to finish before creating a project");
  const auto previous_project = document ? document->id().toString(QUuid::WithoutBraces) :
                                           QString();
  if (render_controller)
    render_controller->reset_document();
  viewport->warp_editor.finish(false);
  if (uniform_before)
    finish_uniform_edit(false);
  viewport->finish_path_drag(true);
  viewport->end_paint(true);
  QJsonObject project_template;
  QString template_environment;
  if (!options.template_path.empty() && !bts::builtin_project_template(options.template_path)) {
    project_template = bts::read_project_template(QString::fromStdString(options.template_path));
    const auto desired =
        project_template["display"].toObject()["view_transform"].toString().toStdString();
    const auto supported = host.display_transforms();
    if (std::find(supported.begin(), supported.end(), desired) == supported.end())
      throw std::invalid_argument(
          "Template display transform is unavailable in this color configuration");
    template_environment = bts::unpack_environment_resource(
        project_template["resource"].toObject());
  }

  const auto utility_maps = bts::load_utility_maps(options.utility_maps);
#ifdef BTS_WITH_MATERIALX
  if (material_importer)
    material_importer->cancel();
#endif
  const auto imported = host.new_project(
      options.absolute_mesh_path, parser.value("environment").toStdString(), &options);
  bts::FileDialog::remember_folder(
      QFileInfo(QString::fromStdString(options.absolute_mesh_path)).absolutePath());
  inspected_map = -1;
  viewport->mesh_map_active = -1;
  document_path.clear();
  viewport->polygon_active = false;
  viewport->polygon_drag = false;
  workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
  viewport->picker_active = false;
  workspace->set_picker_active(false);
  viewport->stroke_anchor.reset();
  viewport->path_active = viewport->path_editing = false;
  viewport->selected_path.clear();
  viewport->selected_vertex = -1;
  viewport->pending_path_point.reset();
  viewport->paths.clear();
  viewport->path_draw.clear();
  quick_enabled = quick_editing = false;
  quick_masks.clear();
  quick_stroke_before.reset();
  quick_brush = bts::BrushSettings{};
  quick_brush.mask_value = 0;
  viewport->quick_mask_gate.reset();
  viewport->quick_mask_edit = false;
  brush = paint_brush = eraser_brush = bts::BrushSettings{};
  smudge_brush = bts::default_smudge_brush();
  clone_brush = bts::BrushSettings{};
  clone_brush.clone = true;
  viewport->clone_source.reset();
  viewport->clone_source_display.reset();
  projection_brush = bts::BrushSettings{};
  projection_brush.projected.emplace();
  projection_brush.values = projection_brush.projected->values;
  symmetry = {};
  host.set_symmetry(symmetry);
  workspace->set_symmetry(symmetry);
  if (options.uv_workflow == bts::UvWorkflow::PreserveTiles)
    brush.channels = {true, false, false, false, false};
  snapshot = host.scene_snapshot();
  viewport->set_scene_cameras(snapshot.cameras);
  if (shader)
    shader->reset_document();
  if (skew)
    skew->reset_document();
  skew_profile_active = false;
  document = std::make_unique<bts::TextureDocument>(options, imported, snapshot);
  const auto imported_shader_notes = options.template_path.empty() ?
                                         host.apply_imported_shader_profiles(*document) :
                                         std::vector<std::string>{};
  if (bts::builtin_project_template(options.template_path))
    bts::apply_builtin_project_template(*document, options.template_path);
  viewport->set_empty_project(false);
  viewport->reset_shader_failure();
  if (!project_template.isEmpty()) {
    bts::apply_project_template(*document, project_template);
    bts::apply_mesh_bake_template(document->id(), project_template["baking"].toObject());
    host.set_environment_resource(template_environment.toStdString());
    const auto env = project_template["environment"].toObject();
    host.set_environment_rotation(env["rotation"].toDouble());
    host.set_environment_alignment(env["alignment"] == "World");
    for (auto field : {"exposure", "opacity", "blur"})
      host.set_environment_appearance(field, env[field].toDouble());
    host.set_display_transform(
        project_template["display"].toObject()["view_transform"].toString().toStdString());
  }

  for (const auto &warning : bts::assign_utility_maps(*document, utility_maps))
    workspace->append_console(QString::fromStdString(warning));
  // Establish fixed channel graphs for every set before the first frame.
  // Later layer/set selection only updates their GPU targets, never topology.
  for (const auto &set : document->texture_sets())
    if (!set.disabled && set.uniform_editable)
      host.prepare_paint_texture(set);
  update_inspection(bts::InspectionMode::Material);
  assets.replace_project({});
  workspace->refresh_alpha_shelf();
  undo_history.clear();
  redo_history.clear();
  refresh_history_actions();
  QStringList display_choices;
  for (const auto &name : host.display_transforms())
    display_choices.append(QString::fromStdString(name));
  workspace->set_display_transforms(display_choices,
                                    host.color_state()["view_transform"].toString());
  QStringList names;
  for (const auto &material : snapshot.materials)
    names.append(QString::fromStdString(material.name));
  workspace->set_texture_sets(names, names.isEmpty() ? -1 : 0);
  for (int row = 0; row < int(snapshot.materials.size()); ++row)
    workspace->texture_sets()->item(row)->setData(Qt::UserRole,
                                                  snapshot.materials[row].session_id);
  if (use_workspace)
    sync_reference_image_state();
  select_material(workspace->texture_sets()->currentRow());
  viewport->handle_navigation_key(Qt::Key_F);
  dirty = true;
  (++document_revision, last_document_edit = bts::performance::Clock::now());
  window->setWindowTitle((document_path.isEmpty() ? QString("Untitled") :
                                                    QFileInfo(document_path).completeBaseName()) +
                         " * — mPainter");
  for (const auto &warning : imported.warnings)
    workspace->append_console(QString::fromStdString(warning));
  for (const auto &note : imported_shader_notes)
    workspace->append_console(QString::fromStdString(note));
  workspace->append_console(QString("Imported %1 objects, %2 triangles; ACEScg")
                                .arg(imported.objects)
                                .arg(imported.triangles));
#ifdef BTS_WITH_MATERIALX
  if (schedule_material_import)
    schedule_material_import();
#endif

  for (const auto &material : snapshot.materials) {
    const auto &set = document->set(material.session_id);
    const QString reason =
        set.uniform_editable ?
            "Editable" :
        QString::fromStdString(material.roughness_unavailable_reason).isEmpty() ?
            "Requires a Principled BSDF connected directly to Surface with unlinked Base Color/Roughness/Metallic/Normal inputs" :
            QString::fromStdString(material.roughness_unavailable_reason);
    workspace->append_console("Texture Set " + QString::fromStdString(set.name) + ": " + reason);
    const int row = workspace->texture_sets()
                            ->findItems(QString::fromStdString(set.name), Qt::MatchExactly)
                            .isEmpty() ?
                        -1 :
                        workspace->texture_sets()->row(
                            workspace->texture_sets()
                                ->findItems(QString::fromStdString(set.name), Qt::MatchExactly)
                                .first());
    if (row >= 0)
      workspace->texture_sets()->item(row)->setToolTip(reason);
  }
  viewport->update();
  if (python_event) {
    if (!previous_project.isEmpty())
      python_event("ProjectEditionLeft", {{"project_id", previous_project}});
    python_event("ProjectCreated",
                 {{"project_id", document->id().toString(QUuid::WithoutBraces)}});
  }
};
const auto recovery_root = parser.isSet("recovery-dir") ?
                               QFileInfo(parser.value("recovery-dir")).absoluteFilePath() :
                               qEnvironmentVariable("BTS_RECOVERY_DIR",
                                                    QStandardPaths::writableLocation(
                                                        QStandardPaths::AppLocalDataLocation) +
                                                        "/recovery");
bts::RecoveryStore recovery(recovery_root);
struct SaveIntent {
  QString path, source;
  QUuid project;
  std::uint64_t revision = 0;
  bool recovery = false, copy = false;
  std::optional<bts::ProjectFileIdentity> expected;
};
std::optional<SaveIntent> requested_save, active_save;
const auto save_options = parser.optionNames();
const bool diagnostic_sync_save = !qEnvironmentVariableIsSet("BTS_VERIFY_ASYNC_SAVE") &&
                                  std::any_of(
                                      save_options.begin(),
                                      save_options.end(),
                                      [](const QString &name) { return name.contains("demo"); });
QTimer save_poll_timer;
save_poll_timer.setInterval(5);
const auto request_save = [&](SaveIntent intent) {
  if (parameter_import)
    parameter_import->cancel();
  if (!requested_save || !intent.recovery)
    requested_save = std::move(intent);
  workspace->setProperty("projectSaveError", QString());
  window->statusBar()->showMessage("Saving…");
  save_poll_timer.start();
};
const auto poll_save = [&] {
  try {
    if (active_save) {
      if (!host.poll_save_project())
        return;
      const auto completed = *active_save;
      active_save.reset();
      schedule_residency_spill();
      if (completed.recovery) {
        last_recovery_document = completed.project;
        last_recovery_revision = completed.revision;
        std::printf("BTS_RECOVERY_READY pid=%lld path=%s background=1\n",
                    static_cast<long long>(QCoreApplication::applicationPid()),
                    qPrintable(completed.path));
      }
      else if (!completed.copy && document && document->id() == completed.project) {
        if (requested_save && requested_save->path == completed.path &&
            requested_save->expected == document_disk_identity)
          requested_save->expected = host.saved_project_identity();
        document_path = completed.path;
        document_disk_identity = host.saved_project_identity();
        if (document_revision == completed.revision)
          dirty = false;
        window->setWindowTitle(QFileInfo(completed.path).completeBaseName() + " — mPainter");
        if (python_event)
          python_event("ProjectSaved",
                       {{"project_id", completed.project.toString(QUuid::WithoutBraces)},
                        {"file_path", completed.path}});
      }
      window->statusBar()->showMessage("Saved " + QTime::currentTime().toString("HH:mm:ss"), 5000);
    }
    if (!requested_save) {
      save_poll_timer.stop();
      return;
    }
    if (!document || document->id() != requested_save->project)
      throw std::runtime_error("Project changed before its save snapshot could be captured");
    const bool interacting = (parameter_import && parameter_import->pending()) || uniform_before ||
                             stroke_before || path_drag_before || viewport->painting_ ||
                             property_dragging || property_idle_timer.isActive() ||
                             ((texture_loader && texture_loader->pending())
#ifdef BTS_WITH_MATERIALX
                              || (material_importer && material_importer->pending())
#endif
                                  ) ||
                             (skew && skew->pending()) || QApplication::activeModalWidget();
    if (!viewport->draw_guard.failed() &&
        (interacting || host.texture_updates_pending() ||
         (requested_save->recovery && bts::performance::ms(last_document_edit) < 2000)))
    {
      viewport->update();
      return;
    }
    auto request = *requested_save;
    request.revision = document_revision;
    host.begin_save_project(
        *document,
        request.path.toStdString(),
        assets.project_assets(),
        [request, &recovery] {
          if (request.recovery)
            recovery.record(request.project, request.path, request.source);
          else
            bts::FileDialog::remember_folder_async(QFileInfo(request.path).absolutePath());
        },
        request.expected);
    active_save = std::move(request);
    requested_save.reset();
  }
  catch (const std::exception &error) {
    requested_save.reset();
    active_save.reset();
    const auto message = "Save failed: " + QString::fromUtf8(error.what());
    qWarning("BTS_PROJECT_SAVE_FAILED %s", qPrintable(message));
    workspace->setProperty("projectSaveError", message);
    workspace->append_console(message);
    if (viewport->draw_guard.failed())
      workspace->set_error_status(
          "Viewport stopped. Recovery/save failed; previous checkpoints are retained.\n" +
          message);
    window->statusBar()->showMessage(message, 15000);
  }
};
QObject::connect(&save_poll_timer,
                 &QTimer::timeout,
                 window.get(),
                 poll_save); /* Start only while a save is pending. */
publish_parameters = [&](std::uint64_t expected,
                         std::uint32_t uid,
                         std::unique_ptr<bts::TextureDocument> &candidate,
                         const std::vector<bts::StudioAsset> &originals,
                         const QUuid &preparation_owner) {
  // Cached edit metadata/COW only: no pixel serialization or image hashing
  // on the interactive owner. Both Import and diagnostics publish here.
  const auto current = [&] {
    return document && candidate && document_revision == expected &&
           candidate->id() == document->id() && selected_material >= 0 &&
           snapshot.materials.at(selected_material).session_id == uid && !opening_project &&
           !active_save && !requested_save && !host.save_project_pending();
  };
  if (!current())
    throw std::runtime_error(
        "Parameter reconstruction target/revision changed before publication");
  viewport->end_paint(false);
  if (uniform_before && finish_uniform_edit)
    finish_uniform_edit(false);
  if (!current())
    throw std::runtime_error("Parameter reconstruction target changed while finishing input");
  const auto before = document->edit_state(), after = candidate->edit_state();
  auto before_settings = before, after_settings = after;
  before_settings.remove("texture_sets");
  before_settings.remove("shader_instances");
  after_settings.remove("texture_sets");
  after_settings.remove("shader_instances");
  if (before_settings != after_settings ||
      document->texture_sets().size() != candidate->texture_sets().size())
    throw std::runtime_error("Parameter reconstruction changed project settings");
  const auto &old_shaders = document->shader_instances(),
             &new_shaders = candidate->shader_instances();
  if (new_shaders.instances.size() != old_shaders.instances.size() + 1 ||
      new_shaders.default_instance != old_shaders.default_instance)
    throw std::runtime_error("Parameter reconstruction must add one local shader instance");
  for (const auto &[id, instance] : old_shaders.instances)
    if (!new_shaders.instances.contains(id) || new_shaders.instances.at(id) != instance)
      throw std::runtime_error("Parameter reconstruction changed a retained shader");
  for (const auto &set : document->texture_sets()) {
    const auto &restored = candidate->set(set.material_session_uid);
    if (restored.id != set.id)
      throw std::runtime_error("Parameter reconstruction changed a Texture Set identity");
    if (set.material_session_uid != uid &&
        (bts::edit_set_state(before, set.id) != bts::edit_set_state(after, set.id) ||
         new_shaders.assignments.at(set.id) != old_shaders.assignments.at(set.id)))
      throw std::runtime_error("Parameter reconstruction changed an unrelated Texture Set");
  }
  const auto &target = candidate->set(uid);
  bts::require_uniform_material_layering(target);
  bts::validate_material_substacks(target);
  host.validate_material_layering(document->set(uid));
  const auto assigned = new_shaders.assignments.at(target.id);
  if (old_shaders.instances.contains(assigned) ||
      new_shaders.for_set(target.id).parameters != target.shader_parameters)
    throw std::runtime_error(
        "Parameter reconstruction did not preserve its fresh shader assignment");
  auto prepared_catalog = bts::prepare_material_parameter_catalog(assets, originals);
  std::vector<std::shared_ptr<const bts::FillTexture>> textures;
  std::set<const bts::FillTexture *> unique;
  const auto retain = [&](const auto &source) {
    if (source && unique.insert(source.get()).second)
      textures.push_back(source);
  };
  for (const auto &input : target.material_layering.materials)
    for (const auto &source : input.textures)
      retain(source);
  for (const auto &layer : target.layers)
    if (layer.kind == bts::LayerKind::Fill)
      for (int c = 0; c < 6 + bts::custom_channel_count; ++c)
        retain(bts::fill_channel_texture(layer.fill, c));
  for (const auto &asset : originals)
    if (std::none_of(textures.begin(), textures.end(), [&](const auto &source) {
          return source->asset == asset.id.toString(QUuid::WithoutBraces).toStdString();
        }))
      throw std::runtime_error("Prepared parameter catalog contains an unreferenced original");
  for (const auto &source : textures) {
    const auto found = std::find_if(originals.begin(), originals.end(), [&](const auto &asset) {
      return asset.id.toString(QUuid::WithoutBraces).toStdString() == source->asset;
    });
    if (found == originals.end() || found->hash().toStdString() != source->hash)
      throw std::runtime_error("Parameter candidate source has no matching prepared original");
  }
  if (!(preparation_owner.isNull() ? host.prepare_texture_resources(textures) :
                                     host.prepare_texture_resources(preparation_owner, textures)))
  {
    viewport->update();
    return false;
  }
  if (!current())
    throw std::runtime_error("Parameter reconstruction became stale during resource preparation");
  auto history = bts::prepare_document_commit(undo_history, uid);
  history.label = "Reconstruct material parameters";
  queue_document_edit(*document, *candidate, uid, before, after);
  static_assert(std::is_nothrow_move_assignable_v<bts::AssetCatalog>);
  bts::publish_document_commit(
      document, candidate, undo_history, redo_history, std::move(history), [&]() noexcept {
        assets = std::move(prepared_catalog);
      });
  mark_document_commit();
  if (preparation_owner.isNull())
    host.cancel_texture_preparation();
  if (undo_history.size() > 128)
    undo_history.erase(undo_history.begin());
  refresh_document_ui(false, uid, false);
  window->setWindowTitle("Reconstructed materials * — mPainter");
  return true;
};
{
  bts::MaterialParameterImportHooks hooks;
  hooks.current = [&](const auto &target) {
    if (!document || document->id() != target.project || document_revision != target.revision ||
        selected_material < 0 || selected_material >= int(snapshot.materials.size()) ||
        snapshot.materials[selected_material].session_id != target.uid ||
        document->set(target.uid).id != target.set || opening_project || quick_enabled ||
        active_save || requested_save || host.save_project_pending())
      return false;
    const auto color = host.color_state();
    if (color["working_space"].toString() != target.working_space)
      return false;
    const auto rows = color["scene_linear_to_xyz"].toArray();
    if (rows.size() != 3)
      return false;
    for (int row = 0; row < 3; ++row) {
      const auto values = rows[row].toArray();
      if (values.size() != 3)
        return false;
      for (int column = 0; column < 3; ++column)
        if (!values[column].isDouble() ||
            values[column].toDouble() != target.scene_linear_to_xyz[row * 3 + column])
          return false;
    }
    return true;
  };
  hooks.validate = [&](const auto &target) {
    if (!document || document->id() != target.project || document_revision != target.revision ||
        document->set(target.uid).id != target.set)
      throw std::runtime_error("Parameter Import target changed");
    host.validate_material_parameter_import(document->set(target.uid));
  };
  hooks.busy = [&] {
    const bool busy = bool(active_save) || bool(requested_save) || host.save_project_pending() ||
                      host.texture_updates_pending() ||
                      (texture_loader && texture_loader->pending()) || (skew && skew->pending());
    if (busy)
      viewport->update();
    return busy;
  };
  hooks.claim = [&](const QUuid &owner) { return host.claim_texture_preparation(owner); };
  hooks.release = [&](const QUuid &owner) { host.cancel_texture_preparation(owner); };
  hooks.publish = [&](auto &candidate, const QUuid &owner) {
    return publish_parameters(candidate.target.revision,
                              candidate.target.uid,
                              candidate.document,
                              candidate.originals,
                              owner);
  };
  parameter_import = std::make_unique<bts::MaterialLayerParameterImportJob>(std::move(hooks));
}
const auto checkpoint = [&](const QString &source, bool before_change = false) {
  if (parameter_import)
    parameter_import->cancel();
  if (!document)
    throw std::runtime_error("No project to recover");
  const auto path = recovery.next_path(document->id());
  if (diagnostic_sync_save) {
    host.save_project(*document, path.toStdString(), assets.project_assets(), true);
    recovery.record(document->id(), path, source);
    last_recovery_document = document->id();
    last_recovery_revision = document_revision;
  }
  else if (before_change) {
    // Reimport/reassignment mutates Main in this same event. Capture its
    // immutable old revision now, rather than queue a snapshot of the new mesh.
    if (active_save || requested_save || host.save_project_pending() ||
        host.texture_updates_pending())
      throw std::runtime_error(
          "Pre-change recovery is busy; the previous completed snapshot is retained");
    const SaveIntent captured{path, source, document->id(), document_revision, true};
    host.begin_save_project(
        *document, path.toStdString(), assets.project_assets(), [captured, &recovery] {
          recovery.record(captured.project, captured.path, captured.source);
        });
    active_save = captured;
    save_poll_timer.start();
  }
  else
    request_save({path, source, document->id(), document_revision, true});
  return path;
};
viewport->draw_error = [&](const QString &message) {
  workspace->set_error_status(message);
  workspace->append_console(message);
  window->statusBar()->showMessage(message);
  try {
    viewport->end_paint(false);
    viewport->finish_path_drag(false);
    if (uniform_before && finish_uniform_edit)
      finish_uniform_edit(false);
    if (document) {
      const auto path = checkpoint(document_path);
      workspace->append_console("Viewport recovery requested: " + path);
    }
  }
  catch (const std::exception &error) {
    workspace->set_error_status(message + "\nRecovery failed: " + QString::fromUtf8(error.what()));
  }
};
const auto detach_draw_error = qScopeGuard([view = QPointer<Viewport>(viewport)] {
  if (view)
    view->draw_error = {};
});
const auto save_document = [&](QString path) {
  if (parameter_import)
    parameter_import->cancel();
  if (uniform_before && finish_uniform_edit)
    finish_uniform_edit(false);
  viewport->finish_path_drag(false);
  if (!document)
    throw std::runtime_error("Create or open a project before saving");
  viewport->end_paint(false);
  if (diagnostic_sync_save && skew && !skew->flush())
    throw std::runtime_error("Skew Normal bake did not finish; project remains unsaved");
  if (!path.endsWith(".blend", Qt::CaseInsensitive))
    path += ".blend";
  // Manual save is itself a durable checkpoint. Do not first serialize a
  // second full recovery file; autosave owns independent recovery snapshots.
  path = QFileInfo(path).absoluteFilePath();
  auto expected = bts::ProjectFileIdentity::read(path);
  if (path == document_path && expected != document_disk_identity) {
    const auto choice = bts::ask_project_save_conflict(workspace, path);
    if (choice == bts::ProjectSaveChoice::Cancel)
      return false;
    if (choice == bts::ProjectSaveChoice::SaveAs) {
      path = bts::FileDialog::getSaveFileName(
          workspace, "Save project as", path, "Blender projects (*.blend)");
      if (path.isEmpty())
        return false;
      if (!path.endsWith(".blend", Qt::CaseInsensitive))
        path += ".blend";
      path = QFileInfo(path).absoluteFilePath();
      expected = bts::ProjectFileIdentity::read(path);
    }
  }
  if (diagnostic_sync_save) {
    host.save_project(*document, path.toStdString(), assets.project_assets(), false, expected);
    document_disk_identity = host.saved_project_identity();
    bts::FileDialog::remember_folder(QFileInfo(path).absolutePath());
    document_path = path;
    dirty = false;
    window->setWindowTitle(QFileInfo(path).completeBaseName() + " — mPainter");
    schedule_residency_spill();
  }
  else
    request_save({path, {}, document->id(), document_revision, false, false, expected});
  return true;
};
if (qEnvironmentVariableIsSet("BTS_AE_PERSISTENCE_DEMO")) {
  auto *state_action = new QAction(workspace);
  state_action->setObjectName("guiAeState");
  QObject::connect(state_action, &QAction::triggered, workspace, [&] {
    QJsonArray sets;
    if (document)
      for (const auto &set : document->texture_sets())
        sets.append(QJsonObject{{"name", QString::fromStdString(set.name)},
                                {"resolution", set.texture_resolution},
                                {"tiles", int(set.tiles.size())},
                                {"layers", int(set.layers.size())}});
    workspace->setProperty(
        "aeState",
        QJsonObject{{"sets", sets},
                    {"dirty", dirty},
                    {"revision", qint64(document_revision)},
                    {"recovered_revision", qint64(last_recovery_revision)},
                    {"gpu_pending", host.texture_updates_pending()},
                    {"save_pending",
                     bool(active_save) || bool(requested_save) || host.save_project_pending()},
                    {"save_error", workspace->property("projectSaveError").toString()}});
  });
  auto *paint_action = new QAction(workspace);
  paint_action->setObjectName("guiAePaint");
  QObject::connect(paint_action, &QAction::triggered, workspace, [&] {
    workspace->findChild<QToolButton *>("addPaintLayer")->click();
    viewport->set_layout({bts::ViewLayout::UvOnly, false});
    viewport->focus_uv({.5, .5}, 1.2);
    send_drag(*viewport,
              viewport->uv_point({.3, .35}),
              viewport->uv_point({.7, .6}) - viewport->uv_point({.3, .35}),
              Qt::LeftButton,
              Qt::NoModifier);
  });
  auto *save_action = new QAction(workspace);
  save_action->setObjectName("guiAeSave");
  QObject::connect(save_action, &QAction::triggered, workspace, [&] {
    save_document(harness.value("gui-fix-demo") + "/ae.blend");
  });
}
bts::ProjectOpenJob project_opener(host);
bts::NativeHeightUiHooks height_hooks;
auto *height_panel = workspace ? workspace->native_material_panel() : nullptr;
if (height_panel) {
  height_hooks.progress = [&](const QString &text) {
    height_panel->set_height_busy(true);
    workspace->set_status(text);
  };
  height_hooks.error = [&](const QString &text) {
    workspace->setProperty("nativeHeightError", text);
    workspace->set_error_status("Authored Height: " + text);
    workspace->append_console("Authored Height: " + text);
  };
  height_hooks.finished = [&] { height_panel->set_height_busy(false); };
  height_hooks.publish = [&](bts::NativeMaterialHeightGpu *gpu,
                             bts::NativeHeightPolicy policy,
                             const bts::NativeHeightUiTarget &target) {
    if (!target.current())
      throw std::runtime_error("Native Height target changed before publication");
    if (gpu)
      host.install_native_height_gpu(*gpu);
    bool published = false;
    edit_texture_set(
        target.material_uid,
        [&](bts::TextureSet &set) {
          if (!host.set_native_height_policy(set, target.layer, policy))
            return;
          host.validate_native_material_target(target.source.id, set);
          if (policy != bts::NativeHeightPolicy::None)
            host.probe_native_height(set, target.layer);
        },
        [&]() noexcept {
          if (gpu)
            gpu->commit();
          published = true;
        });
    if (!published)
      throw std::runtime_error((last_material_error.isEmpty() ?
                                    QString("Native Height edit did not publish") :
                                    last_material_error)
                                   .toStdString());
    // The document/cache commit has completed. A later UI refresh error is
    // a committed-edit warning, never a reason to roll back only its cache.
    try {
      if (!undo_history.empty())
        undo_history.back().label = policy == bts::NativeHeightPolicy::None ?
                                        "Disable raw authored Height" :
                                        "Enable raw authored Height";
      refresh_history_actions();
      viewport->update();
      workspace->setProperty("nativeHeightError", QString());
      if (!last_material_error.isEmpty())
        workspace->append_console("Authored Height committed; refresh warning: " +
                                  last_material_error);
    }
    catch (const std::exception &error) {
      std::fprintf(stderr, "BTS_NATIVE_HEIGHT_COMMITTED_REFRESH_WARNING %s\n", error.what());
    }
  };
}
bts::NativeMaterialHeightUi height_ui(host, std::move(height_hooks));
bts::LongOperationCancel escape_long_operation(
    [&] {
      if (!workspace)
        return false;
      return project_opener.pending() || pending_new->pending() || height_ui.pending() ||
             (native_image_ui && native_image_ui->pending()) ||
             (render_controller && render_controller->active()) ||
             (texture_loader && texture_loader->pending()) ||
             (native_material_loader && native_material_loader->pending()) ||
             (parameter_import && parameter_import->pending())
#ifdef BTS_WITH_MATERIALX
             || (material_importer && material_importer->pending())
#endif
          ;
    },
    [&] {
      if (project_opener.pending()) {
        project_opener.cancel();
        if (auto *progress = workspace->findChild<QProgressDialog *>("projectOpenProgress")) {
          progress->setLabelText("Stopping project Open…");
          progress->setCancelButton(nullptr);
        }
      }
      if (pending_new->pending())
        cancel_pending_new();
      if (height_ui.pending())
        height_ui.cancel();
      if (native_image_ui && native_image_ui->pending())
        native_image_ui->cancel();
      if (render_controller && render_controller->active())
        render_controller->set_active(false);
      if (texture_loader && texture_loader->pending())
        texture_loader->cancel_all();
      if (native_material_loader && native_material_loader->pending()) {
        ++native_apply_serial;
        native_material_loader->cancel_all();
      }
      if (parameter_import && parameter_import->pending())
        parameter_import->cancel();
#ifdef BTS_WITH_MATERIALX
      if (material_importer && material_importer->pending())
        material_importer->cancel();
#endif
      if (asset_progress && asset_progress->active())
        asset_progress->fail("Cancelled");
      workspace->set_status("Operation cancelled");
    });
if (height_panel) {
  height_panel->height_requested = [&](bts::NativeHeightPolicy policy) {
    try {
      if (opening_project || project_opener.pending() || active_save || requested_save ||
          host.save_project_pending() || (render_controller && render_controller->active()))
        throw std::runtime_error(
            "Finish project Open/Save or Rendering before selecting authored Height");
      if (uniform_before && finish_uniform_edit)
        finish_uniform_edit(true);
      if (!document || selected_material < 0 || uniform_before || stroke_before ||
          path_drag_before || viewport->painting_ || property_dragging ||
          property_idle_timer.isActive())
        throw std::runtime_error(
            "Finish the current property or paint gesture before selecting authored Height");
      const auto uid = snapshot.materials[selected_material].session_id;
      const auto &set = document->set(uid);
      if (set.selected_mask || set.selected_geometry || !set.selected_effect.isNull())
        throw std::runtime_error("Select an editable native Fill for authored Height");
      const auto &layer = bts::TextureDocument::layer(const_cast<bts::TextureSet &>(set),
                                                      set.selected_layer);
      if (layer.kind != bts::LayerKind::Fill || layer.instance || !layer.fill.native_material)
        throw std::runtime_error("Select an editable native Fill for authored Height");
      if (layer.fill.native_material->height_policy == policy)
        return;
      bts::require_native_height_fill(policy, set.bump_origin, layer.fill.mode, layer.channels[3]);
      if (policy != bts::NativeHeightPolicy::None && !set.channels[4])
        throw std::runtime_error(
            "Enable the Texture Set Bump channel before selecting authored Height");
      const auto source_id = QUuid(QString::fromStdString(layer.fill.material_resource));
      const auto found = std::find_if(assets.assets().begin(),
                                      assets.assets().end(),
                                      [&](const auto &asset) { return asset.id == source_id; });
      if (found == assets.assets().end() || !bts::is_native_material_asset(*found) ||
          !host.has_native_material(source_id))
        throw std::runtime_error("Native source is not fully prepared");

      bts::NativeHeightUiTarget target;
      target.document = document->id();
      target.layer = layer.id;
      target.material_uid = uid;
      target.instance = *layer.fill.native_material;
      target.source = *found;
      target.resources = assets.assets();
      const auto project = target.document, id = target.layer, set_id = set.id;
      const auto revision = document_revision;
      const auto authored = layer.fill.native_material;
      const auto source = layer.fill.material_resource;
      const auto hash = found->hash();
      target.current = [&, project, id, set_id, revision, authored, source, source_id, hash, uid] {
        if (!document || document->id() != project || document_revision != revision ||
            selected_material < 0 || snapshot.materials[selected_material].session_id != uid ||
            opening_project || uniform_before || stroke_before || path_drag_before ||
            viewport->painting_ || property_dragging || property_idle_timer.isActive() ||
            active_save || requested_save || (render_controller && render_controller->active()))
          return false;
        try {
          const auto &set = document->set(uid);
          if (set.id != set_id || set.selected_layer != id || set.selected_mask ||
              set.selected_geometry || !set.selected_effect.isNull())
            return false;
          const auto &layer = bts::TextureDocument::layer(const_cast<bts::TextureSet &>(set), id);
          if (layer.kind != bts::LayerKind::Fill || layer.instance ||
              layer.fill.material_resource != source || layer.fill.native_material != authored)
            return false;
          const auto asset = std::find_if(
              assets.assets().begin(), assets.assets().end(), [&](const auto &value) {
                return value.id == source_id;
              });
          return asset != assets.assets().end() && asset->hash() == hash;
        }
        catch (const std::exception &) {
          return false;
        }
      };
      height_ui.request(std::move(target), policy);
    }
    catch (const std::exception &error) {
      workspace->setProperty("nativeHeightError", QString::fromUtf8(error.what()));
      workspace->set_error_status("Authored Height: " + QString::fromUtf8(error.what()));
    }
  };
  height_panel->height_cancelled = [&] {
    height_ui.cancel();
    workspace->set_status("Authored Height request cancelled");
  };
  height_panel->set_height_busy(false);
}
const auto detach_height_request = qScopeGuard(
    [panel = QPointer<bts::NativeMaterialPanel>(height_panel)] {
      if (panel) {
        panel->height_requested = {};
        panel->height_cancelled = {};
      }
    });
viewport->before_native_close =
    [root = QPointer<QObject>(window.get()),
     opener = QPointer<bts::ProjectOpenJob>(&project_opener),
     height = QPointer<bts::NativeMaterialHeightUi>(&height_ui),
     import = QPointer<bts::MaterialLayerParameterImportJob>(parameter_import.get())]() noexcept {
      // Context shutdown may precede event-loop return. Use only live QObject
      // state here; never reach back into main's captured callback locals.
      const auto block = [](auto &&self, QObject *object) noexcept -> void {
        object->blockSignals(true);
        for (QObject *child : object->children())
          self(self, child);
      };
      if (root)
        block(block, root.data());
      if (import)
        import->shutdown();
      if (opener)
        opener->shutdown();
      if (height)
        height->shutdown();
    };
const auto detach_open_close = qScopeGuard([view = QPointer<Viewport>(viewport)] {
  if (view)
    view->before_native_close = {};
});
const auto install_opened = [&](const QString &path,
                                std::unique_ptr<bts::PendingProject> pending,
                                std::unique_ptr<bts::PendingProjectGpu> gpu = {}) {
  // Function argument destruction order is unspecified; explicitly retire
  // staged GPU owners before the pending BFD even on an early exception.
  const auto release_stage = qScopeGuard([&gpu] { gpu.reset(); });
  if (parameter_import)
    parameter_import->cancel();
  pending->validate();
  const auto opened_identity = pending->disk_identity;
  bts::require_project_identity(QFileInfo(path).absoluteFilePath(), opened_identity);
  host.validate_project_adoption(*pending, gpu.get());
  auto pending_assets = assets;
  pending_assets.replace_project(pending->resources);
#ifdef BTS_WITH_MATERIALX
  if (material_importer)
    material_importer->cancel();
#endif

  const auto previous_project = document ? document->id().toString(QUuid::WithoutBraces) :
                                           QString();
  viewport->finish_path_drag(true);
  viewport->end_paint(true);
  native_material_loader->cancel_all();
  ++native_apply_serial;
  if (render_controller)
    render_controller->reset_document();
  auto opened = host.adopt_project(
      std::move(pending), QFileInfo(path).absoluteFilePath().toStdString(), std::move(gpu));
  assets = std::move(pending_assets);
  for (const auto &asset : assets.assets())
    if (asset.scope == bts::AssetScope::Project && bts::is_native_material_asset(asset)) {
      native_material_loader->ensure(
          asset,
          assets.assets(),
          [&](const auto &) { viewport->update(); },
          [&](const QString &error) {
            workspace->set_status("Native material could not be restored: " + error);
            workspace->append_console(error);
          });
    }
  workspace->refresh_alpha_shelf();
  inspected_map = -1;
  viewport->mesh_map_active = -1;
  if (shader)
    shader->reset_document();
  if (skew)
    skew->reset_document();
  skew_profile_active = false;
  viewport->set_empty_project(false);
  viewport->reset_shader_failure();
  document = std::make_unique<bts::TextureDocument>(std::move(opened));
  bts::apply_mesh_bake_template(document->id(), document->bake_configuration());
  viewport->polygon_active = false;
  viewport->polygon_drag = false;
  workspace->set_polygon_fill(false, int(viewport->polygon_mode), viewport->polygon_value);
  viewport->picker_active = false;
  workspace->set_picker_active(false);
  viewport->stroke_anchor.reset();
  viewport->path_active = viewport->path_editing = false;
  viewport->selected_path.clear();
  viewport->selected_vertex = -1;
  viewport->pending_path_point.reset();
  viewport->paths.clear();
  viewport->path_draw.clear();
  quick_enabled = quick_editing = false;
  quick_masks.clear();
  quick_stroke_before.reset();
  quick_brush = bts::BrushSettings{};
  quick_brush.mask_value = 0;
  viewport->quick_mask_gate.reset();
  viewport->quick_mask_edit = false;
  brush = paint_brush = eraser_brush = bts::BrushSettings{};
  smudge_brush = bts::default_smudge_brush();
  clone_brush = bts::BrushSettings{};
  clone_brush.clone = true;
  viewport->clone_source.reset();
  viewport->clone_source_display.reset();
  projection_brush = bts::BrushSettings{};
  projection_brush.projected.emplace();
  projection_brush.values = projection_brush.projected->values;
  symmetry = host.symmetry_settings();
  workspace->set_symmetry(symmetry);
  if (document->settings().uv_workflow == bts::UvWorkflow::PreserveTiles)
    brush.channels = {true, false, false, false, false};
  undo_history.clear();
  redo_history.clear();
  refresh_history_actions();
  snapshot = host.scene_snapshot();
  viewport->set_scene_cameras(snapshot.cameras);
  for (const auto &set : document->texture_sets())
    if (!set.disabled && !set.fully_loaded())
      host.change_texture_residency(set);
  schedule_residency_spill();
  QStringList names, displays;
  for (const auto &m : snapshot.materials)
    names.append(QString::fromStdString(m.name));
  for (const auto &name : host.display_transforms())
    displays.append(QString::fromStdString(name));
  workspace->set_display_transforms(displays, host.color_state()["view_transform"].toString());
  workspace->set_texture_sets(names, names.isEmpty() ? -1 : 0);
  for (int row = 0; row < int(snapshot.materials.size()); ++row) {
    auto *item = workspace->texture_sets()->item(row);
    const auto &set = document->set(snapshot.materials[row].session_id);
    item->setData(Qt::UserRole, set.material_session_uid);
    item->setData(Qt::CheckStateRole, set.loaded ? Qt::Checked : Qt::Unchecked);
    item->setForeground(QColor(set.loaded ? bts::theme::Text : bts::theme::Disabled));
  }
  if (use_workspace && document) {
    const auto &ref_state = bts::document_reference_image(*document);
    const QString ref_path = QString::fromStdString(ref_state.path);
    if (!ref_path.isEmpty() && QFileInfo::exists(ref_path)) {
      load_reference_image_file(ref_path, false);
      viewport->ref_center_ = {ref_state.center[0], ref_state.center[1]};
      viewport->ref_vertical_extent_ = float(std::clamp(ref_state.vertical_extent, 0.01, 10000.0));
    }
    viewport->reference_image_enabled = ref_state.enabled;
    workspace->set_reference_image_enabled(viewport->reference_image_enabled);
    workspace->set_reference_image_path(viewport->reference_image.path);
    if (viewport->reference_image_enabled &&
        viewport->view_layout().mode == bts::ViewLayout::SceneOnly)
    {
      auto layout = viewport->view_layout();
      layout.mode = bts::ViewLayout::Split;
      viewport->set_layout(layout);
      workspace->set_view_layout(layout);
    }
  }
  refresh_texture_set_list();
  select_material(workspace->texture_sets()->currentRow());
  update_inspection(bts::InspectionMode::Material);
  viewport->frame_document();
  viewport->update();
#ifdef BTS_WITH_MATERIALX
  if (schedule_material_import)
    schedule_material_import();
#endif

  bts::FileDialog::remember_folder(QFileInfo(path).absolutePath());
  document_path = QFileInfo(path).absoluteFilePath();
  document_disk_identity = opened_identity;
  dirty = false;
  window->setWindowTitle(QFileInfo(path).completeBaseName() + " — mPainter");
  if (python_event) {
    if (!previous_project.isEmpty())
      python_event("ProjectEditionLeft", {{"project_id", previous_project}});
    python_event("ProjectOpened",
                 {{"project_id", document->id().toString(QUuid::WithoutBraces)},
                  {"file_path", document_path}});
  }
};
// Diagnostics retain synchronous ordering. User Open/Recover use the
// worker boundary below and adopt only after complete validation.
const auto open_document = [&](const QString &path) {
  if (project_opener.pending())
    throw std::logic_error("Another project Open is still finishing");
  install_opened(path,
                 bts::PendingProject::read(QFileInfo(path).absoluteFilePath().toStdString()));
};
const auto begin_open_document = [&](const QString &path, bool recovered = false) {
  if (parameter_import)
    parameter_import->cancel();
  if (project_opener.pending()) {
    workspace->set_status("Another project Open is still finishing");
    return;
  }
  viewport->finish_path_drag(true);
  viewport->end_paint(true);
  if (uniform_before && finish_uniform_edit)
    finish_uniform_edit(true);
  const auto revision = document_revision;
  const auto id = document ? document->id() : QUuid{};
  auto *progress = new QProgressDialog("Reading native project…", "Cancel", 0, 0, workspace);
  progress->setObjectName("projectOpenProgress");
  progress->setWindowTitle(recovered ? "Recover project" : "Open project");
  progress->setWindowModality(Qt::NonModal);
  progress->setMinimumDuration(0);
  progress->show();
  QObject::connect(progress, &QProgressDialog::canceled, workspace, [&] {
    project_opener.cancel();
    workspace->set_status("Project Open cancelled");
  });
  opening_project = true;
  workspace->setProperty("projectOpenError", QString());
  try {
    project_opener.start(
        [path](auto cancel, auto report) {
          return bts::PendingProject::prepare(
              QFileInfo(path).absoluteFilePath().toStdString(), cancel, report);
        },
        [&, revision, id] {
          return document_revision == revision && (document ? document->id() : QUuid{}) == id;
        },
        [&, path, recovered, progress](bts::ProjectOpenResult result) {
          install_opened(path, std::move(result.pending), std::move(result.gpu));
          ++document_revision;
          if (recovered) {
            document_path.clear();
            dirty = true;
            last_document_edit = bts::performance::Clock::now();
            window->setWindowTitle("Recovered project * — mPainter");
            workspace->append_console(
                "Recovered snapshot opened. Save As preserves the original project.");
          }
        },
        [&](const QString &error) {
          workspace->setProperty("projectOpenError", error);
          workspace->append_console("Open failed: " + error);
          workspace->set_status("Open failed: " + error);
        },
        [progress](const QString &text) { progress->setLabelText(text); },
        [&, progress] {
          opening_project = false;
          if (material_layering_ui)
            material_layering_ui->synchronize();
          progress->close();
          progress->deleteLater();
        },
        [&] { return !active_save && !requested_save && !host.save_project_pending(); });
  }
  catch (const std::exception &error) {
    opening_project = false;
    progress->close();
    progress->deleteLater();
    workspace->setProperty("projectOpenError", QString::fromUtf8(error.what()));
    workspace->set_status("Open failed: " + QString::fromUtf8(error.what()));
  }
};
if (qEnvironmentVariableIsSet("BTS_AE_PERSISTENCE_DEMO")) {
  auto *open_action = new QAction(workspace);
  open_action->setObjectName("guiAeOpen");
  QObject::connect(open_action, &QAction::triggered, workspace, [&] {
    open_document(
        qEnvironmentVariable("BTS_AE_PROJECT", harness.value("gui-fix-demo") + "/ae.blend"));
  });
}
const auto recover_project = [&](const QString &path) {
  open_document(path);
  document_path.clear();
  dirty = true;
  (++document_revision, last_document_edit = bts::performance::Clock::now());
  window->setWindowTitle("Recovered project * — mPainter");
  workspace->append_console("Recovered snapshot opened. Save As preserves the original project.");
};
bool interval_valid = false;
const int autosave_seconds = parser.value("autosave-seconds").toInt(&interval_valid);
if (!interval_valid || autosave_seconds < 1 || autosave_seconds > 3600)
  throw std::invalid_argument("Autosave interval must be 1–3600 seconds");
QTimer recovery_timer;
recovery_timer.setInterval(autosave_seconds * 1000);
QObject::connect(&recovery_timer, &QTimer::timeout, window.get(), [&] {
  recovery_timer.setInterval(autosave_seconds * 1000);
  if (!document || !dirty ||
      (last_recovery_document == document->id() && last_recovery_revision == document_revision))
    return;
  const bool busy = project_opener.pending() || bool(active_save) || bool(requested_save) ||
                    bts::performance::ms(last_document_edit) < 2000 || uniform_before ||
                    stroke_before || path_drag_before || viewport->painting_ ||
                    property_dragging || property_idle_timer.isActive() ||
                    host.texture_updates_pending() ||
                    ((texture_loader && texture_loader->pending())
#ifdef BTS_WITH_MATERIALX
                     || (material_importer && material_importer->pending())
#endif
                         ) ||
                    QApplication::activeModalWidget();
  if (busy) {
    recovery_timer.setInterval(std::min(autosave_seconds * 1000, 1000));
    return;
  }
  try {
    checkpoint(document_path);
  }
  catch (const std::exception &error) {
    workspace->append_console("Autosave failed: " + QString::fromUtf8(error.what()));
  }
});
recovery_timer.start();
const auto choose_save = [&](bool save_as) {
  QString path = document_path;
  if (save_as || path.isEmpty())
    path = bts::FileDialog::getSaveFileName(workspace,
                                            "Save project",
                                            path.isEmpty() ? "Untitled.blend" : path,
                                            "Blender projects (*.blend)");
  if (path.isEmpty())
    return false;
  try {
    return save_document(path);
  }
  catch (const std::exception &error) {
    QMessageBox::critical(workspace, "Save failed", QString::fromUtf8(error.what()));
    return false;
  }
};
const auto discard_or_save = [&] {
  if (uniform_before && finish_uniform_edit)
    finish_uniform_edit(false);
  viewport->end_paint(false);
  if (!dirty)
    return true;
  const auto answer = QMessageBox::warning(workspace,
                                           "Unsaved project",
                                           "Save changes to this project?",
                                           QMessageBox::Save | QMessageBox::Discard |
                                               QMessageBox::Cancel,
                                           QMessageBox::Save);
  return answer == QMessageBox::Discard || (answer == QMessageBox::Save && choose_save(false));
};
