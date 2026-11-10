// SPDX-License-Identifier: GPL-3.0-or-later
// Harness: the stage-by-stage acceptance sequence driven by the sets_demo timer. Part of
// run_workspace_session() (app/WorkspaceSession.inc), included in order inside its body; it shares
// that function's locals and is not a standalone file.
QObject::connect(&sets_demo, &QTimer::timeout, window.get(), [&] {
  if (workspace->property("generatorInputLoading").toBool() ||
      ((texture_loader && texture_loader->pending())
#ifdef BTS_WITH_MATERIALX
       || (material_importer && material_importer->pending())
#endif
           ) ||
      host.texture_updates_pending() || host.save_project_pending() ||
      workspace->property("smartMaskSaving").toBool() ||
      workspace->property("smartMaterialSaving").toBool() ||
      workspace->property("resourceImporting").toBool() ||
      workspace->property("resourceExporting").toBool() ||
      (smart_material_demo && qEnvironmentVariableIsSet("BTS_SMART_MATERIAL_USER") &&
       QApplication::activeModalWidget()))
    return;
  try {
    if (viewport->frame_count < sets_after_frame)
      return;
    const QString directory = harness.value("new-demo-dir");
    const QString stage = sets_stages[sets_stage];
    if (!lazy_demo &&
        (paint_demo || flow_demo || opacity_demo || spacing_demo || pressure_demo ||
         surface_demo || udim_demo || selection_demo || mask_demo) &&
        viewport->cursor_visible_)
    {
      viewport->cursor_visible_ = false;
      viewport->update();
      sets_after_frame = viewport->frame_count + 1;
      return;
    }
    const auto rendered = viewport->grabFramebuffer();
    const auto presented = window->screen()->grabWindow(window->winId());
    rendered.save(directory + "/" + stage + "-viewport.png");
    presented.save(directory + "/" + stage + "-window.png");
    const QPoint origin = viewport->mapTo(window.get(), QPoint{});
    const double scale = double(presented.width()) / window->width();
    sets_captures.append(QJsonObject{{"stage", stage},
                                     {"native", native_document_state(host)},
                                     {"document", document->to_json()},
                                     {"workspace", workspace_state(*workspace)},
                                     {"color", host.color_state()},
                                     {"inspection", int(host.inspection_mode())},
                                     {"symmetry", bts::symmetry_json(symmetry)},
                                     {"symmetry_copies", viewport->last_symmetry_copies},
                                     {"environment", host.environment_state()},
                                     {"view", viewport->view_state()},
                                     {"viewport_rect",
                                      QJsonArray{qRound(origin.x() * scale),
                                                 qRound(origin.y() * scale),
                                                 qRound(viewport->width() * scale),
                                                 qRound(viewport->height() * scale)}}});
    std::printf("BTS_TEXTURE_SET_DEMO pid=%lld stage=%s\n",
                static_cast<long long>(app.applicationPid()),
                stage.toUtf8().constData());
    std::fflush(stdout);
    if (smart_material_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      int folders = 0;
      QJsonArray mask_hashes;
      for (const auto &layer : set.layers) {
        folders += layer.kind == bts::LayerKind::Folder && layer.name == "Worn Painted Metal";
        if (layer.mask)
          mask_hashes.append(
              QJsonObject{{"name", QString::fromStdString(layer.name)},
                          {"hash", QString::number(layer.mask->pixels.hash(2), 16)}});
      }
      auto capture = sets_captures.last().toObject();
      capture["smart_material"] = QJsonObject{{"ready", bts::worn_metal_ready(set)},
                                              {"folders", folders},
                                              {"masks", mask_hashes},
                                              {"mesh_map_mode", viewport->mesh_map_active},
                                              {"inspection", int(host.inspection_mode())}};
      if (qEnvironmentVariableIsSet("BTS_SMART_MATERIAL_GPU_CHECK") && QStringList{"applied",
                                                                                   "edited",
                                                                                   "reopened",
                                                                                   "dragged",
                                                                                   "user-created",
                                                                                   "user-applied",
                                                                                   "user-reopened"}
                                                                           .contains(stage))
        capture["smart_material_gpu_error"] = host.verify_gpu_texture_stack(
            set.material_session_uid);
      if (qEnvironmentVariableIsSet("BTS_SMART_MATERIAL_USER")) {
        QJsonArray presets;
        for (const auto &asset : assets.assets())
          if (asset.material_preset) {
            int paint_pages = 0;
            QJsonArray sources;
            for (const auto &layer : asset.material_preset->set.layers) {
              paint_pages += int(layer.pixels.page_count(0));
              for (int channel = 0; channel < 15; ++channel)
                if (const auto &texture = bts::fill_channel_texture(layer.fill, channel))
                  sources.append(
                      QString::number(reinterpret_cast<quintptr>(texture->pixels.get()), 16));
            }
            presets.append(QJsonObject{{"name", asset.name},
                                       {"id", asset.id.toString()},
                                       {"scope", bts::asset_scope_name(asset.scope)},
                                       {"hash", asset.hash()},
                                       {"paint_pages", paint_pages},
                                       {"texture_storage", sources}});
          }
        capture["user_materials"] = presets;
        capture["user_error"] = last_material_error;
        capture["user_status"] = workspace->statusBar()->currentMessage();
        if (auto *label = workspace->findChild<QLabel *>("workspaceErrorText");
            label && label->isVisible())
          capture["user_status"] = label->text();
        QJsonArray samples;
        for (int channel : {0, 2})
          for (auto uv : {QPointF(.2, .3), QPointF(.375, .5), QPointF(.75, .7)}) {
            if (const auto pixel = host.diagnostic_gpu_pixel(set.material_session_uid,
                                                             channel,
                                                             int(uv.x() * set.width()),
                                                             int(uv.y() * set.rows())))
            {
              QJsonArray rgba;
              for (float value : *pixel)
                rgba.append(value);
              samples.append(rgba);
            }
          }
        capture["user_gpu_samples"] = samples;
      }
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (editable_path_demo && sets_stage > 0) {
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      const auto *effect = bts::TextureDocument::paint_effect(set);
      const auto &pixels = effect ? effect->pixels : layer.pixels;
      const auto &history = path_history(set);
      QJsonArray hashes, old;
      for (int c = 0; c < 5; ++c)
        hashes.append(QString::number(pixels.hash(c), 16));
      for (float value : layer.pixels.sample(0, 38, 218))
        old.append(value);
      QJsonObject definition;
      int count = 0;
      if (history)
        for (const auto &op : history->operations)
          if (op->path) {
            if (!count)
              definition = bts::surface_path_json(*op->path);
            ++count;
          }
      auto capture = sets_captures.last().toObject();
      capture["curve"] = QJsonObject{
          {"target", QString::fromStdString(set.name)},
          {"definition", definition},
          {"path_count", count},
          {"selected_vertex", viewport->selected_vertex},
          {"active", viewport->path_active},
          {"drag_active", bool(path_drag_before)},
          {"controls_visible", viewport->path_curve_panel->isVisible()},
          {"pressure_control",
           viewport->path_curve_panel->findChild<QDoubleSpinBox *>("surfaceVertexPressure")
               ->value()},
          {"hashes", hashes},
          {"old_mark", old},
          {"native_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"schema", document->to_json()["schema"]},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"undo_label", undo_history.empty() ? QString{} : undo_history.back().label},
          {"display",
           QJsonObject{{"handle", viewport->path_display.handle},
                       {"width", viewport->path_display.width},
                       {"normals", viewport->path_display.normals},
                       {"tangents", viewport->path_display.tangents},
                       {"direction", viewport->path_display.direction}}}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (surface_path_demo && sets_stage > 0) {
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      const auto *effect = bts::TextureDocument::paint_effect(set);
      const auto &pixels = effect            ? effect->pixels :
                           set.selected_mask ? layer.mask->pixels :
                                               layer.pixels;
      const auto &history = path_history(set);
      QJsonArray paths, hashes, old;
      for (int c = 0; c < 5; ++c)
        hashes.append(QString::number(pixels.hash(c), 16));
      for (float value : layer.pixels.sample(0, 38, 218))
        old.append(value);
      if (history)
        for (const auto &op : history->operations)
          if (op->path) {
            const auto &path = *op->path;
            double normal_dot = 0;
            for (int c = 0; c < 3; ++c)
              normal_dot += path.points.front().normal[c] * path.points.back().normal[c];
            paths.append(QJsonObject{{"id", QString::fromStdString(path.id)},
                                     {"name", QString::fromStdString(path.name)},
                                     {"visible", path.visible},
                                     {"points", int(path.points.size())},
                                     {"normal_dot", normal_dot},
                                     {"size", path.brush.diameter},
                                     {"opacity", path.brush.stroke_opacity}});
          }
      auto capture = sets_captures.last().toObject();
      capture["surface_path"] = QJsonObject{
          {"target", QString::fromStdString(set.name)},
          {"effect", effect != nullptr},
          {"active", viewport->path_active},
          {"editing", viewport->path_editing},
          {"pending", bool(viewport->pending_path_point)},
          {"selected", QString::fromStdString(viewport->selected_path)},
          {"panel_visible", viewport->paths_panel->isVisible()},
          {"paths", paths},
          {"hashes", hashes},
          {"base_layer_hash", QString::number(layer.pixels.hash(0), 16)},
          {"old_mark", old},
          {"native_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"history_loaded", !history || history->loaded},
          {"operations", history ? int(history->operations.size()) : 0},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"undo_label", undo_history.empty() ? QString{} : undo_history.back().label}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (dynamic_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray hashes, old;
      for (int c = 0; c < 5; ++c)
        hashes.append(QString::number(layer.pixels.hash(c), 16));
      for (float value : layer.pixels.sample(0, 38, 218))
        old.append(value);
      const auto &parameters = brush.alpha.dynamic;
      auto capture = sets_captures.last().toObject();
      capture["dynamic"] = QJsonObject{
          {"resource", QString::fromStdString(brush.alpha.name())},
          {"cycle", parameters.cycle},
          {"random", parameters.random_start},
          {"roundness", parameters.roundness},
          {"width", parameters.width},
          {"expanded", workspace->findChild<QToolButton *>("dynamicStrokeSection")->isChecked()},
          {"dabs", viewport->dynamic_dabs},
          {"hashes", hashes},
          {"old_mark", old},
          {"native_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"undo_label", undo_history.empty() ? QString{} : undo_history.back().label}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (quick_mask_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray hashes, gap, left, right;
      for (int c = 0; c < 5; ++c)
        hashes.append(QString::number(layer.pixels.hash(c), 16));
      for (float v : layer.pixels.sample(0, 140, 128))
        gap.append(v);
      for (float v : layer.pixels.sample(0, 110, 128))
        left.append(v);
      for (float v : layer.pixels.sample(0, 170, 128))
        right.append(v);
      const auto mask = quick_masks.find(set.material_session_uid);
      const float allowed = mask == quick_masks.end() ? 1 : mask->second.sample_uv(.55, .5);
      auto capture = sets_captures.last().toObject();
      capture["quick_mask"] = QJsonObject{
          {"enabled", quick_enabled},
          {"editing", quick_editing},
          {"center_allowed", allowed},
          {"mask_count", int(quick_masks.size())},
          {"size", brush.diameter},
          {"flow", brush.flow},
          {"spacing", brush.spacing},
          {"alpha", QString::fromStdString(brush.alpha.name())},
          {"gray", brush.mask_value},
          {"hashes", hashes},
          {"gap", gap},
          {"left", left},
          {"right", right},
          {"native_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"dirty", dirty},
          {"title",
           workspace->findChild<QWidget *>("materialProperties")
               ->findChild<QLabel *>("sectionTitle")
               ->text()},
          {"gray_visible", workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->isVisible()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (clone_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray hashes, source, neighbor, blue, copy1, copy2, channels;
      for (int c = 0; c < 5; ++c) {
        hashes.append(QString::number(layer.pixels.hash(c), 16));
        channels.append(brush.channels[c]);
      }
      for (float v : layer.pixels.sample(0, 77, 141))
        source.append(v);
      for (float v : layer.pixels.sample(0, 64, 218))
        neighbor.append(v);
      for (float v : layer.pixels.sample(0, 179, 51))
        blue.append(v);
      for (float v : layer.pixels.sample(0, 115, 72))
        copy1.append(v);
      for (float v : layer.pixels.sample(0, 205, 192))
        copy2.append(v);
      auto capture = sets_captures.last().toObject();
      capture["clone"] = QJsonObject{
          {"active", brush.clone},
          {"absolute", brush.clone_absolute},
          {"source_selected", bool(viewport->clone_source)},
          {"size", brush.diameter},
          {"flow", brush.flow},
          {"spacing", brush.spacing},
          {"opacity", brush.stroke_opacity},
          {"channels", channels},
          {"hashes", hashes},
          {"source", source},
          {"neighbor", neighbor},
          {"blue", blue},
          {"copy1", copy1},
          {"copy2", copy2},
          {"native_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"rough_hash", QString::number(host.texture_hash(set.material_session_uid, 2), 16)},
          {"operations", layer.strokes ? int(layer.strokes->operations.size()) : 0},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"undo_label", undo_history.empty() ? QString{} : undo_history.back().label},
          {"rough_values_visible", workspace->roughness_control()->isVisible()},
          {"title",
           workspace->findChild<QWidget *>("materialProperties")
               ->findChild<QLabel *>("sectionTitle")
               ->text()},
          {"mode_label", workspace->findChild<QToolButton *>("cloneTool")->accessibleName()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (smudge_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray hashes, neighbor, blue, channels;
      for (int c = 0; c < 5; ++c) {
        hashes.append(QString::number(layer.pixels.hash(c), 16));
        channels.append(brush.channels[c]);
      }
      for (float v : layer.pixels.sample(0, 64, 218))
        neighbor.append(v);
      for (float v : layer.pixels.sample(0, 179, 51))
        blue.append(v);
      double tail = 0, white_error = 0, rough_error = 0;
      for (int y = 136; y <= 145; ++y)
        for (int x = 160; x <= 215; ++x) {
          const auto pixel = layer.pixels.sample(0, x, y);
          tail = std::max(tail, double(pixel[3]));
          if (pixel[3] > .01) {
            for (int c = 0; c < 3; ++c)
              white_error = std::max(white_error, std::abs(double(pixel[c] / pixel[3] - 1)));
            const auto rough = layer.pixels.sample(2, x, y);
            if (rough[3] > .01)
              rough_error = std::max(rough_error, std::abs(double(rough[0] / rough[3] - .8)));
          }
        }
      int spatial = 0;
      if (layer.strokes)
        for (const auto &operation : layer.strokes->operations)
          spatial += operation->spatial;
      auto capture = sets_captures.last().toObject();
      capture["smudge"] = QJsonObject{
          {"active", brush.smudge},
          {"size", brush.diameter},
          {"flow", brush.flow},
          {"spacing", brush.spacing},
          {"opacity", brush.stroke_opacity},
          {"alpha", QString::fromStdString(brush.alpha.name())},
          {"hardness", brush.hardness},
          {"pressure_size", brush.pressure_size},
          {"minimum_size", brush.minimum_size},
          {"angle", brush.angle},
          {"follow", brush.follow_path},
          {"channels", channels},
          {"hashes", hashes},
          {"native_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"rough_hash", QString::number(host.texture_hash(set.material_session_uid, 2), 16)},
          {"tail", tail},
          {"white_error", white_error},
          {"rough_error", rough_error},
          {"neighbor", neighbor},
          {"blue", blue},
          {"operations", layer.strokes ? int(layer.strokes->operations.size()) : 0},
          {"spatial", spatial},
          {"loaded", !layer.strokes || layer.strokes->loaded},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"undo_label", undo_history.empty() ? QString{} : undo_history.back().label},
          {"rough_values_visible", workspace->roughness_control()->isVisible()},
          {"title",
           workspace->findChild<QWidget *>("materialProperties")
               ->findChild<QLabel *>("sectionTitle")
               ->text()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (material_projection_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray channels, old;
      for (bool enabled : brush.channels)
        channels.append(enabled);
      for (float value : layer.pixels.sample(0, 64, 205))
        old.append(value);
      double source_error = 0, native_error = 0;
      int samples = 0;
      if (sets_stage == 4 && brush.projected) {
        auto source = *brush.projected;
        source.values = brush.values;
        const auto pane = viewport->pane_rect(bts::ViewKind::Scene);
        for (int y = 104; y <= 152; y += 8)
          for (int x = 64; x <= 192; x += 8) {
            const auto raw = layer.pixels.sample(0, x, y);
            if (raw[3] < .05)
              continue;
            const auto screen = viewport->scene_point(
                {float(2 * (x + .5) / 256 - 1), 0, float(2 * (y + .5) / 256 - 1)});
            const auto expected = source.sample(
                {2 * (screen.x() - pane.center().x()) / pane.height(),
                 -2 * (screen.y() - pane.center().y()) / pane.height()});
            if (!expected)
              continue;
            for (int c = 0; c < 3; ++c)
              source_error = std::max(source_error,
                                      std::abs(double(raw[c] / raw[3] - expected->base_color[c])));
            const auto rough = layer.pixels.sample(2, x, y);
            source_error = std::max(source_error,
                                    std::abs(double(rough[0] / rough[3] - expected->roughness)));
            const auto evaluation = bts::TextureDocument::evaluate(set, x, y);
            const auto encoded = bts::precision_channel(evaluation.material, 0, set.precision[0]);
            const auto actual = host.sample_texture(set.material_session_uid, 0, x, y);
            for (int c = 0; c < 3; ++c)
              native_error = std::max(native_error, std::abs(double(actual[c] - encoded[c])));
            ++samples;
          }
      }
      auto capture = sets_captures.last().toObject();
      capture["projection"] = QJsonObject{
          {"active", bool(brush.projected)},
          {"source", brush.projected ? QString::fromStdString(brush.projected->name) : QString{}},
          {"hardness", brush.hardness},
          {"size", brush.diameter},
          {"spacing", brush.spacing},
          {"alpha", QString::fromStdString(brush.alpha.name())},
          {"pressure_size", brush.pressure_size},
          {"minimum_size", brush.minimum_size},
          {"flow", brush.flow},
          {"opacity", brush.stroke_opacity},
          {"follow", brush.follow_path},
          {"angle", brush.angle},
          {"channels", channels},
          {"roughness", brush.values.roughness},
          {"tiling", brush.projected ? brush.projected->transform.tiling : -1},
          {"offset",
           brush.projected ? QJsonArray{brush.projected->transform.offset[0],
                                        brush.projected->transform.offset[1]} :
                             QJsonArray{}},
          {"scale", brush.projected ? brush.projected->transform.scale : 0},
          {"rotation", brush.projected ? brush.projected->transform.angle : 0},
          {"hash", QString::number(layer.pixels.hash(0), 16)},
          {"native_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"rough_hash", QString::number(host.texture_hash(set.material_session_uid, 2), 16)},
          {"old_sample", old},
          {"source_error", source_error},
          {"native_error", native_error},
          {"samples", samples},
          {"title",
           workspace->findChild<QWidget *>("materialProperties")
               ->findChild<QLabel *>("sectionTitle")
               ->text()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (follow_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const auto image = static_cast<bts::BrushPreview *>(
                             workspace->findChild<QWidget *>("brushStrokePreview"))
                             ->image();
      const auto digest = QCryptographicHash::hash(
                              QByteArray::fromRawData(
                                  reinterpret_cast<const char *>(image.constBits()),
                                  image.sizeInBytes()),
                              QCryptographicHash::Sha256)
                              .toHex();
      auto capture = sets_captures.last().toObject();
      capture["direction"] = QJsonObject{
          {"follow", brush.follow_path},
          {"angle", brush.angle},
          {"hash", QString::number(layer.pixels.hash(0), 16)},
          {"native_hash",
           layer.pixels.empty() ?
               QString{} :
               QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"dabs", viewport->jitter_dabs},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"operations", layer.strokes ? int(layer.strokes->operations.size()) : 0},
          {"preview", QString::fromLatin1(digest)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (stencil_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const auto &stencil = brush.stencil;
      QJsonArray choices;
      auto *tiling = workspace->findChild<QComboBox *>("stencilTiling");
      for (int i = 0; i < tiling->count(); ++i)
        choices.append(tiling->itemText(i));
      double error = 0;
      int white = 0, black = 0;
      if (sets_stage == 4) {
        const auto pane = viewport->pane_rect(bts::ViewKind::Scene);
        for (int y = 80; y <= 176; y += 8)
          for (int x = 80; x <= 176; x += 8) {
            const auto screen = viewport->scene_point(
                {float(2 * (x + .5) / 256 - 1), 0, float(2 * (y + .5) / 256 - 1)});
            const auto sample = stencil.sample(
                {2 * (screen.x() - pane.center().x()) / pane.height(),
                 -2 * (screen.y() - pane.center().y()) / pane.height()});
            error = std::max(error, std::abs(double(layer.pixels.sample(0, x, y)[3] - sample)));
            white += sample > .99;
            black += sample < .01;
          }
      }
      auto capture = sets_captures.last().toObject();
      capture["stencil"] = QJsonObject{
          {"resource",
           stencil.resource ? QString::fromStdString(stencil.resource->name()) : QString{}},
          {"tiling", stencil.tiling},
          {"choices", choices},
          {"offset", QJsonArray{stencil.offset[0], stencil.offset[1]}},
          {"scale", stencil.scale},
          {"angle", stencil.angle},
          {"hash", QString::number(layer.pixels.hash(0), 16)},
          {"native_hash",
           layer.pixels.empty() ?
               QString{} :
               QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"reference_error", error},
          {"white", white},
          {"black", black},
          {"ignore", viewport->stencil_ignore},
          {"operations", layer.strokes ? int(layer.strokes->operations.size()) : 0}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (jitter_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray settings;
      for (auto v : brush.jitter.values())
        settings.append(v);
      const auto image = static_cast<bts::BrushPreview *>(
                             workspace->findChild<QWidget *>("brushStrokePreview"))
                             ->image();
      const auto digest = QCryptographicHash::hash(
                              QByteArray::fromRawData(
                                  reinterpret_cast<const char *>(image.constBits()),
                                  image.sizeInBytes()),
                              QCryptographicHash::Sha256)
                              .toHex();
      auto capture = sets_captures.last().toObject();
      capture["jitter"] = QJsonObject{
          {"settings", settings},
          {"dabs", viewport->jitter_dabs},
          {"hash", QString::number(layer.pixels.hash(0), 16)},
          {"operations", layer.strokes ? int(layer.strokes->operations.size()) : 0},
          {"preview", QString::fromLatin1(digest)},
          {"native_hash",
           layer.pixels.empty() ?
               QString{} :
               QString::number(host.texture_hash(set.material_session_uid, 0), 16)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (instance_demo && sets_stage > 0) {
      QJsonArray hashes, paint_hashes, errors;
      double native_error = 0;
      for (int index = 0; index < 2; ++index) {
        const auto &set = document->set(snapshot.materials[index].session_id);
        hashes.append(index == 0 || sets_stage >= 2 || paint_instance_demo || cycle_demo ?
                          QString::number(host.texture_hash(set.material_session_uid, 0), 16) :
                          QString{});
        QJsonArray own;
        for (const auto &layer : set.layers) {
          own.append(QString::number(layer.pixels.hash(0), 16));
          if (set.instance_problems.contains(layer.id))
            errors.append(set.instance_problems.at(layer.id));
        }
        paint_hashes.append(own);
        if (index == 0 || sets_stage >= 2 || paint_instance_demo || cycle_demo) {
          const auto expected = bts::TextureDocument::evaluate(set, 64, 64).material.base_color;
          const auto actual = host.sample_texture(set.material_session_uid, 0, 64, 64);
          for (int c = 0; c < 3; ++c)
            native_error = std::max(native_error, std::abs(double(actual[c] - expected[c])));
        }
      }
      QJsonObject paint_info;
      if (paint_instance_demo && !instance_source.isNull()) {
        const auto &source_set = document->set(snapshot.materials[0].session_id);
        const auto &paint = bts::TextureDocument::layer(
            document->set(source_set.material_session_uid), instance_source);
        bool spatial = true;
        if (paint.strokes)
          for (const auto &operation : paint.strokes->operations)
            spatial = spatial && operation->spatial;
        bool target_empty = true;
        const auto &target_set = document->set(snapshot.materials[1].session_id);
        if (!instance_created.isNull())
          for (const auto &evaluated : bts::evaluation_layers(target_set))
            if (evaluated.id == instance_created)
              target_empty = evaluated.pixels.empty();
        paint_info = QJsonObject{
            {"source_hash", QString::number(paint.pixels.hash(0), 16)},
            {"operations", paint.strokes ? int(paint.strokes->operations.size()) : 0},
            {"all_spatial", spatial},
            {"target_evaluated_empty", target_empty}};
      }
      QJsonObject cycle_info;
      if (cycle_demo) {
        const auto &source_set = document->set(snapshot.materials[0].session_id);
        bool nested = false;
        int depth = 0;
        for (const auto &layer : source_set.layers)
          if (layer.id == cycle_nested) {
            nested = layer.parent == cycle_folder;
            depth = layer.depth;
          }
        cycle_info = QJsonObject{
            {"empty", cycle_empty.toString()},
            {"root", cycle_root.toString()},
            {"folder", cycle_folder.toString()},
            {"child", cycle_child.toString()},
            {"nested", cycle_nested.toString()},
            {"nested_parent", nested},
            {"nested_depth", depth},
            {"problem_count", int(source_set.instance_problems.size())},
            {"folder_problem", source_set.instance_problems.contains(cycle_folder)},
            {"nested_problem", source_set.instance_problems.contains(cycle_nested)},
            {"warning_visible",
             workspace->findChild<QLabel *>("layerInstanceWarning")->isVisible()},
            {"warning", workspace->findChild<QLabel *>("layerInstanceWarning")->text()},
            {"modal", bool(QApplication::activeModalWidget())}};
      }
      auto *tree = workspace->findChild<QTreeWidget *>("layerInstanceHierarchy");
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      auto capture = sets_captures.last().toObject();
      capture["instance"] = QJsonObject{
          {"cycle", cycle_info},
          {"paint_boundary", paint_info},
          {"source", instance_source.toString()},
          {"created", instance_created.toString()},
          {"selected", set.selected_layer.toString()},
          {"set", selected_material},
          {"clipboard", bool(instance_clipboard)},
          {"hashes", hashes},
          {"paint_hashes", paint_hashes},
          {"errors", errors},
          {"native_error", native_error},
          {"hierarchy", workspace->findChild<QWidget *>("layerInstanceProperties")->isVisible()},
          {"hierarchy_roots", tree->topLevelItemCount()},
          {"hierarchy_children",
           tree->topLevelItemCount() ? tree->topLevelItem(0)->childCount() : 0},
          {"empty", workspace->findChild<QLabel *>("instanceNoProperties")->isVisible()},
          {"fill_properties",
           workspace->findChild<QComboBox *>("fillBaseColorResource")->isVisible()},
          {"paint_enabled", viewport->paint_enabled},
          {"effects_enabled", workspace->findChild<QToolButton *>("addLayerEffect")->isEnabled()},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (geometry_demo && !instance_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray left, right;
      for (float v : host.sample_texture(
               set.material_session_uid, 0, tile_mask_demo ? 64 : 32, hide_demo ? 38 : 64))
        left.append(v);
      for (float v : host.sample_texture(set.material_session_uid,
                                         0,
                                         (tile_mask_demo || hide_demo) ? 64 : 96,
                                         hide_demo ? 38 : 64,
                                         (tile_mask_demo || hide_demo) ? 1002 : 1001))
        right.append(v);
      QJsonObject names;
      for (const auto &[uid, name] : set.geometry_names)
        names[QString::fromStdString(name)] = int(uid);
      auto capture = sets_captures.last().toObject();
      capture["geometry_mask"] = QJsonObject{
          {"visibility_images", int(host.geometry_visibility_image_count())},
          {"scene_probe_right",
           QJsonArray{viewport->scene_point({.8f, 0, -.25f}).x(),
                      viewport->scene_point({.8f, 0, -.25f}).y()}},
          {"uv_probe_right",
           QJsonArray{viewport->uv_point({1.5, .3}).x(), viewport->uv_point({1.5, .3}).y()}},
          {"hide_toggle", hide_excluded_geometry},
          {"native_hidden", host.geometry_visibility_enabled(set.material_session_uid)},
          {"left_paint_hash",
           QString::number(
               bts::TextureDocument::layer(document->set(set.material_session_uid), geometry_upper)
                   .pixels.tile(1001)
                   .hash(0),
               16)},
          {"right_paint_hash",
           QString::number(
               bts::TextureDocument::layer(document->set(set.material_session_uid), geometry_upper)
                   .pixels.tile(1002)
                   .hash(0),
               16)},
          {"scene_probe",
           QJsonArray{viewport->scene_point({-.8f, 0, -.25f}).x(),
                      viewport->scene_point({-.8f, 0, -.25f}).y()}},
          {"uv_probe",
           QJsonArray{viewport->uv_point({.25, .3}).x(), viewport->uv_point({.25, .3}).y()}},
          {"hatching", viewport->geometry_tile_editing},
          {"excluded", int(viewport->geometry_excluded_tiles.size())},
          {"hatch_shapes", int(viewport->geometry_hatch_shapes.size())},
          {"paint_enabled", viewport->paint_enabled},
          {"paint_hash",
           QString::number(
               bts::TextureDocument::layer(document->set(set.material_session_uid), geometry_upper)
                   .pixels.hash(0),
               16)},
          {"left", left},
          {"right", right},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"names", names},
          {"selected", set.selected_geometry},
          {"properties", workspace->findChild<QWidget *>("geometryMaskProperties")->isVisible()},
          {"mask_selected", set.selected_mask},
          {"count", workspace->findChild<QLabel *>("geometryMaskCount")->text()},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (anchor_navigation_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &selected = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                         set.selected_layer);
      const bool valid = set.selected_mask && selected.mask && !selected.mask->effects.empty();
      QJsonArray sphere;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      auto capture = sets_captures.last().toObject();
      capture["anchor_navigation"] = QJsonObject{
          {"source", navigation_source.toString()},
          {"anchor", navigation_point.toString()},
          {"first", navigation_first.toString()},
          {"first_fill", navigation_first_fill.toString()},
          {"second", navigation_second.toString()},
          {"second_fill", navigation_second_fill.toString()},
          {"selected_parent", set.selected_layer.toString()},
          {"selected_effect", set.selected_effect.toString()},
          {"sphere", sphere},
          {"native_valid", valid},
          {"value",
           valid ? QJsonValue(host.sample_texture(set.material_session_uid, 15, 102, 102)[0]) :
                   QJsonValue()},
          {"other",
           valid ? QJsonValue(host.sample_texture(set.material_session_uid, 15, 121, 102)[0]) :
                   QJsonValue()},
          {"hash",
           valid ? QString::number(host.texture_hash(set.material_session_uid, 15), 16) :
                   QString{}},
          {"jump_visible", workspace->findChild<QPushButton *>("goToMaskAnchor")->isVisible()},
          {"anchor_properties", workspace->findChild<QWidget *>("anchorProperties")->isVisible()},
          {"references", workspace->findChild<QListWidget *>("anchorReferences")->count()},
          {"references_visible",
           workspace->findChild<QListWidget *>("anchorReferences")->isVisible()},
          {"empty_visible", workspace->findChild<QLabel *>("anchorNoReferences")->isVisible()},
          {"expanded",
           workspace->findChild<QToolButton *>("anchorReferencesDisclosure")->isChecked()},
          {"undo_count", int(undo_history.size())},
          {"redo_count", int(redo_history.size())},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (anchor_validity_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      validity_consumer);
      const auto found = std::find_if(
          layer.mask->effects.begin(), layer.mask->effects.end(), [&](const auto &effect) {
            return effect.id == validity_fill;
          });
      const auto infos = bts::TextureDocument::mask_anchors(set);
      bool exists = false, enabled = false;
      QString name;
      for (const auto &anchor : infos)
        if (anchor.id == validity_point) {
          exists = true;
          enabled = anchor.enabled;
          name = anchor.name;
        }
      QJsonArray sphere;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      auto capture = sets_captures.last().toObject();
      capture["anchor_validity"] = QJsonObject{
          {"source", validity_source.toString()},
          {"anchor", validity_point.toString()},
          {"consumer", validity_consumer.toString()},
          {"input", validity_fill.toString()},
          {"reference",
           found->anchor_source.isNull() ? QString{} : found->anchor_source.toString()},
          {"source_exists", exists},
          {"source_enabled", enabled},
          {"source_name", name},
          {"local_anchors", int(infos.size())},
          {"sphere", sphere},
          {"value", host.sample_texture(set.material_session_uid, 15, 102, 102)[0]},
          {"other", host.sample_texture(set.material_session_uid, 15, 121, 102)[0]},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
          {"warning", workspace->findChild<QLabel *>("anchorReferenceWarning")->isVisible()},
          {"warning_text", workspace->findChild<QLabel *>("anchorReferenceWarning")->text()},
          {"resource", workspace->findChild<QComboBox *>("maskFillResource")->currentText()},
          {"uniform_visible",
           workspace->findChild<QDoubleSpinBox *>("maskEffectFillValue")->isVisible()},
          {"cross_set_rejected", validity_cross},
          {"cross_set_preserved", validity_preserved},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
#include "ContentAnchorCapture.inc"
#include "FillAnchorCapture.inc"
#include "GeneratorAnchorCapture.inc"
    if (anchor_reference_demo && !qEnvironmentVariableIsSet("BTS_CONTENT_ANCHORS") &&
        sets_stage > 0)
    {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &selected = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                         set.selected_layer);
      const auto &source = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                       anchor_demo_source);
      const bool valid = set.selected_mask && selected.mask && !selected.mask->effects.empty();
      QJsonArray sphere;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      QString source_ink;
      for (const auto &effect : source.mask->effects)
        if (effect.kind == bts::MaskEffectKind::Paint)
          source_ink = QString::number(effect.paint.pixels.hash(2), 16);
      auto capture = sets_captures.last().toObject();
      capture["anchor_reference"] = QJsonObject{
          {"sphere", sphere},
          {"source", anchor_demo_source.toString()},
          {"anchor", anchor_demo_point.toString()},
          {"consumer", anchor_demo_consumer.toString()},
          {"input", anchor_demo_fill.toString()},
          {"native_valid", valid},
          {"value",
           valid ? QJsonValue(host.sample_texture(set.material_session_uid, 15, 102, 102)[0]) :
                   QJsonValue()},
          {"other",
           valid ? QJsonValue(host.sample_texture(set.material_session_uid, 15, 121, 102)[0]) :
                   QJsonValue()},
          {"probe",
           valid ? QJsonValue(host.sample_texture(set.material_session_uid, 15, 121, 38)[0]) :
                   QJsonValue()},
          {"hash",
           valid ? QString::number(host.texture_hash(set.material_session_uid, 15), 16) :
                   QString{}},
          {"source_own_value",
           bts::TextureDocument::mask_value(set, anchor_demo_source, 102, 102)},
          {"source_ink", source_ink},
          {"anchor_properties", workspace->findChild<QWidget *>("anchorProperties")->isVisible()},
          {"references", workspace->findChild<QListWidget *>("anchorReferences")->count()},
          {"no_references", workspace->findChild<QLabel *>("anchorNoReferences")->isVisible()},
          {"anchor_name", workspace->findChild<QLineEdit *>("anchorName")->text()},
          {"resource", workspace->findChild<QComboBox *>("maskFillResource")->currentText()},
          {"uniform_visible",
           workspace->findChild<QDoubleSpinBox *>("maskEffectFillValue")->isVisible()},
          {"mapping_enabled", workspace->findChild<QTabWidget *>("maskFillTabs")->isTabEnabled(0)},
          {"warning", workspace->findChild<QLabel *>("anchorReferenceWarning")->isVisible()},
          {"paint_enabled", viewport->paint_enabled},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (fill_effect_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray sphere, samples;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      for (int y = 4; y < 128; y += 8)
        for (int x = 4; x < 128; x += 8)
          samples.append(host.sample_texture(set.material_session_uid, 15, x, y)[0]);
      auto capture = sets_captures.last().toObject();
      capture["fill_effect"] = QJsonObject{
          {"sphere", sphere},
          {"samples", samples},
          {"value", host.sample_texture(set.material_session_uid, 15, 102, 102)[0]},
          {"other", host.sample_texture(set.material_session_uid, 15, 121, 102)[0]},
          {"middle", host.sample_texture(set.material_session_uid, 15, 64, 64)[0]},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
          {"paint_enabled", viewport->paint_enabled},
          {"frame", bool(viewport->fill_projection && viewport->fill_projection->manipulator)},
          {"frame_button",
           workspace->findChild<QToolButton *>("fillToolbarManipulator")->isVisible()},
          {"v_enabled", workspace->findChild<QDoubleSpinBox *>("maskFillScale1")->isEnabled()},
          {"projection", workspace->findChild<QComboBox *>("maskFillProjection")->currentText()},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (filter_effect_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray sphere, color;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      for (float v : host.sample_texture(set.material_session_uid, 0, 118, 102))
        color.append(v);
      auto capture = sets_captures.last().toObject();
      capture["filter_effect"] = QJsonObject{
          {"sphere", sphere},
          {"mask_value", host.sample_texture(set.material_session_uid, 15, 102, 102)[0]},
          {"mask_other", host.sample_texture(set.material_session_uid, 15, 121, 102)[0]},
          {"mask_hash", QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
          {"color", color},
          {"color_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"paint_enabled", viewport->paint_enabled},
          {"properties", workspace->findChild<QWidget *>("filterProperties")->isVisible()},
          {"channel_visible", workspace->findChild<QComboBox *>("filterChannel")->isVisible()},
          {"steps_visible", workspace->findChild<QSpinBox *>("filterSteps")->isVisible()},
          {"resource", workspace->findChild<QToolButton *>("filterResource")->text()},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (mesh_generator_demo && qEnvironmentVariableIsSet("BTS_CONTENT_GENERATORS") &&
        sets_stage >= 62)
    {
      const auto &set = document->set(snapshot.materials[0].session_id);
      auto capture = sets_captures.last().toObject();
      QJsonArray pixels;
      for (int channel : {0, 2, 4, 16})
        for (int x : {20, 100})
          if (const auto pixel = host.diagnostic_gpu_pixel(
                  set.material_session_uid, channel, x, 64))
            pixels.append(QJsonArray{channel, x, (*pixel)[0], (*pixel)[3]});
      QJsonArray choices;
      auto *channels = workspace->findChild<QComboBox *>("generatorChannel");
      for (int i = 0; i < channels->count(); ++i)
        choices.append(channels->itemData(i).toInt());
      capture["content_generator"] = QJsonObject{
          {"gpu_error", host.verify_gpu_texture_stack(set.material_session_uid)},
          {"samples", pixels},
          {"channel_visible", channels->isVisible()},
          {"channels", choices},
          {"panel_visible",
           workspace->findChild<QWidget *>("maskGeneratorProperties")->isVisible()},
          {"mask", set.selected_mask}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (paint_effect_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray sphere, color, low;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      for (float v : host.sample_texture(set.material_session_uid, 0, 118, 102))
        color.append(v);
      for (float v : host.sample_texture(set.material_session_uid, 0, 118, 38))
        low.append(v);
      QJsonArray material_ink, mask_ink;
      for (const auto &effect : layer.effects)
        if (effect.kind == bts::ContentEffectKind::Paint)
          material_ink.append(QString::number(effect.paint.pixels.hash(0), 16));
      if (layer.mask)
        for (const auto &effect : layer.mask->effects)
          if (effect.kind == bts::MaskEffectKind::Paint)
            mask_ink.append(QString::number(effect.paint.pixels.hash(2), 16));
      auto capture = sets_captures.last().toObject();
      capture["paint_effect"] = QJsonObject{
          {"sphere", sphere},
          {"mask_value", host.sample_texture(set.material_session_uid, 15, 102, 102)[0]},
          {"mask_other", host.sample_texture(set.material_session_uid, 15, 121, 102)[0]},
          {"mask_hash", QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
          {"color", color},
          {"low", low},
          {"color_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"parent_ink", QString::number(layer.pixels.hash(0), 16)},
          {"parent_mask_ink", QString::number(layer.mask->pixels.hash(2), 16)},
          {"material_ink", material_ink},
          {"mask_ink", mask_ink},
          {"paint_enabled", viewport->paint_enabled},
          {"mix_visible", workspace->findChild<QWidget *>("paintEffectMix")->isVisible()},
          {"gray_visible", workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->isVisible()},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (compare_mask_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray sphere, channels;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      auto *channel = workspace->findChild<QComboBox *>("compareChannel");
      for (int i = 0; i < channel->count(); ++i)
        channels.append(channel->itemData(i).toInt());
      auto capture = sets_captures.last().toObject();
      capture["compare_mask"] = QJsonObject{
          {"histogram_ready",
           workspace->findChild<QWidget *>("compareHistogram")
               ->property("gpuHistogramReady")
               .toBool()},
          {"histogram_bins",
           QJsonArray::fromVariantList(workspace->findChild<QWidget *>("compareHistogram")
                                           ->property("gpuHistogramBins")
                                           .toList())},
          {"sphere", sphere},
          {"value", host.sample_texture(set.material_session_uid, 15, 64, 64)[0]},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
          {"paint_enabled", viewport->paint_enabled},
          {"properties", workspace->findChild<QWidget *>("compareMaskProperties")->isVisible()},
          {"channel_enabled", channel->isEnabled()},
          {"channels", channels},
          {"constant_visible",
           workspace->findChild<QDoubleSpinBox *>("compareConstant")->isVisible()},
          {"tolerance_visible",
           workspace->findChild<QDoubleSpinBox *>("compareTolerance")->isVisible()},
          {"identical_warning",
           workspace->findChild<QLabel *>("compareIdenticalWarning")->isVisible()},
          {"horizontal_scroll",
           workspace->findChild<QScrollArea *>("propertiesScroll")
               ->horizontalScrollBar()
               ->maximum()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (levels_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray samples, sphere, channels, components;
      for (float v : host.sample_texture(set.material_session_uid, 0, 64, 64))
        samples.append(v);
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      auto *picker = workspace->findChild<QComboBox *>("levelsChannel"),
           *component = workspace->findChild<QComboBox *>("levelsComponent");
      for (int i = 0; i < picker->count(); ++i)
        channels.append(picker->itemData(i).toInt());
      for (int i = 0; i < component->count(); ++i)
        components.append(component->itemText(i));
      auto *scroll = workspace->findChild<QScrollArea *>("propertiesScroll");
      auto *clamp = workspace->findChild<QCheckBox *>("levelsClamp");
      const auto clamp_rect = QRect(clamp->mapTo(scroll->viewport(), QPoint{}), clamp->size());
      if (sets_stage == 2)
        workspace->grab().save(directory + "/levels-workspace.png");
      auto capture = sets_captures.last().toObject();
      capture["levels"] = QJsonObject{
          {"clamp_rect",
           QJsonArray{clamp_rect.x(), clamp_rect.y(), clamp_rect.width(), clamp_rect.height()}},
          {"properties_viewport",
           QJsonArray{scroll->viewport()->width(), scroll->viewport()->height()}},
          {"histogram_ready",
           workspace->findChild<QWidget *>("levelsHistogram")
               ->property("gpuHistogramReady")
               .toBool()},
          {"histogram_bins",
           QJsonArray::fromVariantList(workspace->findChild<QWidget *>("levelsHistogram")
                                           ->property("gpuHistogramBins")
                                           .toList())},
          {"samples", samples},
          {"roughness", host.sample_texture(set.material_session_uid, 2, 64, 64)[0]},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"sphere", sphere},
          {"channels", channels},
          {"components", components},
          {"component", component->currentData().toInt()},
          {"paint_enabled", viewport->paint_enabled},
          {"undo_count", int(undo_history.size())},
          {"horizontal_scroll", scroll->horizontalScrollBar()->maximum()},
          {"clamp_visible",
           clamp->isVisible() && scroll->viewport()->rect().contains(clamp_rect)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (mesh_generator_demo && sets_stage > 0 &&
        !(qEnvironmentVariableIsSet("BTS_CONTENT_GENERATORS") && sets_stage >= 62))
    {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray sphere, samples;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      for (int x = 4; x < 128; x += 8)
        samples.append(host.sample_texture(set.material_session_uid, 15, x, 64)[0]);
      auto capture = sets_captures.last().toObject();
      capture["mesh_generator"] = QJsonObject{
          {"sphere", sphere},
          {"samples", samples},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
          {"paint_enabled", viewport->paint_enabled},
          {"properties", workspace->findChild<QWidget *>("maskGeneratorProperties")->isVisible()},
          {"controls", workspace->findChild<QDoubleSpinBox *>("generatorContrast")->isVisible()},
          {"input", workspace->findChild<QLabel *>("generatorInputResource")->text()},
          {"axis_visible",
           workspace->findChild<QComboBox *>("generatorPositionAxis")->isVisible()}};
      if (qEnvironmentVariableIsSet("BTS_MESH_GENERATORS")) {
        QJsonArray gpu;
        for (int x = 4; x < 128; x += 8) {
          if (const auto pixel = host.diagnostic_gpu_pixel(set.material_session_uid, 15, x, 64))
            gpu.append((*pixel)[0]);
          else
            gpu.append(QJsonValue{});
        }
        auto value = capture["mesh_generator"].toObject();
        value["gpu_samples"] = gpu;
        value["bake_visible"] =
            workspace->findChild<QPushButton *>("generatorBakeMaps")->isVisible();
        value["light_visible"] =
            workspace->findChild<QWidget *>("generatorLightDirection")->isVisible();
        value["dirt_visible"] =
            workspace->findChild<QDoubleSpinBox *>("generatorDirtCurvature")->isVisible();
        value["input_role"] =
            workspace->findChild<QComboBox *>("generatorInputRole")->currentData().toInt();
        value["bake_opened"] = workspace->property("generatorBakeOpened").toBool();
        capture["mesh_generator"] = value;
      }
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (mask_effects_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray sphere, order;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      for (auto effect = layer.mask->effects.rbegin(); effect != layer.mask->effects.rend();
           ++effect)
        order.append(effect->kind == bts::MaskEffectKind::Fill   ? "fill" :
                     effect->kind == bts::MaskEffectKind::Levels ? "levels" :
                                                                   "selection");
      auto capture = sets_captures.last().toObject();
      capture["mask_effects"] = QJsonObject{
          {"sphere", sphere},
          {"value", host.sample_texture(set.material_session_uid, 15, 64, 64)[0]},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
          {"order", order},
          {"paint_enabled", viewport->paint_enabled},
          {"levels_channel_visible",
           workspace->findChild<QComboBox *>("levelsChannel")->isVisible()},
          {"levels_properties", workspace->findChild<QWidget *>("levelsProperties")->isVisible()},
          {"fill_properties", workspace->findChild<QWidget *>("maskFillProperties")->isVisible()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (mask_replace_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray sphere, samples;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      for (int y = 4; y < 128; y += 8)
        for (int x = 4; x < 128; x += 8)
          samples.append(host.sample_texture(set.material_session_uid, 0, x, y)[0]);
      auto capture = sets_captures.last().toObject();
      capture["mask_replace"] = QJsonObject{
          {"sphere", sphere},
          {"samples", samples},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (smart_mask_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &selected = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                         set.selected_layer);
      QJsonArray sphere;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      QJsonObject asset_info;
      for (const auto &asset : assets.assets())
        if (asset.smart_mask && asset.name == "Vertical mask fixture") {
          asset_info = QJsonObject{
              {"id", asset.id.toString()},
              {"hash", asset.hash()},
              {"paint_hash", QString::number(asset.smart_mask->pixels.hash(2), 16)},
              {"name", asset.name},
              {"scope", int(asset.scope)}};
          break;
        }
      QJsonObject evidence{
          {"asset", asset_info},
          {"loaded", !smart_mask_loaded.isNull()},
          {"loaded_id", smart_mask_loaded.toString()},
          {"source", smart_mask_source.toString()},
          {"target", smart_mask_target.toString()},
          {"empty", smart_mask_empty.toString()},
          {"sphere", sphere},
          {"has_mask", selected.mask.has_value()},
          {"native_hash",
           selected.mask ? QString::number(host.texture_hash(set.material_session_uid, 15), 16) :
                           QString{}},
          {"mask_hash",
           selected.mask ? QString::number(selected.mask->pixels.hash(2), 16) : QString{}}};
      auto capture = sets_captures.last().toObject();
      capture["smart_mask"] = evidence;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (mask_copy_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &source = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                       mask_copy_source);
      QJsonArray sphere, samples;
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      for (int y = 4; y < 128; y += 8)
        for (int x = 4; x < 128; x += 8)
          samples.append(host.sample_texture(set.material_session_uid, 15, x, y)[0]);
      auto capture = sets_captures.last().toObject();
      QJsonObject evidence{{"source", mask_copy_source.toString()},
                           {"destination", mask_copy_destination.toString()},
                           {"source_hash", QString::number(source.mask->pixels.hash(2), 16)},
                           {"native_mask_hash",
                            QString::number(host.texture_hash(set.material_session_uid, 15), 16)},
                           {"samples", samples},
                           {"sphere", sphere},
                           {"menus", mask_copy_menus},
                           {"clipboard", mask_clipboard.has_value()},
                           {"selected", set.selected_layer.toString()}};
      if (!mask_copy_destination.isNull()) {
        const auto &dest = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                       mask_copy_destination);
        evidence["destination_hash"] = QString::number(dest.mask->pixels.hash(2), 16);
      }
      capture["mask_copy"] = evidence;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (color_selection_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray samples, sphere;
      for (int x : {16, 112}) {
        QJsonArray rgba;
        for (float v : host.sample_texture(set.material_session_uid, 0, x, 64))
          rgba.append(v);
        samples.append(rgba);
      }
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      auto capture = sets_captures.last().toObject();
      capture["color_selection"] = QJsonObject{
          {"samples", samples},
          {"sphere", sphere},
          {"picker", id_picker_active},
          {"map", viewport->mesh_map_active},
          {"paint_enabled", viewport->paint_enabled},
          {"properties_visible",
           workspace->findChild<QWidget *>("colorSelectionProperties")->isVisible()},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (passthrough_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray samples, roughness, sphere;
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
          QJsonArray rgba;
          for (float v : host.sample_texture(set.material_session_uid, 0, 4 + x * 8, 4 + y * 8))
            rgba.append(v);
          samples.append(rgba);
          roughness.append(
              host.sample_texture(set.material_session_uid, 2, 4 + x * 8, 4 + y * 8)[0]);
        }
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      auto capture = sets_captures.last().toObject();
      capture["passthrough"] = QJsonObject{
          {"samples", samples},
          {"roughness", roughness},
          {"sphere", sphere},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"paint_enabled", viewport->paint_enabled},
          {"paint_pages", qint64(layer.pixels.page_count(0))},
          {"row_count", workspace->layer_stack()->count()},
          {"properties_visible",
           workspace->findChild<QWidget *>("levelsProperties")->isVisible()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (normal_vectors_demo && sets_stage > 0) {
      auto &set = document->set(snapshot.materials[0].session_id);
      auto &top = bts::TextureDocument::layer(set, set.selected_layer);
      const auto &bottom = set.layers[set.layers.size() - 2];
      QJsonArray upper, lower, sample, sphere;
      for (float value : top.values.normal)
        upper.append(value);
      for (float value : bottom.values.normal)
        lower.append(value);
      for (float value : host.sample_texture(set.material_session_uid, 6, 64, 64))
        sample.append(value);
      QJsonArray complete;
      for (float value : host.sample_texture(set.material_session_uid, 3, 64, 64))
        complete.append(value);
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      auto capture = sets_captures.last().toObject();
      capture["normal_vectors"] = QJsonObject{
          {"complete", complete},
          {"upper", upper},
          {"lower", lower},
          {"sample", sample},
          {"mode", QString::fromLatin1(bts::layer_blend_names[int(top.blending[3])])},
          {"lower_mode", QString::fromLatin1(bts::layer_blend_names[int(bottom.blending[3])])},
          {"sphere", sphere},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 3), 16)},
          {"evidence", normal_vector_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (families_demo && sets_stage > 0) {
      auto &set = document->set(snapshot.materials[0].session_id);
      auto &top = bts::TextureDocument::layer(set, set.selected_layer);
      QJsonArray color, rough, sphere;
      const auto color_sample = host.sample_texture(set.material_session_uid, 0, 64, 64),
                 rough_sample = host.sample_texture(set.material_session_uid, 2, 64, 64);
      for (float value : color_sample)
        color.append(value);
      for (float value : rough_sample)
        rough.append(value);
      for (int c = 0; c < 5; ++c)
        sphere.append(QString::number(host.texture_hash(snapshot.materials[1].session_id, c), 16));
      auto capture = sets_captures.last().toObject();
      capture["families"] = QJsonObject{
          {"color", color},
          {"roughness", rough},
          {"mode",
           QString::fromLatin1(bts::layer_blend_names[int(
               top.blending[workspace->findChild<QComboBox *>("layerChannelContext")
                                ->currentData()
                                .toInt()])])},
          {"channel",
           workspace->findChild<QComboBox *>("layerChannelContext")->currentData().toInt()},
          {"top_roughness", top.values.roughness},
          {"sphere", sphere},
          {"color_hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)},
          {"evidence", families_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (tile_resolution_demo && sets_stage > 0) {
      const auto &set = document->texture_sets().front();
      QJsonObject tiles;
      for (int number : set.tiles)
        tiles[QString::number(number)] = QJsonObject{
            {"width", set.width(number)},
            {"height", set.rows(number)},
            {"override", set.tile_resolutions.contains(number)},
            {"base_hash",
             QString::number(host.texture_hash(set.material_session_uid, 0, number), 16)},
            {"ao_hash",
             QString::number(host.texture_hash(set.material_session_uid, 5, number), 16)}};
      QJsonArray layers;
      for (const auto &layer : set.layers) {
        QJsonArray hashes;
        for (int c = 0; c < 5; ++c)
          hashes.append(QString::number(layer.pixels.hash(c), 16));
        layers.append(
            QJsonObject{{"id", layer.id.toString()},
                        {"author", bts::TextureDocument::authoring_resolution(set, layer)},
                        {"hashes", hashes}});
      }
      auto *tree = workspace->findChild<QTreeWidget *>("uvTiles");
      auto *combo = workspace->findChild<QComboBox *>("uvTileResolution");
      auto capture = sets_captures.last().toObject();
      capture["tile_size"] = QJsonObject{
          {"tiles", tiles},
          {"parent", set.texture_resolution},
          {"layers", layers},
          {"selected",
           tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toInt() : 0},
          {"label", combo->currentText()},
          {"value", combo->currentData().toInt()},
          {"visible", combo->isVisible()},
          {"evidence", tile_resolution_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (custom_demo && sets_stage > 0) {
      auto &set = document->set(snapshot.materials[0].session_id);
      const auto &user = bts::TextureDocument::layer(set, set.selected_layer).users[7];
      QJsonArray pbr;
      for (int c = 0; c < 5; ++c)
        pbr.append(QString::number(host.texture_hash(set.material_session_uid, c), 16));
      QJsonObject info{
          {"name", QString::fromStdString(set.users[7].name)},
          {"present", set.users[7].present},
          {"precision", int(set.users[7].precision)},
          {"brush", brush.users[7]},
          {"value", brush.user_values[7]},
          {"hash", QString::number(user.pixels.hash(2), 16)},
          {"pages", qint64(user.pixels.page_count(2))},
          {"author", bts::custom_authoring_resolution(user, 0)},
          {"loaded", !user.strokes || user.strokes->loaded},
          {"operations", user.strokes ? int(user.strokes->operations.size()) : 0},
          {"native_pbr", pbr},
          {"label", viewport->current_inspection_label()},
          {"property_label",
           workspace->findChild<QToolButton *>("brushUserChannel7")->accessibleName()}};
      if (set.users[7].present) {
        info["native_hash"] = QString::number(host.texture_hash(set.material_session_uid, 14), 16);
        float coverage = 0, source = 0;
        int px = 0, py = 0, number = 1001;
        for (const auto &[tile, pixels] : user.pixels.tiles())
          pixels.visit_pages(
              [&](int channel, std::uint32_t key, std::span<const bts::PaintPixels::Pixel> page) {
                if (channel != 2)
                  return;
                for (std::size_t i = 0; i < page.size(); ++i)
                  if (page[i][3] > coverage) {
                    coverage = page[i][3];
                    source = page[i][0] / coverage;
                    px = int(key % 65536) * bts::PaintPixels::page_side +
                         int(i % bts::PaintPixels::page_side);
                    py = int(key / 65536) * bts::PaintPixels::page_side +
                         int(i / bts::PaintPixels::page_side);
                    number = tile;
                  }
              });
        if (coverage > 0) {
          const int size = bts::custom_authoring_resolution(user, set.texture_resolution);
          const auto sample = host.sample_texture(
              set.material_session_uid,
              14,
              std::clamp(
                  int((px + .5) * set.texture_resolution / size), 0, set.texture_resolution - 1),
              std::clamp(int((py + .5) * set.rows() / size), 0, set.rows() - 1),
              number);
          info["author_value"] = source;
          info["native_sample"] = QJsonArray{sample[0], sample[1], sample[2], sample[3]};
        }
      }
      auto capture = sets_captures.last().toObject();
      capture["custom"] = info;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (precision_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      QJsonArray samples, formats;
      for (int c = 0; c < 6; ++c) {
        auto *combo = workspace->findChild<QComboBox *>(QString("channelPrecision%1").arg(c));
        formats.append(QJsonObject{{"label", combo->currentText()},
                                   {"index", combo->currentIndex()},
                                   {"choices", combo->count()}});
        if (c < 5) {
          const auto pixel = host.sample_texture(set.material_session_uid, c, 64, 64);
          samples.append(QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]});
        }
      }
      auto capture = sets_captures.last().toObject();
      capture["precision"] = QJsonObject{
          {"formats", formats},
          {"samples", samples},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 0), 16)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (resolution_demo || rectangular_demo) {
      auto capture = sets_captures.last().toObject();
      QJsonArray states;
      for (const auto &set : document->texture_sets()) {
        QJsonArray layers;
        for (const auto &layer : set.layers) {
          QJsonArray hashes;
          for (int c = 0; c < 5; ++c)
            hashes.append(QString::number(layer.pixels.hash(c), 16));
          layers.append(
              QJsonObject{{"id", layer.id.toString()},
                          {"author", bts::TextureDocument::authoring_resolution(set, layer)},
                          {"hashes", hashes},
                          {"ao", QString::number(layer.ao_pixels.hash(2), 16)},
                          {"history_loaded", !layer.strokes || layer.strokes->loaded},
                          {"history", layer.strokes ? int(layer.strokes->operations.size()) : 0}});
        }
        states.append(QJsonObject{{"name", QString::fromStdString(set.name)},
                                  {"resolution", set.texture_resolution},
                                  {"height", set.rows()},
                                  {"linked", set.size_linked},
                                  {"layers", layers}});
      }
      capture["dynamic"] = QJsonObject{
          {"sets", states},
          {"selected", workspace->findChild<QComboBox *>("textureSetResolution")->currentText()},
          {"selected_height",
           workspace->findChild<QComboBox *>("textureSetHeight")->currentText()},
          {"height_enabled", workspace->findChild<QComboBox *>("textureSetHeight")->isEnabled()},
          {"linked", workspace->findChild<QToolButton *>("textureSetSizeLock")->isChecked()},
          {"evidence", resolution_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (physical_demo) {
      auto capture = sets_captures.last().toObject();
      const auto scene = host.scene_snapshot();
      const auto &set = document->texture_sets().front();
      const bts::TextureLayer *layer = nullptr;
      for (const auto &candidate : set.layers)
        if (candidate.kind == bts::LayerKind::Fill) {
          if (!layer || candidate.id == set.selected_layer)
            layer = &candidate;
        }
      QJsonArray positions;
      for (const auto &triangle : scene.uv_triangles)
        for (const auto &point : triangle.positions)
          positions.append(QJsonArray{point[0], point[1], point[2]});
      QJsonObject projection;
      if (layer) {
        auto p = layer->fill;
        p.native_per_uv = set.native_per_uv;
        p.cm_per_native = set.cm_per_native;
        const auto effective = bts::fill_effective_uv_scale(p);
        projection = QJsonObject{{"physical", p.physical},
                                 {"custom", p.custom_size},
                                 {"mode", p.mode},
                                 {"scale", QJsonArray{p.scale[0], p.scale[1]}},
                                 {"effective", QJsonArray{effective[0], effective[1]}},
                                 {"size",
                                  QJsonArray{bts::fill_physical_size(p)[0],
                                             bts::fill_physical_size(p)[1],
                                             bts::fill_physical_size(p)[2]}}};
      }
      capture["physical"] = QJsonObject{
          {"units", document->to_json()["units"]},
          {"native_per_uv", QJsonArray{set.native_per_uv[0], set.native_per_uv[1]}},
          {"cm_per_native", set.cm_per_native},
          {"fill", projection},
          {"geometry_hash",
           QString::fromLatin1(
               QCryptographicHash::hash(QJsonDocument(positions).toJson(QJsonDocument::Compact),
                                        QCryptographicHash::Sha256)
                   .toHex())},
          {"source_exists", QFileInfo::exists(harness.value("physical-scale-demo"))},
          {"evidence", physical_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (usd_demo) {
      auto capture = sets_captures.last().toObject();
      const auto scene = host.scene_snapshot();
      QJsonArray uv, positions;
      for (const auto &triangle : scene.uv_triangles) {
        for (const auto &point : triangle.corners)
          uv.append(QJsonArray{point[0], point[1]});
        for (const auto &point : triangle.positions)
          positions.append(QJsonArray{point[0], point[1], point[2]});
      }
      const auto hash = [&](const QJsonArray &data) {
        return QString::fromLatin1(
            QCryptographicHash::hash(QJsonDocument(data).toJson(QJsonDocument::Compact),
                                     QCryptographicHash::Sha256)
                .toHex());
      };
      capture["usd"] = QJsonObject{
          {"meshes", int(scene.mesh_objects)},
          {"triangles", int(scene.uv_triangles.size())},
          {"missing_uv_meshes", int(scene.meshes_without_uvs)},
          {"uv_hash", hash(uv)},
          {"geometry_hash", hash(positions)},
          {"minimum", QJsonArray{scene.minimum[0], scene.minimum[1], scene.minimum[2]}},
          {"maximum", QJsonArray{scene.maximum[0], scene.maximum[1], scene.maximum[2]}},
          {"source_exists", QFileInfo::exists(harness.value("usd-scene-demo"))},
          {"evidence", usd_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (unwrap_demo) {
      auto capture = sets_captures.last().toObject();
      QJsonObject sets;
      const auto geometry = host.scene_snapshot();
      for (std::size_t material = 0; material < geometry.materials.size(); ++material) {
        QJsonArray coordinates;
        double area = 0;
        int count = 0;
        for (const auto &triangle : geometry.uv_triangles)
          if (triangle.material == material) {
            ++count;
            for (const auto &p : triangle.corners)
              coordinates.append(QJsonArray{p[0], p[1]});
            const auto &a = triangle.corners[0], &b = triangle.corners[1],
                       &c = triangle.corners[2];
            area += std::abs(double(b[0] - a[0]) * (c[1] - a[1]) -
                             double(b[1] - a[1]) * (c[0] - a[0])) *
                    .5;
          }
        sets[QString::fromStdString(geometry.materials[material].name)] = QJsonObject{
            {"uv_hash",
             QString::fromLatin1(QCryptographicHash::hash(
                                     QJsonDocument(coordinates).toJson(QJsonDocument::Compact),
                                     QCryptographicHash::Sha256)
                                     .toHex())},
            {"area", area},
            {"triangles", count},
            {"coordinates", coordinates}};
      }
      capture["unwrap"] = QJsonObject{{"sets", sets},
                                      {"evidence", unwrap_evidence},
                                      {"missing_uv_meshes", int(geometry.meshes_without_uvs)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (cameras_demo && sets_stage > 0) {
      auto capture = sets_captures.last().toObject();
      QJsonArray native_cameras;
      for (const auto &camera : host.scene_snapshot().cameras)
        native_cameras.append(QJsonObject{
            {"name", QString::fromStdString(camera.name)},
            {"position", QJsonArray{camera.position[0], camera.position[1], camera.position[2]}},
            {"forward", QJsonArray{camera.forward[0], camera.forward[1], camera.forward[2]}},
            {"up", QJsonArray{camera.up[0], camera.up[1], camera.up[2]}},
            {"fov", camera.vertical_fov},
            {"aspect", camera.aspect}});
      capture["cameras"] = QJsonObject{{"state", viewport->scene_camera_state()},
                                       {"native", native_cameras},
                                       {"environment", host.environment_state()},
                                       {"evidence", camera_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (tangent_demo && sets_stage > 0) {
      auto capture = sets_captures.last().toObject();
      QJsonObject normals;
      for (const auto &set : document->texture_sets())
        if (set.mesh_maps[0]) {
          double error = 0;
          int count = 0;
          for (const auto &[tile, plane] : set.mesh_maps[0]->tiles) {
            const auto &pixels = plane.pixels->rgba;
            for (std::size_t i = 0; i < pixels.size(); i += 4)
              if (pixels[i + 3] > .99) {
                ++count;
                for (int c = 0; c < 3; ++c)
                  error = std::max(
                      error, std::abs(double(pixels[i + c] / pixels[i + 3]) - (c == 2 ? 1 : .5)));
              }
          }
          normals[QString::fromStdString(set.name)] = QJsonObject{{"covered", count},
                                                                  {"neutral_max_error", error}};
        }
      capture["tangents"] = QJsonObject{
          {"environment", host.environment_state()},
          {"shaders", host.tangent_shader_state()},
          {"normal_hash",
           QString::number(host.texture_hash(snapshot.materials[1].session_id, 3), 16)},
          {"baked_normals", normals},
          {"evidence", tangent_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (normal_format_demo && sets_stage > 0) {
      auto capture = sets_captures.last().toObject();
      capture["normal_format"] = QJsonObject{{"environment", host.environment_state()},
                                             {"evidence", normal_format_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (template_demo && sets_stage > 0) {
      auto capture = sets_captures.last().toObject();
      capture["template"] = QJsonObject{{"environment", host.environment_state()},
                                        {"color", host.color_state()},
                                        {"baking", bts::mesh_bake_template(document->id())},
                                        {"evidence", template_evidence},
                                        {"title", window->windowTitle()},
                                        {"path", document_path},
                                        {"dirty", dirty}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (reassign_demo && sets_stage > 0) {
      auto capture = sets_captures.last().toObject();
      capture["reassignment"] = QJsonObject{{"environment", host.environment_state()},
                                            {"dialog", assignment_evidence}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (uv_reproject_demo && sets_stage > 0) {
      const auto &set = document->set(snapshot.materials[0].session_id);
      const auto &layer = set.layers.front();
      const int res = int(document->settings().resolution);
      QJsonArray hashes;
      QJsonArray spaces;
      if (layer.strokes)
        for (const auto &op : layer.strokes->operations) {
          hashes.append(QString::number(op->ink->paint.hash(0), 16));
          spaces.append(op->spatial);
        }
      for (bool spatial : {true, false}) {
        QImage image(res, res, QImage::Format_Grayscale8);
        image.fill(0);
        if (layer.strokes)
          for (const auto &op : layer.strokes->operations)
            if (op->spatial == spatial) {
              for (int y = 0; y < res; ++y)
                for (int x = 0; x < res; ++x)
                  image.scanLine(res - 1 - y)[x] = uchar(
                      std::clamp(std::lround(op->ink->paint.sample(0, x, y)[3] * 255), 0L, 255L));
              break;
            }
        image.save(directory + "/" + sets_stages.at(sets_stage) +
                   (spatial ? "-spatial-alpha.png" : "-uv-alpha.png"));
      }
      QImage mask(res, res, QImage::Format_Grayscale8);
      const auto coverage = host.texture_coverage(set.material_session_uid);
      for (int y = 0; y < res; ++y)
        for (int x = 0; x < res; ++x)
          mask.scanLine(res - 1 - y)[x] = coverage[std::size_t(y) * res + x] ? 255 : 0;
      mask.save(directory + "/" + sets_stages.at(sets_stage) + "-uv-mask.png");
      auto capture = sets_captures.last().toObject();
      capture["reprojection"] = QJsonObject{
          {"ink_hashes", hashes},
          {"spatial", spaces},
          {"environment", host.environment_state()},
          {"colors", QString::number(host.texture_hash(set.material_session_uid, 0), 16)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (reimport_demo && sets_stage > 0) {
      auto capture = sets_captures.last().toObject();
      QJsonArray colors;
      for (const auto &set : document->texture_sets())
        colors.append(QString::number(host.texture_hash(set.material_session_uid, 0), 16));
      capture["reimport"] = QJsonObject{
          {"colors", colors},
          {"environment", host.environment_state()},
          {"profile", host.project_profile()},
          {"undo_enabled", workspace->findChild<QAction *>("undoDocument")->isEnabled()},
          {"selected", workspace->texture_sets()->currentRow()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (recovery_open_demo && sets_stage > 0) {
      auto capture = sets_captures.last().toObject();
      const auto &set = document->set(snapshot.materials[0].session_id);
      const int res = int(document->settings().resolution);
      capture["recovery"] = QJsonObject{
          {"unsaved_path", document_path.isEmpty()},
          {"dirty", dirty},
          {"center_roughness",
           bts::TextureDocument::composite(set, res / 2, res / 2).material.roughness},
          {"snapshots", int(recovery.entries().size())}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (export_demo) {
      auto capture = sets_captures.last().toObject();
      capture["exports"] = export_evidence;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (ao_demo && sets_stage > 0) {
      const auto &set = document->texture_sets().at(1);
      const int res = int(document->settings().resolution);
      QJsonArray samples;
      for (auto uv : {QPointF(.5, .5), QPointF(.2, .5), QPointF(.5, .3), QPointF(.7, .5)}) {
        const auto own = bts::TextureDocument::composite(
            set, int(uv.x() * res), int(uv.y() * res));
        const auto baked = bts::sample_baked_ao(
            set, (int(uv.x() * res) + .5) / res, (int(uv.y() * res) + .5) / res);
        const auto pixel = host.sample_texture(
            set.material_session_uid, 5, int(uv.x() * res), int(uv.y() * res));
        samples.append(QJsonObject{{"uv", QJsonArray{uv.x(), uv.y()}},
                                   {"authored", own.material.ambient_occlusion},
                                   {"coverage", own.ao_coverage},
                                   {"baked", baked.value},
                                   {"value", pixel[0]}});
      }
      auto capture = sets_captures.last().toObject();
      auto *control = workspace->findChild<QComboBox *>("ambientOcclusionMixing");
      capture["ao"] = QJsonObject{
          {"mode", control->currentText()},
          {"enabled", control->isEnabled()},
          {"samples", samples},
          {"hash", QString::number(host.texture_hash(set.material_session_uid, 5), 16)},
          {"baked_hash", QString::fromStdString(set.mesh_maps[3]->tiles.begin()->second.hash)}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (height_demo && sets_stage > 0) {
      const auto &set = document->texture_sets().front();
      auto capture = sets_captures.last().toObject();
      auto *control = workspace->findChild<QComboBox *>("heightNormalMethod");
      QJsonArray samples;
      const int res = int(document->settings().resolution);
      for (auto uv : {QPointF(.4, .46), QPointF(.5, .75), QPointF(.1, .9)}) {
        const auto own = bts::TextureDocument::composite(
            set, int(uv.x() * res), int(uv.y() * res));
        const auto rgba = host.sample_texture(
            set.material_session_uid, 3, int(uv.x() * res), int(uv.y() * res));
        samples.append(QJsonObject{{"height", own.material.bump},
                                   {"coverage", own.coverage[4]},
                                   {"normal", QJsonArray{rgba[0], rgba[1], rgba[2], rgba[3]}}});
      }
      capture["height"] = QJsonObject{
          {"method", control->currentText()},
          {"enabled", control->isEnabled()},
          {"brush", workspace->findChild<QDoubleSpinBox *>("materialBump")->value()},
          {"normal_hash", QString::number(host.texture_hash(set.material_session_uid, 3), 16)},
          {"bump_hash", QString::number(host.texture_hash(set.material_session_uid, 4), 16)},
          {"baked_hash", QString::fromStdString(set.mesh_maps[0]->tiles.begin()->second.hash)},
          {"samples", samples}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (normal_mix_demo) {
      auto capture = sets_captures.last().toObject();
      QJsonArray samples;
      const auto &set = document->texture_sets().front();
      const int res = int(document->settings().resolution);
      if (sets_stage > 0)
        for (auto uv : {QPointF(.45, .4), QPointF(.1, .8)}) {
          const auto rgba = host.sample_texture(
              set.material_session_uid, 3, int(uv.x() * res), int(uv.y() * res));
          const auto authored = bts::TextureDocument::composite(
              set, int(uv.x() * res), int(uv.y() * res));
          samples.append(QJsonObject{{"uv", QJsonArray{uv.x(), uv.y()}},
                                     {"rgba", QJsonArray{rgba[0], rgba[1], rgba[2], rgba[3]}},
                                     {"authored",
                                      QJsonArray{authored.material.normal[0],
                                                 authored.material.normal[1],
                                                 authored.material.normal[2]}},
                                     {"coverage", authored.coverage[3]}});
        }
      auto *control = workspace->findChild<QComboBox *>("normalMixing");
      capture["normal_mix"] = QJsonObject{
          {"mode", control->currentText()},
          {"enabled", control->isEnabled()},
          {"samples", samples},
          {"baked_hash",
           set.mesh_maps[0] ?
               QString::fromStdString(set.mesh_maps[0]->tiles.begin()->second.hash) :
               QString{}},
          {"baked",
           QJsonArray{bts::sample_baked_normal(set, .45, .4).direction[0],
                      bts::sample_baked_normal(set, .45, .4).direction[1],
                      bts::sample_baked_normal(set, .45, .4).direction[2]}}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (utility_import_demo) {
      QJsonArray maps;
      for (const auto &set : document->texture_sets()) {
        QJsonArray roles;
        for (int role = 0; role < 7; ++role) {
          QJsonValue result;
          if (set.mesh_maps[role]) {
            const auto &plane = set.mesh_maps[role]->tiles.begin()->second;
            QJsonArray samples;
            float low = 1e9, high = -1e9;
            for (std::size_t i = 0; i < plane.pixels->rgba.size(); i += 4) {
              low = std::min(low, plane.pixels->rgba[i]);
              high = std::max(high, plane.pixels->rgba[i]);
            }
            for (auto point : {QPoint(0, 0),
                               QPoint(plane.resolution / 3, plane.rows() * 2 / 5),
                               QPoint(plane.resolution - 1, plane.rows() - 1)})
            {
              const auto *pixel = plane.pixels->rgba.data() +
                                  (std::size_t(point.y()) * plane.resolution + point.x()) * 4;
              samples.append(QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]});
            }
            result = QJsonObject{{"name", QString::fromStdString(set.mesh_maps[role]->name)},
                                 {"hash", QString::fromStdString(plane.hash)},
                                 {"resolution", plane.resolution},
                                 {"height", plane.rows()},
                                 {"low", low},
                                 {"high", high},
                                 {"samples", samples}};
          }
          roles.append(result);
        }
        maps.append(roles);
      }
      auto capture = sets_captures.last().toObject();
      capture["utility"] = QJsonObject{{"evidence", utility_import_evidence}, {"maps", maps}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (bake_match_demo) {
      QJsonArray maps;
      for (const auto &set : document->texture_sets()) {
        QJsonArray roles;
        for (int role : {0, 2}) {
          QJsonValue result;
          if (set.mesh_maps[role]) {
            const auto &plane = set.mesh_maps[role]->tiles.at(1001);
            QJsonArray centers;
            int covered = 0;
            for (std::size_t i = 3; i < plane.pixels->rgba.size(); i += 4)
              covered += plane.pixels->rgba[i] > 0;
            for (float u : {.25f, .75f}) {
              const auto index = (std::size_t(plane.rows() / 2) * plane.resolution +
                                  int(u * plane.resolution)) *
                                 4;
              const auto *pixel = plane.pixels->rgba.data() + index;
              centers.append(QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]});
            }
            result = QJsonObject{{"role", role},
                                 {"hash", QString::fromStdString(plane.hash)},
                                 {"covered", covered},
                                 {"centers", centers}};
          }
          roles.append(result);
        }
        maps.append(roles);
      }
      auto capture = sets_captures.last().toObject();
      capture["matching"] = QJsonObject{{"evidence", bake_match_evidence}, {"maps", maps}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (cage_demo) {
      QJsonArray maps;
      for (const auto &set : document->texture_sets()) {
        QJsonArray planes;
        if (set.mesh_maps[0])
          for (const auto &[tile, plane] : set.mesh_maps[0]->tiles) {
            int covered = 0;
            std::array<float, 3> low{1, 1, 1}, high{0, 0, 0};
            for (std::size_t i = 0; i < plane.pixels->rgba.size(); i += 4)
              if (plane.pixels->rgba[i + 3] > 0) {
                ++covered;
                for (int c = 0; c < 3; ++c) {
                  const float value = plane.pixels->rgba[i + c] / plane.pixels->rgba[i + 3];
                  low[c] = std::min(low[c], value);
                  high[c] = std::max(high[c], value);
                }
              }
            planes.append(QJsonObject{{"tile", tile},
                                      {"resolution", plane.resolution},
                                      {"hash", QString::fromStdString(plane.hash)},
                                      {"covered", covered},
                                      {"low", QJsonArray{low[0], low[1], low[2]}},
                                      {"high", QJsonArray{high[0], high[1], high[2]}}});
          }
        maps.append(planes);
      }
      auto capture = sets_captures.last().toObject();
      capture["cage"] = QJsonObject{{"evidence", cage_evidence}, {"maps", maps}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (bake_quality_demo) {
      QJsonArray maps;
      for (const auto &set : document->texture_sets()) {
        QJsonArray planes;
        if (set.mesh_maps[0])
          for (const auto &[tile, plane] : set.mesh_maps[0]->tiles) {
            int zero = 0, full = 0, fractional = 0;
            for (std::size_t i = 3; i < plane.pixels->rgba.size(); i += 4) {
              const float alpha = plane.pixels->rgba[i];
              if (alpha == 0)
                ++zero;
              else if (alpha == 1)
                ++full;
              else
                ++fractional;
            }
            planes.append(QJsonObject{{"tile", tile},
                                      {"width", plane.resolution},
                                      {"height", plane.rows()},
                                      {"hash", QString::fromStdString(plane.hash)},
                                      {"zero", zero},
                                      {"full", full},
                                      {"fractional", fractional}});
          }
        maps.append(planes);
      }
      auto capture = sets_captures.last().toObject();
      capture["quality"] = QJsonObject{{"evidence", bake_quality_evidence}, {"maps", maps}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (selective_bake_demo) {
      auto capture = sets_captures.last().toObject();
      capture["selection_evidence"] = bake_selection_evidence;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (mesh_maps_demo) {
      QJsonArray statistics;
      for (const auto &set : document->texture_sets()) {
        QJsonArray maps;
        for (int role = 0; role < 7; ++role) {
          if (!set.mesh_maps[role]) {
            maps.append(QJsonValue());
            continue;
          }
          QJsonArray planes;
          for (const auto &[tile, plane] : set.mesh_maps[role]->tiles) {
            float low[3] = {1e9f, 1e9f, 1e9f}, high[3] = {-1e9f, -1e9f, -1e9f};
            int covered = 0;
            if (plane.pixels)
              for (std::size_t i = 0; i < plane.pixels->rgba.size(); i += 4)
                if (plane.pixels->rgba[i + 3] > 0) {
                  ++covered;
                  for (int c = 0; c < 3; ++c) {
                    low[c] = std::min(low[c], plane.pixels->rgba[i + c]);
                    high[c] = std::max(high[c], plane.pixels->rgba[i + c]);
                  }
                }
            planes.append(QJsonObject{{"tile", tile},
                                      {"resolution", plane.resolution},
                                      {"hash", QString::fromStdString(plane.hash)},
                                      {"covered", covered},
                                      {"low", QJsonArray{low[0], low[1], low[2]}},
                                      {"high", QJsonArray{high[0], high[1], high[2]}}});
          }
          maps.append(planes);
        }
        statistics.append(maps);
      }
      auto capture = sets_captures.last().toObject();
      capture["mesh_maps"] = statistics;
      capture["mesh_map_mode"] = viewport->mesh_map_active;
      capture["bake_defaults"] = bake_demo_defaults;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (resources_demo) {
      QJsonArray resources, visible, disk;
      for (const auto &asset : assets.assets()) {
        auto metadata = bts::asset_metadata(asset);
        metadata["scope"] = bts::asset_scope_name(asset.scope);
        metadata["bytes"] = int(asset.bytes.size());
        resources.append(metadata);
      }
      auto *list = workspace->findChild<QListWidget *>("shelfAssets");
      for (int i = 0; i < list->count(); ++i)
        visible.append(list->item(i)->text());
      bts::AssetCatalog loaded(assets.directory());
      loaded.load_user_library();
      for (const auto &asset : loaded.assets())
        if (asset.scope == bts::AssetScope::User)
          disk.append(bts::asset_metadata(asset));
      auto capture = sets_captures.last().toObject();
      capture["resources"] = QJsonObject{
          {"catalog", resources},
          {"visible", visible},
          {"scope", workspace->findChild<QComboBox *>("shelfLibraryScope")->currentText()},
          {"search", workspace->findChild<QLineEdit *>("shelfSearch")->text()},
          {"disk_user", disk},
          {"reset_enabled",
           workspace->findChild<QToolButton *>("resetShelfFilters")->isEnabled()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (asset_categories_demo) {
      QJsonArray categories;
      auto *list = workspace->findChild<QListWidget *>("shelfCategories");
      for (int i = 0; i < list->count(); ++i)
        categories.append(list->item(i)->text());
      auto capture = sets_captures.last().toObject();
      capture["asset_categories"] = QJsonObject{{"order", categories},
                                                {"selected", list->currentItem()->text()},
                                                {"brush_hardness", brush.hardness}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (fill_uv_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray hashes, frame;
      const int resolution = int(document->settings().resolution);
      if (layer.kind == bts::LayerKind::Fill && layer.fill.atom)
        for (int c = 0; c < 5; ++c) {
          std::uint64_t hash = 1469598103934665603ULL;
          for (int y = resolution / 64; y < resolution; y += resolution / 32)
            for (int x = resolution / 64; x < resolution; x += resolution / 32)
              for (float value : host.sample_texture(set.material_session_uid, c, x, y)) {
                hash ^= std::bit_cast<std::uint32_t>(value);
                hash *= 1099511628211ULL;
              }
          hashes.append(QString::number(hash, 16));
        }
      for (const auto uv : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}) {
        const auto p = bts::fill_uv_frame(layer.fill, uv.x(), uv.y());
        frame.append(QJsonArray{p[0], p[1]});
      }
      auto capture = sets_captures.last().toObject();
      capture["fill_uv"] = QJsonObject{
          {"hashes", hashes},
          {"frame", frame},
          {"panel_visible", workspace->findChild<QWidget *>("fillProjectionPanel")->isVisible()},
          {"scale_u", workspace->findChild<QDoubleSpinBox *>("fillScaleU")->value()},
          {"scale_v", workspace->findChild<QDoubleSpinBox *>("fillScaleV")->value()},
          {"scale_v_enabled", workspace->findChild<QDoubleSpinBox *>("fillScaleV")->isEnabled()},
          {"rotation", workspace->findChild<QDoubleSpinBox *>("fillUvRotation")->value()},
          {"offset_u", workspace->findChild<QDoubleSpinBox *>("fillOffset0")->value()},
          {"disclosure",
           workspace->findChild<QToolButton *>("fillTransformDisclosure")->isChecked()},
          {"paint_enabled", workspace->findChild<QToolButton *>("paintTool")->isEnabled()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (triplanar_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &p = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                  set.selected_layer)
                          .fill;
      QJsonArray offsets, rotations, scales;
      for (int i = 0; i < 3; ++i) {
        offsets.append(p.offset3[i]);
        rotations.append(p.rotation3[i]);
        scales.append(p.scale3[i]);
      }
      int valid = 0;
      double maximum_normal_error = 0;
      if (set.projection_geometry)
        for (const auto &[tile, points] : set.projection_geometry->tiles)
          for (const auto &point : points)
            if (point.valid) {
              ++valid;
              double length = 0;
              for (float value : point.normal)
                length += value * value;
              maximum_normal_error = std::max(maximum_normal_error, std::abs(length - 1));
            }
      auto capture = sets_captures.last().toObject();
      capture["triplanar"] = QJsonObject{
          {"mode", p.mode},
          {"hardness", p.hardness},
          {"offset3", offsets},
          {"rotation3", rotations},
          {"scale3", scales},
          {"gizmo", p.gizmo},
          {"manipulator", p.manipulator},
          {"uv_offset", p.offset[0]},
          {"geometry_valid", valid},
          {"maximum_normal_error", maximum_normal_error},
          {"expanded", workspace->findChild<QToolButton *>("fill3dDisclosure")->isChecked()},
          {"uv_offset_visible",
           workspace->findChild<QDoubleSpinBox *>("fillOffset0")->isVisible()}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (polygon_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &mask = *bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer)
                              .mask;
      const int resolution = int(document->settings().resolution);
      double sum = 0;
      int white = 0, count = 0;
      for (const auto &triangle : snapshot.uv_triangles)
        if (triangle.material == std::size_t(selected_material)) {
          QPointF center;
          for (const auto &uv : triangle.corners)
            center += QPointF(uv[0], uv[1]) / 3;
          const int u = int(std::floor(center.x())), v = int(std::floor(center.y()));
          const auto value = mask.value(int((center.x() - u) * resolution),
                                        int((center.y() - v) * resolution),
                                        1001 + u + 10 * v);
          sum += value;
          white += value > .99;
          ++count;
        }
      QJsonArray modes;
      for (int which = 0; which < 2; ++which) {
        int mode = -1;
        for (int i = 0; i < 5; ++i)
          if (workspace->findChild<QToolButton *>(QString("polygonMode%1_%2").arg(which).arg(i))
                  ->isChecked())
            mode = i;
        modes.append(mode);
      }
      auto capture = sets_captures.last().toObject();
      capture["polygon"] = QJsonObject{
          {"active", viewport->polygon_active},
          {"mode", int(viewport->polygon_mode)},
          {"modes", modes},
          {"grayscale", viewport->polygon_value},
          {"grayscale_toolbar",
           workspace->findChild<QDoubleSpinBox *>("polygonGrayscale0")->value()},
          {"grayscale_properties",
           workspace->findChild<QDoubleSpinBox *>("polygonGrayscale1")->value()},
          {"picker_enabled",
           workspace->findChild<QToolButton *>("materialPickerTool")->isEnabled()},
          {"brush_visible", workspace->findChild<QDoubleSpinBox *>("brushSize")->isVisible()},
          {"white", white},
          {"sum", sum},
          {"triangles", count}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (picker_demo) {
      const auto values = [](const bts::MaterialValues &v) {
        return QJsonArray{v.base_color[0],
                          v.base_color[1],
                          v.base_color[2],
                          v.base_color[3],
                          v.metallic,
                          v.roughness,
                          v.normal[0],
                          v.normal[1],
                          v.normal[2],
                          v.bump};
      };
      auto capture = sets_captures.last().toObject();
      capture["picker_active"] = viewport->picker_active;
      capture["picker_brush"] = values(brush.values);
      if (viewport->last_picker_uv) {
        const auto uv = *viewport->last_picker_uv;
        const auto &set = document->set(snapshot.materials[selected_material].session_id);
        const int resolution = int(document->settings().resolution),
                  column = int(std::floor(uv.x())), row = int(std::floor(uv.y()));
        capture["picker_expected"] = values(
            bts::TextureDocument::evaluate(set,
                                           int((uv.x() - column) * resolution),
                                           int((uv.y() - row) * resolution),
                                           1001 + column + 10 * row)
                .material);
        capture["picker_uv"] = QJsonArray{uv.x(), uv.y()};
      }
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (lazy_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      QJsonArray samples;
      for (const auto uv : {QPointF(.2, .2), QPointF(.4, .2), QPointF(.54, .2), QPointF(.62, .2)})
        samples.append(
            layer.pixels.sample(2, int(uv.x() * resolution), int(uv.y() * resolution))[3]);
      const auto anchor = viewport->stroke_anchor;
      auto capture = sets_captures.last().toObject();
      capture["lazy_samples"] = samples;
      capture["lazy_anchor"] = anchor ? QJsonValue(QJsonArray{anchor->uv.x(), anchor->uv.y()}) :
                                        QJsonValue(QJsonValue::Null);
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (straight_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      QJsonArray samples;
      for (const auto uv : {QPointF(.2, .75),
                            QPointF(.375, .7),
                            QPointF(.55, .475),
                            QPointF(.375, .7989637),
                            QPointF(.55, .6979274),
                            QPointF(.55, .676),
                            QPointF(.8, .8),
                            QPointF(.7, .4)})
        samples.append(
            layer.pixels.sample(2, int(uv.x() * resolution), int(uv.y() * resolution))[3]);
      const auto anchor = viewport->stroke_anchor;
      auto capture = sets_captures.last().toObject();
      capture["line_samples"] = samples;
      capture["line_anchor"] = anchor ? QJsonValue(QJsonArray{anchor->uv.x(), anchor->uv.y()}) :
                                        QJsonValue(QJsonValue::Null);
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (symmetry_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      double sum = 0;
      int covered = 0;
      for (int y = 0; y < resolution; y += 2)
        for (int x = 0; x < resolution; x += 2) {
          const auto alpha = layer.pixels.sample(2, x, y)[3];
          sum += alpha;
          covered += alpha > 0;
        }
      auto capture = sets_captures.last().toObject();
      capture["symmetry_pixels"] = QJsonObject{{"sum", sum}, {"covered", covered}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (projection_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      double sum = 0;
      int count = 0;
      for (int y = 0; y < resolution; ++y)
        for (int x = 0; x < resolution; ++x) {
          const auto alpha = layer.pixels.sample(2, x, y)[3];
          sum += alpha;
          count += alpha > 0;
        }
      QJsonArray uv_samples;
      const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
      std::optional<bts::PaintPixels::Stamp> contact;
      for (const auto &pane : viewport->visible_panes())
        if (pane.kind == bts::ViewKind::Scene)
          contact = viewport->surface_at(
              pane, {rect.left() + rect.width() * .46, rect.top() + rect.height() * .37});
      if (contact)
        for (auto offset : {QPointF(0, 0), QPointF(.5, 0), QPointF(.5, .5)}) {
          const double u = contact->center[0] + offset.x() * contact->axis_x[0] +
                           offset.y() * contact->axis_y[0],
                       v = contact->center[1] + offset.x() * contact->axis_x[1] +
                           offset.y() * contact->axis_y[1];
          uv_samples.append(layer.pixels.sample(2, int(u * resolution), int(v * resolution))[3]);
        }
      auto capture = sets_captures.last().toObject();
      capture["projection_statistics"] = QJsonObject{
          {"sum", sum}, {"covered", count}, {"uv_samples", uv_samples}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (padding_demo) {
      const auto uid = snapshot.materials[1].session_id;
      QJsonArray samples;
      for (int x : {459, 460, 461, 462, 470, 564}) {
        QJsonArray channels;
        for (int c = 0; c < 5; ++c) {
          QJsonArray values;
          for (float v : host.sample_texture(uid, c, x, 512))
            values.append(v);
          channels.append(values);
        }
        samples.append(QJsonObject{{"x", x}, {"channels", channels}});
      }
      auto capture = sets_captures.last().toObject();
      capture["padding_samples"] = samples;
      capture["padding_control"] =
          workspace->findChild<QComboBox *>("uvPaddingMode")->currentText();
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (alpha_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      QJsonArray samples;
      for (QPointF uv : {QPointF(.78, .6),
                         QPointF(.9, .72),
                         QPointF(.8, .2),
                         QPointF(.25, .2),
                         QPointF(.2, .16)})
        samples.append(
            layer.pixels.sample(2, int(uv.x() * resolution), int(uv.y() * resolution))[3]);
      auto capture = sets_captures.last().toObject();
      capture["alpha_samples"] = samples;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (erase_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      std::array<double, 5> sums{};
      double error = 0;
      for (int y = 0; y < resolution; y += 4)
        for (int x = 0; x < resolution; x += 4) {
          for (int c = 0; c < 5; ++c)
            sums[c] += layer.pixels.sample(c, x, y)[3];
          const float alpha = layer.pixels.sample(2, x, y)[3];
          error = std::max(error,
                           std::abs(bts::TextureDocument::composite(set, x, y).material.roughness -
                                    (.3 + .7 * alpha)));
        }
      auto capture = sets_captures.last().toObject();
      QJsonArray values;
      for (double sum : sums)
        values.append(sum);
      capture["erase_statistics"] = QJsonObject{{"coverage_sums", values},
                                                {"underlying_fill_error", error}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (mask_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      double sum = 0, max_error = 0;
      int covered = 0;
      for (int y = 0; y < resolution; y += 4)
        for (int x = 0; x < resolution; x += 4) {
          const float value = layer.mask ? layer.mask->value(x, y) : 1;
          sum += value;
          covered += value > 0;
          const float evaluated = layer.mask && !layer.mask->enabled ? 1 : value;
          const auto result = bts::TextureDocument::composite(set, x, y);
          max_error = std::max(max_error, std::abs(double(result.material.metallic - evaluated)));
          for (int c = 0; c < 3; ++c)
            max_error = std::max(
                max_error,
                std::abs(double(result.material.base_color[c] - (.2 + .6 * evaluated))));
        }
      auto capture = sets_captures.last().toObject();
      capture["mask_statistics"] = QJsonObject{
          {"covered", covered}, {"sum", sum}, {"coverage_channel_error", max_error}};
      if (isolate_demo && host.inspection_mode() == bts::InspectionMode::Mask)
        capture["mask_image_hash"] = QString::number(
            host.texture_hash(set.material_session_uid, 15), 16);
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (blend_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const int resolution = int(document->settings().resolution);
      const auto value = bts::TextureDocument::composite(set, resolution / 2, resolution / 2),
                 outside = bts::TextureDocument::composite(set, 0, 0);
      auto capture = sets_captures.last().toObject();
      const auto uv = viewport->uv_point({.5, .5}) * viewport->devicePixelRatioF();
      capture["channel_evaluation"] = QJsonObject{
          {"roughness", value.authored.roughness},
          {"coverage", value.coverage[2]},
          {"outside_coverage", outside.coverage[2]},
          {"color",
           QJsonArray{value.material.base_color[0],
                      value.material.base_color[1],
                      value.material.base_color[2]}},
          {"uv_sample", QJsonArray{qRound(uv.x()), qRound(uv.y())}}};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (layer_drag_demo) {
      auto capture = sets_captures.last().toObject();
      capture["drop_indicator_row"] =
          static_cast<bts::LayerStackList *>(workspace->layer_stack())->insertion_row();
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (layer_opacity_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto value = bts::TextureDocument::composite(set).material;
      auto capture = sets_captures.last().toObject();
      capture["composite"] = QJsonObject{
          {"bump", value.bump},
          {"roughness", value.roughness},
          {"color", QJsonArray{value.base_color[0], value.base_color[1], value.base_color[2]}}};
      QJsonArray opacities;
      for (int row = 0; row < workspace->layer_stack()->count(); ++row)
        opacities.append(workspace->layer_stack()->item(row)->data(Qt::UserRole + 1).toDouble());
      capture["row_opacities"] = opacities;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (udim_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      QJsonArray samples;
      for (int number : set.tiles) {
        QJsonArray channels;
        for (int c = 0; c < 5; ++c) {
          const auto p = layer.pixels.sample(
              c, number == 1001 ? resolution - 1 : 0, resolution / 2, number);
          channels.append(QJsonArray{p[0], p[1], p[2], p[3]});
        }
        samples.append(QJsonObject{{"number", number}, {"seam", channels}});
      }
      auto capture = sets_captures.last().toObject();
      capture["tile_samples"] = samples;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (surface_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      QJsonArray islands;
      for (int island = 0; island < 6; ++island) {
        const double u = .03 + .33 * (island % 3), v = island < 3 ? .03 : .52;
        QJsonArray channels;
        for (int c = 0; c < 5; ++c) {
          int covered = 0;
          double sum = 0;
          for (int y = int(v * resolution) + 1; y < int((v + .42) * resolution); ++y)
            for (int x = int(u * resolution) + 1; x < int((u + .27) * resolution); ++x) {
              const auto a = layer.pixels.sample(c, x, y)[3];
              covered += a > 0;
              sum += a;
            }
          channels.append(QJsonObject{{"covered", covered}, {"sum", sum}});
        }
        islands.append(channels);
      }
      // Interior base points hidden by the narrow occluder and the folded sheet.
      QJsonArray hidden;
      for (double world_x : {-.20, .10}) {
        const int x = int((.03 + (world_x + .60) / .75 * .27) * resolution),
                  y = int(.24 * resolution);
        hidden.append(layer.pixels.sample(2, x, y)[3]);
      }
      auto capture = sets_captures.last().toObject();
      capture["islands"] = islands;
      capture["hidden_base_alpha"] = hidden;
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (pressure_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int resolution = int(document->settings().resolution);
      int covered = 0;
      double sum = 0;
      float peak = 0;
      for (int y = 0; y < resolution; ++y)
        for (int x = 0; x < resolution; ++x) {
          const float alpha = layer.pixels.sample(2, x, y)[3];
          covered += alpha > 0;
          sum += alpha;
          peak = std::max(peak, alpha);
        }
      auto capture = sets_captures.last().toObject();
      capture["pressure_pixels"] = QJsonObject{{"covered", covered}, {"sum", sum}, {"peak", peak}};
      capture["input_source"] = "synthetic Qt tablet events; physical device unverified";
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    if (opacity_demo) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      const int center = int(document->settings().resolution) / 2;
      const auto pixel = layer.pixels.sample(2, center, center);
      auto capture = sets_captures.last().toObject();
      capture["center_roughness_pixel"] = QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]};
      sets_captures.replace(sets_captures.size() - 1, capture);
    }
    ++sets_stage;
    if (sets_stage == sets_stages.size()) {
      sets_demo.stop();
      QFile file(directory + (mask_demo          ? "/masks.json" :
                              replace_demo       ? "/replace.json" :
                              blend_demo         ? "/blending.json" :
                              selection_demo     ? "/selection.json" :
                              folder_demo        ? "/folders.json" :
                              layer_drag_demo    ? "/layer-drag.json" :
                              layer_opacity_demo ? "/layer-opacity.json" :
                              layer_context_demo ? "/layer-context.json" :
                              alignment_demo     ? "/alignment.json" :
                              resource_demo      ? "/resource.json" :
                              appearance_demo    ? "/appearance.json" :
                              environment_demo   ? "/environment.json" :
                              udim_demo          ? "/udim.json" :
                              surface_demo       ? "/surface.json" :
                              pressure_demo      ? "/pressure.json" :
                              spacing_demo       ? "/spacing.json" :
                              opacity_demo       ? "/opacity.json" :
                              flow_demo          ? "/flow.json" :
                              paint_demo         ? "/paint.json" :
                              inspection_demo    ? "/inspection.json" :
                              fill_demo          ? "/fill.json" :
                              color_demo         ? "/color.json" :
                              channels_demo      ? "/channels.json" :
                                                   "/texture-sets.json"));
      if (!file.open(QIODevice::WriteOnly))
        qFatal("Cannot write material demo result");
      file.write(
          QJsonDocument(QJsonObject{{"pid", app.applicationPid()}, {"captures", sets_captures}})
              .toJson());
      QTimer::singleShot(300, &app, &QCoreApplication::quit);
    }
    else if (smart_material_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto paint_id = [&] {
        const auto &set = document->set(snapshot.materials[selected_material].session_id);
        for (const auto &layer : set.layers)
          if (layer.name == "Paint")
            return layer.id;
        return QUuid{};
      };
      const auto smart_asset = [&]() -> const bts::StudioAsset & {
        for (const auto &asset : assets.assets())
          if (asset.smart_material)
            return asset;
        throw std::runtime_error("Smart preset missing");
      };
      const auto shelf_apply = [&] {
        auto *categories = workspace->findChild<QListWidget *>("shelfCategories");
        const auto found = categories->findItems("Material presets", Qt::MatchExactly);
        categories->setCurrentItem(found.front());
        auto *list = workspace->findChild<QListWidget *>("shelfAssets");
        const auto items = list->findItems("Worn Painted Metal", Qt::MatchExactly);
        if (items.isEmpty())
          qFatal("Material preset resource missing");
        const auto point = list->visualItemRect(items.front()).center();
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonDblClick,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
      };
      const auto user_asset = [&](const QString &name) -> const bts::StudioAsset & {
        for (const auto &asset : assets.assets())
          if (asset.material_preset && asset.name == name)
            return asset;
        throw std::runtime_error("User material preset missing");
      };
      const auto select_layer = [&](QUuid id) {
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == id &&
              list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
          {
            list->setCurrentRow(row);
            return;
          }
        throw std::runtime_error("User material layer row missing");
      };
      const auto user_shelf = [&](const QString &name) {
        auto *categories = workspace->findChild<QListWidget *>("shelfCategories");
        categories->setCurrentItem(
            categories->findItems("Material presets", Qt::MatchExactly).first());
        workspace->refresh_alpha_shelf();
        auto *list = workspace->findChild<QListWidget *>("shelfAssets");
        const auto matches = list->findItems(name, Qt::MatchExactly);
        if (matches.isEmpty())
          throw std::runtime_error("User smart-material Shelf row missing");
        list->setCurrentItem(matches.first());
        return list;
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("smart-material-demo"));
          workspace->texture_sets()->setCurrentRow(1);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 430, 270, 0});
          break;
        case 2:
          shelf_apply();
          break;
        case 3:
          undo();
          break;
        case 4:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 5: {
          auto *list = workspace->layer_stack();
          for (int row = 0; row < list->count(); ++row)
            if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == paint_id()) {
              list->setCurrentRow(row);
              break;
            }
          break;
        }
        case 6:
          workspace->roughness_control()->setValue(.73);
          break;
        case 7:
          undo();
          break;
        case 8:
          send_key(*viewport, Qt::Key_B);
          break;
        case 9: {
          auto *list = workspace->layer_stack();
          for (int row = 0; row < list->count(); ++row)
            if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == paint_id()) {
              auto *item = list->item(row);
              list->scrollToItem(item);
              const auto point = bts::layer_mask_rect(list->indexFromItem(item),
                                                      list->visualItemRect(item))
                                     .center();
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonPress,
                               point,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::AltModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonRelease,
                               point,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::AltModifier);
              break;
            }
          break;
        }
        case 10:
          send_key(*viewport, Qt::Key_M);
          break;
        case 11:
          workspace->findChild<QAction *>("applyWornPaintedMetal")->trigger();
          break;
        case 12:
          undo();
          break;
        case 13:
          save_document(directory + "/smart-material.blend");
          break;
        case 14:
          open_document(directory + "/smart-material.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 15: {
          workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->setValue(.25);
          const auto point = viewport->uv_point({.35, .45});
          send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 16:
          undo();
          break;
        case 17: {
          auto *list = workspace->layer_stack();
          QMimeData mime;
          mime.setData("application/x-bts-asset", smart_asset().id.toString().toUtf8());
          const QPoint point(100, list->visualItemRect(list->item(0)).top() + 2);
          QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(list->viewport(), &enter);
          QDragMoveEvent move(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(list->viewport(), &move);
          QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(list->viewport(), &drop);
          if (!drop.isAccepted())
            qFatal("Material preset drop rejected");
          break;
        }
        case 18:
          undo();
          break;
        case 19: {
          auto &set = document->set(snapshot.materials[selected_material].session_id);
          for (const auto &layer : set.layers)
            if (layer.kind == bts::LayerKind::Folder && layer.name == "Worn Painted Metal") {
              smart_user_folder = layer.id;
              break;
            }
          select_layer(smart_user_folder);
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          smart_user_bitmap =
              document->set(snapshot.materials[selected_material].session_id).selected_layer;
          workspace->roughness_control()->setValue(.24);
          bts::select_gui_test_file(workspace,
                                    "fillTextureFileDialog",
                                    qEnvironmentVariable("BTS_SMART_MATERIAL_SOURCE"));
          workspace->findChild<QComboBox *>("fillBaseColorResource")->setCurrentIndex(2);
          break;
        }
        case 20: {
          select_layer(smart_user_bitmap);
          workspace->findChild<QToolButton *>("addPaintLayer")->click();
          brush = bts::BrushSettings{};
          brush.diameter = 8;
          brush.hardness = 1;
          select_material(selected_material);
          viewport->repaint();
          const auto point = viewport->uv_point({.35, .5});
          send_drag(*viewport, point, {45, 0}, Qt::LeftButton, Qt::NoModifier);
          const auto paint_layer =
              document->set(snapshot.materials[selected_material].session_id).selected_layer;
          auto *rows = workspace->layer_stack();
          {
            QSignalBlocker block(rows);
            rows->clearSelection();
            for (int row = 0; row < rows->count(); ++row) {
              auto *item = rows->item(row);
              const QUuid id(item->data(Qt::UserRole).toString());
              if (item->data(Qt::UserRole + 10).toString().isEmpty() &&
                  (id == smart_user_folder || id == smart_user_bitmap || id == paint_layer))
                item->setSelected(true);
            }
          }
          workspace->findChild<QAction *>("groupLayers")->trigger();
          smart_user_folder =
              document->set(snapshot.materials[selected_material].session_id).selected_layer;
          select_layer(smart_user_folder);
          QTimer::singleShot(100, workspace, [&] {
            auto *dialog = window->findChild<QInputDialog *>("smartMaterialNameDialog");
            if (!dialog)
              qFatal("Smart-material naming dialog missing");
            dialog->setTextValue("Own metal fixture");
            dialog->accept();
          });
          auto *list = workspace->layer_stack();
          const auto rect = list->visualRect(list->currentIndex());
          const QPoint point_menu(rect.right() - 12, rect.center().y());
          QContextMenuEvent event(
              QContextMenuEvent::Mouse, point_menu, list->viewport()->mapToGlobal(point_menu));
          QApplication::sendEvent(list->viewport(), &event);
          auto *action = workspace->findChild<QAction *>("createSmartMaterial");
          if (!action)
            throw std::runtime_error("Folder context action missing");
          action->trigger();
          for (auto *menu : workspace->findChildren<QMenu *>())
            if (menu->isVisible())
              menu->close();
          break;
        }
        case 21: {
          auto *list = user_shelf("Own metal fixture");
          const auto rect = list->visualItemRect(list->currentItem());
          QMetaObject::invokeMethod(list,
                                    "customContextMenuRequested",
                                    Qt::DirectConnection,
                                    Q_ARG(QPoint, rect.center()));
          bts::select_gui_test_file(
              workspace, "presetExportFileDialog", directory + "/own-material.btsmaterial");
          auto *action = workspace->findChild<QAction *>("exportResourcePreset");
          if (!action)
            throw std::runtime_error("Preset export context action missing");
          action->trigger();
          for (auto *menu : workspace->findChildren<QMenu *>())
            if (menu->isVisible())
              menu->close();
          break;
        }
        case 22:
          open_document(harness.value("smart-material-demo"));
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 23: {
          const auto path = qEnvironmentVariable("BTS_SMART_MATERIAL_SOURCE");
          if (!QFile::rename(path, path + ".unavailable"))
            throw std::runtime_error("Cannot retire own test source image");
          QTimer::singleShot(100, workspace, [&, directory] {
            auto *dialog = workspace->findChild<QDialog *>("resourceImportDialog");
            if (!dialog)
              qFatal("Preset import dialog missing");
            dialog->findChild<QLineEdit *>("resourceImportPath")
                ->setText(directory + "/own-material.btsmaterial");
            dialog->findChild<QLineEdit *>("resourceImportName")
                ->setText("Imported metal fixture");
            dialog->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(2);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
          });
          workspace->findChild<QToolButton *>("importResources")->click();
          break;
        }
        case 24: {
          auto *list = user_shelf("Imported metal fixture");
          QMetaObject::invokeMethod(list,
                                    "itemDoubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QListWidgetItem *, list->currentItem()));
          break;
        }
        case 25:
          undo();
          break;
        case 26:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 27:
          save_document(directory + "/user-material.blend");
          break;
        case 28:
          open_document(directory + "/user-material.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 29: {
          const auto &set = document->set(snapshot.materials[selected_material].session_id);
          for (const auto &layer : set.layers)
            if (layer.kind == bts::LayerKind::Fill && layer.fill.texture) {
              select_layer(layer.id);
              break;
            }
          workspace->roughness_control()->setValue(.81);
          break;
        }
        case 30:
          undo();
          break;
        case 31: {
          auto *list = workspace->layer_stack();
          QMimeData mime;
          mime.setData("application/x-bts-asset",
                       user_asset("Imported metal fixture").id.toString().toUtf8());
          const QPoint point(100, list->visualItemRect(list->item(0)).top() + 2);
          QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(list->viewport(), &enter);
          QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(list->viewport(), &drop);
          if (!drop.isAccepted())
            throw std::runtime_error("User preset drop rejected");
          break;
        }
        case 32:
          undo();
          break;
        case 33: {
          const auto id = user_asset("Imported metal fixture").id;
          for (auto *action : workspace->findChildren<QAction *>("applyUserSmartMaterial"))
            if (QUuid(action->data().toString()) == id) {
              action->trigger();
              break;
            }
          break;
        }
        case 34:
          undo();
          break;
        case 35:
          edit_material([](bts::TextureSet &set) { set.mesh_maps = {}; });
          break;
        case 36: {
          if (!workspace->findChild<QToolButton *>("addSmartMaterial")->isEnabled())
            throw std::runtime_error("User smart-material selector disabled without mesh maps");
          const auto id = user_asset("Imported metal fixture").id;
          for (auto *action : workspace->findChildren<QAction *>("applyUserSmartMaterial"))
            if (QUuid(action->data().toString()) == id) {
              action->trigger();
              break;
            }
          break;
        }
      }
    }
    else if (editable_path_demo) {
      const auto source = [&]() {
        const auto path = selected_path_definition();
        if (!path)
          qFatal("Editable path has no selection");
        return path;
      };
      const auto screen = [&](bts::PathVector point) {
        return viewport->scene_point({float(point[0]), float(point[1]), float(point[2])});
      };
      const auto select_first = [&] {
        auto *list = viewport->paths_panel->findChild<QListWidget *>("pathsList");
        if (!list->count())
          qFatal("Editable path fixture has no path");
        list->setCurrentRow(0);
      };
      const auto vertex = [&](int index) {
        QApplication::processEvents();
        viewport->repaint();
        send_drag(*viewport,
                  screen(source()->points[index].position),
                  {0, 0},
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      const auto move = [&](int index, bts::PathVector destination) {
        QApplication::processEvents();
        viewport->repaint();
        const auto start = screen(source()->points[index].position), end = screen(destination);
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      const auto handle =
          [&](int which, bts::PathVector offset, Qt::KeyboardModifiers modifiers, bool scale) {
            const auto path = source();
            const int index = viewport->selected_vertex;
            if (index < 0)
              qFatal("No selected path vertex for tangent demo");
            const auto handles = bts::surface_path_handles(*path, index);
            auto a = path->points[index].position, b = a;
            for (int c = 0; c < 3; ++c) {
              a[c] += handles[which][c];
              b[c] += scale ? handles[which][c] * 1.3 : handles[which][c] + offset[c];
            }
            QApplication::processEvents();
            viewport->repaint();
            const auto start = screen(a), end = screen(b);
            send_drag(*viewport, start, end - start, Qt::LeftButton, modifiers);
          };
      switch (sets_stage) {
        case 1: {
          open_document(harness.value("editable-path-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          workspace->findChild<QAction *>("selectSurfacePath")->trigger();
          auto *list = viewport->paths_panel->findChild<QListWidget *>("pathsList");
          if (list->count() > 1) {
            list->setCurrentRow(1);
            viewport->paths_panel->findChild<QPushButton *>("deleteSurfacePath")->click();
          }
          select_first();
          auto *pressure = workspace->findChild<QToolButton *>("brushPressureSize");
          if (!pressure->isChecked())
            pressure->click();
          break;
        }
        case 2:
          vertex(1);
          break;
        case 3:
          move(1, {.05, 0, .15});
          break;
        case 4:
        case 17:
        case 23:
        case 28:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 5:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 6: {
          QApplication::processEvents();
          viewport->repaint();
          const auto start = screen(source()->points[1].position), end = screen({.25, 0, .3});
          send_mouse_event(*viewport,
                           QEvent::MouseButtonPress,
                           start,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(
              *viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
          send_key(*viewport, Qt::Key_Escape);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonRelease,
                           end,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 7: {
          const auto point = screen(source()->points[1].position);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonDblClick,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 8:
        case 26:
          viewport->path_curve_panel->findChild<QToolButton *>("surfaceCustomTangents")->click();
          break;
        case 9:
          handle(1, {0, 0, .2}, Qt::NoModifier, false);
          break;
        case 10:
          handle(0, {-.1, 0, .1}, Qt::AltModifier, false);
          break;
        case 11:
          handle(1, {}, Qt::ControlModifier, true);
          break;
        case 12: {
          const auto point = bts::surface_path_at(*source(), 0, .45);
          QApplication::processEvents();
          viewport->repaint();
          send_drag(*viewport, screen(point.position), {0, 0}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 13:
          viewport->path_curve_panel->findChild<QDoubleSpinBox *>("surfaceVertexPressure")
              ->setValue(50);
          break;
        case 14:
          viewport->path_curve_panel->findChild<QToolButton *>("reverseSurfacePath")->click();
          break;
        case 15:
          viewport->path_curve_panel->findChild<QToolButton *>("closeSurfacePath")->click();
          break;
        case 16:
          viewport->path_curve_panel->findChild<QToolButton *>("deleteSurfaceVertex")->click();
          break;
        case 18: {
          QTimer::singleShot(0, workspace, [&] {
            auto *dialog = workspace->findChild<QDialog *>("pathDisplayDialog");
            if (!dialog)
              qFatal("Path display dialog unavailable");
            dialog->findChild<QDoubleSpinBox *>("pathDisplayHandleSize")->setValue(10);
            dialog->findChild<QDoubleSpinBox *>("pathDisplayLineWidth")->setValue(2);
            dialog->findChild<QCheckBox *>("pathDisplayNormals")->setChecked(true);
            dialog->findChild<QPushButton *>("closePathDisplay")->click();
          });
          sets_demo.stop();
          viewport->path_curve_panel->findChild<QToolButton *>("surfacePathDisplay")->click();
          sets_demo.start(600);
          break;
        }
        case 19:
          save_document(directory + "/editable-path.blend");
          break;
        case 20:
          open_document(directory + "/editable-path.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 21:
        case 25:
          workspace->findChild<QAction *>("selectSurfacePath")->trigger();
          select_first();
          vertex(1);
          break;
        case 22:
          move(1, {.2, 0, .25});
          break;
        case 24: {
          open_document(QFileInfo(harness.value("editable-path-demo")).absolutePath() +
                        "/sphere-path.blend");
          int row = -1;
          for (int i = 0; i < int(snapshot.materials.size()); ++i)
            if (snapshot.materials[i].name == "SphereSurface")
              row = i;
          if (row < 0)
            qFatal("Editable sphere path fixture unavailable");
          workspace->texture_sets()->setCurrentRow(row);
          break;
        }
        case 27: {
          QApplication::processEvents();
          viewport->repaint();
          const auto start = screen(source()->points[1].position);
          send_drag(*viewport, start, {-15, -20}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
      }
    }
    else if (surface_path_demo) {
      const auto click = [&](QVector3D point) {
        QApplication::processEvents();
        viewport->repaint();
        send_drag(*viewport, viewport->scene_point(point), {0, 0}, Qt::LeftButton, Qt::NoModifier);
      };
      const auto select_first = [&] {
        auto *list = viewport->paths_panel->findChild<QListWidget *>("pathsList");
        if (!list->count())
          qFatal("Surface path panel is empty");
        list->setCurrentRow(0);
      };
      const auto marker = [&] {
        brush.channels = {true, false, true, false, false};
        brush.values.base_color = {1, 1, 1, 1};
        brush.values.roughness = .8;
        brush.diameter = 12;
        brush.spacing = 100;
        brush.stroke_opacity = 100;
        brush.pressure_size = false;
        brush.alpha.kind = bts::AlphaKind::Plus;
        select_material(selected_material);
        QApplication::processEvents();
        viewport->repaint();
        send_drag(
            *viewport, viewport->uv_point({.15, .85}), {0, 0}, Qt::LeftButton, Qt::NoModifier);
        brush.diameter = 10;
        brush.spacing = 20;
        brush.stroke_opacity = 50;
        brush.alpha.kind = bts::AlphaKind::Shape;
        select_material(selected_material);
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("surface-path-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          if (snapshot.materials[0].name != "A_ProjectionSurface")
            qFatal("Path plane fixture binding changed");
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
            fill.values.base_color = {.08, .1, .18, 1};
            fill.values.roughness = .3;
            bts::TextureDocument::add_paint(set);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::Material);
          marker();
          break;
        }
        case 2:
        case 16:
          workspace->findChild<QAction *>("selectSurfacePath")->trigger();
          break;
        case 3:
          click({-.65f, 0, -.35f});
          break;
        case 4:
          click({.15f, 0, -.35f});
          break;
        case 5:
          click({.65f, 0, 0});
          break;
        case 6:
          send_key(*viewport, Qt::Key_Return);
          break;
        case 7: {
          QApplication::processEvents();
          viewport->repaint();
          send_drag(
              *viewport, viewport->uv_point({.3, .7}), {0, 0}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 8: {
          select_first();
          viewport->paths_panel->findChild<QListWidget *>("pathsList")
              ->currentItem()
              ->setText("Seam trace");
          break;
        }
        case 9:
        case 10: {
          auto *list = viewport->paths_panel->findChild<QListWidget *>("pathsList");
          list->item(0)->setCheckState(sets_stage == 9 ? Qt::Unchecked : Qt::Checked);
          break;
        }
        case 11:
          viewport->paths_panel->findChild<QPushButton *>("duplicateSurfacePath")->click();
          break;
        case 12:
          viewport->paths_panel->findChild<QPushButton *>("deleteSurfacePath")->click();
          break;
        case 13:
        case 19:
        case 27:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 14:
          save_document(directory + "/surface-path.blend");
          break;
        case 15:
          open_document(directory + "/surface-path.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 17:
          select_first();
          break;
        case 18:
          click({.75f, 0, .35f});
          break;
        case 20:
          viewport->path_finished();
          workspace->findChild<QAction *>("addPaintEffect")->trigger();
          break;
        case 21:
          click({-.6f, 0, .3f});
          click({.1f, 0, .3f});
          break;
        case 22:
          click({.65f, 0, .55f});
          break;
        case 23:
          save_document(directory + "/effect-path.blend");
          break;
        case 24:
          open_document(directory + "/effect-path.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 25:
          workspace->findChild<QAction *>("selectSurfacePath")->trigger();
          select_first();
          break;
        case 26:
          click({.75f, 0, -.6f});
          break;
        case 28: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("new-mesh").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          int row = -1;
          for (int i = 0; i < int(snapshot.materials.size()); ++i)
            if (snapshot.materials[i].name == "SphereSurface")
              row = i;
          if (row < 0)
            qFatal("Sphere path fixture unavailable");
          workspace->texture_sets()->setCurrentRow(row);
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
            fill.values.base_color = {.08, .1, .18, 1};
            bts::TextureDocument::add_paint(set);
          });
          send_key(*viewport, Qt::Key_F);
          viewport->focus_uv({.5, .5}, 1.2);
          marker();
          break;
        }
        case 29: {
          workspace->findChild<QAction *>("selectSurfacePath")->trigger();
          QApplication::processEvents();
          viewport->repaint();
          const auto pane = viewport->pane_rect(bts::ViewKind::Scene);
          const auto a = QPointF(pane.left() + pane.width() * .36,
                                 pane.top() + pane.height() * .5),
                     b = QPointF(pane.left() + pane.width() * .48,
                                 pane.top() + pane.height() * .5);
          send_drag(*viewport, a, {0, 0}, Qt::LeftButton, Qt::NoModifier);
          send_drag(*viewport, b, {0, 0}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 30:
          save_document(directory + "/sphere-path.blend");
          break;
        case 31:
          open_document(directory + "/sphere-path.blend");
          break;
      }
    }
    else if (dynamic_demo) {
      const auto uv = [&](double row) {
        QApplication::processEvents();
        viewport->repaint();
        const auto a = viewport->uv_point({.1, row}), b = viewport->uv_point({.9, row});
        send_drag(*viewport, a, b - a, Qt::LeftButton, Qt::NoModifier);
      };
      const auto resource = [&] {
        workspace->findChild<QToolButton *>("resetShelfFilters")->click();
        workspace->findChild<QLineEdit *>("shelfSearch")->setText("dynamicstroke");
        auto *list = workspace->findChild<QListWidget *>("shelfAssets");
        const auto found = list->findItems("Square To Circle", Qt::MatchExactly);
        if (found.size() != 1 || !found[0]->flags().testFlag(Qt::ItemIsEnabled))
          qFatal("Dynamic alpha resource unavailable");
        list->setCurrentItem(found[0]);
        const auto point = list->visualItemRect(found[0]).center();
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonDblClick,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        workspace->findChild<QScrollArea *>("propertiesScroll")
            ->ensureWidgetVisible(workspace->findChild<QToolButton *>("dynamicStrokeSection"));
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("dynamic-stroke-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          if (snapshot.materials[0].name != "A_ProjectionSurface")
            qFatal("Dynamic fixture binding changed");
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
            fill.values.base_color = {.08, .1, .18, 1};
            fill.values.roughness = .3;
            bts::TextureDocument::add_paint(set);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, true, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.values.roughness = .8;
          brush.diameter = 12;
          brush.spacing = 100;
          brush.pressure_size = false;
          brush.alpha.kind = bts::AlphaKind::Plus;
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::Material);
          QApplication::processEvents();
          viewport->repaint();
          send_drag(
              *viewport, viewport->uv_point({.15, .85}), {0, 0}, Qt::LeftButton, Qt::NoModifier);
          brush.diameter = 15;
          select_material(0);
          break;
        }
        case 2:
        case 14:
          resource();
          break;
        case 3:
          workspace->findChild<QToolButton *>("dynamicStrokeSection")->click();
          break;
        case 4:
        case 15:
          workspace->findChild<QSpinBox *>("dynamicStampCycle")->setValue(4);
          brush.spacing = 100;
          brush.diameter = 15;
          select_material(0);
          break;
        case 5:
          uv(.2);
          break;
        case 6:
          workspace->findChild<QComboBox *>("dynamicStampStart")->setCurrentIndex(1);
          break;
        case 7:
          uv(.4);
          break;
        case 8:
        case 17:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 9:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 10:
          workspace->findChild<QDoubleSpinBox *>("dynamicRoundness")->setValue(.25);
          workspace->findChild<QDoubleSpinBox *>("dynamicWidth")->setValue(.2);
          break;
        case 11: {
          QApplication::processEvents();
          viewport->repaint();
          const auto a = viewport->scene_point({-.7f, 0, .35f}),
                     b = viewport->scene_point({.7f, 0, .35f});
          send_drag(*viewport, a, b - a, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 12:
          save_document(directory + "/dynamic-stroke.blend");
          break;
        case 13:
          open_document(directory + "/dynamic-stroke.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 16:
          uv(.5);
          break;
        case 18:
          workspace->findChild<QPushButton *>("clearBrushAlpha")->click();
          break;
      }
    }
    else if (quick_mask_demo) {
      const auto uv = [&](QPointF a, QPointF b) {
        QApplication::processEvents();
        viewport->repaint();
        const auto start = viewport->uv_point(a), end = viewport->uv_point(b);
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      const auto mask_stroke = [&] {
        QApplication::processEvents();
        viewport->repaint();
        const auto a = viewport->scene_point({.1f, 0, .55f}),
                   b = viewport->scene_point({.1f, 0, -.55f});
        send_drag(*viewport, a, b - a, Qt::LeftButton, Qt::NoModifier);
      };
      const auto line = [&] {
        QApplication::processEvents();
        viewport->repaint();
        const auto a = viewport->scene_point({-.75f, 0, 0}),
                   b = viewport->scene_point({.75f, 0, 0});
        send_drag(*viewport, a, b - a, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("quick-mask-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          if (snapshot.materials[0].name != "A_ProjectionSurface")
            qFatal("Quick mask fixture binding changed");
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
            fill.values.base_color = {.08, .1, .18, 1};
            fill.values.roughness = .3;
            bts::TextureDocument::add_paint(set);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, true, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.values.roughness = .8;
          brush.diameter = 15;
          brush.spacing = 100;
          brush.pressure_size = false;
          brush.alpha.kind = bts::AlphaKind::Plus;
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::Material);
          uv({.3, .55}, {.3, .55});
          uv({.25, .85}, {.25, .85});
          brush.values.base_color = {.05, .1, .5, 1};
          uv({.7, .2}, {.7, .2});
          brush.values.base_color = {1, 1, 1, 1};
          select_material(0);
          break;
        }
        case 2:
        case 9:
        case 17:
          workspace->findChild<QAction *>("enableQuickMask")->trigger();
          break;
        case 3:
        case 18:
          mask_stroke();
          break;
        case 4:
        case 11:
          workspace->findChild<QAction *>("editQuickMask")->trigger();
          workspace->findChild<QScrollArea *>("propertiesScroll")
              ->ensureWidgetVisible(workspace->findChild<QDoubleSpinBox *>("brushFlow"));
          break;
        case 5:
          brush.spacing = 20;
          select_material(0);
          line();
          break;
        case 6:
        case 13:
          send_key(*viewport, Qt::Key_Y);
          break;
        case 7:
        case 15:
        case 22:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 8:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 10:
          send_key(*viewport, Qt::Key_I);
          break;
        case 12:
        case 14:
          line();
          break;
        case 16:
          save_document(directory + "/quick-mask-plain.blend");
          break;
        case 19:
          save_document(directory + "/quick-mask-active.blend");
          break;
        case 20:
          open_document(directory + "/quick-mask-active.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 21: {
          QApplication::processEvents();
          viewport->repaint();
          send_drag(
              *viewport, viewport->uv_point({.85, .85}), {0, 0}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
      }
    }
    else if (clone_demo) {
      const auto uv = [&](QPointF a, QPointF b) {
        QApplication::processEvents();
        viewport->repaint();
        const auto start = viewport->uv_point(a), end = viewport->uv_point(b);
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      const auto sample = [&](bool scene) {
        QApplication::processEvents();
        viewport->repaint();
        QKeyEvent held(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier);
        QApplication::sendEvent(viewport, &held);
        const auto point = scene ? viewport->scene_point({-.4f, 0, .1f}) :
                                   viewport->uv_point({.3, .55});
        send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
        QKeyEvent released(QEvent::KeyRelease, Qt::Key_V, Qt::NoModifier);
        QApplication::sendEvent(viewport, &released);
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("clone-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          if (snapshot.materials[0].name != "A_ProjectionSurface")
            qFatal("Clone fixture binding changed");
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
            fill.values.base_color = {.08, .1, .18, 1};
            fill.values.roughness = .3;
            bts::TextureDocument::add_paint(set);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, true, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.values.roughness = .8;
          brush.diameter = 18;
          brush.spacing = 100;
          brush.pressure_size = false;
          brush.alpha.kind = bts::AlphaKind::Plus;
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          uv({.3, .55}, {.3, .55});
          uv({.25, .85}, {.25, .85});
          brush.values.base_color = {.05, .1, .5, 1};
          uv({.7, .2}, {.7, .2});
          brush.values.base_color = {0, 1, 0, 1};
          select_material(0);
          break;
        }
        case 2:
        case 15:
        case 23:
          send_key(*viewport, Qt::Key_6);
          workspace->findChild<QScrollArea *>("propertiesScroll")
              ->ensureWidgetVisible(workspace->findChild<QDoubleSpinBox *>("brushFlow"));
          break;
        case 3:
        case 24:
          uv({.7, .5}, {.75, .5});
          break;
        case 4:
        case 9:
          sample(true);
          break;
        case 5: {
          QApplication::processEvents();
          viewport->repaint();
          const auto a = viewport->scene_point({.25f, 0, .1f}),
                     b = viewport->scene_point({.5f, 0, -.1f});
          send_drag(*viewport, a, b - a, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 6:
        case 12:
        case 19:
        case 27:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 7:
        case 13:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          workspace->findChild<QAction *>("cloneAbsolute")->trigger();
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(20);
          break;
        case 10:
          uv({.45, .28}, {.454, .284});
          break;
        case 11:
          uv({.8, .75}, {.804, .754});
          break;
        case 14:
          send_key(*viewport, Qt::Key_1);
          break;
        case 16:
        case 20:
          workspace->findChild<QToolButton *>("brushChannel2")->click();
          break;
        case 17:
        case 25:
          sample(false);
          break;
        case 18:
          uv({.6, .8}, {.604, .804});
          break;
        case 21:
          save_document(directory + "/clone.blend");
          break;
        case 22:
          open_document(directory + "/clone.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 26:
          uv({.75, .4}, {.754, .404});
          break;
      }
    }
    else if (smudge_demo) {
      const auto uv = [&](QPointF a, QPointF b) {
        QApplication::processEvents();
        viewport->repaint();
        const auto start = viewport->uv_point(a), end = viewport->uv_point(b);
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      const auto tool = [&](int key) {
        send_key(*viewport, key);
        workspace->findChild<QScrollArea *>("propertiesScroll")
            ->ensureWidgetVisible(workspace->findChild<QDoubleSpinBox *>("brushFlow"));
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("smudge-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          if (snapshot.materials[0].name != "A_ProjectionSurface")
            qFatal("Smudge fixture binding changed");
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
            fill.values.base_color = {.08, .1, .18, 1};
            fill.values.roughness = .3;
            bts::TextureDocument::add_paint(set);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, true, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.values.roughness = .8;
          brush.diameter = 18;
          brush.spacing = 100;
          brush.pressure_size = false;
          brush.alpha.kind = bts::AlphaKind::Plus;
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          uv({.3, .55}, {.3, .55});
          uv({.25, .85}, {.25, .85});
          brush.values.base_color = {.05, .1, .5, 1};
          uv({.7, .2}, {.7, .2});
          brush.values.base_color = {0, 1, 0, 1};
          select_material(0);
          break;
        }
        case 2:
        case 7:
        case 17:
          tool(Qt::Key_5);
          break;
        case 3: {
          QApplication::processEvents();
          viewport->repaint();
          const auto a = viewport->scene_point({-.4f, 0, .1f}),
                     b = viewport->scene_point({.7f, 0, .1f});
          send_drag(*viewport, a, b - a, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 4:
        case 9:
        case 13:
        case 19:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 5:
        case 10:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 6:
          tool(Qt::Key_1);
          break;
        case 8:
          uv({.6, .55}, {.6, .35});
          break;
        case 11:
        case 14:
          workspace->findChild<QToolButton *>("brushChannel2")->click();
          break;
        case 12:
          uv({.6, .42}, {.85, .42});
          break;
        case 15:
          save_document(directory + "/smudge.blend");
          break;
        case 16:
          open_document(directory + "/smudge.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 18:
          uv({.6, .4}, {.4, .4});
          break;
        case 20:
          workspace->findChild<QToolButton *>("addPaintLayer")->click();
          break;
        case 21:
          uv({.3, .55}, {.7, .55});
          break;
        case 22:
          uv({.3, .55}, {.3, .55});
          break;
      }
    }
    else if (material_projection_demo) {
      const auto paint = [&](bool scene, float row) {
        QApplication::processEvents();
        viewport->repaint();
        const auto start = scene ? viewport->scene_point({-.7f, 0, row}) :
                                   viewport->uv_point({.15, row}),
                   end = scene ? viewport->scene_point({.7f, 0, row - .2f}) :
                                 viewport->uv_point({.85, row});
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      const auto source = [&](const QString &name = "Blue Weave") {
        workspace->findChild<QToolButton *>("resetShelfFilters")->click();
        workspace->findChild<QLineEdit *>("shelfSearch")->setText(name);
        auto *list = workspace->findChild<QListWidget *>("shelfAssets");
        const auto found = list->findItems(name, Qt::MatchExactly);
        if (found.size() != 1 || !found[0]->flags().testFlag(Qt::ItemIsEnabled))
          qFatal("Projection shelf material unavailable");
        list->setCurrentItem(found[0]);
        const auto point = list->visualItemRect(found[0]).center();
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonDblClick,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
      };
      const auto target = [&] {
        workspace->findChild<QScrollArea *>("propertiesScroll")
            ->ensureWidgetVisible(workspace->findChild<QToolButton *>("projectionResource"));
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("material-projection-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          if (snapshot.materials[0].name != "A_ProjectionSurface")
            qFatal("Projection fixture source binding changed");
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &fill = bts::TextureDocument::layer(set, set.selected_layer);
            fill.values.base_color = {.2, .2, .2, 1};
            fill.values.roughness = .2;
            bts::TextureDocument::add_paint(set);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, true, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.values.roughness = 1;
          brush.diameter = 15;
          brush.spacing = 100;
          brush.pressure_size = false;
          brush.alpha.kind = bts::AlphaKind::Plus;
          brush.follow_path = false;
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          QApplication::processEvents();
          viewport->repaint();
          send_drag(
              *viewport, viewport->uv_point({.25, .8}), {0, 0}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 2:
        case 6:
        case 19:
          send_key(*viewport, Qt::Key_3);
          target();
          break;
        case 3:
          source();
          break;
        case 4:
          paint(true, .1);
          break;
        case 5:
        case 16:
        case 23:
          send_key(*viewport, Qt::Key_1);
          break;
        case 7: {
          const auto point = viewport->pane_rect(bts::ViewKind::Scene).center();
          QKeyEvent held(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
          QApplication::sendEvent(viewport, &held);
          send_drag(*viewport, point, {30, -20}, Qt::MiddleButton, Qt::NoModifier);
          send_drag(*viewport, point, {40, 0}, Qt::LeftButton, Qt::NoModifier);
          send_drag(*viewport, point, {35, 0}, Qt::RightButton, Qt::NoModifier);
          QKeyEvent released(QEvent::KeyRelease, Qt::Key_S, Qt::NoModifier);
          QApplication::sendEvent(viewport, &released);
          break;
        }
        case 8:
          paint(false, .25);
          break;
        case 9:
        case 22:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 10:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 11:
          workspace->findChild<QComboBox *>("projectionTiling")->setCurrentIndex(3);
          workspace->findChild<QDoubleSpinBox *>("projectionWeaveDensity")->setValue(48);
          source("Basic Hard");
          break;
        case 12:
          workspace->display_transform_control()->setCurrentText("Standard");
          break;
        case 13:
          workspace->display_transform_control()->setCurrentText("ACES 1.3");
          break;
        case 14:
          workspace->findChild<QAction *>("projectionUniform")->trigger();
          break;
        case 15:
          workspace->findChild<QAction *>("projectionBlueWeave")->trigger();
          break;
        case 17:
          save_document(directory + "/material-projection.blend");
          break;
        case 18:
          open_document(directory + "/material-projection.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 20:
          workspace->findChild<QAction *>("projectionBlueWeave")->trigger();
          break;
        case 21:
          paint(true, -.55);
          break;
      }
    }
    else if (follow_demo) {
      const auto follow = [&](bool on) {
        auto *button = workspace->findChild<QToolButton *>("brushFollowPath");
        if (button->isChecked() != on)
          button->click();
        workspace->findChild<QScrollArea *>("propertiesScroll")->ensureWidgetVisible(button);
      };
      const auto diagonal = [&](bool reversed = false) {
        QApplication::processEvents();
        viewport->repaint();
        auto start = viewport->scene_point({-.7f, 0, .45f}),
             end = viewport->scene_point({.65f, 0, -.45f});
        if (reversed)
          std::swap(start, end);
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      const auto click = [&] {
        QApplication::processEvents();
        viewport->repaint();
        send_drag(
            *viewport, viewport->uv_point({.25, .8}), {0, 0}, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("follow-path-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {
                .04, .04, .04, 1};
            bts::TextureDocument::add_paint(set);
          });
          viewport->capture_jitter = true;
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, false, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.diameter = 15;
          brush.flow = brush.stroke_opacity = 100;
          brush.spacing = 100;
          brush.pressure_size = false;
          brush.alpha.kind = bts::AlphaKind::Plus;
          brush.jitter = {};
          brush.stencil = {};
          brush.angle = 0;
          brush.follow_path = false;
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          follow(false);
          break;
        }
        case 2:
        case 6:
          diagonal();
          break;
        case 3:
        case 7:
        case 11:
        case 15:
        case 21:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 4:
        case 18:
          if (sets_stage == 18) {
            brush.channels = {true, false, false, false, false};
            brush.values.base_color = {1, 1, 1, 1};
            brush.alpha.kind = bts::AlphaKind::Plus;
            brush.diameter = 15;
            brush.spacing = 100;
          }
          follow(true);
          break;
        case 5:
        case 19:
        case 14:
          click();
          break;
        case 8:
        case 12:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 9:
          workspace->findChild<QDoubleSpinBox *>("brushAngle")->setValue(45);
          break;
        case 10: {
          const auto start = viewport->uv_point({.2, .2}), bend = viewport->uv_point({.8, .2}),
                     next = viewport->uv_point({.8, .65}), end = viewport->uv_point({.35, .85});
          send_mouse_event(*viewport,
                           QEvent::MouseButtonPress,
                           start,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          for (auto point : {bend, next, end})
            send_mouse_event(
                *viewport, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonRelease,
                           end,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 13:
          follow(false);
          break;
        case 16:
          save_document(directory + "/follow-path.blend");
          break;
        case 17:
          open_document(directory + "/follow-path.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 20:
          diagonal(true);
          break;
      }
    }
    else if (stencil_demo) {
      const auto hold = [&](int key) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
      };
      const auto release = [&](int key) {
        QKeyEvent event(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
      };
      const auto gesture = [&](Qt::MouseButton button, QPointF delta, bool cancel = false) {
        const auto start = viewport->pane_rect(bts::ViewKind::Scene).center();
        hold(Qt::Key_S);
        if (cancel) {
          send_mouse_event(
              *viewport, QEvent::MouseButtonPress, start, button, button, Qt::NoModifier);
          send_mouse_event(
              *viewport, QEvent::MouseMove, start + delta, Qt::NoButton, button, Qt::NoModifier);
          QKeyEvent repeat(QEvent::KeyRelease, Qt::Key_S, Qt::NoModifier, QString{}, true);
          QApplication::sendEvent(viewport, &repeat);
          send_key(*viewport, Qt::Key_Escape);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonRelease,
                           start + delta,
                           button,
                           Qt::NoButton,
                           Qt::NoModifier);
        }
        else
          send_drag(*viewport, start, delta, button, Qt::NoModifier);
        release(Qt::Key_S);
      };
      const auto paint = [&](bool scene) {
        QApplication::processEvents();
        viewport->repaint();
        const auto point = scene ? viewport->scene_point({0, 0, 0}) : viewport->uv_point({.5, .5});
        send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
      };
      const auto target = [&] {
        workspace->findChild<QScrollArea *>("propertiesScroll")
            ->ensureWidgetVisible(workspace->findChild<QToolButton *>("stencilResource"));
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("stencil-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {
                .04, .04, .04, 1};
            bts::TextureDocument::add_paint(set);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, true, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.values.roughness = 1;
          brush.diameter = 100;
          brush.flow = brush.stroke_opacity = 100;
          brush.spacing = 60;
          brush.pressure_size = false;
          brush.alpha.kind = bts::AlphaKind::Uniform;
          brush.stencil = {};
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 180, 590, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          target();
          break;
        }
        case 2:
          workspace->findChild<QComboBox *>("stencilTiling")->setCurrentIndex(3);
          break;
        case 3:
          workspace->findChild<QAction *>("stencilSignPlus")->trigger();
          workspace->findChild<QComboBox *>("stencilTiling")->setCurrentIndex(0);
          workspace->findChild<QDoubleSpinBox *>("stencilScale")->setValue(.25);
          break;
        case 4:
          paint(true);
          break;
        case 5:
          gesture(Qt::MiddleButton, {45, -30});
          break;
        case 6:
          gesture(Qt::LeftButton, {70, 0});
          break;
        case 7:
          gesture(Qt::RightButton, {60, -10});
          break;
        case 8:
          gesture(Qt::MiddleButton, {90, 40}, true);
          break;
        case 9: {
          hold(Qt::Key_S);
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
          send_drag(*viewport,
                    {rect.right() - 52, rect.top() + 46},
                    {0, 0},
                    Qt::LeftButton,
                    Qt::NoModifier);
          release(Qt::Key_S);
          break;
        }
        case 10:
          workspace->findChild<QAction *>("clearStencil")->trigger();
          break;
        case 11:
          paint(true);
          break;
        case 12:
        case 15:
        case 18:
        case 21:
        case 25:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 13: {
          const auto asset = std::find_if(
              assets.assets().begin(), assets.assets().end(), [](const auto &entry) {
                return entry.alpha && entry.alpha->kind == bts::AlphaKind::Plus;
              });
          if (asset == assets.assets().end())
            qFatal("Sign Plus resource missing");
          auto *panel = workspace->findChild<QWidget *>("stencilProperties");
          QMimeData mime;
          mime.setData("application/x-bts-asset", asset->id.toString().toUtf8());
          QDragEnterEvent enter({30, 30}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(panel, &enter);
          QDropEvent drop({30, 30}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(panel, &drop);
          if (!drop.isAccepted())
            qFatal("Stencil resource drop rejected");
          workspace->findChild<QDoubleSpinBox *>("stencilScale")->setValue(.25);
          break;
        }
        case 14:
          paint(false);
          break;
        case 16:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 17: {
          hold(Qt::Key_N);
          QKeyEvent repeat(QEvent::KeyRelease, Qt::Key_N, Qt::NoModifier, QString{}, true);
          QApplication::sendEvent(viewport, &repeat);
          if (!viewport->stencil_ignore)
            qFatal("Auto-repeat released stencil bypass");
          paint(false);
          release(Qt::Key_N);
          break;
        }
        case 19: {
          QImage image(32, 16, QImage::Format_RGB32);
          for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 32; ++x)
              image.setPixel(x, y, x < 16 ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
          const auto path = directory + "/half-stencil.png";
          image.save(path);
          sets_demo.stop();
          QTimer::singleShot(100, workspace, [&, path] {
            auto *dialog = workspace->findChild<QFileDialog *>("stencilImportDialog");
            if (!dialog)
              qFatal("Stencil image chooser missing");
            dialog->setDirectory(QFileInfo(path).absolutePath());
            dialog->selectFile(path);
            if (auto *field = dialog->findChild<QLineEdit *>("fileNameEdit"))
              field->setText(QFileInfo(path).fileName());
            qInfo("BTS_STENCIL_IMPORT_TEST selected=%s",
                  qPrintable(dialog->selectedFiles().join(";")));
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
          });
          workspace->findChild<QAction *>("importStencilImage")->trigger();
          sets_demo.start(100);
          break;
        }
        case 20:
          paint(false);
          break;
        case 22:
          save_document(directory + "/brush-stencil.blend");
          break;
        case 23:
          open_document(directory + "/brush-stencil.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 24:
          brush.stencil = {};
          brush.alpha.kind = bts::AlphaKind::Uniform;
          brush.channels = {true, false, false, false, false};
          brush.values.base_color = {.1, .6, .2, 1};
          brush.diameter = 40;
          select_material(0);
          paint(false);
          break;
      }
    }
    else if (jitter_demo) {
      const auto configure = [&](float size, float flow, float angle, float position) {
        const std::array<const char *, 4> ids{
            "brushSizeJitter", "brushFlowJitter", "brushAngleJitter", "brushPositionJitter"};
        const std::array<float, 4> values{size, flow, angle, position};
        for (int i = 0; i < 4; ++i)
          workspace->findChild<QDoubleSpinBox *>(ids[i])->setValue(values[i]);
        workspace->findChild<QScrollArea *>("propertiesScroll")
            ->ensureWidgetVisible(workspace->findChild<QDoubleSpinBox *>("brushPositionJitter"));
      };
      const auto paint = [&](bool scene, float row) {
        QApplication::processEvents();
        viewport->repaint();
        QPointF start, end;
        if (scene) {
          start = viewport->scene_point({-.7f, 0, row});
          end = viewport->scene_point({.7f, 0, row});
        }
        else {
          start = viewport->uv_point({.15, row});
          end = viewport->uv_point({.85, row});
        }
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("jitter-demo").toStdString();
          options.resolution = bts::DocumentResolution::Pixels256;
          create_project(options);
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {
                .04, .04, .04, 1};
            bts::TextureDocument::add_paint(set);
          });
          viewport->capture_jitter = true;
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.2);
          brush.channels = {true, false, false, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.diameter = 10;
          brush.flow = brush.stroke_opacity = 100;
          brush.spacing = 60;
          brush.pressure_size = false;
          brush.alpha = {};
          brush.jitter = {};
          select_material(0);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({80, 200, 570, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        }
        case 2:
          configure(80, 0, 0, 0);
          break;
        case 3:
          paint(true, .55);
          break;
        case 4:
          configure(0, 80, 0, 0);
          break;
        case 5:
          paint(true, .18);
          break;
        case 6:
          configure(0, 0, 0, 100);
          break;
        case 7:
          paint(true, -.2);
          break;
        case 8:
          configure(0, 0, 90, 0);
          break;
        case 9:
          brush.alpha.kind = bts::AlphaKind::Plus;
          select_material(0);
          paint(true, -.57);
          break;
        case 10:
          configure(80, 80, 90, 100);
          break;
        case 11:
          paint(false, .9);
          break;
        case 12:
        case 18:
        case 21:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 13:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 14:
          save_document(directory + "/brush-jitter.blend");
          break;
        case 15:
          open_document(directory + "/brush-jitter.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 16:
          brush.channels = {true, false, false, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.spacing = 60;
          configure(80, 80, 90, 100);
          break;
        case 17:
          paint(false, .05);
          break;
        case 19:
          configure(0, 0, 0, 0);
          break;
        case 20:
          paint(false, .5);
          break;
      }
    }
    else if (instance_demo) {
      const auto parent = [&](QUuid id, bool hierarchy = false) {
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == id &&
              list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
          {
            auto *item = list->item(row);
            list->scrollToItem(item);
            const auto index = list->indexFromItem(item);
            const auto rect = list->visualRect(index);
            const auto point = hierarchy ? bts::layer_instance_rect(index, rect).center() :
                                           QPoint(bts::layer_thumbnail_x(index, rect) + 12,
                                                  rect.center().y());
            send_mouse_event(*list->viewport(),
                             QEvent::MouseButtonPress,
                             point,
                             Qt::LeftButton,
                             Qt::LeftButton,
                             Qt::NoModifier);
            send_mouse_event(*list->viewport(),
                             QEvent::MouseButtonRelease,
                             point,
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
            return;
          }
        qFatal("Instance parent row missing");
      };
      const auto source = [&] {
        workspace->texture_sets()->setCurrentRow(0);
        parent(instance_source);
      };
      const auto target = [&] {
        workspace->texture_sets()->setCurrentRow(1);
        parent(instance_created);
      };
      const auto jump = [&] {
        auto *tree = workspace->findChild<QTreeWidget *>("layerInstanceHierarchy");
        auto *item = tree->topLevelItem(0);
        if (!item || QUuid(item->data(0, Qt::UserRole + 1).toString()) != instance_source)
          qFatal("Instance source hierarchy identity mismatch");
        tree->setCurrentItem(item);
        const auto point = tree->visualItemRect(item).center();
        send_mouse_event(*tree->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*tree->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        send_mouse_event(*tree->viewport(),
                         QEvent::MouseButtonDblClick,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*tree->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
      };
      if (cycle_demo)
        switch (sets_stage) {
          case 1:
            open_document(harness.value("instance-cycle-demo"));
            workspace->texture_sets()->setCurrentRow(0);
            viewport->set_layout({bts::ViewLayout::Split, false});
            viewport->focus_uv({.5, .5}, 1.3);
            workspace->layer_settings_tabs()->setCurrentIndex(0);
            workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 2:
            workspace->findChild<QToolButton *>("addLayerFolder")->click();
            cycle_empty = instance_source =
                document->set(snapshot.materials[0].session_id).selected_layer;
            break;
          case 3:
          case 10:
            workspace->layer_stack()->setFocus();
            send_key(*workspace->layer_stack(), Qt::Key_C, Qt::ControlModifier);
            break;
          case 4:
            workspace->layer_stack()->setFocus();
            send_key(
                *workspace->layer_stack(), Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
            cycle_root = instance_created =
                document->set(snapshot.materials[0].session_id).selected_layer;
            break;
          case 5:
            parent(cycle_root, true);
            break;
          case 6:
            jump();
            break;
          case 7: {
            const auto &set = document->set(snapshot.materials[0].session_id);
            for (auto it = set.layers.rbegin(); it != set.layers.rend(); ++it)
              if (it->kind == bts::LayerKind::Paint && !it->instance && !it->pixels.empty()) {
                cycle_child = it->id;
                break;
              }
            if (cycle_child.isNull())
              qFatal("Cycle fixture needs painted source layer");
            parent(cycle_child);
            break;
          }
          case 8:
            workspace->findChild<QAction *>("groupLayers")->trigger();
            cycle_folder = instance_source =
                document->set(snapshot.materials[0].session_id).selected_layer;
            break;
          case 9:
            parent(cycle_folder);
            break;
          case 11:
            parent(cycle_child);
            break;
          case 12:
            workspace->layer_stack()->setFocus();
            send_key(
                *workspace->layer_stack(), Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
            cycle_nested = instance_created =
                document->set(snapshot.materials[0].session_id).selected_layer;
            break;
          case 13:
          case 17:
          case 21:
            parent(cycle_nested, true);
            break;
          case 14:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 15:
            save_document(directory + "/valid-folders.blend");
            break;
          case 16:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 18:
            save_document(directory + "/cyclic-folders.blend");
            break;
          case 19:
            open_document(directory + "/cyclic-folders.blend");
            workspace->texture_sets()->setCurrentRow(0);
            viewport->focus_uv({.5, .5}, 1.3);
            update_inspection(bts::InspectionMode::BaseColor);
            parent(cycle_nested, true);
            break;
          case 20:
            parent(cycle_nested);
            break;
          case 22:
            open_document(directory + "/valid-folders.blend");
            workspace->texture_sets()->setCurrentRow(0);
            viewport->focus_uv({.5, .5}, 1.3);
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 23:
            instance_source = cycle_empty;
            instance_created = cycle_root;
            parent(cycle_root);
            break;
          case 24:
            parent(cycle_root, true);
            break;
          case 25:
            jump();
            break;
        }
      else if (paint_instance_demo) {
        const auto stroke = [&](bool reverse) {
          brush.channels = {true, false, false, false, false};
          brush.values.base_color = {1, 1, 1, 1};
          brush.diameter = 10;
          brush.flow = brush.stroke_opacity = 100;
          QVector3D minimum(1e9, 1e9, 1e9), maximum(-1e9, -1e9, -1e9);
          for (const auto &triangle : snapshot.uv_triangles)
            if (triangle.material == 0)
              for (const auto &point : triangle.positions)
                for (int axis = 0; axis < 3; ++axis) {
                  minimum[axis] = std::min(minimum[axis], point[axis]);
                  maximum[axis] = std::max(maximum[axis], point[axis]);
                }
          QApplication::processEvents();
          viewport->repaint();
          const auto center = viewport->scene_point((minimum + maximum) * .5);
          const QPointF delta(18, reverse ? -18 : 18);
          send_drag(*viewport, center - delta, delta * 2, Qt::LeftButton, Qt::NoModifier);
        };
        switch (sets_stage) {
          case 1:
            open_document(harness.value("paint-instance-demo"));
            workspace->texture_sets()->setCurrentRow(0);
            viewport->set_layout({bts::ViewLayout::Split, false});
            viewport->focus_uv({.5, .5}, 1.3);
            workspace->layer_settings_tabs()->setCurrentIndex(0);
            workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 2:
            workspace->findChild<QToolButton *>("addPaintLayer")->click();
            instance_source = document->set(snapshot.materials[0].session_id).selected_layer;
            break;
          case 3:
            stroke(false);
            break;
          case 4:
            workspace->layer_stack()->setFocus();
            send_key(*workspace->layer_stack(), Qt::Key_C, Qt::ControlModifier);
            break;
          case 5:
            workspace->texture_sets()->setCurrentRow(1);
            break;
          case 6:
            workspace->layer_stack()->setFocus();
            send_key(
                *workspace->layer_stack(), Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
            instance_created = document->set(snapshot.materials[1].session_id).selected_layer;
            break;
          case 7:
            parent(instance_created);
            break;
          case 8:
          case 17:
            parent(instance_created, true);
            break;
          case 9:
          case 18:
            jump();
            break;
          case 10:
          case 19:
            parent(instance_source);
            break;
          case 11:
            stroke(true);
            break;
          case 12:
          case 21:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 13:
          case 22:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 14:
          case 23:
            target();
            break;
          case 15:
            save_document(directory + "/paint-instances.blend");
            break;
          case 16:
            open_document(directory + "/paint-instances.blend");
            workspace->texture_sets()->setCurrentRow(1);
            viewport->focus_uv({.5, .5}, 1.3);
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 20:
            stroke(false);
            break;
        }
      }
      else
        switch (sets_stage) {
          case 1:
            workspace->texture_sets()->setCurrentRow(0);
            edit_material([&](bts::TextureSet &set) {
              bts::TextureDocument::set_resolution(set, 128);
              set.precision[0] = bts::ChannelPrecision::RGB32F;
              bts::TextureDocument::add_fill(set);
              instance_source = set.selected_layer;
              auto &layer = bts::TextureDocument::layer(set, instance_source);
              layer.values.base_color = {.15, .35, .65, 1};
              layer.fill.atom = true;
              layer.fill.mode = 1;
              layer.fill.border_width = .9;
              layer.blending[0] = bts::LayerBlend::Screen;
            });
            viewport->set_layout({bts::ViewLayout::Split, false});
            viewport->focus_uv({.5, .5}, 1.3);
            workspace->layer_settings_tabs()->setCurrentIndex(0);
            workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 2:
            workspace->texture_sets()->setCurrentRow(1);
            edit_material([&](bts::TextureSet &set) {
              bts::TextureDocument::set_resolution(set, 128);
              set.precision[0] = bts::ChannelPrecision::RGB32F;
              bts::TextureDocument::add_fill(set);
              bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {
                  .12, .18, .24, 1};
              bts::TextureDocument::add_paint(set);
              instance_paint = set.selected_layer;
              bts::LevelsEffect identity;
              identity.channel = 0;
              bts::TextureDocument::layer(set, instance_paint).effects.push_back(identity);
            });
            break;
          case 3:
            brush.channels = {true, false, false, false, false};
            brush.values.base_color = {1, 1, 1, 1};
            brush.diameter = 8;
            brush.flow = brush.stroke_opacity = 100;
            QApplication::processEvents();
            viewport->repaint();
            for (auto line : {std::array<QPointF, 2>{QPointF(.3, .5), QPointF(.7, .5)},
                              std::array<QPointF, 2>{QPointF(.5, .3), QPointF(.5, .7)}})
            {
              const auto point = viewport->uv_point(line[0]);
              send_drag(*viewport,
                        point,
                        viewport->uv_point(line[1]) - point,
                        Qt::LeftButton,
                        Qt::NoModifier);
            }
            break;
          case 4:
            source();
            break;
          case 5:
            workspace->layer_stack()->setFocus();
            send_key(*workspace->layer_stack(), Qt::Key_C, Qt::ControlModifier);
            if (!instance_clipboard)
              qFatal("Focused layer copy failed");
            break;
          case 6:
            workspace->texture_sets()->setCurrentRow(1);
            break;
          case 7: {
            workspace->texture_sets()->setFocus();
            QKeyEvent event(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
            QApplication::sendEvent(workspace->texture_sets(), &event);
            break;
          }
          case 8:
            workspace->layer_stack()->setFocus();
            send_key(
                *workspace->layer_stack(), Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
            instance_created = document->set(snapshot.materials[1].session_id).selected_layer;
            if (!bts::TextureDocument::layer(document->set(snapshot.materials[1].session_id),
                                             instance_created)
                     .instance)
              qFatal("Focused instance paste failed");
            break;
          case 9:
          case 20:
            parent(instance_created, true);
            break;
          case 10:
          case 21:
            jump();
            break;
          case 11:
          case 22:
            parent(instance_source);
            break;
          case 12:
            workspace->findChild<QDoubleSpinBox *>("fillAtomBorderWidth")->setValue(.2);
            break;
          case 13:
          case 24:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 14:
          case 25:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 15:
            workspace->findChild<QDoubleSpinBox *>("fillAtomBorderWidth")->setValue(.9);
            break;
          case 16:
          case 26:
            target();
            break;
          case 17:
            parent(instance_created);
            break;
          case 18:
            save_document(directory + "/instances.blend");
            break;
          case 19:
            open_document(directory + "/instances.blend");
            workspace->texture_sets()->setCurrentRow(1);
            viewport->focus_uv({.5, .5}, 1.3);
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 23:
            workspace->findChild<QDoubleSpinBox *>("fillAtomBorderWidth")->setValue(.2);
            break;
        }
    }
    else if (geometry_demo) {
      const auto target = [&](int kind, bool context = false) {
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == geometry_upper &&
              list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
          {
            auto *item = list->item(row);
            list->scrollToItem(item);
            const auto index = list->indexFromItem(item);
            const auto rect = list->visualRect(index);
            const auto point = (kind == 0 ? bts::layer_geometry_rect(index, rect) :
                                kind == 1 ? QRect(bts::layer_thumbnail_x(index, rect),
                                                  rect.center().y() - 12,
                                                  24,
                                                  24) :
                                            bts::layer_mask_rect(index, rect))
                                   .center();
            if (context) {
              QContextMenuEvent event(
                  QContextMenuEvent::Mouse, point, list->viewport()->mapToGlobal(point));
              QApplication::sendEvent(list->viewport(), &event);
            }
            else {
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonPress,
                               point,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonRelease,
                               point,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::NoModifier);
            }
            return;
          }
        qFatal("Geometry parent row missing");
      };
      const auto mesh = [&](const QString &name, bool included) {
        auto *list = workspace->findChild<QListWidget *>("geometryMeshList");
        const auto items = list->findItems(name, Qt::MatchExactly);
        if (items.size() != 1)
          qFatal("Geometry name missing: %s", qPrintable(name));
        items.front()->setCheckState(included ? Qt::Checked : Qt::Unchecked);
      };
      const auto menu = [&](const char *name) {
        target(0, true);
        auto menus = workspace->findChildren<QMenu *>("geometryMaskContextMenu");
        if (menus.empty())
          qFatal("Geometry menu missing");
        auto *popup = menus.last();
        auto *action = popup->findChild<QAction *>(name);
        if (!action || !action->isEnabled())
          qFatal("Geometry action unavailable: %s", name);
        action->trigger();
        popup->close();
      };
      if (hide_demo)
        switch (sets_stage) {
          case 1:
            open_document(harness.value("hide-excluded-demo"));
            workspace->texture_sets()->setCurrentRow(0);
            {
              auto &set = document->set(snapshot.materials[0].session_id);
              geometry_upper = set.selected_layer;
              set.selected_geometry = false;
              set.selected_mask = false;
              set.selected_effect = QUuid();
              select_material(0);
            }
            viewport->set_layout({bts::ViewLayout::Split, false});
            viewport->focus_uv({1, .5}, 2.2);
            workspace->layer_settings_tabs()->setCurrentIndex(0);
            workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 2:
          case 4:
          case 11:
            workspace->findChild<QToolButton *>("hideExcludedGeometry")->click();
            break;
          case 3:
            brush.channels = {true, false, false, false, false};
            brush.values.base_color = {1, 1, 1, 1};
            brush.diameter = 8;
            brush.flow = brush.stroke_opacity = 100;
            QApplication::processEvents();
            viewport->repaint();
            {
              const auto point = viewport->scene_point({-.8f, 0, -.25f});
              send_drag(*viewport,
                        point,
                        viewport->scene_point({.8f, 0, -.25f}) - point,
                        Qt::LeftButton,
                        Qt::NoModifier);
            }
            break;
          case 5:
          case 15:
            target(0);
            break;
          case 6:
          case 16:
            mesh("1001", true);
            break;
          case 7:
          case 17:
            target(1);
            break;
          case 8:
          case 10:
          case 18:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 9:
          case 19:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 12:
            save_document(directory + "/hidden-tiles.blend");
            break;
          case 13:
            open_document(directory + "/hidden-tiles.blend");
            workspace->texture_sets()->setCurrentRow(0);
            viewport->focus_uv({1, .5}, 2.2);
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 14:
            workspace->findChild<QAction *>("toggleHideExcludedGeometry")->trigger();
            break;
          case 20:
            target(0);
            workspace->findChild<QComboBox *>("geometryMaskType")->setCurrentIndex(0);
            break;
          case 21:
            mesh("RightPart", false);
            break;
          case 22:
            target(1);
            break;
          case 23:
            workspace->findChild<QToolButton *>("hideExcludedGeometry")->click();
            break;
          case 24:
            target(0);
            workspace->findChild<QComboBox *>("geometryMaskType")->setCurrentIndex(1);
            mesh("1001", false);
            break;
          case 25:
            viewport->set_layout({bts::ViewLayout::SceneOnly, false});
            target(1);
            break;
          case 26:
            viewport->set_layout({bts::ViewLayout::Split, false});
            break;
          case 27:
            workspace->findChild<QToolButton *>("hideExcludedGeometry")->click();
            break;
        }
      else if (tile_mask_demo)
        switch (sets_stage) {
          case 1: {
            bts::NewProjectOptions options;
            options.absolute_mesh_path = harness.value("tile-mask-demo").toStdString();
            options.resolution = bts::DocumentResolution::Pixels128;
            options.uv_workflow = bts::UvWorkflow::PreserveTiles;
            create_project(options);
            edit_material([&](bts::TextureSet &set) {
              set.precision[0] = bts::ChannelPrecision::RGB32F;
              bts::TextureDocument::add_fill(set);
              geometry_lower = set.selected_layer;
              bts::TextureDocument::layer(set, geometry_lower).values.base_color = {
                  .8, .1, .05, 1};
              bts::TextureDocument::add_paint(set);
              geometry_upper = set.selected_layer;
              bts::LevelsEffect levels;
              levels.channel = 0;
              bts::TextureDocument::layer(set, geometry_upper).effects.push_back(levels);
            });
            viewport->set_layout({bts::ViewLayout::Split, false});
            viewport->focus_uv({1, .5}, 2.2);
            workspace->layer_settings_tabs()->setCurrentIndex(0);
            workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          }
          case 2:
            brush.channels = {true, false, false, false, false};
            brush.values.base_color = {1, 1, 1, 1};
            brush.diameter = 12;
            brush.flow = brush.stroke_opacity = 100;
            QApplication::processEvents();
            viewport->repaint();
            {
              const auto point = viewport->uv_point({.2, .5});
              send_drag(*viewport,
                        point,
                        viewport->uv_point({1.8, .5}) - point,
                        Qt::LeftButton,
                        Qt::NoModifier);
            }
            break;
          case 3:
          case 10:
            target(0);
            break;
          case 4:
            workspace->findChild<QComboBox *>("geometryMaskType")->setCurrentIndex(1);
            break;
          case 5:
          case 11:
            mesh("1001", false);
            break;
          case 6:
            mesh("1002", false);
            break;
          case 7:
          case 9:
          case 16:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 8:
          case 14:
            target(1);
            break;
          case 12:
            save_document(directory + "/tile-mask.blend");
            break;
          case 13:
            open_document(directory + "/tile-mask.blend");
            workspace->texture_sets()->setCurrentRow(0);
            viewport->focus_uv({1, .5}, 2.2);
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 15:
            target(0);
            mesh("1001", true);
            break;
          case 17:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 18:
            workspace->findChild<QPushButton *>("geometryIncludeAll")->click();
            break;
        }
      else
        switch (sets_stage) {
          case 1: {
            bts::NewProjectOptions options;
            options.absolute_mesh_path = harness.value("geometry-mask-demo").toStdString();
            create_project(options);
            edit_material([&](bts::TextureSet &set) {
              bts::TextureDocument::set_resolution(set, 128);
              set.precision[0] = bts::ChannelPrecision::RGB32F;
              bts::TextureDocument::add_fill(set);
              geometry_lower = set.selected_layer;
              auto &lower = bts::TextureDocument::layer(set, geometry_lower);
              lower.name = "Undercoat";
              lower.values.base_color = {.8, .1, .05, 1};
              bts::TextureDocument::add_fill(set);
              geometry_upper = set.selected_layer;
              auto &upper = bts::TextureDocument::layer(set, geometry_upper);
              upper.name = "Top paint";
              upper.values.base_color = {.05, .2, .8, 1};
              bts::LevelsEffect levels;
              levels.channel = 0;
              upper.effects.push_back(levels);
            });
            viewport->set_layout({bts::ViewLayout::Split, false});
            viewport->focus_uv({.5, .5}, 1.3);
            workspace->layer_settings_tabs()->setCurrentIndex(0);
            workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          }
          case 2:
          case 18:
          case 20:
          case 22:
            target(0);
            break;
          case 3:
            mesh("RightPart", false);
            break;
          case 4:
          case 11:
          case 16:
          case 24:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 5:
          case 12:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 6:
            workspace->findChild<QPushButton *>("geometryExcludeAll")->click();
            break;
          case 7:
            mesh("LeftPart", true);
            break;
          case 8:
            menu("copyGeometryMask");
            break;
          case 9:
          case 15:
            workspace->findChild<QPushButton *>("geometryIncludeAll")->click();
            break;
          case 10:
            menu("pasteGeometryMask");
            break;
          case 13:
            save_document(directory + "/geometry-mask.blend");
            break;
          case 14:
            open_document(directory + "/geometry-mask.blend");
            workspace->texture_sets()->setCurrentRow(0);
            viewport->focus_uv({.5, .5}, 1.3);
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 17:
            target(1);
            break;
          case 19:
            workspace->findChild<QAction *>("addWhiteMask")->trigger();
            break;
          case 21:
            target(2);
            break;
          case 23:
            menu("excludeAllGeometry");
            break;
        }
    }
    else if (anchor_navigation_demo) {
      const auto source = [&] {
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == navigation_source &&
              QUuid(list->item(row)->data(Qt::UserRole + 10).toString()) == navigation_point)
          {
            list->scrollToItem(list->item(row));
            list->setCurrentRow(row);
            return;
          }
        qFatal("Navigation source row missing");
      };
      const auto jump = [&] {
        auto *button = workspace->findChild<QPushButton *>("goToMaskAnchor");
        if (!button->isVisible())
          qFatal("Go to anchor is unavailable");
        button->click();
      };
      const auto activate = [&](QUuid parent, QUuid effect, bool keyboard = false) {
        auto *list = workspace->findChild<QListWidget *>("anchorReferences");
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == parent &&
              QUuid(list->item(row)->data(Qt::UserRole + 1).toString()) == effect)
          {
            list->setCurrentRow(row);
            list->scrollToItem(list->item(row));
            if (keyboard) {
              list->setFocus();
              QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
              QApplication::sendEvent(list, &event);
            }
            else {
              const auto point = list->visualItemRect(list->item(row)).center();
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonPress,
                               point,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonRelease,
                               point,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::NoModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonDblClick,
                               point,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonRelease,
                               point,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::NoModifier);
            }
            return;
          }
        qFatal("Navigation consumer row missing");
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("anchor-navigation-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          {
            auto &set = document->set(snapshot.materials[0].session_id);
            navigation_first = set.selected_layer;
            for (const auto &effect :
                 bts::TextureDocument::layer(set, set.selected_layer).mask->effects)
              if (effect.kind == bts::MaskEffectKind::Fill && !effect.anchor_source.isNull()) {
                navigation_point = effect.anchor_source;
                navigation_first_fill = effect.id;
              }
            for (const auto &anchor : bts::TextureDocument::mask_anchors(set))
              if (anchor.id == navigation_point)
                navigation_source = anchor.layer;
            if (navigation_source.isNull())
              qFatal("Navigation fixture has no linked anchor");
            set.selected_mask = true;
            set.selected_effect = navigation_first_fill;
            select_material(0);
          }
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
        case 6:
        case 12:
        case 14:
        case 28:
          jump();
          break;
        case 3:
        case 4:
        case 21:
        case 22:
          workspace->findChild<QToolButton *>("anchorReferencesDisclosure")->click();
          break;
        case 5:
        case 15:
          activate(navigation_first, navigation_first_fill);
          break;
        case 7:
          activate(navigation_first, navigation_first_fill, true);
          break;
        case 8:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          navigation_second = document->set(snapshot.materials[0].session_id).selected_layer;
          break;
        case 9:
          workspace->findChild<QAction *>("addBlackMask")->trigger();
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 10:
          workspace->findChild<QAction *>("addMaskFillEffect")->trigger();
          navigation_second_fill = document->set(snapshot.materials[0].session_id).selected_effect;
          break;
        case 11: {
          auto *combo = workspace->findChild<QComboBox *>("maskFillResource");
          const int index = combo->findData(navigation_point.toString());
          if (index < 2)
            qFatal("Navigation source absent from resource picker");
          combo->setCurrentIndex(index);
          break;
        }
        case 13:
        case 18:
        case 27:
          activate(navigation_second, navigation_second_fill);
          break;
        case 16:
        case 19:
          workspace->findChild<QComboBox *>("maskFillResource")->setCurrentIndex(0);
          break;
        case 17:
        case 20:
          source();
          break;
        case 23:
        case 24:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          source();
          break;
        case 25:
          save_document(directory + "/navigation.blend");
          break;
        case 26:
          open_document(directory + "/navigation.blend");
          workspace->texture_sets()->setCurrentRow(0);
          viewport->focus_uv({.5, .5}, 1.3);
          update_inspection(bts::InspectionMode::Mask);
          break;
      }
    }
    else if (anchor_validity_demo) {
      const auto parent_mask = [&](QUuid id) {
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == id &&
              list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
          {
            auto *item = list->item(row);
            list->scrollToItem(item);
            const auto index = list->indexFromItem(item);
            const auto point = bts::layer_mask_rect(index, list->visualRect(index)).center();
            send_mouse_event(*list->viewport(),
                             QEvent::MouseButtonPress,
                             point,
                             Qt::LeftButton,
                             Qt::LeftButton,
                             Qt::NoModifier);
            send_mouse_event(*list->viewport(),
                             QEvent::MouseButtonRelease,
                             point,
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
            return;
          }
        qFatal("Anchor validity parent missing");
      };
      const auto select = [&](QUuid parent, QUuid effect) {
        if (!document->set(snapshot.materials[0].session_id).selected_mask)
          parent_mask(parent);
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == parent &&
              QUuid(list->item(row)->data(Qt::UserRole + 10).toString()) == effect)
          {
            list->scrollToItem(list->item(row));
            list->setCurrentRow(row);
            return;
          }
        qFatal("Anchor validity effect missing");
      };
      const auto consumer = [&] { select(validity_consumer, validity_fill); };
      const auto undo = [&] {
        workspace->findChild<QAction *>("undoDocument")->trigger();
        consumer();
      };
      const auto remove = [&] {
        select(validity_source, validity_point);
        auto *list = workspace->layer_stack();
        const auto rect = list->visualItemRect(list->currentItem());
        const QPoint point(rect.right() - 10, rect.center().y());
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        consumer();
      };
      const auto rename = [&](const QString &name) {
        auto *field = workspace->findChild<QLineEdit *>("anchorName");
        field->setText(name);
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(field, &event);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("anchor-validity-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          {
            auto &set = document->set(snapshot.materials[0].session_id);
            validity_consumer = set.selected_layer;
            for (const auto &effect :
                 bts::TextureDocument::layer(set, set.selected_layer).mask->effects)
              if (effect.kind == bts::MaskEffectKind::Fill && !effect.anchor_source.isNull()) {
                validity_point = effect.anchor_source;
                validity_fill = effect.id;
              }
            for (const auto &anchor : bts::TextureDocument::mask_anchors(set))
              if (anchor.id == validity_point) {
                validity_source = anchor.layer;
                validity_name = anchor.name;
              }
            if (validity_source.isNull())
              qFatal("Validity fixture has no linked anchor");
            set.selected_mask = true;
            set.selected_effect = validity_fill;
            select_material(0);
          }
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          save_document(directory + "/valid.blend");
          break;
        case 3:
          parent_mask(validity_source);
          {
            auto *action = workspace->findChild<QAction *>("moveLayerUp");
            if (!action->isEnabled())
              qFatal("Source layer move disabled");
            action->trigger();
          }
          consumer();
          break;
        case 4:
        case 6:
        case 8:
        case 11:
        case 12:
        case 13:
        case 19:
        case 25:
          undo();
          break;
        case 5:
          select(validity_source, validity_point);
          workspace->layer_stack()->currentItem()->setCheckState(Qt::Unchecked);
          consumer();
          break;
        case 7:
        case 9:
        case 15:
          remove();
          break;
        case 10:
          parent_mask(validity_source);
          workspace->findChild<QAction *>("addMaskAnchor")->trigger();
          validity_new_point = document->set(snapshot.materials[0].session_id).selected_effect;
          rename(validity_name);
          consumer();
          break;
        case 14: {
          const auto before = document->to_json();
          const auto other = document->set(snapshot.materials[1].session_id).id;
          edit_material([&](bts::TextureSet &set) {
            try {
              bts::TextureDocument::assign_mask_anchor(set, validity_fill, validity_point, other);
            }
            catch (const std::exception &) {
              validity_cross = true;
              throw;
            }
          });
          validity_preserved = before == document->to_json();
          break;
        }
        case 16:
          save_document(directory + "/broken.blend");
          break;
        case 17:
          open_document(directory + "/broken.blend");
          workspace->texture_sets()->setCurrentRow(0);
          viewport->focus_uv({.5, .5}, 1.3);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 18:
          workspace->findChild<QComboBox *>("maskFillResource")->setCurrentIndex(0);
          break;
        case 20:
          open_document(directory + "/valid.blend");
          workspace->texture_sets()->setCurrentRow(0);
          viewport->focus_uv({.5, .5}, 1.3);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 21:
          select(validity_source, validity_point);
          rename(QString(120, QLatin1Char('L')));
          consumer();
          break;
        case 22:
          save_document(directory + "/long-name.blend");
          break;
        case 23:
          open_document(directory + "/long-name.blend");
          workspace->texture_sets()->setCurrentRow(0);
          viewport->focus_uv({.5, .5}, 1.3);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 24:
          select(validity_source, validity_point);
          rename(validity_name);
          consumer();
          break;
      }
    }
    else if (anchor_reference_demo && qEnvironmentVariableIsSet("BTS_CONTENT_ANCHORS")) {
#include "ContentAnchorDemo.inc"
    }
    else if (anchor_reference_demo) {
      const auto select = [&](QUuid parent, QUuid effect) {
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == parent &&
              QUuid(list->item(row)->data(Qt::UserRole + 10).toString()) == effect)
          {
            list->scrollToItem(list->item(row));
            list->setCurrentRow(row);
            return;
          }
        qFatal("Anchor demo target row missing");
      };
      const auto consumer = [&] { select(anchor_demo_consumer, anchor_demo_fill); };
      const auto undo = [&] {
        workspace->findChild<QAction *>("undoDocument")->trigger();
        consumer();
      };
      const auto redo = [&] {
        workspace->findChild<QAction *>("redoDocument")->trigger();
        consumer();
      };
      const auto paint_source = [&] {
        const auto &set = document->set(snapshot.materials[0].session_id);
        QUuid paint;
        for (const auto &effect : bts::TextureDocument::layer(
                                      document->set(set.material_session_uid), anchor_demo_source)
                                      .mask->effects)
          if (effect.kind == bts::MaskEffectKind::Paint)
            paint = effect.id;
        select(anchor_demo_source, paint);
        brush = bts::BrushSettings{};
        brush.mask_value = 0;
        brush.diameter = 5;
        brush.hardness = 1;
        select_material(selected_material);
        QApplication::processEvents();
        viewport->repaint();
        const auto start = viewport->uv_point({.95, .2});
        send_drag(*viewport,
                  start,
                  viewport->uv_point({.95, .4}) - start,
                  Qt::LeftButton,
                  Qt::NoModifier);
        viewport->cursor_visible_ = false;
        consumer();
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("anchor-reference-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          {
            auto &set = document->set(snapshot.materials[0].session_id);
            anchor_demo_source = set.selected_layer;
            set.selected_mask = true;
            set.selected_effect =
                bts::TextureDocument::layer(set, set.selected_layer).mask->effects.back().id;
            select_material(0);
          }
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          workspace->findChild<QAction *>("addMaskAnchor")->trigger();
          anchor_demo_point = document->set(snapshot.materials[0].session_id).selected_effect;
          break;
        case 3: {
          auto *name = workspace->findChild<QLineEdit *>("anchorName");
          name->setText("Cube mask reference");
          QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          QApplication::sendEvent(name, &event);
          break;
        }
        case 4:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          anchor_demo_consumer = document->set(snapshot.materials[0].session_id).selected_layer;
          break;
        case 5:
          workspace->findChild<QAction *>("addBlackMask")->trigger();
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 6:
          workspace->findChild<QAction *>("addMaskFillEffect")->trigger();
          anchor_demo_fill = document->set(snapshot.materials[0].session_id).selected_effect;
          break;
        case 7: {
          auto *resource = workspace->findChild<QComboBox *>("maskFillResource");
          const int index = resource->findData(anchor_demo_point.toString());
          if (index < 2)
            qFatal("Lower mask anchor absent from Fill resource choices");
          resource->setCurrentIndex(index);
          break;
        }
        case 8:
        case 19:
          paint_source();
          break;
        case 9:
        case 11:
        case 13:
        case 20:
        case 22:
          undo();
          break;
        case 10:
        case 21:
          redo();
          break;
        case 12:
          select(anchor_demo_source, anchor_demo_point);
          send_key(*viewport, Qt::Key_Down, Qt::AltModifier);
          consumer();
          break;
        case 14:
        case 23:
          select(anchor_demo_source, anchor_demo_point);
          break;
        case 15:
          consumer();
          workspace->findChild<QDoubleSpinBox *>("maskEffectFillOpacity")->setValue(50);
          break;
        case 16:
          workspace->findChild<QDoubleSpinBox *>("maskEffectFillOpacity")->setValue(100);
          break;
        case 17:
          save_document(directory + "/anchors.blend");
          break;
        case 18:
          open_document(directory + "/anchors.blend");
          workspace->texture_sets()->setCurrentRow(0);
          viewport->focus_uv({.5, .5}, 1.3);
          update_inspection(bts::InspectionMode::Mask);
          break;
      }
    }
    else if (fill_effect_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto tabs = [&](int index) {
        if (auto *tabs = workspace->findChild<QTabWidget *>("maskFillTabs"))
          tabs->setCurrentIndex(index);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("fill-effect-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          {
            auto &set = document->set(snapshot.materials[0].session_id);
            set.selected_mask = true;
            set.selected_effect =
                bts::TextureDocument::layer(set, set.selected_layer).mask->effects.back().id;
            select_material(0);
          }
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 300, 450, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          workspace->findChild<QAction *>("addMaskFillEffect")->trigger();
          break;
        case 3:
          workspace->findChild<QDoubleSpinBox *>("maskEffectFillValue")->setValue(.25);
          break;
        case 4:
        case 30:
          send_key(*viewport, Qt::Key_Down, Qt::AltModifier);
          break;
        case 5:
        case 7:
        case 27:
        case 29:
        case 31:
          undo();
          break;
        case 6:
        case 28:
          redo();
          break;
        case 8:
          tabs(0);
          break;
        case 9:
          workspace->findChild<QCheckBox *>("maskFillScaleLock")->click();
          break;
        case 10:
          workspace->findChild<QDoubleSpinBox *>("maskFillScale0")->setValue(4);
          break;
        case 11:
          workspace->findChild<QDoubleSpinBox *>("maskFillScale1")->setValue(2);
          break;
        case 12:
          workspace->findChild<QDoubleSpinBox *>("maskFillRotation")->setValue(30);
          break;
        case 13:
          workspace->findChild<QDoubleSpinBox *>("maskFillOffset0")->setValue(.125);
          workspace->findChild<QDoubleSpinBox *>("maskFillOffset1")->setValue(.25);
          break;
        case 14:
        case 15:
          workspace->findChild<QToolButton *>("fillToolbarManipulator")->click();
          break;
        case 16:
          tabs(1);
          workspace->findChild<QComboBox *>("maskFillResource")->setCurrentIndex(1);
          break;
        case 17:
          tabs(0);
          workspace->findChild<QDoubleSpinBox *>("maskFillScale0")->setValue(3);
          break;
        case 18:
          workspace->findChild<QComboBox *>("maskFillFiltering")->setCurrentIndex(2);
          break;
        case 19:
          workspace->findChild<QPushButton *>("maskFillResetMapping")->click();
          break;
        case 20:
          workspace->findChild<QDoubleSpinBox *>("maskFillOffset0")->setValue(2);
          workspace->findChild<QDoubleSpinBox *>("maskFillOffset1")->setValue(2);
          break;
        case 21:
          edit_material([](bts::TextureSet &set) {
            auto &mask = bts::TextureDocument::layer(set, set.selected_layer).mask;
            for (auto &effect : mask->effects)
              if (effect.id == set.selected_effect)
                effect.mapping.wrap = 1;
          });
          break;  // Exercise retained legacy Clamp; not a new shared-editor mode.
        case 22:
          workspace->findChild<QComboBox *>("maskFillWrap")->setCurrentIndex(1);
          break;
        case 23:
          workspace->findChild<QPushButton *>("maskFillResetMapping")->click();
          tabs(1);
          workspace->findChild<QComboBox *>("maskFillResource")->setCurrentIndex(0);
          break;
        case 24:
          save_document(directory + "/fill-effect.blend");
          break;
        case 25:
          open_document(directory + "/fill-effect.blend");
          workspace->texture_sets()->setCurrentRow(0);
          viewport->focus_uv({.5, .5}, 1.3);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 26:
          workspace->findChild<QDoubleSpinBox *>("maskEffectFillValue")->setValue(.4);
          break;
      }
    }
    else if (filter_effect_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("filter-effect-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          {
            auto &set = document->set(snapshot.materials[0].session_id);
            set.selected_mask = true;
            set.selected_effect =
                bts::TextureDocument::layer(set, set.selected_layer).mask->effects.back().id;
            select_material(0);
          }
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 300, 450, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          workspace->findChild<QAction *>("addFilterEffect")->trigger();
          break;
        case 3:
        case 14:
          workspace->findChild<QAction *>("filterInvert")->trigger();
          break;
        case 4:
        case 15:
          send_key(*viewport, Qt::Key_Down, Qt::AltModifier);
          break;
        case 5:
        case 7:
        case 16:
        case 20:
        case 22:
          undo();
          break;
        case 6:
        case 21:
          redo();
          break;
        case 8:
          workspace->findChild<QDoubleSpinBox *>("filterOpacity")->setValue(50);
          break;
        case 9:
          workspace->findChild<QDoubleSpinBox *>("filterOpacity")->setValue(100);
          break;
        case 10:
          workspace->findChild<QAction *>("filterPosterize")->trigger();
          break;
        case 11:
          workspace->findChild<QSpinBox *>("filterSteps")->setValue(2);
          break;
        case 12:
          workspace->findChild<QAction *>("filterNone")->trigger();
          break;
        case 13: {
          auto *list = workspace->layer_stack();
          for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole + 10).toString().isEmpty() &&
                QUuid(list->item(row)->data(Qt::UserRole).toString()) ==
                    document->set(snapshot.materials[0].session_id).selected_layer)
            {
              const auto index = list->model()->index(row, 0);
              const auto point = QPoint(bts::layer_thumbnail_x(index, list->visualRect(index)) +
                                            12,
                                        list->visualRect(index).center().y());
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonPress,
                               point,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonRelease,
                               point,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::NoModifier);
              break;
            }
          workspace->findChild<QAction *>("addFilterEffect")->trigger();
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        }
        case 17:
          save_document(directory + "/filters.blend");
          break;
        case 18:
          open_document(directory + "/filters.blend");
          workspace->texture_sets()->setCurrentRow(0);
          {
            auto &set = document->set(snapshot.materials[0].session_id);
            set.selected_mask = true;
            update_inspection(bts::InspectionMode::Mask);
            set.selected_mask = false;
            update_inspection(bts::InspectionMode::BaseColor);
          }
          break;
        case 19:
          workspace->findChild<QDoubleSpinBox *>("filterOpacity")->setValue(50);
          break;
        case 23:
        case 24:
          workspace->layer_stack()->currentItem()->setCheckState(sets_stage == 23 ? Qt::Unchecked :
                                                                                    Qt::Checked);
          break;
      }
    }
    else if (paint_effect_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto stroke =
          [&](double x, double low, double high, bool erase = false, bool cancel = false) {
            brush = bts::BrushSettings{};
            brush.hardness = 1;
            brush.diameter = 5;
            brush.mask_value = 0;
            brush.values.base_color = {1, .1, .05, 1};
            brush.erase = erase;
            select_material(selected_material);
            QApplication::processEvents();
            viewport->repaint();
            const auto start = viewport->uv_point({x, low}), end = viewport->uv_point({x, high});
            if (cancel) {
              send_mouse_event(*viewport,
                               QEvent::MouseButtonPress,
                               start,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(
                  *viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
              send_key(*viewport, Qt::Key_Escape);
            }
            else
              send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
            viewport->cursor_visible_ = false;
          };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("paint-effect-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.precision[0] = bts::ChannelPrecision::RGB32F;
            bts::TextureDocument::add_fill(set);
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.values.base_color = {.2, .4, .8, 1};
            bts::LevelsEffect levels;
            levels.output_low = 1;
            levels.output_high = 0;
            layer.effects.push_back(levels);
            bts::TextureDocument::add_compare_mask(set);
            layer.mask->effects.back().left = 3;
            layer.mask->effects.back().right = 3;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 300, 450, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          workspace->findChild<QAction *>("addPaintEffect")->trigger();
          break;
        case 3:
          stroke(.8, .72, .9);
          break;
        case 4:
        case 9:
        case 14:
        case 23:
        case 26:
        case 30:
          undo();
          break;
        case 5:
        case 15:
        case 24:
          redo();
          break;
        case 6:
        case 7:
          workspace->layer_stack()->currentItem()->setCheckState(sets_stage == 6 ? Qt::Unchecked :
                                                                                   Qt::Checked);
          break;
        case 8:
          send_key(*viewport, Qt::Key_Down, Qt::AltModifier);
          break;
        case 10:
          workspace->findChild<QDoubleSpinBox *>("paintEffectOpacity")->setValue(50);
          break;
        case 11:
          workspace->findChild<QDoubleSpinBox *>("paintEffectOpacity")->setValue(100);
          break;
        case 12: {
          auto *list = workspace->layer_stack();
          for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole + 10).toString().isEmpty() &&
                QUuid(list->item(row)->data(Qt::UserRole).toString()) ==
                    document->set(snapshot.materials[0].session_id).selected_layer)
            {
              const auto index = list->model()->index(row, 0);
              const auto point = QPoint(bts::layer_thumbnail_x(index, list->visualRect(index)) +
                                            12,
                                        list->visualRect(index).center().y());
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonPress,
                               point,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonRelease,
                               point,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::NoModifier);
              break;
            }
          workspace->findChild<QAction *>("addPaintEffect")->trigger();
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        }
        case 13:
          stroke(.92, .72, .9);
          break;
        case 16:
          workspace->findChild<QDoubleSpinBox *>("paintEffectOpacity")->setValue(50);
          break;
        case 17:
          workspace->findChild<QDoubleSpinBox *>("paintEffectOpacity")->setValue(100);
          break;
        case 18:
        case 19: {
          auto *combo = workspace->findChild<QComboBox *>("paintEffectBlend");
          combo->setCurrentIndex(combo->findData(sets_stage == 18 ? 2 : 0));
          break;
        }
        case 20:
          save_document(directory + "/paint-effects.blend");
          break;
        case 21:
          open_document(directory + "/paint-effects.blend");
          workspace->texture_sets()->setCurrentRow(0);
          {
            auto &set = document->set(snapshot.materials[0].session_id);
            set.selected_mask = true;
            update_inspection(bts::InspectionMode::Mask);
            set.selected_mask = false;
            update_inspection(bts::InspectionMode::BaseColor);
          }
          break;
        case 22:
          stroke(.92, .2, .4);
          break;
        case 25:
          stroke(.92, .72, .9, true);
          break;
        case 27:
          stroke(.86, .2, .4, false, true);
          break;
        case 28: {
          auto *list = workspace->layer_stack();
          for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole + 12).toBool() &&
                list->item(row)->data(Qt::UserRole + 13).toInt() ==
                    int(bts::MaskEffectKind::Paint))
            {
              list->setCurrentRow(row);
              break;
            }
          if (!document->set(snapshot.materials[0].session_id).selected_mask) {
            auto &set = document->set(snapshot.materials[0].session_id);
            const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            set.selected_mask = true;
            set.selected_effect = layer.mask->effects.back().id;
            select_material(selected_material);
          }
          update_inspection(bts::InspectionMode::Mask);
          break;
        }
        case 29: {
          brush = bts::BrushSettings{};
          brush.mask_value = 0;
          brush.hardness = 1;
          brush.diameter = 8;
          select_material(selected_material);
          QApplication::processEvents();
          viewport->repaint();
          QVector3D center;
          int count = 0;
          for (const auto &triangle : snapshot.uv_triangles)
            if (triangle.material == 0)
              for (const auto &position : triangle.positions) {
                center += QVector3D(position[0], position[1], position[2]);
                ++count;
              }
          center /= count;
          const auto point = viewport->scene_point({center[0], center[1], center[2]});
          send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
          viewport->cursor_visible_ = false;
          break;
        }
      }
    }
    else if (compare_mask_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto combo = [&](const char *name, int index) {
        workspace->findChild<QComboBox *>(name)->setCurrentIndex(index);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("compare-mask-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.precision[4] = bts::ChannelPrecision::L32F;
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.bump = .8;
            bts::TextureDocument::add_fill(set);
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.values.bump = 0;
            layer.blending[4] = bts::LayerBlend::Multiply;
            bts::LevelsEffect levels;
            levels.channel = 0;
            layer.effects.push_back(levels);
            bts::TextureDocument::add_mask(set, 0);
            bts::MaskEffect fill;
            fill.kind = bts::MaskEffectKind::Fill;
            fill.fill = .25;
            layer.mask->effects.push_back(fill);
            set.selected_effect = fill.id;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          workspace->findChild<QAction *>("addCompareMask")->trigger();
          break;
        case 3:
          combo("compareRight", 3);
          break;
        case 4:
          combo("compareOperator", 0);
          break;
        case 5:
          combo("compareOperator", 1);
          break;
        case 6:
          workspace->findChild<QDoubleSpinBox *>("compareTolerance")->setValue(.75);
          break;
        case 7:
          combo("compareLeft", 3);
          break;
        case 8:
          combo("compareLeft", 1);
          combo("compareOperator", 0);
          break;
        case 9:
          combo("compareLeft", 0);
          combo("compareOperator", 2);
          break;
        case 10:
          edit_material(
              [](bts::TextureSet &set) { set.layers[set.layers.size() - 2].values.bump = .2; });
          break;
        case 11:
        case 14:
        case 19:
        case 23:
        case 25:
          undo();
          break;
        case 12:
          combo("compareLeft", 1);
          break;
        case 13:
          edit_material([](bts::TextureSet &set) { set.layers.back().values.bump = .8; });
          break;
        case 15:
          workspace->findChild<QDoubleSpinBox *>("compareHardness")->setValue(0);
          break;
        case 16:
          workspace->findChild<QDoubleSpinBox *>("compareHardness")->setValue(.9);
          break;
        case 17:
          edit_material([](bts::TextureSet &set) {
            auto &effects = set.layers.back().mask->effects;
            bts::MaskEffect fill;
            fill.kind = bts::MaskEffectKind::Fill;
            fill.fill = .25;
            effects.insert(effects.begin(), fill);
          });
          combo("compareLeft", 2);
          combo("compareOperator", 0);
          break;
        case 18:
          edit_material(
              [](bts::TextureSet &set) { set.layers.back().mask->effects[0].fill = .75; });
          break;
        case 20:
          save_document(directory + "/compare-mask.blend");
          break;
        case 21:
          open_document(directory + "/compare-mask.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 22:
          workspace->findChild<QDoubleSpinBox *>("compareConstant")->setValue(.1);
          break;
        case 24:
          redo();
          break;
        case 26:
        case 27: {
          auto *list = workspace->layer_stack();
          for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole + 10).toString() ==
                document->set(snapshot.materials[0].session_id).selected_effect.toString())
            {
              list->item(row)->setCheckState(sets_stage == 26 ? Qt::Unchecked : Qt::Checked);
              break;
            }
          break;
        }
      }
    }
    else if (levels_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto component = [&](int value) {
        auto *combo = workspace->findChild<QComboBox *>("levelsComponent");
        combo->setCurrentIndex(combo->findData(value));
      };
      const auto invert = [&] { workspace->findChild<QPushButton *>("levelsInvert")->click(); };
      const auto reset = [&] { workspace->findChild<QPushButton *>("levelsReset")->click(); };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("levels-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.precision[0] = bts::ChannelPrecision::RGB32F;
            set.precision[2] = bts::ChannelPrecision::L32F;
            bts::TextureDocument::add_fill(set);
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.values.base_color = {.2, .4, .8, 1};
            layer.values.roughness = .25;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          {
            auto *context = workspace->findChild<QComboBox *>("layerChannelContext");
            context->setCurrentIndex(context->findData(0));
          }
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 2:
          workspace->findChild<QAction *>("addLevelsEffect")->trigger();
          break;
        case 3:
          component(1);
          invert();
          break;
        case 4:
          component(2);
          invert();
          break;
        case 5:
          component(3);
          workspace->findChild<QDoubleSpinBox *>("levelsGamma")->setValue(2);
          break;
        case 6:
          component(0);
          invert();
          break;
        case 7:
          undo();
          break;
        case 8:
          redo();
          break;
        case 9:
          reset();
          break;
        case 10:
          workspace->findChild<QPushButton *>("levelsAuto")->click();
          break;
        case 11:
          workspace->findChild<QDoubleSpinBox *>("levelsOutputLow")->setValue(-1);
          workspace->findChild<QDoubleSpinBox *>("levelsOutputHigh")->setValue(2);
          break;
        case 12:
          workspace->findChild<QCheckBox *>("levelsClamp")->click();
          break;
        case 13:
          reset();
          break;
        case 14: {
          auto *combo = workspace->findChild<QComboBox *>("levelsChannel");
          combo->setCurrentIndex(combo->findData(2));
          update_inspection(bts::InspectionMode::Roughness);
          break;
        }
        case 15:
          invert();
          break;
        case 16:
          workspace->findChild<QCheckBox *>("levelsClamp")->click();
          break;
        case 17:
          reset();
          break;
        case 18: {
          auto *combo = workspace->findChild<QComboBox *>("levelsChannel");
          combo->setCurrentIndex(combo->findData(0));
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        }
        case 19:
          component(1);
          invert();
          component(2);
          invert();
          component(3);
          workspace->findChild<QDoubleSpinBox *>("levelsGamma")->setValue(2);
          break;
        case 20:
          save_document(directory + "/levels.blend");
          break;
        case 21:
          open_document(directory + "/levels.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 22:
          component(1);
          break;
        case 23:
          component(3);
          break;
        case 24:
          workspace->findChild<QDoubleSpinBox *>("levelsGamma")->setValue(1);
          break;
        case 25:
        case 27:
        case 29:
        case 31:
          undo();
          break;
        case 26:
          redo();
          break;
        case 28: {
          auto *graph = workspace->findChild<QWidget *>("levelsInputRamp");
          const QPointF point(9 + (graph->width() - 18) * .5, 32);
          send_mouse_event(*graph,
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*graph,
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 30:
          edit_material([](bts::TextureSet &set) {
            set.channels[1] = false;
            set.channels[4] = false;
          });
          break;
      }
    }
    else if (mesh_generator_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto input = [&](const QString &name, bool cancel = false) {
        sets_demo.stop();
        if (cancel)
          QTimer::singleShot(100, workspace, [&] {
            auto *dialog = workspace->findChild<QFileDialog *>("generatorInputDialog");
            if (!dialog)
              qFatal("Generator input dialog missing");
            dialog->reject();
          });
        else
          bts::select_gui_test_file(
              workspace, "generatorInputDialog", harness.value("generator-fixtures") + "/" + name);
        workspace->findChild<QPushButton *>("generatorReplaceInput")->click();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("mesh-generator-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.mesh_maps[4].reset();
            set.mesh_maps[5].reset();
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::add_mask(set, 0);
            bts::MaskEffect fill;
            fill.kind = bts::MaskEffectKind::Fill;
            fill.fill = .75;
            auto &mask = *bts::TextureDocument::layer(set, set.selected_layer).mask;
            mask.effects.push_back(fill);
            set.selected_effect = fill.id;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          workspace->findChild<QAction *>("addMaskGenerator")->trigger();
          break;
        case 3:
        case 23:
          workspace->findChild<QAction *>("generatorWorldHeight")->trigger();
          break;
        case 4:
          input("position-forward.png");
          break;
        case 5:
          input("position-flipped.png");
          break;
        case 6:
        case 8:
        case 10:
        case 12:
        case 16:
        case 18:
        case 24:
        case 26:
        case 30:
        case 32:
          undo();
          break;
        case 7:
        case 17:
        case 25:
          redo();
          break;
        case 9:
          workspace->findChild<QComboBox *>("generatorPositionAxis")->setCurrentIndex(0);
          break;
        case 11:
          workspace->findChild<QDoubleSpinBox *>("generatorContrast")->setValue(2);
          break;
        case 13:
          workspace->findChild<QAction *>("generatorCurvature")->trigger();
          break;
        case 14:
          input("curvature-forward.png");
          break;
        case 15:
          input("curvature-flipped.png");
          break;
        case 19:
          input("invalid.png");
          break;
        case 20:
          input("", true);
          break;
        case 21:
          save_document(directory + "/mesh-generator.blend");
          break;
        case 22:
          open_document(directory + "/mesh-generator.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 27:
        case 28:
          workspace->layer_stack()->currentItem()->setCheckState(sets_stage == 27 ? Qt::Unchecked :
                                                                                    Qt::Checked);
          break;
        case 29:
          workspace->findChild<QDoubleSpinBox *>("generatorOpacity")->setValue(50);
          break;
        case 31:
          workspace->findChild<QCheckBox *>("generatorInvert")->click();
          break;
        case 33:
          workspace->findChild<QAction *>("generatorAmbientOcclusion")->trigger();
          break;
        case 34:
          input("ao-forward.png");
          break;
        case 35:
          input("ao-flipped.png");
          break;
        case 36:
          undo();
          break;
        case 37:
          workspace->findChild<QAction *>("generatorThickness")->trigger();
          break;
        case 38:
          input("thickness-forward.png");
          break;
        case 39:
          workspace->findChild<QAction *>("generatorDirt")->trigger();
          break;
        case 40:
          workspace->findChild<QDoubleSpinBox *>("generatorDirtCurvature")->setValue(1);
          break;
        case 41:
          undo();
          break;
        case 42: {
          auto *role = workspace->findChild<QComboBox *>("generatorInputRole");
          role->setCurrentIndex(role->findData(4));
          input("curvature-flipped.png");
          break;
        }
        case 43:
          undo();
          break;
        case 44:
          workspace->findChild<QAction *>("generatorWorldNormalLight")->trigger();
          break;
        case 45:
          input("normal-up.png");
          break;
        case 46:
          workspace->findChild<QDoubleSpinBox *>("generatorLightZ")->setValue(0);
          break;
        case 47:
          workspace->findChild<QDoubleSpinBox *>("generatorLightX")->setValue(1);
          break;
        case 48:
          input("normal-side.png");
          break;
        case 49:
          undo();
          break;
        case 50:
          workspace->findChild<QDoubleSpinBox *>("generatorLightZ")->setValue(1);
          break;
        case 51:
          save_document(directory + "/mesh-generators.blend");
          break;
        case 52:
          open_document(directory + "/mesh-generators.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 53:
          workspace->findChild<QDoubleSpinBox *>("generatorLightX")->setValue(-1);
          break;
        case 54:
          undo();
          break;
        case 55:
        case 56:
          workspace->layer_stack()->currentItem()->setCheckState(sets_stage == 55 ? Qt::Unchecked :
                                                                                    Qt::Checked);
          break;
        case 57:
          input("invalid.png");
          break;
        case 58:
          input("", true);
          break;
        case 59:
          edit_material([](bts::TextureSet &set) { set.mesh_maps[1].reset(); });
          break;
        case 60:
          sets_demo.stop();
          QTimer::singleShot(100, workspace, [&] {
            auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
            if (!dialog)
              qFatal("Generator Bake Mesh Maps dialog missing");
            workspace->setProperty("generatorBakeOpened", dialog->isVisible());
            dialog->reject();
          });
          workspace->findChild<QPushButton *>("generatorBakeMaps")->click();
          sets_demo.start(600);
          break;
        case 61:
          undo();
          break;
        case 62:
          workspace->findChild<QToolButton *>("addPaintLayer")->click();
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::add_displacement_channel(set);
            set.ao_channel = true;
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.name = "Generated Paint content";
            layer.channels.fill(true);
            layer.channels[3] = false;
            layer.ao_enabled = true;
            layer.displacement.enabled = true;
            set.selected_mask = false;
            set.selected_effect = QUuid();
          });
          update_inspection(bts::InspectionMode::Material);
          break;
        case 63:
        case 80:
          workspace->findChild<QAction *>("addMaskGenerator")->trigger();
          if (sets_stage == 80) {
            workspace->findChild<QAction *>("generatorCurvature")->trigger();
            content_test_effect = document->set(snapshot.materials[0].session_id).selected_effect;
          }
          break;
        case 64:
          workspace->findChild<QAction *>("generatorWorldHeight")->trigger();
          break;
        case 65:
        case 66:
        case 78: {
          auto *channels = workspace->findChild<QComboBox *>("generatorChannel");
          channels->setCurrentIndex(channels->findData(sets_stage == 65 ? 2 :
                                                       sets_stage == 66 ? 4 :
                                                                          14));
          break;
        }
        case 67:
          workspace->findChild<QAction *>("generatorThickness")->trigger();
          break;
        case 68:
          input("thickness-flipped.png");
          break;
        case 69:
        case 71:
        case 82:
        case 86:
          undo();
          break;
        case 70:
          workspace->findChild<QDoubleSpinBox *>("generatorOpacity")->setValue(50);
          break;
        case 72:
        case 73:
          workspace->layer_stack()->currentItem()->setCheckState(sets_stage == 72 ? Qt::Unchecked :
                                                                                    Qt::Checked);
          break;
        case 74:
          workspace->findChild<QAction *>("generatorAmbientOcclusion")->trigger();
          break;
        case 75:
          workspace->findChild<QAction *>("generatorDirt")->trigger();
          break;
        case 76:
          workspace->findChild<QAction *>("generatorWorldNormalLight")->trigger();
          break;
        case 77:
          workspace->findChild<QDoubleSpinBox *>("generatorLightX")->setValue(1);
          break;
        case 79:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          break;
        case 81: {
          auto *blend = workspace->findChild<QComboBox *>("generatorBlend");
          blend->setCurrentIndex(blend->findData(int(bts::LayerBlend::Screen)));
          break;
        }
        case 83:
          save_document(directory + "/content-generators.blend");
          break;
        case 84:
          open_document(directory + "/content-generators.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Material);
          break;
        case 85:
          workspace->findChild<QDoubleSpinBox *>("generatorContrast")->setValue(2);
          break;
        case 87:
          workspace->findChild<QAction *>("addBlackMask")->trigger();
          workspace->findChild<QAction *>("addMaskGenerator")->trigger();
          workspace->findChild<QAction *>("generatorWorldHeight")->trigger();
          content_test_mask_effect =
              document->set(snapshot.materials[0].session_id).selected_effect;
          break;
        case 88:
        case 91: {
          const bool mask = sets_stage == 91;
          const auto id = mask ? content_test_mask_effect : content_test_effect;
          const auto parent = document->set(snapshot.materials[0].session_id).selected_layer;
          auto *list = workspace->layer_stack();
          bool parent_found = false;
          for (int i = 0; i < list->count(); ++i)
            if (QUuid(list->item(i)->data(Qt::UserRole).toString()) == parent &&
                list->item(i)->data(Qt::UserRole + 10).toString().isEmpty())
            {
              list->scrollToItem(list->item(i));
              const auto index = list->indexFromItem(list->item(i));
              const auto rect = list->visualRect(index);
              const auto point = mask ? bts::layer_mask_rect(index, rect).center() :
                                        QPoint(bts::layer_thumbnail_x(index, rect) + 12,
                                               rect.center().y());
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonPress,
                               point,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*list->viewport(),
                               QEvent::MouseButtonRelease,
                               point,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::NoModifier);
              parent_found = true;
              break;
            }
          if (!parent_found)
            qFatal("Content generator parent row missing");
          bool found = false;
          for (int i = 0; i < list->count(); ++i)
            if (QUuid(list->item(i)->data(Qt::UserRole + 10).toString()) == id) {
              list->scrollToItem(list->item(i));
              list->setCurrentRow(i);
              found = true;
              break;
            }
          if (!found)
            qFatal("Content generator effect row missing");
          break;
        }
        case 89:
          save_document(directory + "/content-coexist.blend");
          break;
        case 90:
          open_document(directory + "/content-coexist.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Material);
          break;
      }
    }
    else if (mask_effects_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto select = [&](bts::MaskEffectKind kind) {
        const auto &set = document->set(snapshot.materials[0].session_id);
        const auto &mask = *bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                        set.selected_layer)
                                .mask;
        QUuid id;
        for (const auto &effect : mask.effects)
          if (effect.kind == kind)
            id = effect.id;
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole + 10).toString()) == id) {
            list->setCurrentRow(row);
            QApplication::processEvents();
            return;
          }
        qFatal("Mask effect child missing");
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("mask-effects-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::add_mask(set, 0);
            auto &mask = *bts::TextureDocument::layer(set, set.selected_layer).mask;
            mask.effects.push_back(bts::MaskEffect{});
            set.selected_effect = mask.effects[0].id;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          workspace->findChild<QAction *>("addMaskFillEffect")->trigger();
          break;
        case 3:
          workspace->findChild<QDoubleSpinBox *>("maskEffectFillValue")->setValue(.25);
          break;
        case 4:
          workspace->findChild<QAction *>("addLevelsEffect")->trigger();
          break;
        case 5:
        case 16:
          workspace->findChild<QPushButton *>("levelsInvert")->click();
          break;
        case 6:
        case 19:
          send_key(*viewport, Qt::Key_Down, Qt::AltModifier);
          break;
        case 7:
        case 13:
        case 20:
        case 22:
          undo();
          break;
        case 8:
        case 21:
          redo();
          break;
        case 9:
          send_key(*viewport, Qt::Key_Up, Qt::AltModifier);
          break;
        case 10:
        case 11:
          workspace->layer_stack()->currentItem()->setCheckState(sets_stage == 10 ? Qt::Unchecked :
                                                                                    Qt::Checked);
          break;
        case 12:
          select(bts::MaskEffectKind::Fill);
          workspace->findChild<QDoubleSpinBox *>("maskEffectFillOpacity")->setValue(50);
          break;
        case 14:
          select(bts::MaskEffectKind::Levels);
          workspace->findChild<QCheckBox *>("levelsClamp")->click();
          break;
        case 15:
          workspace->findChild<QPushButton *>("levelsReset")->click();
          break;
        case 17:
          save_document(directory + "/mask-effects.blend");
          break;
        case 18:
          open_document(directory + "/mask-effects.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Mask);
          break;
      }
    }
    else if (mask_replace_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("mask-replace-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.precision[0] = bts::ChannelPrecision::RGB32F;
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {
                .5, .5, .5, 1};
            bts::TextureDocument::add_mask(set, 0);
            auto &mask = *bts::TextureDocument::layer(set, set.selected_layer).mask;
            mask.effects.push_back(bts::MaskEffect{});
            set.selected_effect = mask.effects[0].id;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 2: {
          auto *button = workspace->findChild<QToolButton *>("addLayerMask");
          if (!button->isEnabled())
            qFatal("Mask replacement menu disabled");
          workspace->findChild<QAction *>("addWhiteMask")->trigger();
          break;
        }
        case 3:
          undo();
          break;
        case 4:
          redo();
          break;
        case 5:
          save_document(directory + "/mask-replace.blend");
          break;
        case 6:
          open_document(directory + "/mask-replace.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 7:
          workspace->findChild<QAction *>("addBlackMask")->trigger();
          break;
        case 8:
          undo();
          break;
        case 9: {
          auto *list = workspace->layer_stack();
          const auto index = list->currentIndex();
          const auto point = bts::layer_mask_rect(index, list->visualRect(index)).center();
          QContextMenuEvent event(
              QContextMenuEvent::Mouse, point, list->viewport()->mapToGlobal(point));
          QApplication::sendEvent(list->viewport(), &event);
          const auto menus = workspace->findChildren<QMenu *>("maskContextMenu");
          auto *popup = menus.last();
          popup->findChild<QAction *>("removeMask")->trigger();
          popup->close();
          break;
        }
        case 10:
          undo();
          break;
      }
    }
    else if (smart_mask_demo) {
      const auto preset = [&]() -> const bts::StudioAsset & {
        for (const auto &asset : assets.assets())
          if (asset.smart_mask && asset.name == "Vertical mask fixture")
            return asset;
        throw std::runtime_error("Saved mask generator missing");
      };
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto stroke = [&](double x) {
        brush = bts::BrushSettings{};
        brush.hardness = 1;
        brush.diameter = 5;
        brush.mask_value = 1;
        select_material(selected_material);
        QApplication::processEvents();
        viewport->repaint();
        const auto start = viewport->uv_point({x, .25});
        send_drag(*viewport,
                  start,
                  viewport->uv_point({x, .75}) - start,
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      const auto drop = [&](bool replace) {
        auto *list = workspace->layer_stack();
        const auto index = list->currentIndex();
        const auto point = QPoint(bts::layer_thumbnail_x(index, list->visualRect(index)) + 12,
                                  list->visualRect(index).center().y());
        QMimeData mime;
        mime.setData("application/x-bts-asset", preset().id.toString().toUtf8());
        const auto modifiers = replace ? Qt::ControlModifier : Qt::NoModifier;
        QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, modifiers);
        QApplication::sendEvent(list->viewport(), &enter);
        QDropEvent event(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, modifiers);
        QApplication::sendEvent(list->viewport(), &event);
        if (!enter.isAccepted() || !event.isAccepted())
          qFatal("Smart-mask drop rejected");
        update_inspection(bts::InspectionMode::Mask);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("smart-mask-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.layers = {bts::TextureLayer{}};
            set.selected_layer = set.layers[0].id;
            set.selected_effect = QUuid();
            set.selected_mask = false;
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {0, 0, 0, 1};
            bts::TextureDocument::add_fill(set);
            smart_mask_source = set.selected_layer;
            bts::TextureDocument::add_mask(set, 0);
            auto &mask = *bts::TextureDocument::layer(set, set.selected_layer).mask;
            if (!smart_mask_loaded.isNull())
              mask = *preset().smart_mask;
            else {
              bts::MaskEffect effect;
              effect.enabled = false;
              effect.colors = {{1, 0, 0}};
              mask.effects.push_back(effect);
              if (qEnvironmentVariableIsSet("BTS_SMART_MASK_BITMAP")) {
                auto image = std::make_shared<bts::FillTexture>();
                image->asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
                image->hash = std::string(64, 'c');
                image->name = "Smart-mask bitmap fixture";
                image->raw = true;
                image->width = image->height = 8;
                auto pixels = std::make_shared<std::vector<bts::FillTexture::Pixel>>(64);
                for (int y = 0; y < 8; ++y)
                  for (int x = 0; x < 8; ++x)
                    (*pixels)[y * 8 + x] = {float(x + y) / 14, 0, 0, 1};
                image->pixels = std::move(pixels);
                bts::MaskEffect fill;
                fill.kind = bts::MaskEffectKind::Fill;
                fill.opacity = .65f;
                fill.mapping.projection.texture = std::move(image);
                mask.effects.push_back(std::move(fill));
              }
            }
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 2:
          if (smart_mask_loaded.isNull())
            stroke(.73);
          break;
        case 3:
          if (smart_mask_loaded.isNull()) {
            QTimer::singleShot(100, workspace, [&] {
              auto *dialog = window->findChild<QInputDialog *>("smartMaskNameDialog");
              if (!dialog)
                qFatal("Smart-mask naming dialog missing");
              dialog->setTextValue("Vertical mask fixture");
              dialog->accept();
            });
            auto *list = workspace->layer_stack();
            const auto index = list->currentIndex();
            const auto point = bts::layer_mask_rect(index, list->visualRect(index)).center();
            QContextMenuEvent event(
                QContextMenuEvent::Mouse, point, list->viewport()->mapToGlobal(point));
            QApplication::sendEvent(list->viewport(), &event);
            const auto menus = workspace->findChildren<QMenu *>("maskContextMenu");
            auto *popup = menus.last();
            popup->findChild<QAction *>("createSmartMask")->trigger();
            popup->close();
          }
          break;
        case 4: {
          auto *categories = workspace->findChild<QListWidget *>("shelfCategories");
          const auto matches = categories->findItems("Mask generators", Qt::MatchExactly);
          categories->setCurrentItem(matches.first());
          workspace->refresh_alpha_shelf();
          auto *shelf = workspace->findChild<QListWidget *>("shelfAssets");
          const auto items = shelf->findItems("Vertical mask fixture", Qt::MatchExactly);
          if (items.isEmpty() || !(items.first()->flags() & Qt::ItemIsEnabled))
            qFatal("Smart-mask shelf entry unavailable");
          shelf->setCurrentItem(items.first());
          break;
        }
        case 5:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          edit_material([&](bts::TextureSet &set) {
            smart_mask_target = set.selected_layer;
            bts::TextureDocument::add_mask(set, 0);
            bts::MaskEffect effect;
            effect.enabled = false;
            effect.colors = {{0, 1, 0}};
            bts::TextureDocument::layer(set, set.selected_layer).mask->effects.push_back(effect);
          });
          update_inspection(bts::InspectionMode::Mask);
          stroke(.88);
          break;
        case 6:
          drop(false);
          break;
        case 7:
          undo();
          break;
        case 8:
          redo();
          break;
        case 9:
          drop(true);
          break;
        case 10:
          undo();
          break;
        case 11:
          redo();
          break;
        case 12:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          smart_mask_empty = document->set(snapshot.materials[0].session_id).selected_layer;
          break;
        case 13:
          drop(false);
          break;
        case 14:
          undo();
          break;
        case 15:
          redo();
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 16:
          save_document(directory + "/smart-mask.blend");
          break;
        case 17:
          open_document(directory + "/smart-mask.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Mask);
          break;
        case 18:
          stroke(.93);
          break;
        case 19:
          undo();
          break;
        case 20:
          redo();
          break;
        case 21:
          undo();
          break;
      }
    }
    else if (mask_copy_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto target = [&](QUuid id) {
        auto *list = workspace->layer_stack();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == id &&
              list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
          {
            list->setCurrentRow(row);
            break;
          }
        QApplication::processEvents();
        for (int row = 0; row < list->count(); ++row)
          if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == id &&
              list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
          {
            const auto index = list->model()->index(row, 0);
            const auto point = bts::layer_mask_rect(index, list->visualRect(index)).center();
            send_mouse_event(*list->viewport(),
                             QEvent::MouseButtonPress,
                             point,
                             Qt::LeftButton,
                             Qt::LeftButton,
                             Qt::NoModifier);
            send_mouse_event(*list->viewport(),
                             QEvent::MouseButtonRelease,
                             point,
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
            break;
          }
        QApplication::processEvents();
        update_inspection(bts::InspectionMode::Mask);
      };
      const auto menu = [&](const QString &action) {
        auto *list = workspace->layer_stack();
        const auto index = list->currentIndex();
        const auto point = bts::layer_mask_rect(index, list->visualRect(index)).center();
        QContextMenuEvent event(
            QContextMenuEvent::Mouse, point, list->viewport()->mapToGlobal(point));
        QApplication::sendEvent(list->viewport(), &event);
        const auto menus = workspace->findChildren<QMenu *>("maskContextMenu");
        auto *popup = menus.isEmpty() ? nullptr : menus.last();
        if (!popup || !popup->isVisible())
          qFatal("Mask context popup missing");
        auto *copy = popup->findChild<QAction *>("copyMaskContent"),
             *paste = popup->findChild<QAction *>("pasteIntoMask");
        mask_copy_menus.append(QJsonObject{{"stage", sets_stages[sets_stage]},
                                           {"copy", copy && copy->isEnabled()},
                                           {"paste", paste && paste->isEnabled()}});
        auto *command = popup->findChild<QAction *>(action);
        if (!command || !command->isEnabled())
          qFatal("Mask clipboard command disabled");
        command->trigger();
        popup->close();
      };
      const auto stroke = [&](double x) {
        brush = bts::BrushSettings{};
        brush.hardness = 1;
        brush.diameter = 5;
        brush.mask_value = 1;
        select_material(selected_material);
        QApplication::processEvents();
        viewport->repaint();
        const auto start = viewport->uv_point({x, .25});
        send_drag(*viewport,
                  start,
                  viewport->uv_point({x, .75}) - start,
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("mask-copy-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([&](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.layers = {bts::TextureLayer{}};
            set.selected_layer = set.layers[0].id;
            set.selected_mask = false;
            set.selected_effect = QUuid();
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {0, 0, 0, 1};
            bts::TextureDocument::add_fill(set);
            mask_copy_source = set.selected_layer;
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {1, 1, 1, 1};
            bts::TextureDocument::add_mask(set, 0);
            bts::MaskEffect retained;
            retained.enabled = false;
            retained.colors = {{1, 0, 0}};
            bts::TextureDocument::layer(set, set.selected_layer).mask->effects.push_back(retained);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          target(mask_copy_source);
          break;
        case 2:
          stroke(.73);
          break;
        case 3:
          menu("copyMaskContent");
          break;
        case 4:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          edit_material([&](bts::TextureSet &set) {
            mask_copy_destination = set.selected_layer;
            bts::TextureDocument::add_mask(set, 0);
            bts::TextureDocument::layer(set, set.selected_layer)
                .mask->effects.push_back(bts::MaskEffect{});
          });
          target(mask_copy_destination);
          break;
        case 5:
          menu("pasteIntoMask");
          break;
        case 6:
          undo();
          break;
        case 7:
          redo();
          break;
        case 8:
          target(mask_copy_source);
          stroke(.88);
          break;
        case 9:
          target(mask_copy_destination);
          menu("pasteIntoMask");
          break;
        case 10:
          target(mask_copy_source);
          menu("copyMaskContent");
          break;
        case 11:
          target(mask_copy_destination);
          menu("pasteIntoMask");
          break;
        case 12:
          undo();
          break;
        case 13:
          redo();
          break;
        case 14:
          save_document(directory + "/mask-copy.blend");
          break;
        case 15:
          open_document(directory + "/mask-copy.blend");
          workspace->texture_sets()->setCurrentRow(0);
          target(mask_copy_destination);
          break;
        case 16:
          stroke(.93);
          break;
        case 17:
          undo();
          break;
        case 18:
          redo();
          break;
        case 19:
          undo();
          break;
      }
    }
    else if (color_selection_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto click = [&](QPointF point) {
        send_mouse_event(*viewport,
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*viewport,
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("color-selection-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.layers = {bts::TextureLayer{}};
            set.selected_layer = set.layers[0].id;
            set.selected_effect = QUuid();
            set.selected_mask = false;
            set.precision[0] = bts::ChannelPrecision::RGB32F;
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {0, 0, 0, 1};
            bts::TextureDocument::add_fill(set);
            bts::TextureDocument::layer(set, set.selected_layer).values.base_color = {1, 1, 1, 1};
            std::vector<float> pixels(128 * 128 * 4);
            for (int y = 0; y < 128; ++y)
              for (int x = 0; x < 128; ++x) {
                const auto i = (y * 128 + x) * 4;
                pixels[i] = x < 64;
                pixels[i + 1] = x >= 64;
                pixels[i + 3] = 1;
              }
            auto data = std::make_shared<const bts::MeshMapPixels>(128, std::move(pixels));
            bts::MeshMap map;
            map.name = "Two-color ID witness";
            map.tiles[1001] = {128, data->hash, data};
            set.mesh_maps[2] = map;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->focus_uv({.5, .5}, 1.3);
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 300, 450, 0});
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 2:
          workspace->findChild<QAction *>("addColorSelectionMask")->trigger();
          break;
        case 3:
        case 5:
          workspace->findChild<QPushButton *>("selectionPickColor")->click();
          break;
        case 4:
          QApplication::processEvents();
          viewport->repaint();
          click(viewport->uv_point({.25, .5}));
          if (bts::TextureDocument::layer(
                  document->set(snapshot.materials[0].session_id),
                  document->set(snapshot.materials[0].session_id).selected_layer)
                  .mask->effects[0]
                  .colors.size() != 1)
            qFatal("UV ID color was not selected");
          break;
        case 6: {
          QApplication::processEvents();
          viewport->repaint();
          QVector3D low(1e9, 1e9, 1e9), high(-1e9, -1e9, -1e9);
          for (const auto &triangle : snapshot.uv_triangles)
            if (triangle.material == 0)
              for (const auto &position : triangle.positions)
                for (int c = 0; c < 3; ++c) {
                  low[c] = std::min(low[c], float(position[c]));
                  high[c] = std::max(high[c], float(position[c]));
                }
          click(viewport->scene_point((low + high) * .5));
          if (bts::TextureDocument::layer(
                  document->set(snapshot.materials[0].session_id),
                  document->set(snapshot.materials[0].session_id).selected_layer)
                  .mask->effects[0]
                  .colors.size() != 2)
            qFatal("3D ID color was not added");
          break;
        }
        case 7:
          workspace->findChild<QListWidget *>("selectionColors")->setCurrentRow(1);
          workspace->findChild<QPushButton *>("selectionRemoveColor")->click();
          break;
        case 8:
          workspace->findChild<QDoubleSpinBox *>("selectionOutput")->setValue(.25);
          break;
        case 9:
          workspace->findChild<QDoubleSpinBox *>("selectionTolerance")->setValue(1);
          break;
        case 10:
          workspace->findChild<QDoubleSpinBox *>("selectionHardness")->setValue(0);
          break;
        case 11:
          undo();
          break;
        case 12:
          redo();
          break;
        case 13:
        case 14: {
          auto *list = workspace->layer_stack();
          const auto index = list->model()->index(0, 0);
          const auto point = bts::layer_mask_rect(index, list->visualRect(index)).center();
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::ShiftModifier);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::ShiftModifier);
          break;
        }
        case 15:
          save_document(directory + "/color-selection.blend");
          break;
        case 16:
          open_document(directory + "/color-selection.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 17:
          workspace->findChild<QPushButton *>("selectionInvertOutput")->click();
          break;
        case 18:
          undo();
          break;
        case 19:
          redo();
          break;
        case 20:
          undo();
          break;
        case 21:
          workspace->findChild<QPushButton *>("selectionClearIdMap")->click();
          break;
        case 22:
          workspace->findChild<QPushButton *>("selectionIdMap")->click();
          break;
        case 23: {
          auto *list = workspace->layer_stack();
          const auto rect = list->visualItemRect(list->currentItem());
          const QPointF point(rect.right() - 9, rect.center().y());
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 24:
          undo();
          break;
        case 25:
          workspace->findChild<QPushButton *>("selectionPickColor")->click();
          send_key(*viewport, Qt::Key_Escape);
          break;
      }
    }
    else if (passthrough_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto redo = [&] { workspace->findChild<QAction *>("redoDocument")->trigger(); };
      const auto mode = [&](bts::LayerBlend blend) {
        QApplication::processEvents();
        workspace->findChild<QAction *>("editLayerBlend")->trigger();
        auto *menu = workspace->findChild<QMenu *>("layerBlendMenu");
        if (!menu)
          qFatal("Passthrough menu missing");
        menu->findChild<QAction *>(QString("layerBlend%1").arg(int(blend)))->trigger();
        menu->close();
      };
      const auto context = [&] {
        auto *combo = workspace->findChild<QComboBox *>("layerChannelContext");
        combo->setCurrentIndex(combo->findData(0));
        QApplication::processEvents();
      };
      const auto invert = [&] { workspace->findChild<QPushButton *>("levelsInvert")->click(); };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("passthrough-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            set.layers = {bts::TextureLayer{}};
            set.selected_layer = set.layers[0].id;
            set.selected_mask = false;
            set.selected_effect = QUuid();
            set.precision[0] = bts::ChannelPrecision::RGB32F;
            set.precision[2] = bts::ChannelPrecision::L32F;
            bts::TextureDocument::add_fill(set);
            auto &lower = bts::TextureDocument::layer(set, set.selected_layer);
            lower.values.base_color = {0, 0, 0, 1};
            lower.values.roughness = .2f;
            bts::TextureDocument::add_fill(set);
            auto &atom = bts::TextureDocument::layer(set, set.selected_layer);
            atom.channels = {true, false, false, false, false};
            atom.values.base_color = {1, 1, 1, 1};
            atom.fill.atom = true;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          workspace->layer_settings_tabs()->setCurrentIndex(0);
          workspace->inspector_splitter()->setSizes({100, 350, 400, 0});
          context();
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 2:
          workspace->findChild<QToolButton *>("addPaintLayer")->click();
          break;
        case 3:
          mode(bts::LayerBlend::Passthrough);
          break;
        case 4:
          workspace->findChild<QAction *>("addLevelsEffect")->trigger();
          break;
        case 5:
          invert();
          break;
        case 6:
          mode(bts::LayerBlend::Normal);
          break;
        case 7:
          mode(bts::LayerBlend::Passthrough);
          break;
        case 8:
        case 9: {
          auto *item = workspace->layer_stack()->currentItem();
          if (item->data(Qt::UserRole + 10).toString().isEmpty())
            qFatal("Effect child not selected");
          item->setCheckState(sets_stage == 8 ? Qt::Unchecked : Qt::Checked);
          break;
        }
        case 10: {
          auto *combo = workspace->findChild<QComboBox *>("levelsChannel");
          combo->setCurrentIndex(combo->findData(2));
          break;
        }
        case 11:
          workspace->findChild<QPushButton *>("levelsReset")->click();
          break;
        case 12: {
          auto *combo = workspace->findChild<QComboBox *>("levelsChannel");
          combo->setCurrentIndex(combo->findData(0));
          invert();
          break;
        }
        case 13:
          save_document(directory + "/passthrough.blend");
          break;
        case 14:
          open_document(directory + "/passthrough.blend");
          workspace->texture_sets()->setCurrentRow(0);
          context();
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 15:
          invert();
          break;
        case 16:
          undo();
          break;
        case 17:
          redo();
          break;
        case 18:
          undo();
          break;
        case 19: {
          auto *list = workspace->layer_stack();
          auto *item = list->currentItem();
          const auto rect = list->visualItemRect(item);
          const QPoint point(rect.right() - 9, rect.center().y());
          QMouseEvent press(QEvent::MouseButtonPress,
                            QPointF(point),
                            QPointF(list->viewport()->mapToGlobal(point)),
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier);
          QApplication::sendEvent(list->viewport(), &press);
          QMouseEvent release(QEvent::MouseButtonRelease,
                              QPointF(point),
                              QPointF(list->viewport()->mapToGlobal(point)),
                              Qt::LeftButton,
                              Qt::NoButton,
                              Qt::NoModifier);
          QApplication::sendEvent(list->viewport(), &release);
          break;
        }
        case 20:
          undo();
          break;
        case 21:
          workspace->findChild<QCheckBox *>("levelsClamp")->click();
          break;
        case 22:
          workspace->findChild<QPushButton *>("levelsReset")->click();
          break;
      }
    }
    else if (normal_vectors_demo) {
      const auto context = [&] {
        auto *combo = workspace->findChild<QComboBox *>("layerChannelContext");
        combo->setCurrentIndex(combo->findData(3));
        QApplication::processEvents();
      };
      const auto mode = [&](bts::LayerBlend blend) {
        QApplication::processEvents();
        workspace->findChild<QAction *>("editLayerBlend")->trigger();
        auto *menu = workspace->findChild<QMenu *>("layerBlendMenu");
        if (!menu)
          qFatal("Normal layer menu unavailable");
        auto *action = menu->findChild<QAction *>(QString("layerBlend%1").arg(int(blend)));
        if (!action)
          qFatal("Normal vector mode unavailable");
        action->trigger();
        menu->close();
      };
      const auto pair = [&](bts::NormalVector lower, bts::NormalVector upper) {
        edit_material([&](bts::TextureSet &set) {
          set.precision[3] = bts::ChannelPrecision::RGB32F;
          set.layers[set.layers.size() - 2].values.normal = lower;
          bts::TextureDocument::layer(set, set.selected_layer).values.normal = upper;
        });
      };
      const auto a = bts::normalized_normal({.4f, .2f, .89f}),
                 b = bts::normalized_normal({-.25f, .35f, .9f});
      if (sets_stage == 1) {
        open_document(harness.value("normal-vectors-demo"));
        workspace->texture_sets()->setCurrentRow(0);
        edit_material([](bts::TextureSet &set) {
          bts::TextureDocument::set_resolution(set, 128);
          for (int i = 0; i < 2; ++i) {
            bts::TextureDocument::add_fill(set);
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.channels = {false, false, false, true, false};
            layer.ao_enabled = false;
            layer.values.normal = {0, 0, 1};
          }
          bts::TextureDocument::add_mask(set, 1);
          bts::TextureDocument::layer(set, set.selected_layer).mask->enabled = false;
          set.selected_mask = false;
        });
        viewport->set_layout({bts::ViewLayout::Split, false});
        viewport->frame_uv_document();
        workspace->layer_settings_tabs()->setCurrentIndex(0);
        workspace->inspector_splitter()->setSizes({100, 510, 220, 0});
        context();
        update_inspection(bts::InspectionMode::Normal);
      }
      else if (sets_stage >= 2 && sets_stage <= 16) {
        if (sets_stage == 5)
          pair(a, b);
        if (sets_stage == 8)
          pair(b, a);
        if (sets_stage == 11)
          pair({0, 0, 1}, b);
        if (sets_stage == 14)
          pair(a, {0, 0, 1});
        const std::array<bts::LayerBlend, 3> modes{bts::LayerBlend::NormalCombine,
                                                   bts::LayerBlend::NormalDetail,
                                                   bts::LayerBlend::NormalInverseDetail};
        mode(modes[(sets_stage - 2) % 3]);
        if (sets_stage <= 4) {
          bts::ExportRequest request;
          request.directory = (directory + "/normal-" + sets_stages[sets_stage]).toStdString();
          QDir().mkpath(QString::fromStdString(request.directory));
          request.padding = bts::ExportPadding::Infinite;
          request.tiles = host.export_source(*document,
                                             {document->set(snapshot.materials[0].session_id).id});
          std::atomic_bool cancel{false};
          bts::write_texture_export(request, cancel, [](int, const std::string &) {});
          normal_vector_evidence.append(
              QJsonObject{{"stage", sets_stages[sets_stage]},
                          {"directory", QString::fromStdString(request.directory)}});
        }
      }
      else
        switch (sets_stage) {
          case 17:
            pair(a, b);
            mode(bts::LayerBlend::NormalDetail);
            edit_material([](bts::TextureSet &set) {
              const auto normal = bts::normalized_normal({-.2f, .25f, .94f});
              std::vector<float> pixels(128 * 128 * 4);
              for (std::size_t i = 0; i < pixels.size(); i += 4) {
                for (int c = 0; c < 3; ++c)
                  pixels[i + c] = normal[c] * .5f + .5f;
                pixels[i + 3] = 1;
              }
              auto data = std::make_shared<const bts::MeshMapPixels>(128, std::move(pixels));
              bts::MeshMap map;
              map.name = "Independent baked normal witness";
              map.tiles.emplace(1001, bts::MeshMapPlane{128, data->hash, data});
              set.mesh_maps[0] = std::move(map);
            });
            save_document(directory + "/normal-vectors.blend");
            break;
          case 18:
            open_document(directory + "/normal-vectors.blend");
            workspace->texture_sets()->setCurrentRow(0);
            context();
            update_inspection(bts::InspectionMode::Normal);
            break;
          case 19:
            mode(bts::LayerBlend::NormalCombine);
            break;
          case 20:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 21:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 22:
            mode(bts::LayerBlend::NormalDetail);
            break;
        }
    }
    else if (families_demo) {
      const auto context = [&](int channel) {
        auto *combo = workspace->findChild<QComboBox *>("layerChannelContext");
        combo->setCurrentIndex(combo->findData(channel));
        QApplication::processEvents();
      };
      const auto mode = [&](bts::LayerBlend blend) {
        QApplication::processEvents();
        workspace->findChild<QAction *>("editLayerBlend")->trigger();
        auto *menu = workspace->findChild<QMenu *>("layerBlendMenu");
        if (!menu)
          qFatal("Layer blend menu unavailable");
        auto *action = menu->findChild<QAction *>(QString("layerBlend%1").arg(int(blend)));
        if (!action)
          qFatal("Requested blend unavailable");
        action->trigger();
        menu->close();
      };
      const auto export_scalar = [&](const QString &name) {
        bts::ExportRequest request;
        request.directory = (directory + "/blend-" + name).toStdString();
        QDir().mkpath(QString::fromStdString(request.directory));
        request.mesh = "blend";
        request.padding = bts::ExportPadding::Infinite;
        request.standard_outputs = false;
        request.packed = bts::PackedOutput{"$textureSet_Roughness",
                                           {bts::PackedSource::Roughness,
                                            bts::PackedSource::Roughness,
                                            bts::PackedSource::Roughness}};
        request.tiles = host.export_source(*document,
                                           {document->set(snapshot.materials[0].session_id).id});
        std::atomic_bool cancel{false};
        bts::write_texture_export(request, cancel, [](int, const std::string &) {});
        families_evidence.append(QJsonObject{
            {"stage", name},
            {"path", QString::fromStdString(request.directory) + "/CubeSurface_Roughness.png"}});
      };
      if (sets_stage == 1) {
        open_document(harness.value("blend-families-demo"));
        workspace->texture_sets()->setCurrentRow(0);
        edit_material([](bts::TextureSet &set) {
          bts::TextureDocument::set_resolution(set, 128);
          bts::TextureDocument::add_fill(set);
          auto &bottom = bts::TextureDocument::layer(set, set.selected_layer);
          bottom.channels = {false, false, true, false, false};
          bottom.ao_enabled = false;
          bottom.values.roughness = .3;
          bts::TextureDocument::add_fill(set);
          auto &top = bts::TextureDocument::layer(set, set.selected_layer);
          top.channels = {false, false, true, false, false};
          top.ao_enabled = false;
          top.values.roughness = .5;
          bts::TextureDocument::add_mask(set, 1);
          bts::TextureDocument::layer(set, set.selected_layer).mask->enabled = false;
          set.selected_mask = false;
        });
        viewport->set_layout({bts::ViewLayout::Split, false});
        viewport->frame_uv_document();
        workspace->layer_settings_tabs()->setCurrentIndex(0);
        workspace->inspector_splitter()->setSizes({100, 510, 220, 0});
        context(2);
        update_inspection(bts::InspectionMode::Roughness);
      }
      else if (sets_stage >= 2 && sets_stage <= 25) {
        const int index = sets_stage - 2;
        if (index == 10)
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::layer(set, set.selected_layer).values.roughness = .75;
          });
        if (index == 16) {
          edit_material([](bts::TextureSet &set) {
            set.precision[0] = bts::ChannelPrecision::RGB32F;
            auto &top = bts::TextureDocument::layer(set, set.selected_layer);
            top.channels[0] = true;
            top.values.base_color = {.2f, .8f, .3f, 1};
            auto &bottom = set.layers[set.layers.size() - 2];
            bottom.channels[0] = true;
            bottom.values.base_color = {.7f, .2f, .4f, 1};
          });
          context(0);
          update_inspection(bts::InspectionMode::BaseColor);
        }
        if (index == 20)
          edit_material([](bts::TextureSet &set) {
            auto &top = bts::TextureDocument::layer(set, set.selected_layer);
            top.values.base_color = {1, 1, 1, 1};
            top.fill.atom = true;
            top.fill.scale = {4, 4};
            auto &bottom = set.layers[set.layers.size() - 2];
            bottom.values.base_color = {.6f, .6f, .6f, 1};
          });
        mode(family_modes[index]);
        if (index < 16)
          export_scalar(family_stages[sets_stage]);
      }
      else
        switch (sets_stage) {
          case 26:
            save_document(directory + "/blend-families.blend");
            break;
          case 27:
            open_document(directory + "/blend-families.blend");
            workspace->texture_sets()->setCurrentRow(0);
            context(0);
            update_inspection(bts::InspectionMode::BaseColor);
            break;
          case 28:
            mode(bts::LayerBlend::Tint);
            break;
          case 29:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 30:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 31:
            mode(bts::LayerBlend::Value);
            break;
        }
    }
    else if (tile_resolution_demo) {
      const auto select_tile = [&](int number) {
        auto *tree = workspace->findChild<QTreeWidget *>("uvTiles");
        auto *root = tree->topLevelItem(0);
        if (!root)
          qFatal("UV tile tree is missing");
        if (!number) {
          tree->setCurrentItem(root);
          return;
        }
        for (int i = 0; i < root->childCount(); ++i)
          if (root->child(i)->data(0, Qt::UserRole).toInt() == number) {
            tree->setCurrentItem(root->child(i));
            return;
          }
        qFatal("UV tile selection is unavailable");
      };
      const auto size = [&](int pixels) {
        auto *combo = workspace->findChild<QComboBox *>("uvTileResolution");
        if (!combo->isVisible() || combo->findData(pixels) < 0)
          qFatal("UV tile resolution control is unavailable");
        combo->setCurrentIndex(combo->findData(pixels));
      };
      const auto parent_size = [&](int pixels) {
        select_tile(0);
        auto *combo = workspace->findChild<QComboBox *>("textureSetResolution");
        combo->setCurrentIndex(combo->findData(pixels));
      };
      const auto export_maps = [&](const QString &name) {
        bts::ExportRequest request;
        request.directory = (directory + "/maps-" + name).toStdString();
        QDir().mkpath(QString::fromStdString(request.directory));
        request.mesh = "UDIM";
        request.padding = bts::ExportPadding::Infinite;
        request.tiles = host.export_source(*document, {document->texture_sets().front().id});
        QJsonArray dimensions;
        for (const auto &tile : request.tiles) {
          if (tile.normal.size() != std::size_t(tile.resolution) * tile.rows() * 4)
            qFatal("Native UDIM buffer dimensions differ from its resolution");
          dimensions.append(QJsonObject{{"tile", tile.number},
                                        {"width", tile.resolution},
                                        {"height", tile.rows()},
                                        {"normal_floats", qint64(tile.normal.size())},
                                        {"ao_floats", qint64(tile.ao.size())}});
        }
        std::atomic_bool cancel{false};
        bts::write_texture_export(request, cancel, [](int, const std::string &) {});
        tile_resolution_evidence.append(QJsonObject{{"export", name}, {"dimensions", dimensions}});
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("tile-resolution-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 1024);
            bts::MeshMap map;
            map.name = "Tile-independent AO fixture";
            for (int number : set.tiles) {
              std::vector<float> pixels(128 * 128 * 4, .8f);
              for (std::size_t i = 3; i < pixels.size(); i += 4)
                pixels[i] = 1;
              auto data = std::make_shared<const bts::MeshMapPixels>(128, std::move(pixels));
              map.tiles.emplace(number, bts::MeshMapPlane{128, data->hash, data});
            }
            set.mesh_maps[3] = std::move(map);
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({190, 420, 220, 0});
          break;
        case 2:
          select_tile(1002);
          break;
        case 3:
          size(512);
          export_maps("override");
          break;
        case 4:
          select_tile(1001);
          break;
        case 5:
          select_tile(1002);
          break;
        case 6:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 7:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          save_document(directory + "/tile-resolution.blend");
          break;
        case 9:
          open_document(directory + "/tile-resolution.blend");
          workspace->texture_sets()->setCurrentRow(0);
          select_tile(1002);
          export_maps("reopened");
          break;
        case 10: {
          auto &set = document->set(snapshot.materials[0].session_id);
          const auto before = bts::TextureDocument::layer(set, set.selected_layer).pixels.hash(0);
          brush.channels = {true, false, false, false, false};
          brush.ao = false;
          brush.values.base_color = {.8f, .01f, .01f, 1};
          brush.diameter = 8;
          QApplication::processEvents();
          viewport->repaint();
          const auto point = viewport->uv_point({.94, .5});
          send_drag(*viewport,
                    point,
                    viewport->uv_point({1.06, .5}) - point,
                    Qt::LeftButton,
                    Qt::NoModifier);
          auto &after = document->set(snapshot.materials[0].session_id);
          if (bts::TextureDocument::layer(after, after.selected_layer).pixels.hash(0) == before)
            qFatal("Mixed-resolution UDIM brush did not change authoring data");
          break;
        }
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 12:
          size(1024);
          break;
        case 13:
          parent_size(512);
          select_tile(1002);
          break;
        case 14:
          parent_size(1024);
          select_tile(1002);
          size(512);
          break;
        case 15:
          size(0);
          break;
        case 16:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (custom_demo) {
      const auto value = [&](double v) {
        workspace->findChild<QDoubleSpinBox *>("userChannelValue7")->setValue(v);
      };
      const auto prepare = [&] {
        QApplication::processEvents();
        viewport->repaint();
      };
      const auto paint = [&](bool scene, double v) {
        for (int c = 0; c < 5; ++c)
          if (auto *button = workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c));
              button->isChecked())
            button->click();
        if (brush.ao)
          workspace->findChild<QToolButton *>("brushChannel5")->click();
        if (!brush.users[7])
          workspace->findChild<QToolButton *>("brushUserChannel7")->click();
        value(v);
        auto *control = workspace->findChild<QDoubleSpinBox *>("userChannelValue7");
        for (auto *parent = control->parentWidget(); parent; parent = parent->parentWidget())
          if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
            scroll->ensureWidgetVisible(control);
            break;
          }
        prepare();
        auto &set = document->set(snapshot.materials[0].session_id);
        const auto before =
            bts::TextureDocument::layer(set, set.selected_layer).users[7].pixels.hash(2);
        QPointF from, to;
        if (scene) {
          std::array<float, 3> center{};
          int count = 0;
          for (const auto &t : snapshot.uv_triangles)
            if (t.material == 0)
              for (const auto &p : t.positions) {
                for (int c = 0; c < 3; ++c)
                  center[c] += p[c];
                ++count;
              }
          for (auto &v : center)
            v /= count;
          const auto middle = viewport->scene_point({center[0], center[1], center[2]});
          from = middle + QPointF(-16, 8);
          to = middle + QPointF(18, 8);
        }
        else {
          const bts::UvFaceTriangle *best = nullptr;
          double area = -1;
          for (const auto &t : snapshot.uv_triangles)
            if (t.material == 0) {
              const auto &p = t.corners;
              const double a = std::abs((p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) -
                                        (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]));
              if (a > area) {
                area = a;
                best = &t;
              }
            }
          if (!best)
            qFatal("No custom channel UV triangle");
          QPointF uv;
          for (const auto &p : best->corners)
            uv += QPointF(p[0] / 3., p[1] / 3.);
          from = viewport->uv_point(uv);
          to = viewport->uv_point(uv + QPointF(.015, 0));
        }
        send_drag(*viewport, from, to - from, Qt::LeftButton, Qt::NoModifier);
        auto &after = document->set(snapshot.materials[0].session_id);
        if (bts::TextureDocument::layer(after, after.selected_layer).users[7].pixels.hash(2) ==
            before)
          qFatal("Custom channel brush did not write pixels");
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("custom-channel-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material(
              [](bts::TextureSet &set) { bts::TextureDocument::set_resolution(set, 128); });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({80, 510, 220, 0});
          break;
        case 2:
          workspace->findChild<QAction *>("addUserChannel7")->trigger();
          break;
        case 3: {
          auto *name = workspace->findChild<QLineEdit *>("userChannelName7");
          name->setText("BTS coverage");
          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          QApplication::sendEvent(name, &enter);
          if (document->set(snapshot.materials[0].session_id).users[7].name != "BTS coverage")
            qFatal("Inline custom channel rename was not committed");
          break;
        }
        case 4:
          for (int c = 0; c < 5; ++c)
            if (auto *button = workspace->findChild<QToolButton *>(
                    QString("brushChannel%1").arg(c));
                button->isChecked())
              button->click();
          if (brush.ao)
            workspace->findChild<QToolButton *>("brushChannel5")->click();
          workspace->findChild<QToolButton *>("brushUserChannel7")->click();
          value(.4);
          break;
        case 5:
          paint(true, .4);
          break;
        case 6:
          for (int i = 0; i < 5; ++i)
            send_key(*viewport, Qt::Key_C);
          if (int(host.inspection_mode()) != 14)
            qFatal("Custom channel is absent from C cycling");
          break;
        case 7:
          paint(false, .75);
          break;
        case 8:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 9:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 10:
          send_key(*viewport, Qt::Key_M);
          break;
        case 11:
          workspace->findChild<QComboBox *>("userChannelPrecision7")->setCurrentIndex(0);
          break;
        case 12:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 13:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 14:
          save_document(directory + "/custom-channel.blend");
          break;
        case 15:
          open_document(directory + "/custom-channel.blend");
          workspace->texture_sets()->setCurrentRow(0);
          viewport->inspection_requested(bts::InspectionMode(14));
          break;
        case 16:
          paint(false, .2);
          break;
        case 17:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 18:
          workspace->findChild<QToolButton *>("removeUserChannel7")->click();
          break;
        case 19:
          workspace->findChild<QAction *>("addUserChannel7")->trigger();
          viewport->inspection_requested(bts::InspectionMode(14));
          break;
      }
    }
    else if (precision_demo) {
      const auto format = [&](int channel, int value) {
        auto *combo = workspace->findChild<QComboBox *>(
            QString("channelPrecision%1").arg(channel));
        if (!combo || !combo->isEnabled() || combo->count() != 9)
          qFatal("Channel precision control unavailable");
        combo->setCurrentIndex(value);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("precision-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::set_resolution(set, 128);
            bts::TextureDocument::add_fill(set);
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.values.base_color = {.123456f, .456789f, .789123f, 1};
            layer.values.metallic = .234567f;
            layer.values.roughness = .456789f;
            layer.channels = {true, true, true, false, false};
            layer.ao_enabled = false;
          });
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({80, 510, 220, 0});
          break;
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
          format(0, sets_stage - 2);
          break;
        case 11:
          format(1, 8);
          break;
        case 12:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 13:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 14:
          save_document(directory + "/channel-precision.blend");
          break;
        case 15:
          open_document(directory + "/channel-precision.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 16:
          format(0, 0);
          format(1, 1);
          break;
      }
    }
    else if (rectangular_demo) {
      const auto width = [&](int value) {
        auto *combo = workspace->findChild<QComboBox *>("textureSetResolution");
        combo->setCurrentIndex(combo->findData(value));
      };
      const auto link = [&](bool value) {
        workspace->findChild<QToolButton *>("textureSetSizeLock")->setChecked(value);
      };
      const auto height = [&](int value) {
        auto *combo = workspace->findChild<QComboBox *>("textureSetHeight");
        if (!combo->isEnabled())
          qFatal("Unlinked height control unavailable");
        combo->setCurrentIndex(combo->findData(value));
        if (document->set(snapshot.materials[0].session_id).rows() != value)
          qFatal("Rectangular height was rejected");
      };
      const auto export_maps = [&](const QString &name) {
        bts::ExportRequest request;
        request.directory = (directory + "/maps-" + name).toStdString();
        QDir().mkpath(QString::fromStdString(request.directory));
        request.mesh = "rectangle";
        request.packed = bts::PackedOutput{};
        request.padding = bts::ExportPadding::Infinite;
        request.tiles = host.export_source(*document,
                                           {document->set(snapshot.materials[0].session_id).id});
        if (request.tiles.front().normal.size() !=
            std::size_t(request.tiles.front().resolution) * request.tiles.front().rows() * 4)
          qFatal("Rectangular native image dimensions invalid");
        std::atomic_bool cancel{false};
        bts::write_texture_export(request, cancel, [](int, const std::string &) {});
        resolution_evidence.append(QJsonObject{{"export", name},
                                               {"width", request.tiles.front().resolution},
                                               {"height", request.tiles.front().rows()}});
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("rectangular-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          link(true);
          width(2048);
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          break;
        case 2:
          link(false);
          break;
        case 3:
          height(1024);
          export_maps("rectangular");
          break;
        case 4:
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 5:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 6:
          link(true);
          break;
        case 7:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 8:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 9:
          link(false);
          height(1024);
          break;
        case 10:
          save_document(directory + "/rectangular.blend");
          break;
        case 11:
          open_document(directory + "/rectangular.blend");
          workspace->texture_sets()->setCurrentRow(0);
          export_maps("reopened");
          break;
        case 12: {
          auto &initial = document->set(snapshot.materials[0].session_id);
          const auto before =
              bts::TextureDocument::layer(initial, initial.selected_layer).pixels.hash(0);
          brush.channels = {true, false, false, false, false};
          brush.ao = false;
          brush.values.base_color = {.8f, .01f, .01f, 1};
          brush.diameter = 9;
          QApplication::processEvents();
          viewport->repaint();
          const auto start = viewport->uv_point({.3, .25});
          send_drag(*viewport,
                    start,
                    viewport->uv_point({.55, .25}) - start,
                    Qt::LeftButton,
                    Qt::NoModifier);
          auto &after = document->set(snapshot.materials[0].session_id);
          if (bts::TextureDocument::layer(after, after.selected_layer).pixels.hash(0) == before)
            qFatal("Rectangular brush failed to modify canonical raster");
          break;
        }
        case 13:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 14:
          width(1024);
          height(2048);
          export_maps("portrait");
          break;
        case 15:
          width(2048);
          link(true);
          break;
      }
    }
    else if (resolution_demo) {
      const auto size = [&](int resolution) {
        auto *combo = workspace->findChild<QComboBox *>("textureSetResolution");
        if (!combo->isEnabled() || combo->findData(resolution) < 0)
          qFatal("Texture Set resolution unavailable");
        combo->setCurrentIndex(combo->findData(resolution));
        if (document->set(snapshot.materials[selected_material].session_id).texture_resolution !=
            resolution)
          qFatal("Native Texture Set size change rejected: %s",
                 qPrintable(workspace->findChild<QPlainTextEdit *>("consoleLog")->toPlainText()));
      };
      const auto export_maps = [&](const QString &name) {
        bts::ExportRequest request;
        request.directory = (directory + "/maps-" + name).toStdString();
        QDir().mkpath(QString::fromStdString(request.directory));
        request.mesh = "resolution";
        request.tiles = host.export_source(*document,
                                           {document->set(snapshot.materials[0].session_id).id});
        std::atomic_bool cancel{false};
        bts::write_texture_export(request, cancel, [](int, const std::string &) {});
        resolution_evidence.append(
            QJsonObject{{"export", name}, {"size", request.tiles.front().resolution}});
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("dynamic-resolution-demo"));
          workspace->texture_sets()->setCurrentRow(1);
          edit_material([](bts::TextureSet &set) {
            set.ao_channel = true;
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            bts::PaintPixels::Stamp ao{{.5, .5}, {.06, 0}, {0, .06}};
            ao.channels = {false, false, true, false, false};
            ao.values.roughness = .2;
            const int res = bts::TextureDocument::authoring_resolution(set, layer);
            layer.author_resolution = res;
            layer.ao_pixels.stamp(res, ao, set.tiles);
          });
          workspace->texture_sets()->setCurrentRow(0);
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          export_maps("original");
          break;
        case 2:
          size(128);
          export_maps("low");
          break;
        case 3:
          size(2048);
          export_maps("high");
          break;
        case 4:
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 5:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 6:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 7:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          size(128);
          break;
        case 9:
          save_document(directory + "/dynamic-resolution.blend");
          break;
        case 10:
          open_document(directory + "/dynamic-resolution.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 11: {
          auto &initial = document->set(snapshot.materials[0].session_id);
          const auto before =
              bts::TextureDocument::layer(initial, initial.selected_layer).pixels.hash(0);
          brush.channels = {true, false, false, false, false};
          brush.ao = false;
          brush.values.base_color = {.8f, .01f, .01f, 1};
          brush.diameter = 9;
          QApplication::processEvents();
          viewport->repaint();
          const auto start = viewport->uv_point({.3, .25});
          send_drag(*viewport,
                    start,
                    viewport->uv_point({.55, .25}) - start,
                    Qt::LeftButton,
                    Qt::NoModifier);
          auto &after = document->set(snapshot.materials[0].session_id);
          if (bts::TextureDocument::layer(after, after.selected_layer).pixels.hash(0) == before)
            qFatal("Low-resolution brush must update retained canonical pixels");
          break;
        }
        case 12:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 13:
          size(1024);
          export_maps("restored");
          break;
        case 14:
          size(2048);
          export_maps("high-reopened");
          break;
      }
    }
    else if (physical_demo) {
      const auto export_maps = [&](const QString &name) {
        bts::ExportRequest request;
        request.directory = (directory + "/maps-" + name).toStdString();
        QDir().mkpath(QString::fromStdString(request.directory));
        request.mesh = "physical-plane";
        request.tiles = host.export_source(*document, {document->texture_sets().front().id});
        const auto outputs = bts::export_file_list(request);
        std::atomic_bool cancel{false};
        bts::write_texture_export(request, cancel, [](int, const std::string &) {});
        QJsonArray names;
        for (const auto &output : outputs)
          names.append(QString::fromStdString(output.name));
        physical_evidence.append(QJsonObject{{"export", name}, {"files", names}});
      };
      const auto create = [&](int mode) {
        bts::NewProjectDialog dialog(workspace);
        QTimer::singleShot(100, &dialog, [&, mode] {
          auto *preset = dialog.findChild<QComboBox *>("projectTemplate");
          preset->setCurrentIndex(preset->findData(":pbr"));
          const auto source = mode == 1 ? directory + "/plane.obj" :
                              mode == 2 ? directory + "/centimeters.usda" :
                                          harness.value("physical-scale-demo");
          dialog.findChild<QLineEdit *>("meshPath")->setText(source);
          auto *resolution = dialog.findChild<QComboBox *>("documentResolution");
          resolution->setCurrentIndex(resolution->findData(512));
          dialog.findChild<QToolButton *>("physicalUnitsDisclosure")->setChecked(true);
          dialog.findChild<QCheckBox *>("autoPhysicalMaterials")->setChecked(true);
          if (mode == 3) {
            dialog.findChild<QCheckBox *>("useInternalUnitScale")->setChecked(false);
            dialog.findChild<QComboBox *>("customUnitName")->setCurrentIndex(1);
            dialog.findChild<QDoubleSpinBox *>("customUnitScale")->setValue(1);
          }
          dialog.findChild<QScrollArea *>("newProjectScroll")
              ->ensureWidgetVisible(dialog.findChild<QWidget *>("physicalUnitsDetails"));
          dialog.grab().save(directory + "/" + sets_stages.at(sets_stage) + "-new.png");
          dialog.findChild<QPushButton *>("createProject")->click();
        });
        sets_demo.stop();
        if (dialog.exec() != QDialog::Accepted || !dialog.options())
          qFatal("Physical New rejected");
        create_project(*dialog.options());
        viewport->set_layout({bts::ViewLayout::Split, false});
        viewport->frame_uv_document();
        sets_demo.start(600);
      };
      const auto assign = [&] {
        workspace->findChild<QToolButton *>("resetShelfFilters")->click();
        auto *list = workspace->findChild<QListWidget *>("shelfAssets");
        const auto matches = list->findItems("Atom Material (10 cm)", Qt::MatchExactly);
        if (matches.isEmpty())
          qFatal("Physical test material not visible");
        list->scrollToItem(matches.front());
        QApplication::processEvents();
        const auto point = list->visualItemRect(matches.front()).center();
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonDblClick,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        auto &set = document->set(snapshot.materials[0].session_id);
        if (bts::TextureDocument::layer(set, set.selected_layer).kind != bts::LayerKind::Fill ||
            !bts::TextureDocument::layer(set, set.selected_layer).fill.atom)
          qFatal("Shelf material must create a real procedural fill");
      };
      const auto configure = [&](bool internal, double centimeters, bool automatic) {
        QTimer::singleShot(100, workspace, [&, internal, centimeters, automatic] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          dialog->findChild<QToolButton *>("physicalUnitsDisclosure")->setChecked(true);
          dialog->findChild<QCheckBox *>("useInternalUnitScale")->setChecked(internal);
          dialog->findChild<QComboBox *>("customUnitName")->setCurrentIndex(1);
          dialog->findChild<QDoubleSpinBox *>("customUnitScale")->setValue(centimeters);
          dialog->findChild<QCheckBox *>("autoPhysicalMaterials")->setChecked(automatic);
          dialog->findChild<QScrollArea *>("projectConfigurationScroll")
              ->ensureWidgetVisible(dialog->findChild<QWidget *>("physicalUnitsDetails"));
          dialog->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-configuration.png");
          dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
          const auto error = dialog->findChild<QLabel *>("replacementMeshError")->text();
          if (!error.isEmpty())
            qFatal("Physical unit change failed: %s", qPrintable(error));
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        sets_demo.start(600);
      };
      const auto choose_fill = [&] {
        auto &set = document->set(snapshot.materials[0].session_id);
        for (const auto &layer : set.layers)
          if (layer.kind == bts::LayerKind::Fill) {
            set.selected_layer = layer.id;
            set.selected_mask = false;
            break;
          }
        refresh_document_ui();
      };
      switch (sets_stage) {
        case 1:
          create(0);
          break;
        case 2:
          assign();
          export_maps("internal");
          break;
        case 3:
          configure(false, 50, true);
          export_maps("unit50");
          break;
        case 4:
          workspace->findChild<QCheckBox *>("fillCustomSize")->setChecked(true);
          for (auto axis : {"X", "Y", "Z"})
            workspace->findChild<QDoubleSpinBox *>(QString("fillPhysicalSize") + axis)
                ->setValue(20);
          export_maps("size20");
          break;
        case 5:
          workspace->findChild<QCheckBox *>("fillCustomSize")->setChecked(false);
          workspace->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(1);
          export_maps("tri50");
          break;
        case 6:
          configure(true, 50, true);
          export_maps("tri100");
          break;
        case 7:
          edit_material([](bts::TextureSet &set) { bts::TextureDocument::add_paint(set); });
          save_document(directory + "/physical.blend");
          break;
        case 8: {
          const auto source = harness.value("physical-scale-demo");
          if (!QFile::rename(source, source + ".hidden"))
            qFatal("Cannot hide owned physical fixture");
          open_document(directory + "/physical.blend");
          export_maps("opened");
          break;
        }
        case 9: {
          brush.channels = {true, false, false, false, false};
          brush.ao = false;
          brush.values.base_color = {.8f, .02f, .01f, 1};
          brush.diameter = 8;
          QApplication::processEvents();
          viewport->repaint();
          const auto from = viewport->uv_point({.45, .5});
          send_drag(*viewport,
                    from,
                    viewport->uv_point({.6, .5}) - from,
                    Qt::LeftButton,
                    Qt::NoModifier);
          auto &set = document->set(snapshot.materials[0].session_id);
          if (!bts::TextureDocument::layer(set, set.selected_layer).pixels.page_count(0))
            qFatal("Physical project stroke must record paint");
          break;
        }
        case 10:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 11: {
          const auto source = harness.value("physical-scale-demo");
          if (!QFile::rename(source + ".hidden", source))
            qFatal("Cannot restore owned physical fixture");
          choose_fill();
          configure(true, 50, false);
          workspace->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(0);
          workspace->findChild<QComboBox *>("fillScaleMode")->setCurrentIndex(0);
          export_maps("tiling");
          break;
        }
        case 12:
          assign();
          export_maps("assigned-tiling");
          break;
        case 13:
          create(1);
          break;
        case 14:
          assign();
          export_maps("obj");
          break;
        case 15:
          create(3);
          break;
        case 16:
          assign();
          export_maps("matched");
          break;
        case 17:
          create(2);
          break;
        case 18:
          assign();
          export_maps("centimeters");
          break;
        case 19:
          configure(false, 1, true);
          export_maps("custom-centimeter");
          break;
        case 20:
          workspace->findChild<QDoubleSpinBox *>("fillScaleU")->setValue(2);
          export_maps("authored");
          break;
        case 21:
          configure(false, .5, true);
          export_maps("changed-units");
          break;
        case 22:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          export_maps("undo-authoring");
          break;
        case 23:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          export_maps("redo-authoring");
          break;
      }
    }
    else if (usd_demo) {
      const auto selection = [&](QDialog *parent, const QString &scope, const QString &variant) {
        QTimer::singleShot(100, parent, [&, parent, scope, variant] {
          auto *dialog = parent->findChild<QDialog *>("usdScopeDialog");
          if (!dialog)
            qFatal("USD scope dialog missing");
          auto *combo = dialog->findChild<QComboBox *>("usdScope");
          const int index = combo->findData(scope);
          if (index < 0)
            qFatal("USD scope missing: %s", qPrintable(scope));
          combo->setCurrentIndex(index);
          if (!variant.isEmpty()) {
            auto *variants = dialog->findChild<QComboBox *>("usdVariant");
            if (!variants || variants->findText(variant) < 0)
              qFatal("USD modeling variant missing");
            variants->setCurrentText(variant);
          }
          dialog->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-scope.png");
          dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        parent->findChild<QPushButton *>("usdChangeScope")->click();
      };
      const auto create = [&](int mode) {
        bts::NewProjectDialog dialog(workspace);
        QTimer::singleShot(100, &dialog, [&, mode] {
          auto *preset = dialog.findChild<QComboBox *>("projectTemplate");
          preset->setCurrentIndex(preset->findData(":pbr"));
          dialog.findChild<QLineEdit *>("meshPath")
              ->setText(harness.value("usd-scene-demo") + (mode >= 8 ? ".no-uv.usda" : ""));
          auto *resolution = dialog.findChild<QComboBox *>("documentResolution");
          resolution->setCurrentIndex(resolution->findData(512));
          if (mode)
            selection(
                &dialog, mode == 7 ? "/World/Other" : "/World/Asset", mode == 5 ? "high" : "");
          auto *subdivision = dialog.findChild<QSpinBox *>("usdSubdivision");
          auto *frame = dialog.findChild<QDoubleSpinBox *>("usdFrame");
          if (mode == 2 || mode == 3) {
            if (!frame->isEnabled())
              qFatal("Animated USD frame control disabled");
            frame->setValue(10);
          }
          if (mode == 3 || mode == 9)
            subdivision->setValue(2);
          if (mode == 5)
            subdivision->setValue(1);
          if (mode == 9)
            dialog.findChild<QCheckBox *>("newAutoUnwrap")->setChecked(true);
          usd_evidence.append(
              QJsonObject{{"new_mode", mode},
                          {"scope", dialog.findChild<QLabel *>("usdScopeSummary")->text()},
                          {"frame_enabled", frame->isEnabled()},
                          {"subdivision_enabled", subdivision->isEnabled()},
                          {"subdivision", subdivision->value()},
                          {"frame", frame->value()}});
          dialog.grab().save(directory + "/" + sets_stages.at(sets_stage) + "-new.png");
          if (mode == 4)
            dialog.reject();
          else
            dialog.findChild<QPushButton *>("createProject")->click();
        });
        sets_demo.stop();
        const int result = dialog.exec();
        if (mode == 4) {
          if (result != QDialog::Rejected)
            qFatal("USD New cancellation failed");
        }
        else {
          if (result != QDialog::Accepted || !dialog.options())
            qFatal("USD New options rejected");
          if (mode == 8) {
            const auto before = document->to_json();
            bool failed = false;
            try {
              create_project(*dialog.options());
            }
            catch (const std::exception &error) {
              failed = true;
              usd_evidence.append(QJsonObject{{"missing_uv_rejected", true},
                                              {"error", QString::fromUtf8(error.what())}});
            }
            if (!failed || document->to_json() != before)
              qFatal("USD missing UV rejection must preserve project");
          }
          else
            create_project(*dialog.options());
        }
        sets_demo.start(600);
      };
      const auto paint = [&] {
        workspace->texture_sets()->setCurrentRow(0);
        select_material(0);
        brush.channels = {true, false, false, false, false};
        brush.ao = false;
        brush.values.base_color = {.8f, .03f, .01f, 1};
        brush.diameter = 9;
        viewport->set_layout({bts::ViewLayout::Split, false});
        viewport->frame_uv_document();
        QApplication::processEvents();
        viewport->repaint();
        const auto point = viewport->uv_point(QPointF(.5, .5));
        send_drag(*viewport, point, QPointF(10, 0), Qt::LeftButton, Qt::NoModifier);
        auto &set = document->set(snapshot.materials[0].session_id);
        if (!bts::TextureDocument::layer(set, set.selected_layer).pixels.page_count(0))
          qFatal("USD UV stroke must record paint");
      };
      const auto configure = [&](bool replace) {
        QTimer::singleShot(100, workspace, [&, replace] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          dialog->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-configuration.png");
          if (!replace) {
            dialog->reject();
            return;
          }
          dialog->findChild<QLineEdit *>("replacementMeshPath")
              ->setText(harness.value("usd-scene-demo"));
          selection(dialog, "/World/Asset", "low");
          dialog->findChild<QDoubleSpinBox *>("usdFrame")->setValue(10);
          dialog->findChild<QSpinBox *>("usdSubdivision")->setValue(0);
          dialog->findChild<QCheckBox *>("preserveStrokePositions")->setChecked(false);
          dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
          const auto error = dialog->findChild<QLabel *>("replacementMeshError")->text();
          if (!error.isEmpty())
            qFatal("USD frame reimport failed: %s", qPrintable(error));
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          create(0);
          break;
        case 2:
          create(4);
          break;
        case 3:
          create(1);
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          break;
        case 4:
          create(2);
          break;
        case 5:
          create(3);
          break;
        case 6:
          create(5);
          break;
        case 7:
          configure(false);
          break;
        case 8:
          save_document(directory + "/usd-scene.blend");
          break;
        case 9: {
          const auto source = harness.value("usd-scene-demo");
          if (!QFile::rename(source, source + ".hidden"))
            qFatal("Cannot hide owned USD fixture");
          open_document(directory + "/usd-scene.blend");
          usd_evidence.append(QJsonObject{{"packed_source_absent", !QFileInfo::exists(source)}});
          break;
        }
        case 10:
          paint();
          break;
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 12: {
          const auto source = harness.value("usd-scene-demo");
          if (!QFile::rename(source + ".hidden", source))
            qFatal("Cannot restore owned USD fixture");
          configure(true);
          break;
        }
        case 13:
          create(7);
          break;
        case 14:
          create(8);
          break;
        case 15:
          create(9);
          viewport->frame_uv_document();
          break;
        case 16:
          paint();
          break;
        case 17:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (unwrap_demo) {
      const auto create = [&](const QString &path, int mode) {
        bts::NewProjectDialog dialog(workspace);
        QTimer::singleShot(100, &dialog, [&, mode, path] {
          auto *preset = dialog.findChild<QComboBox *>("projectTemplate");
          preset->setCurrentIndex(preset->findData(":pbr"));
          dialog.findChild<QLineEdit *>("meshPath")->setText(path);
          auto *size = dialog.findChild<QComboBox *>("documentResolution");
          size->setCurrentIndex(size->findData(512));
          auto *toggle = dialog.findChild<QCheckBox *>("newAutoUnwrap");
          const bool default_off = !toggle->isChecked();
          QTimer::singleShot(100, &dialog, [&] {
            auto *options = dialog.findChild<QDialog *>("autoUnwrapOptions");
            if (!options)
              qFatal("Auto unwrap options unavailable while unchecked");
            const auto combo = [&](const char *name) {
              return options->findChild<QComboBox *>(name);
            };
            const bool defaults = combo("unwrapSeams")->currentIndex() == 0 &&
                                  combo("unwrapIslands")->currentIndex() == 0 &&
                                  combo("unwrapPacking")->currentIndex() == 0 &&
                                  combo("unwrapMargin")->currentIndex() == 1;
            unwrap_evidence.append(QJsonObject{{"options_default", defaults},
                                               {"off_options_enabled", !toggle->isChecked()}});
            if (mode == 2) {
              for (auto name : {"unwrapSeams", "unwrapIslands", "unwrapPacking"})
                combo(name)->setCurrentIndex(1);
              combo("unwrapMargin")->setCurrentIndex(3);
            }
            else if (mode == 3) {
              combo("unwrapPacking")->setCurrentIndex(1);
              combo("unwrapMargin")->setCurrentIndex(2);
            }
            options->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-options.png");
            options->accept();
          });
          dialog.findChild<QPushButton *>("newAutoUnwrapOptions")->click();
          toggle->setChecked(mode != 0);
          unwrap_evidence.append(QJsonObject{{"default_off", default_off}, {"mode", mode}});
          dialog.grab().save(directory + "/" + sets_stages.at(sets_stage) + "-new.png");
          dialog.findChild<QPushButton *>("createProject")->click();
        });
        sets_demo.stop();
        if (dialog.exec() != QDialog::Accepted || !dialog.options())
          qFatal("Auto unwrap New rejected");
        create_project(*dialog.options());
        sets_demo.start(600);
      };
      const auto paint = [&] {
        select_material(0);
        brush.channels = {true, false, false, false, false};
        brush.ao = false;
        brush.values.base_color = {.85f, .04f, .02f, 1};
        brush.diameter = 8;
        viewport->set_layout({bts::ViewLayout::Split, false});
        viewport->frame_uv_document();
        QApplication::processEvents();
        viewport->repaint();
        const bts::UvFaceTriangle *best = nullptr;
        double best_area = 0;
        for (const auto &t : snapshot.uv_triangles)
          if (t.material == 0) {
            const auto &a = t.corners[0], &b = t.corners[1], &c = t.corners[2];
            const double area = std::abs(double(b[0] - a[0]) * (c[1] - a[1]) -
                                         double(b[1] - a[1]) * (c[0] - a[0]));
            if (area > best_area) {
              best = &t;
              best_area = area;
            }
          }
        if (!best)
          qFatal("No generated UV triangle to paint");
        QPointF center;
        for (const auto &p : best->corners)
          center += QPointF(p[0] / 3., p[1] / 3.);
        const auto point = viewport->uv_point(center);
        send_drag(*viewport, point, QPointF(4, 0), Qt::LeftButton, Qt::NoModifier);
        auto &set = document->set(snapshot.materials[0].session_id);
        if (!bts::TextureDocument::layer(set, set.selected_layer).pixels.page_count(0))
          qFatal("Generated UV brush must record paint");
      };
      switch (sets_stage) {
        case 1: {
          const auto before = document->to_json();
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("auto-unwrap-demo").toStdString();
          bool rejected = false;
          QString error;
          try {
            host.new_project(
                options.absolute_mesh_path, parser.value("environment").toStdString(), &options);
          }
          catch (const std::exception &e) {
            rejected = true;
            error = QString::fromUtf8(e.what());
          }
          unwrap_evidence.append(
              QJsonObject{{"missing_off_rejected", rejected},
                          {"error", error},
                          {"document_retained", document->to_json() == before}});
          if (!rejected || document->to_json() != before)
            qFatal("Missing UV import must reject before adoption");
          break;
        }
        case 2:
          create(harness.value("auto-unwrap-demo"), 1);
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          break;
        case 3:
          select_material(1);
          viewport->frame_uv_document();
          break;
        case 4:
          paint();
          break;
        case 5:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 6:
          save_document(directory + "/auto-unwrap.blend");
          break;
        case 7:
          open_document(directory + "/auto-unwrap.blend");
          break;
        case 8:
          paint();
          break;
        case 9:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 10:
        case 11: {
          const bool enabled = sets_stage == 11;
          QTimer::singleShot(100, workspace, [&, enabled] {
            auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
            dialog->findChild<QLineEdit *>("replacementMeshPath")
                ->setText(harness.value("auto-unwrap-demo"));
            dialog->findChild<QCheckBox *>("projectAutoUnwrap")->setChecked(enabled);
            dialog->findChild<QCheckBox *>("preserveStrokePositions")->setChecked(false);
            dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
            const auto error = dialog->findChild<QLabel *>("replacementMeshError")->text();
            unwrap_evidence.append(QJsonObject{{"reimport", enabled}, {"error", error}});
            if (!enabled) {
              if (error.isEmpty())
                qFatal("Missing UV replacement must fail without unwrap");
              dialog->grab().save(directory + "/reimport-off-error.png");
              dialog->reject();
            }
            else if (!error.isEmpty())
              qFatal("Generated UV replacement failed: %s", qPrintable(error));
          });
          sets_demo.stop();
          workspace->findChild<QAction *>("projectConfiguration")->trigger();
          sets_demo.start(600);
          break;
        }
        case 12:
          create(harness.value("new-mesh"), 0);
          break;
        case 13:
          create(harness.value("new-mesh"), 2);
          viewport->frame_uv_document();
          break;
        case 14:
          create(harness.value("new-mesh"), 3);
          viewport->frame_uv_document();
          break;
      }
    }
    else if (cameras_demo) {
      const auto create = [&](bool cameras) {
        bts::NewProjectDialog dialog(workspace);
        QTimer::singleShot(100, &dialog, [&, cameras] {
          auto *preset = dialog.findChild<QComboBox *>("projectTemplate");
          preset->setCurrentIndex(preset->findData(":pbr"));
          dialog.findChild<QLineEdit *>("meshPath")->setText(harness.value("camera-import-demo"));
          auto *size = dialog.findChild<QComboBox *>("documentResolution");
          size->setCurrentIndex(size->findData(512));
          auto *toggle = dialog.findChild<QCheckBox *>("newImportCameras");
          const bool default_on = toggle->isChecked();
          toggle->setChecked(cameras);
          camera_evidence.append(QJsonObject{{"stage", cameras ? "new-on" : "new-off"},
                                             {"pbr_default_on", default_on},
                                             {"enabled", toggle->isChecked()}});
          dialog.grab().save(directory +
                             (cameras ? "/camera-import-on.png" : "/camera-import-off.png"));
          dialog.findChild<QPushButton *>("createProject")->click();
        });
        sets_demo.stop();
        if (dialog.exec() != QDialog::Accepted || !dialog.options())
          qFatal("Camera scene New was rejected");
        create_project(*dialog.options());
        sets_demo.start(600);
      };
      const auto select_camera = [&](const QString &name) {
        auto *combo = viewport->findChild<QComboBox *>("sceneCameraSelector");
        const int index = combo->findText(name);
        if (index < 0)
          qFatal("Imported camera missing: %s", qPrintable(name));
        combo->setCurrentIndex(index);
      };
      const auto wheel = [&](bts::ViewKind kind) {
        for (const auto &pane : viewport->visible_panes())
          if (pane.kind == kind) {
            const QPointF point((pane.x + pane.width * .5) / viewport->devicePixelRatioF(),
                                (pane.y + pane.height * .5) / viewport->devicePixelRatioF());
            QWheelEvent event(point,
                              viewport->mapToGlobal(point.toPoint()),
                              QPoint{},
                              QPoint(0, 120),
                              Qt::NoButton,
                              Qt::NoModifier,
                              Qt::NoScrollPhase,
                              false);
            QApplication::sendEvent(viewport, &event);
            return;
          }
      };
      const auto configure = [&](int mode) {
        QTimer::singleShot(100, workspace, [&, mode] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          auto *toggle = dialog->findChild<QCheckBox *>("projectImportCameras");
          const bool disabled = !toggle->isEnabled();
          if (mode) {
            dialog->findChild<QLineEdit *>("replacementMeshPath")
                ->setText(harness.value("camera-import-demo"));
            toggle->setChecked(mode == 2);
          }
          camera_evidence.append(QJsonObject{{"stage",
                                              mode == 0 ? "configuration-disabled" :
                                              mode == 1 ? "reimport-off" :
                                                          "reimport-on"},
                                             {"disabled_before_mesh", disabled},
                                             {"enabled_after_mesh", toggle->isEnabled()},
                                             {"checked", toggle->isChecked()}});
          dialog->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-configuration.png");
          if (mode) {
            dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
            if (!dialog->findChild<QLabel *>("replacementMeshError")->text().isEmpty())
              qFatal("Camera reimport failed: %s",
                     qPrintable(dialog->findChild<QLabel *>("replacementMeshError")->text()));
          }
          else
            dialog->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          create(false);
          break;
        case 2:
          create(true);
          break;
        case 3:
          select_camera("FrontReference");
          break;
        case 4:
          select_camera("AngleReference");
          break;
        case 5:
          wheel(bts::ViewKind::Uv);
          break;
        case 6:
          wheel(bts::ViewKind::Scene);
          break;
        case 7: {
          auto *back = viewport->findChild<QPushButton *>("restoreSceneCamera");
          if (!back->isVisible() || !back->text().contains("AngleReference"))
            qFatal("Imported-camera navigation back control missing");
          back->click();
          break;
        }
        case 8:
          configure(0);
          break;
        case 9:
          configure(1);
          break;
        case 10:
          configure(2);
          break;
        case 11:
          select_camera("FrontReference");
          break;
        case 12:
          save_document(directory + "/cameras.blend");
          break;
        case 13:
          open_document(directory + "/cameras.blend");
          break;
        case 14:
          select_camera("FrontReference");
          break;
        case 15: {
          brush.channels = {true, false, false, false, false};
          brush.ao = false;
          brush.values.base_color = {.8f, .05f, .02f, 1};
          brush.diameter = 12;
          select_material(0);
          QApplication::processEvents();
          viewport->repaint();
          const auto from = viewport->scene_point({-.35f, 0, 0}),
                     to = viewport->scene_point({.35f, 0, 0});
          send_drag(*viewport, from, to - from, Qt::LeftButton, Qt::NoModifier);
          auto &set = document->set(snapshot.materials[0].session_id);
          if (!bts::TextureDocument::layer(set, set.selected_layer).pixels.page_count(0))
            qFatal("Paint through imported camera must record a real stroke");
          break;
        }
        case 16:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (tangent_demo) {
      const auto configure = [&](bool enabled, bool apply) {
        QTimer::singleShot(100, workspace, [&, enabled, apply] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          if (!dialog)
            qFatal("Tangent configuration missing");
          auto *mode = dialog->findChild<QCheckBox *>("projectTangentPerFragment");
          const auto before = mode->isChecked();
          mode->setChecked(enabled);
          tangent_evidence.append(QJsonObject{
              {"stage", sets_stages.at(sets_stage)},
              {"before", before},
              {"chosen", mode->isChecked()},
              {"normal", dialog->findChild<QComboBox *>("projectNormalFormat")->currentText()}});
          dialog->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-configuration.png");
          if (apply) {
            dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
            if (!dialog->findChild<QLabel *>("replacementMeshError")->text().isEmpty())
              qFatal("Tangent configuration rejected");
          }
          else
            dialog->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        if (workspace->findChild<QDialog *>("meshBakeDialog"))
          qFatal("Tangent toggle must not start a bake");
        sets_demo.start(600);
      };
      const auto bake = [&](bool external) {
        auto *poll = new QTimer(workspace);
        QObject::connect(poll, &QTimer::timeout, workspace, [&, poll, external] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeProgress");
          if (!dialog)
            return;
          auto *button = dialog->findChild<QPushButton *>("finishMeshBake");
          if (button->text() == "Close")
            qFatal("Tangent bake failed: %s",
                   qPrintable(dialog->findChild<QLabel *>("meshBakeProgressStatus")->text()));
          if (button->text() != "OK")
            return;
          auto *bar = dialog->findChild<QProgressBar *>("bakeProgressBar");
          const int expected = external ? 2 : 14;
          if (bar->value() != expected || bar->maximum() != expected)
            qFatal("Tangent bake job count mismatch");
          tangent_evidence.append(
              QJsonObject{{"stage", external ? "external-bake" : "low-as-high-bake"},
                          {"done", bar->value()},
                          {"total", bar->maximum()}});
          dialog->grab().save(directory + (external ? "/external-bake-complete.png" :
                                                      "/fragment-bake-complete.png"));
          poll->stop();
          button->click();
        });
        QTimer::singleShot(100, workspace, [&, external, poll] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
          dialog->findChild<QCheckBox *>("bakeUseProjectMesh")->setChecked(!external);
          if (external)
            dialog->findChild<QListWidget *>("bakeHighMeshes")->addItem(harness.value("new-mesh"));
          dialog->findChild<QCheckBox *>("bakeIgnoreBackface")->setChecked(false);
          auto *maps = dialog->findChild<QListWidget *>("bakeMapTypes");
          for (int i = 0; i < 7; ++i)
            maps->item(i)->setCheckState(!external || i == 0 ? Qt::Checked : Qt::Unchecked);
          auto *size = dialog->findChild<QComboBox *>("bakeResolution");
          size->setCurrentIndex(size->findData(128));
          dialog->findChild<QSpinBox *>("bakeSecondaryRays")->setValue(8);
          dialog->findChild<QSpinBox *>("bakeDilation")->setValue(0);
          dialog->findChild<QComboBox *>("bakeAntialiasing")->setCurrentIndex(0);
          dialog->findChild<QCheckBox *>("bakeDimensionsLocked")->setChecked(true);
          dialog->grab().save(directory + (external ? "/external-bake-settings.png" :
                                                      "/fragment-bake-settings.png"));
          poll->start(50);
          dialog->findChild<QPushButton *>("startMeshBake")->click();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger();
        poll->deleteLater();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1: {
          open_document(harness.value("fragment-tangent-demo"));
          for (const auto &description : document->texture_sets()) {
            auto &set = document->set(description.material_session_uid);
            set.mesh_maps = {};
          }
          auto &sphere = document->set(snapshot.materials[1].session_id);
          sphere.layers = {bts::TextureLayer{}};
          sphere.selected_layer = sphere.layers.front().id;
          sphere.selected_mask = false;
          sphere.ao_channel = false;
          sphere.base_values.ambient_occlusion = 1;
          sphere.base_values.roughness = .3;
          sphere.normal_mixing = bts::NormalMixing::Replace;
          bts::TextureDocument::add_fill(sphere);
          auto &fill = bts::TextureDocument::layer(sphere, sphere.selected_layer);
          fill.channels = {false, false, false, true, false};
          fill.ao_enabled = false;
          fill.values.normal = bts::normalized_normal({.55f, .35f, .76f});
          document->set_tangent_per_fragment(
              false);  // Establish fixed channel graphs for every set before the first frame.
          // Later layer/set selection only updates their GPU targets, never topology.
          for (const auto &set : document->texture_sets())
            if (!set.disabled && set.uniform_editable)
              host.prepare_paint_texture(set);
          workspace->texture_sets()->setCurrentRow(1);
          refresh_document_ui();
          break;
        }
        case 2:
          configure(true, false);
          break;
        case 3:
          configure(true, true);
          break;
        case 4:
          configure(true, false);
          break;
        case 5:
          bake(false);
          break;
        case 6:
          bake(true);
          break;
        case 7:
          save_document(directory + "/fragment-tangents.blend");
          break;
        case 8:
          open_document(directory + "/fragment-tangents.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 9:
          configure(false, true);
          break;
        case 10:
          configure(true, true);
          break;
        case 11: {
          workspace->texture_sets()->setCurrentRow(0);
          auto &set = document->set(snapshot.materials[0].session_id);
          brush.channels = {false, false, true, false, false};
          brush.ao = false;
          brush.values.roughness = .8;
          select_material(0);
          QApplication::processEvents();
          viewport->repaint();
          const auto from = viewport->uv_point({.4, .5});
          send_drag(*viewport,
                    from,
                    viewport->uv_point({.6, .5}) - from,
                    Qt::LeftButton,
                    Qt::NoModifier);
          auto &painted_set = document->set(snapshot.materials[0].session_id);
          if (!bts::TextureDocument::layer(painted_set, painted_set.selected_layer)
                   .pixels.page_count(2))
            qFatal("Tangent reopened paint fixture must contain a real stroke");
          break;
        }
        case 12:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (normal_format_demo) {
      const auto configure = [&](bool directx, bool accept) {
        QTimer::singleShot(100, workspace, [&, directx, accept] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          auto *format = dialog->findChild<QComboBox *>("projectNormalFormat");
          format->setCurrentIndex(format->findData(
              int(directx ? bts::NormalMapFormat::DirectX : bts::NormalMapFormat::OpenGL)));
          if (!dialog->findChild<QLineEdit *>("replacementMeshPath")->text().isEmpty())
            qFatal("Format change must not require replacement mesh");
          normal_format_evidence.append(QJsonObject{
              {"stage", sets_stages.at(sets_stage)},
              {"choice", format->currentText()},
              {"preserve_enabled",
               dialog->findChild<QCheckBox *>("preserveStrokePositions")->isEnabled()}});
          dialog->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-configuration.png");
          if (accept) {
            dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
            if (!dialog->findChild<QLabel *>("replacementMeshError")->text().isEmpty())
              qFatal("Format change failed");
          }
          else
            dialog->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        sets_demo.start(600);
      };
      const auto export_maps = [&](const QString &name) {
        auto *poll = new QTimer(workspace);
        QObject::connect(poll, &QTimer::timeout, workspace, [poll, workspace] {
          auto *dialog = workspace->findChild<QDialog *>("textureExportDialog");
          if (!dialog || !dialog->property("exportFinished").toBool())
            return;
          if (!dialog->property("exportError").toString().isEmpty())
            qFatal("Normal export failed: %s",
                   qPrintable(dialog->property("exportError").toString()));
          poll->stop();
          dialog->accept();
        });
        QTimer::singleShot(100, workspace, [&, name, poll] {
          auto *dialog = workspace->findChild<QDialog *>("textureExportDialog");
          const auto output = directory + "/" + name;
          QDir().mkpath(output);
          dialog->findChild<QLineEdit *>("exportDirectory")->setText(output);
          auto *sets = dialog->findChild<QListWidget *>("exportTextureSets");
          for (int row = 0; row < sets->count(); ++row)
            sets->item(row)->setCheckState(row == 0 ? Qt::Checked : Qt::Unchecked);
          auto *format = dialog->findChild<QComboBox *>("exportFormat");
          format->setCurrentIndex(0);
          auto *precision = dialog->findChild<QComboBox *>("exportPrecision");
          precision->setCurrentIndex(precision->findData(0));
          auto *size = dialog->findChild<QComboBox *>("exportSize");
          size->setCurrentIndex(size->findData(1024));
          dialog->findChild<QComboBox *>("exportPadding")->setCurrentIndex(1);
          dialog->findChild<QComboBox *>("exportPreset")->setCurrentIndex(0);
          dialog->grab().save(directory + "/" + name + "-export.png");
          dialog->findChild<QPushButton *>("runTextureExport")->click();
          poll->start(50);
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("exportTextures")->trigger();
        poll->deleteLater();
        sets_demo.start(600);
      };
      const auto stroke = [&](double height) {
        brush.channels = {false, false, false, false, true};
        brush.ao = false;
        brush.values.bump = float(height);
        brush.diameter = 14;
        select_material(0);
        QApplication::processEvents();
        viewport->repaint();
        std::array<double, 2> center{};
        double area = -1;
        for (const auto &triangle : snapshot.uv_triangles)
          if (triangle.material == 0) {
            const auto &p = triangle.corners;
            const auto candidate = std::abs((p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) -
                                            (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]));
            if (candidate > area) {
              area = candidate;
              center = {0, 0};
              for (const auto &v : p) {
                center[0] += v[0] / 3.;
                center[1] += v[1] / 3.;
              }
            }
          }
        const auto from = viewport->uv_point({center[0] - .02, center[1]});
        send_drag(*viewport,
                  from,
                  viewport->uv_point({center[0] + .02, center[1]}) - from,
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1: {
          open_document(harness.value("normal-format-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          auto &set = document->set(snapshot.materials[0].session_id);
          set.mesh_maps[0].reset();
          bts::TextureDocument::add_paint(set);
          document->set_normal_map_format(bts::NormalMapFormat::OpenGL);
          host.apply_texture_set(set, int(document->settings().resolution));
          refresh_document_ui();
          stroke(.2);
          if (!bts::TextureDocument::layer(
                   document->set(snapshot.materials[0].session_id),
                   document->set(snapshot.materials[0].session_id).selected_layer)
                   .pixels.page_count(4))
            qFatal("Normal convention fixture must contain an actual height stroke before export");
          break;
        }
        case 2:
          configure(true, false);
          break;
        case 3:
          configure(true, true);
          break;
        case 4:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 5:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 6:
          export_maps("directx");
          break;
        case 7:
          configure(false, true);
          break;
        case 8:
          export_maps("opengl");
          break;
        case 9:
          save_document(directory + "/normal-format.blend");
          break;
        case 10:
          open_document(directory + "/normal-format.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 11:
          stroke(.4);
          break;
        case 12:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (template_demo) {
      const auto path = template_load ? harness.value("template-load-demo") :
                                        directory + "/custom.btst";
      const auto new_project = [&](int mode) {
        bts::NewProjectDialog dialog(workspace);
        QTimer::singleShot(100, &dialog, [&, mode] {
          auto *combo = dialog.findChild<QComboBox *>("projectTemplate");
          if (mode < 2)
            combo->setCurrentIndex(combo->findData(":pbr"));
          else {
            int index = combo->findData(path);
            if (index < 0) {
              dialog.findChild<QPushButton *>("browseProjectTemplate")->click();
              auto *picker = dialog.findChild<QFileDialog *>("projectTemplatePicker");
              if (!picker)
                qFatal("Template browse picker missing");
              picker->selectFile(path);
              QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
              index = combo->findData(path);
            }
            if (index < 0)
              qFatal("Custom template selection missing");
            combo->setCurrentIndex(index);
          }
          dialog.findChild<QLineEdit *>("meshPath")->setText(harness.value("new-mesh"));
          if (template_load)
            dialog.findChild<QComboBox *>("documentResolution")
                ->setCurrentIndex(
                    dialog.findChild<QComboBox *>("documentResolution")->findData(512));
          QJsonObject item{
              {"stage", sets_stages.at(sets_stage)},
              {"kind", mode < 2 ? "pbr" : "custom"},
              {"normal", dialog.findChild<QComboBox *>("normalMapFormat")->currentText()},
              {"resolution",
               dialog.findChild<QComboBox *>("documentResolution")->currentData().toInt()},
              {"template", combo->currentText()}};
          template_evidence.append(item);
          dialog.grab().save(directory + "/" + sets_stages.at(sets_stage) + "-new-dialog.png");
          if (mode == 0)
            dialog.reject();
          else
            dialog.findChild<QPushButton *>("createProject")->click();
        });
        sets_demo.stop();
        if (dialog.exec() == QDialog::Accepted) {
          auto options = dialog.options();
          if (!options)
            qFatal("Template New options absent");
          create_project(*options);
        }
        sets_demo.start(600);
      };
      const auto save_template = [&](bool accept) {
        QTimer::singleShot(100, workspace, [&, accept] {
          auto *picker = workspace->findChild<QFileDialog *>("saveTemplatePicker");
          if (!picker)
            qFatal("Save template picker missing");
          picker->selectFile(path);
          picker->grab().save(directory + "/template-save-dialog.png");
          if (accept)
            QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
          else
            picker->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("saveProjectTemplate")->trigger();
        sets_demo.start(600);
      };
      const auto inspect_bake = [&] {
        QTimer::singleShot(100, workspace, [&] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
          if (!dialog)
            qFatal("Template bake settings missing");
          template_evidence.append(QJsonObject{
              {"stage", "bake-ui"},
              {"resolution",
               dialog->findChild<QComboBox *>("bakeResolution")->currentData().toInt()},
              {"height", dialog->findChild<QComboBox *>("bakeHeight")->currentData().toInt()},
              {"locked", dialog->findChild<QCheckBox *>("bakeDimensionsLocked")->isChecked()},
              {"rays", dialog->findChild<QSpinBox *>("bakeSecondaryRays")->value()},
              {"sampling",
               dialog->findChild<QComboBox *>("bakeAntialiasing")->currentData().toInt()},
              {"dilation", dialog->findChild<QSpinBox *>("bakeDilation")->value()}});
          dialog->grab().save(directory + "/template-bake-dialog.png");
          dialog->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger();
        sets_demo.start(600);
      };
      const auto paint = [&] {
        brush.channels = {false, false, true, false, false};
        brush.ao = false;
        brush.values.roughness = .8;
        select_material(0);
        const auto from = viewport->uv_point({.45, .5});
        send_drag(
            *viewport, from, viewport->uv_point({.55, .5}) - from, Qt::LeftButton, Qt::NoModifier);
      };
      const int step = template_load ? sets_stage + 5 : sets_stage;
      switch (step) {
        case 1: {
          open_document(harness.value("template-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          auto &set = document->set(snapshot.materials[0].session_id);
          set.ao_channel = true;
          set.channels[1] = false;
          set.normal_mixing = bts::NormalMixing::Replace;
          set.bump_method = bts::BumpMethod::Sharp;
          set.padding_3d = false;
          const auto resource = bts::pack_environment_resource(
              host.environment_state()["resource"].toString());
          QFile file(directory + "/template-source.hdr");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Template resource copy failed");
          file.write(QByteArray::fromBase64(resource["data"].toString().toLatin1()));
          file.close();
          host.set_environment_resource(file.fileName().toStdString());
          host.set_environment_rotation(30);
          host.set_environment_appearance("exposure", 1);
          host.set_environment_appearance("opacity", 80);
          host.set_environment_appearance("blur", 25);
          host.set_environment_alignment(false);
          host.set_display_transform("Standard");
          QJsonObject bake{{"resolution", 128},
                           {"height", 64},
                           {"locked", false},
                           {"dilation", 8},
                           {"rays", 16},
                           {"sampling", 2},
                           {"diffusion", true},
                           {"project_mesh", true},
                           {"enabled", QJsonArray{true, false, false, false, false, true, false}},
                           {"frontal", .02},
                           {"rear", .03},
                           {"relative", false},
                           {"average", false},
                           {"ignore_backface", true}};
          bts::apply_mesh_bake_template(document->id(), bake);
          host.apply_texture_set(set, int(document->settings().resolution));
          refresh_document_ui();
          break;
        }
        case 2:
          save_template(false);
          if (QFileInfo::exists(path))
            qFatal("Cancel wrote a template");
          break;
        case 3:
          save_template(true);
          if (!QFileInfo::exists(path))
            qFatal("Save did not write template");
          if (!QFile::remove(directory + "/template-source.hdr"))
            qFatal("Owned template source removal failed");
          break;
        case 4:
          new_project(0);
          break;
        case 5:
          new_project(1);
          break;
        case 6:
          new_project(2);
          break;
        case 7:
          inspect_bake();
          break;
        case 8:
          save_document(directory + "/templated.blend");
          break;
        case 9:
          open_document(directory + "/templated.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 10:
          paint();
          break;
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (reassign_demo) {
      const auto configure = [&] {
        QTimer::singleShot(100, workspace, [&] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          dialog->findChild<QLineEdit *>("replacementMeshPath")
              ->setText(harness.value("replacement-mesh"));
          dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
          if (!dialog->findChild<QLabel *>("replacementMeshError")->text().isEmpty())
            qFatal("Renamed mesh failed: %s",
                   qPrintable(dialog->findChild<QLabel *>("replacementMeshError")->text()));
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        sets_demo.start(600);
      };
      const auto assign = [&](bool apply) {
        QTimer::singleShot(100, workspace, [&, apply] {
          auto *dialog = workspace->findChild<QDialog *>("textureSetReassignmentDialog");
          if (!dialog)
            qFatal("Assignment dialog missing");
          QUuid cube;
          for (const auto &set : document->texture_sets())
            if (set.name == "CubeSurface")
              cube = set.id;
          int row = -1;
          for (auto *label : dialog->findChildren<QLabel *>())
            if (label->objectName().startsWith("assignmentMaterial") &&
                label->text() == "CompanionRenamed")
              row = label->objectName().mid(18).toInt();
          if (row < 0)
            qFatal("Renamed destination missing");
          auto *choice = dialog->findChild<QComboBox *>(QString("setAssignment%1").arg(row));
          auto *undo = dialog->findChild<QPushButton *>("undoSetAssignment"),
               *redo = dialog->findChild<QPushButton *>("redoSetAssignment");
          const auto evidence = [&](const QString &stage) {
            QJsonArray selected;
            for (auto *combo : dialog->findChildren<QComboBox *>())
              selected.append(combo->currentData().toString());
            assignment_evidence.append(QJsonObject{{"stage", stage},
                                                   {"assigned", selected},
                                                   {"undo", undo->isEnabled()},
                                                   {"redo", redo->isEnabled()},
                                                   {"document", document->to_json()}});
            dialog->grab().save(directory + "/assignment-" + stage + ".png");
          };
          evidence("initial");
          QMimeData mime;
          mime.setData("application/x-bts-texture-set", cube.toString().toUtf8());
          QDragEnterEvent enter(
              QPoint(10, 10), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(choice, &enter);
          QDropEvent drop(QPointF(10, 10), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(choice, &drop);
          if (!enter.isAccepted() || !drop.isAccepted() ||
              QUuid(choice->currentData().toString()) != cube)
            qFatal("Disabled Texture Set drop rejected");
          evidence("swapped");
          undo->click();
          evidence("undo");
          redo->click();
          evidence("redo");
          if (apply)
            dialog->findChild<QPushButton *>("applySetAssignment")->click();
          else
            dialog->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("reassignTextureSets")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("reassign-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 2:
          configure();
          break;
        case 3:
          assign(false);
          break;
        case 4:
          assign(true);
          break;
        case 5:
          save_document(directory + "/assigned.blend");
          break;
        case 6:
          open_document(directory + "/assigned.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 7:
          brush.channels = {false, false, true, false, false};
          brush.ao = false;
          brush.values.roughness = .7;
          select_material(0);
          {
            const auto from = viewport->uv_point({.2, .2});
            send_drag(*viewport,
                      from,
                      viewport->uv_point({.3, .2}) - from,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
          break;
        case 8:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (uv_reproject_demo) {
      const auto stroke_uv = [&](QPointF a, QPointF b) {
        const auto from = viewport->uv_point(a);
        send_drag(*viewport, from, viewport->uv_point(b) - from, Qt::LeftButton, Qt::NoModifier);
      };
      const auto configure = [&](bool preserve) {
        QTimer::singleShot(100, workspace, [&, preserve] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          dialog->findChild<QLineEdit *>("replacementMeshPath")
              ->setText(harness.value("replacement-mesh"));
          auto *control = dialog->findChild<QCheckBox *>("preserveStrokePositions");
          if (!control->isEnabled())
            qFatal("Preserve checkbox must enable after choosing a mesh");
          control->setChecked(preserve);
          dialog->grab().save(directory + (preserve ? "/preserve-on.png" : "/preserve-off.png"));
          dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
          if (!dialog->findChild<QLabel *>("replacementMeshError")->text().isEmpty())
            qFatal("Spatial reprojection failed: %s",
                   qPrintable(dialog->findChild<QLabel *>("replacementMeshError")->text()));
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1: {
          open_document(harness.value("uv-reproject-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          brush.channels = {true, false, false, false, false};
          brush.ao = false;
          brush.values.base_color = {1, 1, 1, 1};
          brush.diameter = 10;
          select_material(0);
          const auto from = viewport->scene_point({.95f, -.5f, 1.f}),
                     to = viewport->scene_point({1.2f, -.5f, 1.25f});
          send_drag(*viewport, from, to - from, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 2: {
          std::array<double, 2> center{};
          double nearest = 1e20;
          for (const auto &triangle : snapshot.uv_triangles)
            if (triangle.material == 0) {
              double y = 0;
              for (const auto &p : triangle.positions)
                y += p[1] / 3.;
              if (y < nearest) {
                nearest = y;
                center = {0, 0};
                for (const auto &uv : triangle.corners) {
                  center[0] += uv[0] / 3.;
                  center[1] += uv[1] / 3.;
                }
              }
            }
          stroke_uv({center[0] - .02, center[1]}, {center[0] + .02, center[1]});
          break;
        }
        case 3:
          save_document(directory + "/original.blend");
          break;
        case 4:
          open_document(directory + "/original.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 5:
          configure(true);
          break;
        case 6:
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 7:
          update_inspection(bts::InspectionMode::Material);
          break;
        case 8:
          save_document(directory + "/on.blend");
          break;
        case 9:
          open_document(directory + "/on.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 10:
          brush.channels = {false, false, true, false, false};
          brush.ao = false;
          brush.values.roughness = .7;
          select_material(0);
          stroke_uv({.2, .2}, {.3, .2});
          break;
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 12:
          open_document(directory + "/original.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 13:
          configure(false);
          break;
        case 14:
          update_inspection(bts::InspectionMode::Material);
          break;
        case 15:
          open_document(directory + "/original.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
      }
    }
    else if (reimport_demo) {
      const auto configure = [&](int mode) {
        QTimer::singleShot(100, workspace, [&, mode] {
          auto *dialog = workspace->findChild<QDialog *>("projectConfigurationDialog");
          if (!dialog)
            qFatal("Project Configuration missing");
          const auto file = mode == 2 ? directory + "/missing.obj" :
                            mode == 3 ? directory + "/wrong/replacement.obj" :
                                        harness.value("replacement-mesh");
          dialog->findChild<QLineEdit *>("replacementMeshPath")->setText(file);
          dialog->grab().save(directory + QString("/configuration%1.png").arg(mode));
          if (mode == 0) {
            dialog->reject();
            return;
          }
          dialog->findChild<QPushButton *>("commitReplacementMesh")->click();
          if (mode == 1) {
            if (!dialog->findChild<QLabel *>("replacementMeshError")->text().isEmpty())
              qFatal("Replacement failed: %s",
                     qPrintable(dialog->findChild<QLabel *>("replacementMeshError")->text()));
          }
          else {
            if (dialog->findChild<QLabel *>("replacementMeshError")->text().isEmpty())
              qFatal("Invalid replacement admitted");
            dialog->grab().save(directory + QString("/rejected%1.png").arg(mode));
            dialog->reject();
          }
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("projectConfiguration")->trigger();
        sets_demo.start(600);
      };
      const auto stroke = [&](QPointF a, QPointF b) {
        const auto from = viewport->uv_point(a);
        send_drag(*viewport, from, viewport->uv_point(b) - from, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1: {
          open_document(harness.value("mesh-reimport-demo"));
          workspace->texture_sets()->setCurrentRow(1);
          brush.channels = {true, false, false, false, false};
          brush.ao = false;
          brush.values.base_color = {1, 1, 1, 1};
          select_material(1);
          stroke({.35, .5}, {.65, .5});
          stroke({.5, .35}, {.5, .65});
          auto &cube = document->set(snapshot.materials[0].session_id);
          bts::TextureDocument::add_fill(cube);
          auto &fill = bts::TextureDocument::layer(cube, cube.selected_layer);
          fill.channels = {true, false, false, false, false};
          fill.ao_enabled = false;
          fill.fill.atom = true;
          fill.fill.mode = 1;
          fill.values.base_color = {.8, .8, .8, 1};
          bts::TextureDocument::add_mask(cube, 1);
          cube.selected_mask = false;
          host.apply_texture_set(cube, int(document->settings().resolution));
          workspace->texture_sets()->setCurrentRow(0);
          refresh_document_ui();
          break;
        }
        case 2:
          configure(0);
          break;
        case 3:
          configure(2);
          break;
        case 4:
          configure(3);
          break;
        case 5:
          configure(1);
          break;
        case 6:
          workspace->texture_sets()->setCurrentRow(1);
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 7:
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Material);
          break;
        case 8:
          save_document(directory + "/reimported.blend");
          break;
        case 9:
          open_document(directory + "/reimported.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 10:
          workspace->texture_sets()->setCurrentRow(1);
          brush.channels = {false, false, true, false, false};
          brush.ao = false;
          brush.values.roughness = .7;
          select_material(1);
          stroke({.2, .3}, {.3, .3});
          break;
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (recovery_crash_demo || recovery_open_demo) {
      const auto stroke = [&](double x0, double y0, double x1, double y1, float value) {
        brush.channels = {false, false, true, false, false};
        brush.ao = false;
        brush.values.roughness = value;
        select_material(0);
        const auto from = viewport->uv_point({x0, y0});
        send_drag(
            *viewport, from, viewport->uv_point({x1, y1}) - from, Qt::LeftButton, Qt::NoModifier);
      };
      if (recovery_crash_demo) {
        switch (sets_stage) {
          case 1: {
            open_document(harness.value("recovery-crash-demo"));
            workspace->texture_sets()->setCurrentRow(0);
            std::uint32_t random = 0x7e57a11u;
            const int res = int(document->settings().resolution);
            for (const auto &description : document->texture_sets()) {
              auto &set = document->set(description.material_session_uid);
              for (int role = 4; role < 7; ++role) {
                std::vector<float> pixels(std::size_t(res) * res * 4);
                for (std::size_t i = 0; i < pixels.size(); i += 4) {
                  for (int c = 0; c < 3; ++c) {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    pixels[i + c] = float(random >> 8) / 16777216.f;
                  }
                  pixels[i + 3] = 1;
                }
                auto data = std::make_shared<bts::MeshMapPixels>(res, std::move(pixels));
                bts::MeshMap map;
                map.name = "Recovery IO workload";
                map.tiles[1001] = {res, data->hash, data, res};
                set.mesh_maps[role] = std::move(map);
              }
            }
            break;
          }
          case 2:
            save_document(directory + "/last-good.blend");
            recovery_good_document = document->to_json();
            break;
          case 3: {
            stroke(.45, .5, .55, .5, .8);
            const auto path = directory + "/last-good.blend";
            QFile original(path);
            if (!original.open(QIODevice::ReadOnly))
              qFatal("Recovery baseline missing");
            const auto hash =
                QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256).toHex();
            QSaveFile witness(directory + "/witness.json");
            if (!witness.open(QIODevice::WriteOnly))
              qFatal("Recovery witness unavailable");
            const auto bytes = QJsonDocument(
                                   QJsonObject{{"pid", double(QCoreApplication::applicationPid())},
                                               {"baseline", recovery_good_document},
                                               {"edited", document->to_json()},
                                               {"last_good_sha256", QString::fromLatin1(hash)},
                                               {"target", path}})
                                   .toJson();
            if (witness.write(bytes) != bytes.size() || !witness.commit())
              qFatal("Recovery witness commit failed");
            std::printf("BTS_INTERRUPTIBLE_SAVE pid=%lld target=%s\n",
                        static_cast<long long>(QCoreApplication::applicationPid()),
                        qPrintable(path));
            std::fflush(stdout);
            save_document(path);
            qFatal("Crash test failed to interrupt the actual save");
          }
        }
      }
      else {
        switch (sets_stage) {
          case 1:
            if (harness.is_set("recover-latest-demo")) {
              QTimer::singleShot(100, workspace, [&] {
                auto *dialog = workspace->findChild<QDialog *>("recoveryDialog");
                if (!dialog)
                  qFatal("Recovery chooser missing");
                dialog->grab().save(directory + "/recovery-chooser.png");
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Open)->click();
              });
              const auto path = bts::choose_recovery(workspace, recovery);
              if (path.isEmpty())
                qFatal("Recovery chooser empty");
              recover_project(path);
            }
            else
              open_document(
                  harness.value(recovery_auto_demo ? "recovery-auto-demo" : "recovery-open-demo"));
            workspace->texture_sets()->setCurrentRow(0);
            break;
          case 2:
            stroke(.2, .3, .3, .3, .4);
            break;
          case 3:
            if (recovery_auto_demo) {
              sets_demo.stop();
              QEventLoop wait;
              QTimer poll;
              poll.setInterval(50);
              QObject::connect(&poll, &QTimer::timeout, &wait, [&] {
                if (!recovery.entries().empty())
                  wait.quit();
              });
              QTimer::singleShot(60000, &wait, &QEventLoop::quit);
              poll.start();
              wait.exec();
              if (recovery.entries().empty())
                qFatal("Idle autosave did not write a snapshot");
              sets_demo.start(600);
            }
            else
              workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 4:
            recover_project(recovery.entries().front().path);
            break;
          case 5:
            stroke(.65, .7, .75, .7, .6);
            break;
          case 6:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
        }
      }
    }
    else if (settings_demo) {
      const auto settings = [&](int operation) {
        QTimer::singleShot(100, workspace, [&, operation] {
          auto *dialog = workspace->findChild<QDialog *>("textureExportDialog");
          if (!dialog)
            qFatal("Export settings dialog missing");
          auto *tabs = dialog->findChild<QTabWidget *>("exportTabs");
          auto *sets = dialog->findChild<QListWidget *>("exportTextureSets");
          auto *size = dialog->findChild<QComboBox *>("exportSize");
          auto *format = dialog->findChild<QComboBox *>("exportFormat");
          auto *precision = dialog->findChild<QComboBox *>("exportPrecision");
          auto *presets = dialog->findChild<QComboBox *>("exportPreset");
          auto *save = dialog->findChild<QPushButton *>("saveExportSettings");
          if (operation == 0) {
            const auto output = directory + "/settings-only";
            QDir().mkpath(output);
            dialog->findChild<QLineEdit *>("exportDirectory")->setText(output);
            sets->item(0)->setCheckState(Qt::Checked);
            sets->item(1)->setCheckState(Qt::Unchecked);
            format->setCurrentIndex(0);
            precision->setCurrentIndex(precision->findData(1));
            size->setCurrentIndex(size->findData(512));
            dialog->findChild<QComboBox *>("exportPadding")->setCurrentIndex(4);
            dialog->findChild<QSpinBox *>("exportPaddingWidth")->setValue(16);
            dialog->findChild<QPushButton *>("newPackedExportPreset")->click();
            auto *name = dialog->findChild<QLineEdit *>("exportPresetName");
            name->setText("BTS saved packed");
            QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
            dialog->findChild<QLineEdit *>("exportPackedName")->setText("$mesh/$textureSet_saved");
            dialog->findChild<QComboBox *>("exportPackedR")->setCurrentIndex(4);
            dialog->findChild<QComboBox *>("exportPackedG")->setCurrentIndex(6);
            dialog->findChild<QComboBox *>("exportPackedB")->setCurrentIndex(5);
            presets->setCurrentIndex(presets->findText("BTS saved packed"));
          }
          if (operation == 2) {
            size->setCurrentIndex(size->findData(1024));
            precision->setCurrentIndex(precision->findData(0));
            dialog->findChild<QPushButton *>("newPackedExportPreset")->click();
          }
          if (operation == 3)
            size->setCurrentIndex(size->findData(256));
          dialog->findChild<QListWidget *>("exportPresets")
              ->setCurrentRow(presets->currentIndex());
          tabs->setCurrentIndex(1);
          const bool template_save_disabled = !save->isEnabled();
          tabs->setCurrentIndex(0);
          if (!template_save_disabled || !save->isEnabled())
            qFatal("Save settings tab enablement failed");
          QJsonArray selected;
          for (int row = 0; row < sets->count(); ++row)
            if (sets->item(row)->checkState() == Qt::Checked)
              selected.append(sets->item(row)->text());
          export_evidence.append(QJsonObject{
              {"operation", operation},
              {"stage", sets_stages.at(sets_stage)},
              {"directory", dialog->findChild<QLineEdit *>("exportDirectory")->text()},
              {"format", format->currentText()},
              {"precision", precision->currentText()},
              {"size", size->currentData().toInt()},
              {"padding", dialog->findChild<QComboBox *>("exportPadding")->currentText()},
              {"width", dialog->findChild<QSpinBox *>("exportPaddingWidth")->value()},
              {"selected", selected},
              {"preset", presets->currentText()},
              {"preset_count", presets->count()},
              {"template_save_disabled", template_save_disabled},
              {"progress_visible",
               dialog->findChild<QProgressBar *>("exportProgress")->isVisible()},
              {"packed_name", dialog->findChild<QLineEdit *>("exportPackedName")->text()},
              {"red", dialog->findChild<QComboBox *>("exportPackedR")->currentText()},
              {"green", dialog->findChild<QComboBox *>("exportPackedG")->currentText()},
              {"blue", dialog->findChild<QComboBox *>("exportPackedB")->currentText()}});
          dialog->grab().save(directory + "/" + sets_stages.at(sets_stage) + "-settings.png");
          if (operation == 0 || operation == 3)
            save->click();
          else
            dialog->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("exportTextures")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("export-settings-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 2:
          settings(0);
          break;
        case 3:
          settings(1);
          break;
        case 4:
          settings(2);
          break;
        case 5:
          settings(1);
          break;
        case 6:
          save_document(directory + "/settings.blend");
          break;
        case 7:
          open_document(directory + "/settings.blend");
          break;
        case 8:
          settings(1);
          break;
        case 9:
          brush.channels = {false, false, true, false, false};
          brush.ao = false;
          brush.values.roughness = .8;
          select_material(0);
          {
            const auto from = viewport->uv_point({.45, .5});
            send_drag(*viewport,
                      from,
                      viewport->uv_point({.55, .5}) - from,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
          break;
        case 10:
          settings(3);
          break;
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 12:
          settings(1);
          break;
        case 13:
          open_document(harness.value("export-settings-demo"));
          settings(4);
          break;
        case 14:
          open_document(directory + "/settings.blend");
          break;
        case 15:
          settings(1);
          break;
      }
    }
    else if (export_demo) {
      if (sets_stage == 1) {
        open_document(harness.value(export_padding_demo ? "export-padding-demo" :
                                    packing_demo        ? "export-packing-demo" :
                                    size_demo           ? "export-size-demo" :
                                    naming_demo         ? "export-naming-demo" :
                                                          "export-demo"));
        workspace->texture_sets()->setCurrentRow(1);
        auto &set = document->set(snapshot.materials[1].session_id);
        set.base_values.roughness = .123456f;
        set.base_values.bump = -.125f;
        set.base_values.normal = {.1f, .2f, std::sqrt(.95f)};
        host.apply_texture_set(set, int(document->settings().resolution));
        if (export_padding_demo) {
          workspace->texture_sets()->setCurrentRow(0);
          auto &cube = document->set(snapshot.materials[0].session_id);
          bts::TextureDocument::add_fill(cube);
          auto &fill = bts::TextureDocument::layer(cube, cube.selected_layer);
          fill.fill.atom = true;
          fill.fill.scale = {2, 2};
          fill.channels = {true, false, false, false, false};
          fill.ao_enabled = false;
          fill.values.base_color = {.8, .8, .8, 1};
          host.apply_texture_set(cube, int(document->settings().resolution));
          const auto source = host.export_source(*document, {cube.id});
          const int res = source[0].resolution;
          QImage mask(res, res, QImage::Format_Grayscale8);
          for (int y = 0; y < res; ++y)
            for (int x = 0; x < res; ++x)
              mask.scanLine(res - 1 - y)[x] = source[0].coverage[std::size_t(y) * res + x] ? 255 :
                                                                                             0;
          mask.save(directory + "/uv-mask.png");
        }
        update_inspection(bts::InspectionMode::Material);
      }
      else if (sets_stage >= 2 && sets_stage <= (export_padding_demo ? 6 : 5)) {
        const int encoding = sets_stage - 2;
        const QString label =
            (export_padding_demo ?
                 QStringList{"none", "infinite", "transparent", "default", "diffusion"} :
             packing_demo ? QStringList{"reference", "orm", "remapped", "constants"} :
                            QStringList{"png8", "png16", "exr16", "exr32"})
                .at(encoding);
        const auto output = directory + "/" + label;
        QDir().mkpath(output);
        bool configured = false;
        QTimer poll;
        poll.setInterval(100);
        QObject::connect(&poll, &QTimer::timeout, workspace, [&] {
          auto *dialog = workspace->findChild<QDialog *>("textureExportDialog");
          if (!dialog)
            return;
          if (!configured) {
            configured = true;
            if (naming_demo && encoding == 0) {
              dialog->findChild<QPushButton *>("duplicateExportPreset")->click();
              if (dialog->findChild<QComboBox *>("exportPreset")->currentIndex() != 0)
                qFatal("Editing a template must not select it");
              auto *name = dialog->findChild<QLineEdit *>("exportPresetName");
              name->setText("BTS_reference_tokens");
              QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
              auto *table = dialog->findChild<QTableWidget *>("exportOutputNames");
              for (int c = 0; c < 8; ++c)
                table->item(c, 0)->setText(c == 0 ? "$mesh/$textureSet_BaseColor" :
                                                    "$mesh_$textureSet_$channel");
              dialog->findChild<QComboBox *>("exportPreset")
                  ->setCurrentIndex(dialog->findChild<QComboBox *>("exportPreset")
                                        ->findText("BTS_reference_tokens"));
              dialog->findChild<QTabWidget *>("exportTabs")->setCurrentIndex(1);
              dialog->grab().save(directory + "/tokens-template.png");
              dialog->findChild<QTabWidget *>("exportTabs")->setCurrentIndex(0);
            }
            if (packing_demo && encoding > 0) {
              if (encoding == 1) {
                dialog->findChild<QPushButton *>("newPackedExportPreset")->click();
                auto *name = dialog->findChild<QLineEdit *>("exportPresetName");
                name->setText("BTS_packed_test");
                QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
                dialog->findChild<QComboBox *>("exportPreset")
                    ->setCurrentIndex(dialog->findChild<QComboBox *>("exportPreset")
                                          ->findText("BTS_packed_test"));
              }
              else {
                auto *list = dialog->findChild<QListWidget *>("exportPresets");
                list->setCurrentItem(list->findItems("BTS_packed_test", Qt::MatchExactly).front());
              }
              auto *name = dialog->findChild<QLineEdit *>("exportPackedName");
              if (encoding == 2) {
                name->setText("$textureSet_MRO");
                dialog->findChild<QComboBox *>("exportPackedR")->setCurrentIndex(2);
                dialog->findChild<QComboBox *>("exportPackedG")->setCurrentIndex(0);
                dialog->findChild<QComboBox *>("exportPackedB")->setCurrentIndex(1);
              }
              if (encoding == 3) {
                name->setText("$textureSet_Test");
                dialog->findChild<QComboBox *>("exportPackedR")->setCurrentIndex(4);
                dialog->findChild<QComboBox *>("exportPackedG")->setCurrentIndex(6);
                dialog->findChild<QComboBox *>("exportPackedB")->setCurrentIndex(5);
              }
              dialog->findChild<QTabWidget *>("exportTabs")->setCurrentIndex(1);
              dialog->grab().save(directory + "/" + label + "-mapping.png");
              dialog->findChild<QTabWidget *>("exportTabs")->setCurrentIndex(0);
            }
            auto *sets = dialog->findChild<QListWidget *>("exportTextureSets");
            sets->item(0)->setCheckState(size_demo || export_padding_demo ? Qt::Checked :
                                                                            Qt::Unchecked);
            if (export_padding_demo) {
              sets->item(1)->setCheckState(Qt::Unchecked);
              auto *size = dialog->findChild<QComboBox *>("exportSize");
              size->setCurrentIndex(size->findData(256));
              auto *padding = dialog->findChild<QComboBox *>("exportPadding");
              padding->setCurrentIndex(encoding);
              auto *width = dialog->findChild<QSpinBox *>("exportPaddingWidth");
              width->setValue(16);
              if (width->isEnabled() != (encoding >= 2))
                qFatal("Wrong padding width enablement");
            }
            if (size_demo) {
              auto *size = dialog->findChild<QComboBox *>("exportSize");
              auto *override = dialog->findChild<QCheckBox *>("exportSizeOverride");
              auto *global = dialog->findChild<QPushButton *>("exportGlobalSettings");
              if (encoding == 0) {
                sets->setCurrentRow(1);
                override->setChecked(true);
                size->setCurrentIndex(size->findData(512));
                dialog->grab().save(directory + "/size-override512.png");
                global->click();
                size->setCurrentIndex(size->findData(1024));
              }
              else if (encoding == 1) {
                global->click();
                size->setCurrentIndex(size->findData(256));
              }
              else if (encoding == 2) {
                sets->setCurrentRow(1);
                override->setChecked(false);
                dialog->grab().save(directory + "/size-inherited.png");
              }
              else {
                global->click();
                size->setCurrentIndex(size->findData(1024));
              }
            }
            dialog->findChild<QLineEdit *>("exportDirectory")->setText(output);
            dialog->findChild<QComboBox *>("exportFormat")
                ->setCurrentIndex(export_padding_demo ? 0 :
                                  packing_demo        ? (encoding == 3 ? 1 : 0) :
                                  encoding < 2        ? 0 :
                                                        1);
            auto *precision = dialog->findChild<QComboBox *>("exportPrecision");
            precision->setCurrentIndex(precision->findData(export_padding_demo ? 0 :
                                                           packing_demo ? (encoding == 3 ? 3 : 0) :
                                                                          encoding));
            dialog->grab().save(directory + "/" + label + "-settings.png");
            dialog->findChild<QPushButton *>("runTextureExport")->click();
            return;
          }
          if (!dialog->property("exportFinished").toBool())
            return;
          if (!dialog->property("exportError").toString().isEmpty())
            qFatal("Export failed: %s", qPrintable(dialog->property("exportError").toString()));
          QJsonArray files;
          if (naming_demo) {
            QDirIterator iterator(output, QDir::Files, QDirIterator::Subdirectories);
            while (iterator.hasNext())
              files.append(QDir(output).relativeFilePath(iterator.next()));
          }
          else
            for (const auto &name : QDir(output).entryList(QDir::Files))
              files.append(name);
          export_evidence.append(QJsonObject{
              {"encoding", label},
              {"precision", dialog->findChild<QComboBox *>("exportPrecision")->currentText()},
              {"count", dialog->findChild<QLabel *>("exportCount")->text()},
              {"files", files},
              {"console", dialog->findChild<QPlainTextEdit *>("exportConsole")->toPlainText()}});
          dialog->grab().save(directory + "/" + label + "-complete.png");
          poll.stop();
          dialog->reject();
        });
        sets_demo.stop();
        poll.start();
        workspace->findChild<QAction *>("exportTextures")->trigger();
        sets_demo.start(600);
      }
      else if (sets_stage == (export_padding_demo ? 7 : 6)) {
        QTimer::singleShot(100, workspace, [&] {
          auto *dialog = workspace->findChild<QDialog *>("textureExportDialog");
          if (!dialog)
            qFatal("Export settings missing");
          if (naming_demo) {
            auto *list = dialog->findChild<QListWidget *>("exportPresets");
            list->setCurrentItem(
                list->findItems("BTS_reference_tokens", Qt::MatchExactly).front());
            auto *table = dialog->findChild<QTableWidget *>("exportOutputNames");
            auto *button = dialog->findChild<QPushButton *>("runTextureExport");
            const auto original = table->item(0, 0)->text();
            table->item(0, 0)->setText("$unknown/out");
            if (button->isEnabled())
              qFatal("Unknown token admitted");
            const auto unknown = dialog->findChild<QLabel *>("exportCount")->text();
            table->item(0, 0)->setText("$mesh_$textureSet_Metallic");
            if (button->isEnabled())
              qFatal("Duplicate name admitted");
            const auto collision = dialog->findChild<QLabel *>("exportCount")->text();
            table->item(0, 0)->setText("../outside");
            if (button->isEnabled())
              qFatal("Parent path admitted");
            const auto traversal = dialog->findChild<QLabel *>("exportCount")->text();
            table->item(0, 0)->setText(original);
            export_evidence.append(QJsonObject{
                {"unknown_rejected", unknown},
                {"collision_rejected", collision},
                {"traversal_rejected", traversal},
                {"restored", button->isEnabled()},
                {"preset", dialog->findChild<QComboBox *>("exportPreset")->currentText()}});
          }
          if (size_demo) {
            auto *sets = dialog->findChild<QListWidget *>("exportTextureSets");
            sets->setCurrentRow(1);
            export_evidence.append(
                QJsonObject{{"local_override",
                             dialog->findChild<QCheckBox *>("exportSizeOverride")->isChecked()},
                            {"inherited_size",
                             dialog->findChild<QComboBox *>("exportSize")->currentData().toInt()},
                            {"local_size_disabled",
                             !dialog->findChild<QComboBox *>("exportSize")->isEnabled()},
                            {"row", sets->item(1)->text()}});
          }
          if (packing_demo) {
            {
              auto *list = dialog->findChild<QListWidget *>("exportPresets");
              list->setCurrentItem(list->findItems("BTS_packed_test", Qt::MatchExactly).front());
            }
            export_evidence.append(QJsonObject{
                {"packed_name", dialog->findChild<QLineEdit *>("exportPackedName")->text()},
                {"red", dialog->findChild<QComboBox *>("exportPackedR")->currentText()},
                {"green", dialog->findChild<QComboBox *>("exportPackedG")->currentText()},
                {"blue", dialog->findChild<QComboBox *>("exportPackedB")->currentText()}});
          }
          if (export_padding_demo)
            export_evidence.append(QJsonObject{
                {"padding", dialog->findChild<QComboBox *>("exportPadding")->currentText()},
                {"width", dialog->findChild<QSpinBox *>("exportPaddingWidth")->value()},
                {"width_enabled",
                 dialog->findChild<QSpinBox *>("exportPaddingWidth")->isEnabled()}});
          export_evidence.append(QJsonObject{
              {"retained_format", dialog->findChild<QComboBox *>("exportFormat")->currentText()},
              {"retained_precision",
               dialog->findChild<QComboBox *>("exportPrecision")->currentText()},
              {"retained_count", dialog->findChild<QLabel *>("exportCount")->text()}});
          dialog->reject();
        });
        sets_demo.stop();
        workspace->findChild<QAction *>("exportTextures")->trigger();
        sets_demo.start(600);
      }
    }
    else if (recover_channel_demo) {
      const auto remove = [&] { workspace->findChild<QToolButton *>("removeChannel5")->click(); };
      const auto add = [&] {
        auto *action = workspace->findChild<QAction *>("addChannel5");
        if (!action->isEnabled())
          qFatal("Recover AO channel action unavailable");
        action->trigger();
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("recover-channel-demo"));
          workspace->texture_sets()->setCurrentRow(1);
          viewport->set_layout({bts::ViewLayout::Split, false});
          viewport->frame_uv_document();
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({80, 510, 220, 0});
          break;
        case 2:
          remove();
          break;
        case 3:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 4:
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 5:
          add();
          break;
        case 6:
          remove();
          break;
        case 7:
          save_document(directory + "/removed-channel.blend");
          break;
        case 8:
          open_document(directory + "/removed-channel.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 9:
          add();
          break;
        case 10: {
          auto &set = document->set(snapshot.materials[1].session_id);
          const auto before =
              bts::TextureDocument::layer(set, set.selected_layer).ao_pixels.hash(2);
          brush.channels = {false, false, false, false, false};
          brush.ao = true;
          brush.values.ambient_occlusion = .25f;
          brush.diameter = 10;
          QApplication::processEvents();
          viewport->repaint();
          const auto start = viewport->uv_point({.7, .3});
          send_drag(*viewport,
                    start,
                    viewport->uv_point({.7, .7}) - start,
                    Qt::LeftButton,
                    Qt::NoModifier);
          auto &after = document->set(snapshot.materials[1].session_id);
          if (bts::TextureDocument::layer(after, after.selected_layer).ao_pixels.hash(2) == before)
            qFatal("Recovered AO channel must accept new paint");
          break;
        }
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 12:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 13:
          workspace->texture_sets()->setCurrentRow(1);
          break;
      }
    }
    else if (ao_demo) {
      const auto mode = [&](int value) {
        auto *control = workspace->findChild<QComboBox *>("ambientOcclusionMixing");
        control->setCurrentIndex(control->findData(value));
      };
      const auto configure = [&](double value) {
        for (int c = 0; c < 5; ++c) {
          auto *button = workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c));
          if (button->isChecked())
            button->click();
        }
        auto *ao = workspace->findChild<QToolButton *>("brushChannel5");
        if (!ao->isChecked())
          ao->click();
        workspace->findChild<QDoubleSpinBox *>("materialAmbientOcclusion")->setValue(value);
        workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(10);
        workspace->findChild<QDoubleSpinBox *>("alphaHardness")->setValue(1);
      };
      const auto stroke = [&](double u) {
        const auto from = viewport->uv_point({u, .2});
        send_drag(
            *viewport, from, viewport->uv_point({u, .8}) - from, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("ao-demo"));
          workspace->texture_sets()->setCurrentRow(1);
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({80, 510, 220, 0});
          break;
        case 2:
          workspace->findChild<QAction *>("addChannel5")->trigger();
          break;
        case 3:
          mode(1);
          break;
        case 4:
          configure(1);
          stroke(.5);
          break;
        case 5:
          mode(0);
          break;
        case 6:
          configure(.5);
          stroke(.5);
          break;
        case 7:
          mode(1);
          break;
        case 8:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 9:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 10:
          update_inspection(bts::InspectionMode::AmbientOcclusion);
          break;
        case 11:
          update_inspection(bts::InspectionMode::Material);
          break;
        case 12:
          save_document(directory + "/ao.blend");
          break;
        case 13:
          open_document(directory + "/ao.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 14:
          configure(.25);
          stroke(.7);
          break;
        case 15:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 16:
          workspace->findChild<QToolButton *>("removeChannel5")->click();
          break;
        case 17:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 18:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 19:
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 20: {
          auto *context = workspace->findChild<QComboBox *>("layerChannelContext");
          context->setCurrentIndex(context->findData(5));
          break;
        }
      }
    }
    else if (height_demo) {
      const auto method = [&](int mode) {
        workspace->findChild<QComboBox *>("heightNormalMethod")->setCurrentIndex(mode);
      };
      const auto stroke = [&](QPointF from, QPointF to) {
        const auto point = viewport->uv_point(from);
        send_drag(
            *viewport, point, viewport->uv_point(to) - point, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("height-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            set.layers.resize(1);
            set.selected_layer = set.layers.front().id;
            set.selected_mask = false;
            set.bump_method = bts::BumpMethod::SmoothSobel;
          });
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({80, 510, 220, 0});
          update_inspection(bts::InspectionMode::Material);
          break;
        case 2:
          for (int c = 0; c < 5; ++c) {
            auto *button = workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c));
            if (button->isChecked() != (c == 4))
              button->click();
          }
          workspace->findChild<QDoubleSpinBox *>("materialBump")->setValue(.2);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(7);
          workspace->findChild<QDoubleSpinBox *>("alphaHardness")->setValue(1);
          break;
        case 3:
          stroke({.2, .4}, {.7, .55});
          break;
        case 4:
          method(1);
          break;
        case 5:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 6:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 7:
          update_inspection(bts::InspectionMode::Bump);
          break;
        case 8:
          update_inspection(bts::InspectionMode::Normal);
          break;
        case 9:
          update_inspection(bts::InspectionMode::Material);
          break;
        case 10:
          save_document(directory + "/height.blend");
          break;
        case 11:
          open_document(directory + "/height.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 12:
          method(0);
          break;
        case 13:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 14:
          for (int c = 0; c < 5; ++c) {
            auto *button = workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c));
            if (button->isChecked() != (c == 4))
              button->click();
          }
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(7);
          workspace->findChild<QDoubleSpinBox *>("alphaHardness")->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("materialBump")->setValue(-.2);
          stroke({.4, .7}, {.6, .8});
          break;
        case 15:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 16:
          workspace->findChild<QToolButton *>("removeChannel4")->click();
          break;
        case 17:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (normal_mix_demo) {
      const auto mode = [&](int value) {
        auto *control = workspace->findChild<QComboBox *>("normalMixing");
        control->setCurrentIndex(control->findData(value));
      };
      switch (sets_stage) {
        case 1:
          open_document(harness.value("normal-mix-demo"));
          workspace->texture_sets()->setCurrentRow(0);
          edit_material([](bts::TextureSet &set) {
            set.layers.resize(1);
            set.selected_layer = set.layers.front().id;
            set.selected_mask = false;
            set.normal_mixing = bts::NormalMixing::Combine;
          });
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({80, 510, 220, 0});
          update_inspection(bts::InspectionMode::Material);
          break;
        case 2:
          mode(1);
          break;
        case 3:
          for (int c : {0, 1, 2, 4})
            workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c))->click();
          workspace->findChild<QDoubleSpinBox *>("materialNormal1")->setValue(.6);
          workspace->findChild<QDoubleSpinBox *>("materialNormal2")->setValue(.8);
          {
            const auto point = viewport->uv_point({.3, .4});
            send_drag(*viewport,
                      point,
                      viewport->uv_point({.6, .4}) - point,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
          break;
        case 4:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 5:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          edit_material([](bts::TextureSet &set) {
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.channels = {false, false, false, true, false};
          });
          workspace->findChild<QDoubleSpinBox *>("materialNormal1")->setValue(.6);
          workspace->findChild<QDoubleSpinBox *>("materialNormal2")->setValue(.8);
          break;
        case 6:
          mode(0);
          break;
        case 7:
          mode(1);
          break;
        case 8:
          workspace->findChild<QToolButton *>("removeChannel3")->click();
          break;
        case 9:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 10:
          mode(0);
          break;
        case 11:
          update_inspection(bts::InspectionMode::Normal);
          break;
        case 12:
          update_inspection(bts::InspectionMode::Material);
          break;
        case 13:
          save_document(directory + "/normal-mix.blend");
          break;
        case 14:
          open_document(directory + "/normal-mix.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 15:
          mode(1);
          break;
        case 16:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 17:
          workspace->findChild<QDoubleSpinBox *>("materialNormal1")->setValue(.4);
          break;
        case 18:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 19:
          workspace->layer_stack()->currentItem()->setCheckState(Qt::Unchecked);
          break;
        case 20:
          workspace->layer_stack()->currentItem()->setCheckState(Qt::Checked);
          break;
      }
    }
    else if (utility_import_demo) {
      const auto create = [&](int mode) {
        bts::NewProjectDialog dialog(workspace);
        QTimer::singleShot(100, &dialog, [&, mode] {
          dialog.findChild<QLineEdit *>("meshPath")->setText(harness.value("new-mesh"));
          const auto add = [&](const QString &filename) {
            QTimer::singleShot(100, &dialog, [&, filename] {
              auto *picker = dialog.findChild<QFileDialog *>("utilityMapPicker");
              if (!picker)
                qFatal("Utility image chooser missing");
              picker->selectFile(harness.value("utility-import-demo") + "/" + filename);
              picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Open)->click();
            });
            dialog.findChild<QPushButton *>("addUtilityMaps")->click();
            QEventLoop wait;
            auto *poll = new QTimer(&wait);
            poll->setInterval(20);
            QObject::connect(poll, &QTimer::timeout, &wait, [&] {
              auto *picker = dialog.findChild<QFileDialog *>("utilityMapPicker");
              if (!picker || !picker->isVisible())
                wait.quit();
            });
            poll->start();
            wait.exec();
          };
          auto *files = dialog.findChild<QListWidget *>("projectUtilityMaps");
          if (mode == 0) {
            add("CubeSurface_normal.png");
            dialog.findChild<QPushButton *>("clearUtilityMaps")->click();
            if (files->count() != 0)
              qFatal("Clear utility maps failed");
            utility_import_evidence.append(
                QJsonObject{{"label", "cleared"}, {"count", files->count()}});
            dialog.grab().save(directory + "/utility-cleared.png");
            dialog.reject();
            return;
          }
          add(mode == 1 ? "CubeSurface_normal.png" : "CubeSurface_normal_base.png");
          add("SphereSurface_ambient_occlusion.png");
          add("WrongSet_curvature.png");
          QJsonArray paths;
          for (int i = 0; i < files->count(); ++i)
            paths.append(files->item(i)->text());
          utility_import_evidence.append(
              QJsonObject{{"label", mode == 1 ? "legacy-list" : "base-list"}, {"files", paths}});
          dialog.grab().save(directory +
                             (mode == 1 ? "/utility-legacy-list.png" : "/utility-base-list.png"));
          dialog.findChild<QPushButton *>("createProject")->click();
        });
        sets_demo.stop();
        if (dialog.exec() == QDialog::Accepted) {
          const auto options = dialog.options();
          if (!options)
            qFatal("Utility import options missing");
          create_project(*options);
        }
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          create(0);
          break;
        case 2:
          create(1);
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          break;
        case 3:
          workspace->texture_sets()->setCurrentRow(1);
          send_key(*viewport, Qt::Key_B);
          break;
        case 4:
          create(2);
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          break;
        case 5:
          workspace->texture_sets()->setCurrentRow(0);
          send_key(*viewport, Qt::Key_B);
          break;
        case 6: {
          const auto before = document->to_json(), native_before = native_document_state(host);
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("new-mesh").toStdString();
          options.utility_maps = {QFileInfo(directory + "/bad/CubeSurface_curvature.png")
                                      .absoluteFilePath()
                                      .toStdString()};
          QString error;
          try {
            create_project(options);
          }
          catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
          }
          if (error.isEmpty() || document->to_json() != before ||
              native_document_state(host) != native_before)
            qFatal("Invalid utility map replaced the live project");
          utility_import_evidence.append(QJsonObject{{"label", "rejected"},
                                                     {"error", error},
                                                     {"document_preserved", true},
                                                     {"native_preserved", true}});
          break;
        }
        case 7:
          save_document(directory + "/utility.blend");
          break;
        case 8:
          for (const auto &name : QStringList{"CubeSurface_normal.png",
                                              "CubeSurface_normal_base.png",
                                              "SphereSurface_ambient_occlusion.png",
                                              "WrongSet_curvature.png"})
            if (QFile::exists(harness.value("utility-import-demo") + "/" + name))
              qFatal("Verification-owned source copy must be removed before Open");
          open_document(directory + "/utility.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 9:
          workspace->texture_sets()->setCurrentRow(1);
          send_key(*viewport, Qt::Key_B);
          break;
        case 10:
          send_key(*viewport, Qt::Key_M);
          for (int i = 0; i < 3; ++i)
            send_key(*viewport, Qt::Key_C);
          workspace->roughness_control()->setValue(1);
          {
            const auto point = viewport->uv_point({.3, .4});
            send_drag(*viewport,
                      point,
                      viewport->uv_point({.6, .4}) - point,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
          break;
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (bake_match_demo) {
      const auto bake = [&](int mode) {
        QTimer::singleShot(100, workspace, [&, mode] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
          if (!dialog)
            qFatal("Matching dialog missing");
          auto *match = dialog->findChild<QComboBox *>("bakeMatch");
          auto *low = dialog->findChild<QLineEdit *>("bakeLowSuffix");
          auto *high = dialog->findChild<QLineEdit *>("bakeHighSuffix");
          auto *ignore = dialog->findChild<QLineEdit *>("bakeIgnoreSuffix");
          auto *files = dialog->findChild<QListWidget *>("bakeHighMeshes");
          const auto evidence = [&](const QString &label) {
            bake_match_evidence.append(
                QJsonObject{{"label", label},
                            {"match", match->currentText()},
                            {"low", low->text()},
                            {"high", high->text()},
                            {"ignore", ignore->text()},
                            {"low_enabled", low->isEnabled()},
                            {"high_enabled", high->isEnabled()},
                            {"ignore_enabled", ignore->isEnabled()},
                            {"count", dialog->findChild<QLabel *>("bakeSelectedCount")->text()}});
            dialog->findChild<QScrollArea *>()->ensureWidgetVisible(match);
            dialog->grab().save(directory + "/match-" + label + ".png");
          };
          const auto fixtures = harness.value("bake-match-demo");
          if (mode == 0) {
            evidence("defaults");
            match->setCurrentIndex(1);
            low->setText("_LOW");
            high->setText("_HIGH");
            files->addItem(fixtures + "/match_high.obj");
            dialog->findChild<QDoubleSpinBox *>("bakeFrontalDistance")->setValue(.8);
            dialog->findChild<QDoubleSpinBox *>("bakeRearDistance")->setValue(.1);
            dialog->findChild<QCheckBox *>("bakeRelativeDistances")->setChecked(false);
            auto *resolution = dialog->findChild<QComboBox *>("bakeResolution");
            resolution->setCurrentIndex(resolution->findData(128));
            dialog->findChild<QSpinBox *>("bakeDilation")->setValue(0);
            auto *maps = dialog->findChild<QListWidget *>("bakeMapTypes");
            for (int i = 0; i < 7; ++i)
              maps->item(i)->setCheckState(i == 0 || i == 2 ? Qt::Checked : Qt::Unchecked);
            evidence("uppercase");
            dialog->findChild<QPushButton *>("saveBakeSettings")->click();
            return;
          }
          if (mode == 6) {
            evidence("retained");
            dialog->reject();
            return;
          }
          if (mode == 2) {
            low->setText("_low");
            high->setText("_high");
          }
          if (mode == 3)
            match->setCurrentIndex(0);
          if (mode == 4) {
            match->setCurrentIndex(1);
            files->clear();
            files->addItem(fixtures + "/match_high_case.obj");
          }
          if (mode == 5) {
            files->clear();
            files->addItem(fixtures + "/match_high.obj");
          }
          evidence(QString("bake%1").arg(mode));
          dialog->findChild<QPushButton *>("startMeshBake")->click();
        });
        if (mode > 0 && mode < 6) {
          auto *poll = new QTimer(workspace);
          poll->setInterval(100);
          QObject::connect(poll, &QTimer::timeout, workspace, [&, poll] {
            if (auto *error = workspace->findChild<QMessageBox *>())
              qFatal("Matching projection error: %s", qPrintable(error->text()));
            auto *dialog = workspace->findChild<QDialog *>("meshBakeProgress");
            if (!dialog)
              return;
            auto *button = dialog->findChild<QPushButton *>("finishMeshBake");
            if (button->text() == "Close")
              qFatal("Matching worker failed");
            if (button->text() == "OK") {
              if (dialog->findChild<QProgressBar *>("bakeProgressBar")->maximum() != 2)
                qFatal("Matching output count must stay two");
              poll->stop();
              poll->deleteLater();
              button->click();
            }
          });
          poll->start();
        }
        sets_demo.stop();
        workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          bake(0);
          break;
        case 2:
          bake(1);
          break;
        case 3:
          bake(2);
          send_key(*viewport, Qt::Key_B);
          break;
        case 4:
          bake(3);
          break;
        case 5:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 6:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 7:
          bake(4);
          break;
        case 8:
          bake(5);
          break;
        case 9:
          save_document(directory + "/matching.blend");
          break;
        case 10:
          open_document(directory + "/matching.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 11:
          send_key(*viewport, Qt::Key_B);
          send_key(*viewport, Qt::Key_B);
          break;
        case 12:
          bake(6);
          break;
        case 13:
          send_key(*viewport, Qt::Key_M);
          for (int i = 0; i < 3; ++i)
            send_key(*viewport, Qt::Key_C);
          workspace->roughness_control()->setValue(1);
          {
            const auto point = viewport->uv_point({.3, .4});
            send_drag(*viewport,
                      point,
                      viewport->uv_point({.6, .4}) - point,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
          break;
        case 14:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (cage_demo) {
      const auto bake = [&](int mode) {
        QTimer::singleShot(100, workspace, [&, mode] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
          if (!dialog)
            qFatal("Cage dialog missing");
          auto *source = dialog->findChild<QCheckBox *>("bakeUseProjectMesh");
          auto *cage = dialog->findChild<QCheckBox *>("bakeUseCage");
          auto *file = dialog->findChild<QLineEdit *>("bakeCageFile");
          auto *front = dialog->findChild<QDoubleSpinBox *>("bakeFrontalDistance");
          auto *rear = dialog->findChild<QDoubleSpinBox *>("bakeRearDistance");
          auto *relative = dialog->findChild<QCheckBox *>("bakeRelativeDistances");
          auto *average = dialog->findChild<QCheckBox *>("bakeAverageNormals");
          auto *backface = dialog->findChild<QCheckBox *>("bakeIgnoreBackface");
          const auto evidence = [&](const QString &label) {
            cage_evidence.append(QJsonObject{
                {"label", label},
                {"cage", cage->isChecked()},
                {"file", file->text()},
                {"frontal", front->value()},
                {"rear", rear->value()},
                {"frontal_enabled", front->isEnabled()},
                {"rear_enabled", rear->isEnabled()},
                {"relative", relative->isChecked()},
                {"relative_enabled", relative->isEnabled()},
                {"average", average->isChecked()},
                {"average_enabled", average->isEnabled()},
                {"backface", backface->isChecked()},
                {"backface_enabled", backface->isEnabled()},
                {"high_count", dialog->findChild<QListWidget *>("bakeHighMeshes")->count()},
                {"bake_enabled", dialog->findChild<QPushButton *>("startMeshBake")->isEnabled()}});
            dialog->findChild<QScrollArea *>()->ensureWidgetVisible(front);
            dialog->grab().save(directory + "/cage-" + label + ".png");
          };
          const auto choose = [&](const QString &path, const char *button) {
            QTimer::singleShot(100, dialog, [&, path] {
              auto *chooser = workspace->findChild<QFileDialog *>();
              if (!chooser)
                qFatal("Native OBJ chooser missing");
              chooser->selectFile(path);
              chooser->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Open)->click();
            });
            dialog->findChild<QPushButton *>(button)->click();
          };
          const auto fixtures = harness.value("cage-demo");
          if (mode == 0) {
            evidence("defaults");
            choose(fixtures + "/cage_high.obj", "addBakeHighMesh");
            cage->setChecked(true);
            evidence("enabled-empty");
            choose(fixtures + "/cage_valid.obj", "chooseBakeCage");
            evidence("selected-file");
            cage->setChecked(false);
            front->setValue(.05);
            rear->setValue(.02);
            relative->setChecked(false);
            auto *resolution = dialog->findChild<QComboBox *>("bakeResolution");
            resolution->setCurrentIndex(resolution->findData(128));
            dialog->findChild<QSpinBox *>("bakeDilation")->setValue(0);
            auto *maps = dialog->findChild<QListWidget *>("bakeMapTypes");
            for (int i = 1; i < 7; ++i)
              maps->item(i)->setCheckState(Qt::Unchecked);
            evidence("distance-settings");
            dialog->findChild<QPushButton *>("saveBakeSettings")->click();
            return;
          }
          if (mode == 8) {
            evidence("retained");
            dialog->reject();
            return;
          }
          if (mode == 1) {
            front->setValue(.01);
            rear->setValue(.01);
          }
          if (mode == 2) {
            front->setValue(.05);
            rear->setValue(.02);
          }
          if (mode == 3)
            relative->setChecked(true);
          if (mode == 4) {
            front->setValue(.3);
            relative->setChecked(false);
          }
          if (mode == 5)
            cage->setChecked(true);
          if (mode == 6)
            file->setText(fixtures + "/cage_bad.obj");
          if (mode == 7) {
            evidence("after-error");
            file->setText(fixtures + "/cage_valid.obj");
            evidence("restored-file");
            dialog->findChild<QPushButton *>("saveBakeSettings")->click();
            return;
          }
          evidence(QString("bake%1").arg(mode));
          dialog->findChild<QPushButton *>("startMeshBake")->click();
        });
        if (mode > 0 && mode < 7) {
          auto *poll = new QTimer(workspace);
          poll->setInterval(100);
          QObject::connect(poll, &QTimer::timeout, workspace, [&, poll, mode] {
            if (auto *error = workspace->findChild<QMessageBox *>()) {
              if (mode != 6)
                qFatal("Native projection failed: %s", qPrintable(error->text()));
              cage_evidence.append(
                  QJsonObject{{"label", "mismatch-error"}, {"error", error->text()}});
              error->grab().save(directory + "/cage-mismatch.png");
              poll->stop();
              poll->deleteLater();
              error->accept();
              return;
            }
            auto *dialog = workspace->findChild<QDialog *>("meshBakeProgress");
            if (!dialog)
              return;
            auto *button = dialog->findChild<QPushButton *>("finishMeshBake");
            if (button->text() == "Close")
              qFatal("Cage bake worker failed");
            if (button->text() == "OK") {
              if (mode == 6)
                qFatal("Mismatched cage unexpectedly accepted");
              poll->stop();
              poll->deleteLater();
              button->click();
            }
          });
          poll->start();
        }
        sets_demo.stop();
        workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          bake(0);
          break;
        case 2:
          bake(1);
          break;
        case 3:
          bake(2);
          break;
        case 4:
          bake(3);
          break;
        case 5:
          bake(4);
          send_key(*viewport, Qt::Key_B);
          break;
        case 6:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 7:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          bake(5);
          break;
        case 9:
          bake(6);
          break;
        case 10:
          bake(7);
          break;
        case 11:
          save_document(directory + "/cage.blend");
          break;
        case 12:
          open_document(directory + "/cage.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 13:
          send_key(*viewport, Qt::Key_B);
          break;
        case 14:
          send_key(*viewport, Qt::Key_M);
          for (int i = 0; i < 3; ++i)
            send_key(*viewport, Qt::Key_C);
          workspace->roughness_control()->setValue(1);
          {
            const auto point = viewport->uv_point({.3, .4});
            send_drag(*viewport,
                      point,
                      viewport->uv_point({.6, .4}) - point,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
          break;
        case 15:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 16:
          bake(8);
          break;
      }
    }
    else if (bake_quality_demo) {
      const auto bake = [&](int mode) {
        QTimer::singleShot(100, workspace, [&, mode] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
          if (!dialog)
            qFatal("Quality bake dialog missing");
          auto *width = dialog->findChild<QComboBox *>("bakeResolution");
          auto *height = dialog->findChild<QComboBox *>("bakeHeight");
          auto *lock = dialog->findChild<QCheckBox *>("bakeDimensionsLocked");
          auto *aa = dialog->findChild<QComboBox *>("bakeAntialiasing");
          auto *dilation = dialog->findChild<QSpinBox *>("bakeDilation");
          auto *diffusion = dialog->findChild<QCheckBox *>("bakeDiffusion");
          const auto evidence = [&](const QString &label) {
            bake_quality_evidence.append(
                QJsonObject{{"label", label},
                            {"width", width->currentData().toInt()},
                            {"height", height->currentData().toInt()},
                            {"locked", lock->isChecked()},
                            {"height_enabled", height->isEnabled()},
                            {"dilation", dilation->value()},
                            {"diffusion", diffusion->isChecked()},
                            {"aa", aa->currentData().toInt()},
                            {"count", dialog->findChild<QLabel *>("bakeSelectedCount")->text()}});
            dialog->grab().save(directory + "/quality-" + label + ".png");
          };
          if (mode == 0) {
            evidence("defaults");
            width->setCurrentIndex(width->findData(1024));
            evidence("locked1024");
            lock->setChecked(false);
            height->setCurrentIndex(height->findData(512));
            dilation->setValue(8);
            diffusion->setChecked(true);
            aa->setCurrentIndex(aa->findData(2));
            width->setCurrentIndex(width->findData(8192));
            evidence("maximum");
            width->setCurrentIndex(width->findData(1024));
            auto *tree = dialog->findChild<QTreeWidget *>("bakeTextureSets");
            tree->topLevelItem(0)->child(1)->setCheckState(0, Qt::Unchecked);
            auto *maps = dialog->findChild<QListWidget *>("bakeMapTypes");
            for (int i = 1; i < 7; ++i)
              maps->item(i)->setCheckState(Qt::Unchecked);
            dialog->findChild<QCheckBox *>("bakeUseProjectMesh")->setChecked(true);
            evidence("configured");
            dialog->findChild<QPushButton *>("saveBakeSettings")->click();
            return;
          }
          if (mode == 8) {
            evidence("retained");
            dialog->reject();
            return;
          }
          if (mode == 2) {
            width->setCurrentIndex(width->findData(128));
            height->setCurrentIndex(height->findData(64));
            dilation->setValue(0);
            diffusion->setChecked(false);
            aa->setCurrentIndex(aa->findData(1));
          }
          if (mode >= 3 && mode <= 5)
            aa->setCurrentIndex(aa->findData(1 << (mode - 2)));
          if (mode == 6)
            dilation->setValue(8);
          if (mode == 7)
            diffusion->setChecked(true);
          evidence(QString("bake%1").arg(mode));
          dialog->findChild<QPushButton *>("startMeshBake")->click();
        });
        if (mode != 0 && mode != 8) {
          auto *poll = new QTimer(workspace);
          poll->setInterval(100);
          QObject::connect(poll, &QTimer::timeout, workspace, [&, poll] {
            auto *dialog = workspace->findChild<QDialog *>("meshBakeProgress");
            if (!dialog)
              return;
            auto *button = dialog->findChild<QPushButton *>("finishMeshBake");
            if (button->text() == "Close")
              qFatal("Quality bake failed: %s",
                     qPrintable(dialog->findChild<QLabel *>("meshBakeProgressStatus")->text()));
            if (button->text() == "OK") {
              if (dialog->findChild<QProgressBar *>("bakeProgressBar")->maximum() != 1)
                qFatal("Quality bake must schedule exactly one output");
              poll->stop();
              poll->deleteLater();
              button->click();
            }
          });
          poll->start();
        }
        sets_demo.stop();
        workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          bake(0);
          break;
        case 2:
          bake(1);
          break;
        case 3:
          send_key(*viewport, Qt::Key_B);
          viewport->frame_uv_document();
          break;
        case 4:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 5:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 6:
          save_document(directory + "/quality.blend");
          break;
        case 7:
          open_document(directory + "/quality.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 8:
          bake(2);
          break;
        case 9:
          bake(3);
          break;
        case 10:
          bake(4);
          break;
        case 11:
          bake(5);
          break;
        case 12:
          bake(6);
          break;
        case 13:
          bake(7);
          break;
        case 14:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 15:
          bake(8);
          break;
      }
    }
    else if (selective_bake_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto bake = [&](int mode) {
        QTimer::singleShot(100, workspace, [&, mode] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
          if (!dialog)
            qFatal("Selective bake dialog missing");
          auto *tree = dialog->findChild<QTreeWidget *>("bakeTextureSets");
          auto *maps = dialog->findChild<QListWidget *>("bakeMapTypes");
          auto *source = dialog->findChild<QCheckBox *>("bakeUseProjectMesh");
          auto *start = dialog->findChild<QPushButton *>("startMeshBake");
          const auto evidence = [&](const QString &label) {
            QJsonArray sets, roles;
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
              auto *item = tree->topLevelItem(i);
              QJsonArray children;
              for (int j = 0; j < item->childCount(); ++j)
                children.append(
                    QJsonObject{{"tile", item->child(j)->text(0)},
                                {"checked", item->child(j)->checkState(0) == Qt::Checked}});
              sets.append(QJsonObject{{"name", item->text(0)},
                                      {"checked", item->checkState(0) == Qt::Checked},
                                      {"tiles", children}});
            }
            for (int i = 0; i < 7; ++i)
              roles.append(maps->item(i)->checkState() == Qt::Checked);
            bake_selection_evidence.append(
                QJsonObject{{"label", label},
                            {"sets", sets},
                            {"maps", roles},
                            {"count", dialog->findChild<QLabel *>("bakeSelectedCount")->text()},
                            {"bake_enabled", start->isEnabled()}});
            dialog->grab().save(directory + "/selection-" + label + ".png");
          };
          if (mode == 0) {
            evidence("all");
            source->setChecked(true);
            auto *first = tree->topLevelItem(0);
            if (first->childCount())
              first->child(1)->setCheckState(0, Qt::Unchecked);
            else
              tree->topLevelItem(1)->setCheckState(0, Qt::Unchecked);
            evidence("one-tile");
            for (int i = 1; i < 7; ++i)
              maps->item(i)->setCheckState(Qt::Unchecked);
            evidence("normal");
            maps->item(0)->setCheckState(Qt::Unchecked);
            evidence("none");
            maps->item(0)->setCheckState(Qt::Checked);
            dialog->findChild<QComboBox *>("bakeResolution")
                ->setCurrentIndex(dialog->findChild<QComboBox *>("bakeResolution")->findData(128));
            dialog->findChild<QPushButton *>("saveBakeSettings")->click();
            return;
          }
          if (mode == 1) {
            evidence("retained");
            dialog->reject();
            return;
          }
          if (mode == 3) {
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
              auto *item = tree->topLevelItem(i);
              item->setCheckState(0, Qt::Unchecked);
              item->setCheckState(0, Qt::Checked);
            }
            for (int i = 0; i < 7; ++i)
              maps->item(i)->setCheckState(Qt::Checked);
          }
          if (mode == 4) {
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
              tree->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
            auto *first = tree->topLevelItem(0);
            if (first->childCount())
              first->child(0)->setCheckState(0, Qt::Checked);
            else
              first->setCheckState(0, Qt::Checked);
            for (int i = 0; i < 7; ++i)
              maps->item(i)->setCheckState(i == 0 ? Qt::Checked : Qt::Unchecked);
            dialog->findChild<QComboBox *>("bakeResolution")
                ->setCurrentIndex(dialog->findChild<QComboBox *>("bakeResolution")->findData(256));
          }
          evidence(mode == 2 ? "partial" : mode == 3 ? "full" : "rebake");
          start->click();
        });
        if (mode >= 2) {
          auto *poll = new QTimer(workspace);
          poll->setInterval(100);
          QObject::connect(poll, &QTimer::timeout, workspace, [&, poll, mode] {
            auto *dialog = workspace->findChild<QDialog *>("meshBakeProgress");
            if (!dialog)
              return;
            auto *button = dialog->findChild<QPushButton *>("finishMeshBake");
            if (button->text() == "Close")
              qFatal("Selective bake failed: %s",
                     qPrintable(dialog->findChild<QLabel *>("meshBakeProgressStatus")->text()));
            if (button->text() == "OK") {
              auto *tasks = dialog->findChild<QListWidget *>("bakeProgressTasks");
              QJsonArray names;
              for (int i = 0; i < tasks->count(); ++i)
                names.append(tasks->item(i)->text());
              bake_selection_evidence.append(QJsonObject{
                  {"label", QString("complete%1").arg(mode)},
                  {"tasks", names},
                  {"count", dialog->findChild<QProgressBar *>("bakeProgressBar")->maximum()}});
              dialog->grab().save(directory + QString("/complete%1.png").arg(mode));
              poll->stop();
              poll->deleteLater();
              button->click();
            }
          });
          poll->start();
        }
        sets_demo.stop();
        workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          bake(0);
          break;
        case 2:
          bake(1);
          break;
        case 3:
          bake(2);
          break;
        case 4:
          workspace->texture_sets()->setCurrentRow(0);
          send_key(*viewport, Qt::Key_B);
          break;
        case 5:
          bake(3);
          break;
        case 6:
          bake(4);
          break;
        case 7:
          undo();
          break;
        case 8:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 9:
          bake(1);
          break;
        case 10:
          save_document(directory + "/selective.blend");
          break;
        case 11:
          open_document(directory + "/selective.blend");
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 12:
          send_key(*viewport, Qt::Key_B);
          break;
        case 13:
          send_key(*viewport, Qt::Key_M);
          for (int i = 0; i < 3; ++i)
            send_key(*viewport, Qt::Key_C);
          workspace->roughness_control()->setValue(1);
          {
            const auto point = viewport->uv_point({.3, .4});
            send_drag(*viewport,
                      point,
                      viewport->uv_point({.6, .4}) - point,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
          break;
        case 14:
          undo();
          break;
      }
    }
    else if (mesh_maps_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto bake = [&](bool cancel) {
        QTimer::singleShot(100, workspace, [&, cancel] {
          auto *dialog = workspace->findChild<QDialog *>("meshBakeDialog");
          if (!dialog)
            qFatal("Bake dialog missing");
          bake_demo_defaults = QJsonObject{
              {"resolution",
               dialog->findChild<QComboBox *>("bakeResolution")->currentData().toInt()},
              {"dilation", dialog->findChild<QSpinBox *>("bakeDilation")->value()},
              {"rays", dialog->findChild<QSpinBox *>("bakeSecondaryRays")->value()},
              {"source_checked",
               dialog->findChild<QCheckBox *>("bakeUseProjectMesh")->isChecked()},
              {"selected_count", dialog->findChild<QLabel *>("bakeSelectedCount")->text()}};
          dialog->grab().save(directory + "/bake-dialog.png");
          if (cancel) {
            dialog->reject();
            return;
          }
          dialog->findChild<QCheckBox *>("bakeUseProjectMesh")->setChecked(true);
          dialog->findChild<QPushButton *>("startMeshBake")->click();
        });
        if (!cancel) {
          auto *poll = new QTimer(workspace);
          poll->setInterval(100);
          QObject::connect(poll, &QTimer::timeout, workspace, [&, poll] {
            auto *dialog = workspace->findChild<QDialog *>("meshBakeProgress");
            if (!dialog)
              return;
            auto *button = dialog->findChild<QPushButton *>("finishMeshBake");
            if (button->text() == "Close")
              qFatal("Bake failed: %s",
                     qPrintable(dialog->findChild<QLabel *>("meshBakeProgressStatus")->text()));
            if (button->text() == "OK") {
              dialog->grab().save(directory + "/bake-complete.png");
              poll->stop();
              poll->deleteLater();
              button->click();
            }
          });
          poll->start();
        }
        sets_demo.stop();
        workspace->findChild<QAction *>("bakeMeshMapsAction")->trigger();
        sets_demo.start(600);
      };
      switch (sets_stage) {
        case 1:
          bake(true);
          break;
        case 2:
          bake(false);
          workspace->layer_settings_tabs()->setCurrentIndex(1);
          workspace->inspector_splitter()->setSizes({100, 560, 140, 0});
          break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
          send_key(*viewport, Qt::Key_B);
          break;
        case 10: {
          QKeyEvent press(QEvent::KeyPress, Qt::Key_B, Qt::ShiftModifier);
          QCoreApplication::sendEvent(viewport, &press);
          QKeyEvent release(QEvent::KeyRelease, Qt::Key_B, Qt::NoModifier);
          QCoreApplication::sendEvent(viewport, &release);
          break;
        }
        case 11:
          send_key(*viewport, Qt::Key_M);
          break;
        case 12:
          undo();
          break;
        case 13:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 14:
          save_document(directory + "/mesh-maps.blend");
          break;
        case 15:
          open_document(directory + "/mesh-maps.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 16:
          send_key(*viewport, Qt::Key_B);
          break;
        case 17:
          edit_material([](bts::TextureSet &set) { set.mesh_maps[5].reset(); });
          break;
        case 18:
          undo();
          break;
        case 19: {
          send_key(*viewport, Qt::Key_M);
          for (int i = 0; i < 3; ++i)
            send_key(*viewport, Qt::Key_C);
          workspace->roughness_control()->setValue(1);
          const auto point = viewport->uv_point({.3, .4});
          send_drag(*viewport,
                    point,
                    viewport->uv_point({.6, .4}) - point,
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 20:
          undo();
          break;
      }
    }
    else if (asset_categories_demo) {
      const auto category = [&](const QString &name) {
        auto *list = workspace->findChild<QListWidget *>("shelfCategories");
        const auto found = list->findItems(name, Qt::MatchExactly);
        if (found.isEmpty())
          qFatal("Asset category missing");
        list->setCurrentItem(found.front());
      };
      const auto apply = [&](const QString &name) {
        auto *list = workspace->findChild<QListWidget *>("shelfAssets");
        const auto found = list->findItems(name, Qt::MatchExactly);
        if (found.isEmpty())
          qFatal("Preset missing");
        list->scrollToItem(found.front());
        const auto point = list->visualItemRect(found.front()).center();
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonDblClick,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          category("Alphas");
          break;
        case 2:
          workspace->findChild<QLineEdit *>("shelfSearch")->setText("missing");
          category("Procedurals");
          break;
        case 3:
          category("Brushes");
          break;
        case 4:
          apply("Basic Hard");
          break;
        case 5:
          apply("Basic Soft");
          break;
        case 6:
          category("Materials");
          break;
        case 7:
          apply("Matte Gray");
          break;
        case 8:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 9:
          QTimer::singleShot(100, workspace, [&] {
            auto *dialog = workspace->findChild<QDialog *>("resourceImportDialog");
            if (!dialog)
              qFatal("Texture import dialog missing");
            dialog->findChild<QLineEdit *>("resourceImportPath")
                ->setText(harness.value("asset-categories-demo"));
            dialog->findChild<QLineEdit *>("resourceImportName")->setText("Texture Witness");
            dialog->findChild<QComboBox *>("resourceImportUsage")->setCurrentIndex(1);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
          });
          workspace->findChild<QToolButton *>("importResources")->click();
          break;
        case 10:
          category("Textures");
          break;
        case 11:
          category("Environments");
          break;
        case 12:
          category("Particles");
          break;
        case 13:
          category("All");
          break;
        case 14:
          save_document(directory + "/asset-categories.blend");
          break;
        case 15:
          open_document(directory + "/asset-categories.blend");
          break;
      }
    }
    else if (asset_search_demo) {
      auto *query = workspace->findChild<QLineEdit *>("shelfSearch");
      switch (sets_stage) {
        case 1:
          QTimer::singleShot(100, workspace, [&] {
            auto *dialog = workspace->findChild<QDialog *>("resourceImportDialog");
            if (!dialog)
              qFatal("Search import dialog missing");
            dialog->findChild<QLineEdit *>("resourceImportPath")
                ->setText(harness.value("asset-search-demo"));
            dialog->findChild<QLineEdit *>("resourceImportName")
                ->setText("SphereSurface_Roughness");
            dialog->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(1);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
          });
          workspace->findChild<QToolButton *>("importResources")->click();
          break;
        case 2:
          query->setText("s:project u:alpha n:Sphere");
          break;
        case 3:
          query->setText("s:project u:environment n:Sphere");
          break;
        case 4:
          query->setText("s:builtin u:alpha n:Atom");
          break;
        case 5:
          query->setText("s:builtin u:alpha n:atom=");
          break;
        case 6:
          query->setText("n:\"Sign Plus\"=");
          break;
        case 7:
          query->setText("s:project p:SphereSurface_Roughness.png");
          break;
        case 8:
          workspace->findChild<QToolButton *>("resetShelfFilters")->click();
          break;
        case 9: {
          query->setText("n:missing");
          auto *categories = workspace->findChild<QListWidget *>("shelfCategories");
          categories->setCurrentRow(2);
          break;
        }
        case 10:
          save_document(directory + "/asset-search.blend");
          break;
        case 11:
          open_document(directory + "/asset-search.blend");
          query->setText("s:project u:alpha n:Sphere");
          break;
      }
    }
    else if (resources_demo) {
      const auto category = [&](const QString &name) {
        auto *list = workspace->findChild<QListWidget *>("shelfCategories");
        const auto matches = list->findItems(name, Qt::MatchExactly);
        if (matches.isEmpty())
          qFatal("Shelf category missing");
        list->setCurrentItem(matches.front());
      };
      const auto import = [&](int scope, const QString &name) {
        QTimer::singleShot(100, workspace, [&, scope, name] {
          auto *dialog = workspace->findChild<QDialog *>("resourceImportDialog");
          if (!dialog)
            qFatal("Resource dialog missing");
          dialog->findChild<QLineEdit *>("resourceImportPath")
              ->setText(harness.value("resources-demo"));
          dialog->findChild<QLineEdit *>("resourceImportName")->setText(name);
          dialog->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(scope);
          dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        workspace->findChild<QToolButton *>("importResources")->click();
      };
      switch (sets_stage) {
        case 1:
          import(1, "SphereSurface_Roughness");
          break;
        case 2:
          category("Project");
          break;
        case 3:
          workspace->findChild<QLineEdit *>("shelfSearch")->setText("missing");
          category("All");
          break;
        case 4:
          import(0, "Session Witness");
          break;
        case 5:
          import(2, "User Witness");
          break;
        case 6:
          workspace->findChild<QComboBox *>("shelfLibraryScope")->setCurrentIndex(4);
          break;
        case 7:
          QTimer::singleShot(100, workspace, [&] {
            auto *dialog = workspace->findChild<QDialog *>("shelfPreferencesDialog");
            if (!dialog)
              qFatal("Shelf preferences missing");
            dialog->grab().save(directory + "/shelf-preferences.png");
            dialog->reject();
          });
          workspace->findChild<QAction *>("shelfPreferencesAction")->trigger();
          break;
        case 8:
          save_document(directory + "/resources.blend");
          break;
        case 9:
          open_document(directory + "/resources.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
        case 10: {
          category("Project");
          auto *list = workspace->findChild<QListWidget *>("shelfAssets");
          const auto matches = list->findItems("SphereSurface_Roughness", Qt::MatchExactly);
          if (matches.isEmpty())
            qFatal("Packed project resource missing");
          const auto point = list->visualItemRect(matches.front()).center();
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonDblClick,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 11: {
          workspace->roughness_control()->setValue(1);
          const auto point = viewport->uv_point({.4, .4});
          send_drag(*viewport,
                    point,
                    viewport->uv_point({.7, .4}) - point,
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 12:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 13: {
          bts::NewProjectOptions options;
          options.absolute_mesh_path = harness.value("new-mesh").toStdString();
          create_project(options);
          break;
        }
        case 14:
          category("Project");
          break;
        case 15:
          category("All");
          workspace->findChild<QComboBox *>("shelfLibraryScope")->setCurrentIndex(2);
          break;
        case 16:
          workspace->findChild<QComboBox *>("shelfLibraryScope")->setCurrentIndex(4);
          break;
      }
    }
    else if (triplanar_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      switch (sets_stage) {
        case 1:
          workspace->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(1);
          break;
        case 2:
          workspace->findChild<QToolButton *>("fill3dDisclosure")->click();
          break;
        case 3:
          workspace->findChild<QDoubleSpinBox *>("fill3d0_0")->setValue(.2);
          break;
        case 4:
          send_key(*viewport, Qt::Key_E);
          break;
        case 5:
          send_key(*viewport, Qt::Key_W);
          break;
        case 6:
          send_key(*viewport, Qt::Key_R);
          break;
        case 7:
          workspace->findChild<QDoubleSpinBox *>("fillTriplanarHardness")->setValue(.1);
          break;
        case 8:
          undo();
          break;
        case 9:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 10:
          save_document(directory + "/triplanar.blend");
          break;
        case 11:
          open_document(directory + "/triplanar.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 12:
          workspace->findChild<QDoubleSpinBox *>("fill3d1_2")->setValue(25);
          break;
        case 13:
          undo();
          break;
        case 14:
          workspace->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(0);
          break;
        case 15:
          workspace->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(1);
          break;
        case 16:
          workspace->findChild<QToolButton *>("fillToolbarManipulator")->click();
          break;
      }
    }
    else if (fill_uv_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      switch (sets_stage) {
        case 1:
          workspace->findChild<QComboBox *>("fillBaseColorResource")->setCurrentIndex(1);
          break;
        case 2:
          workspace->findChild<QDoubleSpinBox *>("fillScaleU")->setValue(4);
          break;
        case 3:
          workspace->findChild<QDoubleSpinBox *>("fillUvRotation")->setValue(30);
          break;
        case 4:
          workspace->findChild<QDoubleSpinBox *>("fillOffset0")->setValue(.1);
          break;
        case 5:
          workspace->findChild<QToolButton *>("fillTransformDisclosure")->click();
          break;
        case 6:
          workspace->findChild<QToolButton *>("fillTransformDisclosure")->click();
          break;
        case 7:
          undo();
          break;
        case 8:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 9:
          save_document(directory + "/fill-uv.blend");
          break;
        case 10:
          open_document(directory + "/fill-uv.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::BaseColor);
          break;
        case 11:
          workspace->findChild<QDoubleSpinBox *>("fillScaleU")->setValue(2);
          break;
        case 12:
          undo();
          break;
        case 13:
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 14:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 15:
          workspace->findChild<QComboBox *>("fillBaseColorResource")->setCurrentIndex(0);
          break;
        case 16:
          undo();
          break;
      }
    }
    else if (polygon_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto mode = [&](int value) {
        workspace->findChild<QToolButton *>(QString("polygonMode0_%1").arg(value))->click();
      };
      const auto uv_click = [&](bool other = false) {
        std::size_t first = 0;
        while (first < snapshot.uv_triangles.size() &&
               snapshot.uv_triangles[first].material != std::size_t(selected_material))
          ++first;
        std::size_t chosen = first;
        if (other) {
          const std::array<std::size_t, 1> seed{first};
          const auto island = bts::polygon_fill_selection(
              snapshot, selected_material, bts::PolygonFillMode::Island, seed);
          for (std::size_t i = first + 1; i < snapshot.uv_triangles.size(); ++i)
            if (snapshot.uv_triangles[i].material == std::size_t(selected_material) &&
                std::find(island.begin(), island.end(), i) == island.end())
            {
              chosen = i;
              break;
            }
        }
        QPointF uv;
        for (const auto &corner : snapshot.uv_triangles[chosen].corners)
          uv += QPointF(corner[0], corner[1]) / 3;
        const auto point = viewport->uv_point(uv);
        send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          mode(0);
          break;
        case 2:
          uv_click();
          break;
        case 3: {
          mode(1);
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
          send_drag(*viewport,
                    {rect.left() + rect.width() * .75, rect.top() + rect.height() * .6},
                    {0, 0},
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 4:
          mode(3);
          uv_click(true);
          break;
        case 5:
          mode(2);
          uv_click();
          break;
        case 6:
          mode(3);
          send_key(*viewport, Qt::Key_X);
          uv_click();
          break;
        case 7:
          undo();
          break;
        case 8:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 9: {
          send_key(*viewport, Qt::Key_X);
          mode(1);
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene).adjusted(2, 35, -2, -2);
          send_drag(*viewport,
                    rect.topLeft(),
                    rect.bottomRight() - rect.topLeft(),
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 10:
          undo();
          break;
        case 11:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 12:
          save_document(directory + "/polygon.blend");
          break;
        case 13:
          open_document(directory + "/polygon.blend");
          workspace->texture_sets()->setCurrentRow(0);
          update_inspection(bts::InspectionMode::Mask);
          send_key(*viewport, Qt::Key_4);
          break;
        case 14:
          send_key(*viewport, Qt::Key_M);
          send_key(*viewport, Qt::Key_1);
          break;
        case 15: {
          workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->setValue(.4);
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
          send_drag(*viewport,
                    {rect.left() + rect.width() * .75, rect.top() + rect.height() * .6},
                    {0, 0},
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 16:
          undo();
          break;
      }
    }
    else if (picker_demo) {
      const auto pick = [&](QPointF uv) {
        const auto point = viewport->uv_point(uv);
        send_drag(*viewport, point, {2, 0}, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          send_key(*viewport, Qt::Key_P);
          break;
        case 2:
          pick({.8, .5});
          break;
        case 3:
          workspace->roughness_control()->setValue(.1);
          send_key(*viewport, Qt::Key_P);
          break;
        case 4:
          pick({.3, .5});
          break;
        case 5:
          send_key(*viewport, Qt::Key_P);
          pick({.55, .5});
          break;
        case 6:
          send_key(*viewport, Qt::Key_P);
          break;
        case 7: {
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
          send_drag(*viewport,
                    {rect.left() + rect.width() * .38, rect.top() + rect.height() * .56},
                    {0, 0},
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 8:
          workspace->findChild<QToolButton *>("brushChannel2")->click();
          workspace->roughness_control()->setValue(.07);
          break;
        case 9:
          send_key(*viewport, Qt::Key_P);
          pick({.3, .5});
          break;
        case 10:
          send_key(*viewport, Qt::Key_2);
          break;
        case 11:
          send_key(*viewport, Qt::Key_P);
          pick({.8, .5});
          break;
        case 12: {
          send_key(*viewport, Qt::Key_P);
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
          send_drag(*viewport,
                    {rect.left() + rect.width() * .95, rect.top() + rect.height() * .1},
                    {0, 0},
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 13:
          send_key(*viewport, Qt::Key_P);
          send_key(*viewport, Qt::Key_Escape);
          break;
      }
    }
    else if (lazy_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto stroke = [&] {
        const auto start = viewport->uv_point({.2, .2}), end = viewport->uv_point({.65, .2});
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          workspace->roughness_control()->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(6);
          send_key(*viewport, Qt::Key_D);
          break;
        case 2:
          workspace->findChild<QDoubleSpinBox *>("lazyMouseDistance")->setValue(80);
          break;
        case 3:
          stroke();
          break;
        case 4:
          undo();
          break;
        case 5:
          send_key(*viewport, Qt::Key_D);
          break;
        case 6:
          stroke();
          break;
        case 7:
          undo();
          break;
        case 8:
          send_key(*viewport, Qt::Key_D);
          workspace->findChild<QDoubleSpinBox *>("lazyMouseDistance")->setValue(8);
          break;
        case 9:
          stroke();
          break;
        case 10:
          undo();
          break;
        case 11:
          send_key(*viewport, Qt::Key_D);
          break;
      }
    }
    else if (straight_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto click = [&](QPointF uv, Qt::KeyboardModifiers modifiers) {
        const auto point = viewport->uv_point(uv);
        send_mouse_event(
            *viewport, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, modifiers);
        send_mouse_event(
            *viewport, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, modifiers);
      };
      switch (sets_stage) {
        case 1:
          workspace->roughness_control()->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(5);
          click({.2, .75}, Qt::NoModifier);
          break;
        case 2:
          click({.55, .65}, Qt::ShiftModifier);
          break;
        case 3:
          click({.55, .3}, Qt::ShiftModifier);
          break;
        case 4:
          undo();
          break;
        case 5:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 6:
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(3);
          click({.2, .9}, Qt::NoModifier);
          break;
        case 7:
          click({.55, .676}, Qt::ShiftModifier | Qt::ControlModifier);
          break;
        case 8:
          undo();
          break;
        case 9:
          undo();
          break;
        case 10:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 11:
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 12:
          click({.8, .8}, Qt::ShiftModifier);
          break;
        case 13:
          undo();
          break;
        case 14:
          save_document(directory + "/straight.blend");
          break;
        case 15:
          open_document(directory + "/straight.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
        case 16:
          workspace->roughness_control()->setValue(.9);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(3);
          click({.7, .4}, Qt::ShiftModifier);
          break;
        case 17:
          undo();
          break;
      }
    }
    else if (symmetry_demo) {
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto stamp = [&](double x, double y) {
        const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
        send_drag(*viewport,
                  {rect.left() + rect.width() * x, rect.top() + rect.height() * y},
                  {0, 0},
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      const auto center = [&] {
        std::array<double, 3> low{1e20, 1e20, 1e20}, high{-1e20, -1e20, -1e20};
        for (const auto &triangle : snapshot.uv_triangles)
          if (triangle.material == 1)
            for (const auto &point : triangle.positions)
              for (int i = 0; i < 3; ++i) {
                low[i] = std::min(low[i], double(point[i]));
                high[i] = std::max(high[i], double(point[i]));
              }
        std::array<double, 3> result;
        for (int i = 0; i < 3; ++i)
          result[i] = (low[i] + high[i]) * .5;
        return result;
      };
      switch (sets_stage) {
        case 1:
          send_key(*viewport, Qt::Key_L);
          break;
        case 2:
          workspace->findChild<QToolButton *>("symmetryManipulator")->click();
          break;
        case 3: {
          const auto handle = viewport->symmetry_handle();
          if (!handle)
            qFatal("Symmetry manipulator missing");
          send_drag(*viewport, handle->p1(), {-24, 0}, Qt::LeftButton, Qt::NoModifier);
          break;
        }
        case 4: {
          workspace->findChild<QToolButton *>("symmetryManipulator")->click();
          workspace->findChild<QComboBox *>("symmetryAxis")->setCurrentIndex(0);
          workspace->findChild<QDoubleSpinBox *>("symmetryOffset")->setValue(center()[0]);
          auto *assets = workspace->findChild<QListWidget *>("shelfAssets");
          const auto items = assets->findItems("Sign Plus", Qt::MatchExactly);
          if (items.isEmpty())
            qFatal("Symmetry alpha missing");
          assets->setCurrentItem(items.front());
          const auto point = assets->visualItemRect(items.front()).center();
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonDblClick,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          workspace->roughness_control()->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(18);
          break;
        }
        case 5:
          stamp(.38, .56);
          break;
        case 6:
          undo();
          break;
        case 7:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          send_key(*viewport, Qt::Key_L);
          break;
        case 9:
          stamp(.38, .35);
          break;
        case 10:
          undo();
          break;
        case 11: {
          workspace->findChild<QComboBox *>("symmetryMode")->setCurrentIndex(1);
          workspace->findChild<QComboBox *>("symmetryAxis")->setCurrentIndex(2);
          workspace->findChild<QSpinBox *>("symmetryCopies")->setValue(4);
          const auto c = center();
          for (int i = 0; i < 3; ++i)
            workspace->findChild<QDoubleSpinBox *>(QString("symmetryCenter%1").arg(i))
                ->setValue(c[i]);
          send_key(*viewport, Qt::Key_L);
          break;
        }
        case 12:
          stamp(.38, .35);
          break;
        case 13:
          undo();
          break;
        case 14:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 15:
          save_document(directory + "/symmetry.blend");
          break;
        case 16:
          open_document(directory + "/symmetry.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
        case 17:
          workspace->roughness_control()->setValue(.9);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(12);
          stamp(.38, .5);
          break;
        case 18:
          undo();
          break;
      }
    }
    else if (projection_demo) {
      auto *alignment = workspace->findChild<QComboBox *>("brushAlignment");
      auto *cull = workspace->findChild<QCheckBox *>("brushBackfaceCulling");
      auto *angle = workspace->findChild<QDoubleSpinBox *>("brushCullingAngle");
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      const auto stamp = [&] {
        const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
        send_drag(*viewport,
                  {rect.left() + rect.width() * .46, rect.top() + rect.height() * .37},
                  {0, 0},
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1: {
          auto *assets = workspace->findChild<QListWidget *>("shelfAssets");
          const auto items = assets->findItems("Sign Plus", Qt::MatchExactly);
          if (items.isEmpty())
            qFatal("Plus alpha missing");
          assets->setCurrentItem(items.front());
          const auto point = assets->visualItemRect(items.front()).center();
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonDblClick,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*assets->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          workspace->roughness_control()->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(30);
          workspace->findChild<QScrollArea *>("propertiesScroll")->ensureWidgetVisible(alignment);
          stamp();
          break;
        }
        case 2:
          undo();
          alignment->setCurrentIndex(0);
          stamp();
          break;
        case 3:
          undo();
          alignment->setCurrentIndex(2);
          stamp();
          break;
        case 4:
          undo();
          alignment->setCurrentIndex(3);
          stamp();
          break;
        case 5:
          undo();
          alignment->setCurrentIndex(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(60);
          stamp();
          break;
        case 6:
          undo();
          alignment->setCurrentIndex(2);
          stamp();
          break;
        case 7:
          undo();
          alignment->setCurrentIndex(0);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(30);
          angle->setValue(10);
          break;
        case 8:
          stamp();
          break;
        case 9:
          cull->setChecked(false);
          break;
        case 10:
          stamp();
          break;
        case 11:
          undo();
          break;
        case 12:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 13:
          alignment->setCurrentIndex(3);
          break;
        case 14:
          alignment->setCurrentIndex(1);
          cull->setChecked(true);
          angle->setValue(90);
          break;
        case 15:
          save_document(directory + "/projection.blend");
          break;
        case 16:
          open_document(directory + "/projection.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
      }
    }
    else if (padding_demo) {
      auto *control = workspace->findChild<QComboBox *>("uvPaddingMode");
      const auto inspect = [&](int channel) {
        send_key(*viewport, Qt::Key_M);
        for (int c = 0; c <= channel; ++c)
          send_key(*viewport, Qt::Key_C);
      };
      switch (sets_stage) {
        case 1:
          control->setCurrentIndex(0);
          break;
        case 2:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 3:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 4:
          inspect(3);
          break;
        case 5:
          control->setCurrentIndex(1);
          break;
        case 6:
          inspect(2);
          break;
        case 7:
          save_document(directory + "/padding.blend");
          break;
        case 8:
          open_document(directory + "/padding.blend");
          workspace->texture_sets()->setCurrentRow(1);
          inspect(2);
          viewport->focus_uv({.45, .5}, .06);
          break;
        case 9:
          control->setCurrentIndex(0);
          break;
        case 10:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 11:
          workspace->texture_sets()->setCurrentRow(1);
          break;
      }
    }
    else if (alpha_demo) {
      const auto shelf = [&](const QString &name) {
        auto *categories = workspace->findChild<QListWidget *>("shelfCategories");
        categories->setCurrentRow(2);
        auto *assets = workspace->findChild<QListWidget *>("shelfAssets");
        const auto items = assets->findItems(name, Qt::MatchExactly);
        if (items.isEmpty())
          qFatal("Alpha shelf resource missing");
        assets->setCurrentItem(items.front());
        const auto point = assets->visualItemRect(items.front()).center();
        send_mouse_event(*assets->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*assets->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        send_mouse_event(*assets->viewport(),
                         QEvent::MouseButtonDblClick,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        send_mouse_event(*assets->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
      };
      const auto stamp = [&](QPointF uv) {
        const auto point = viewport->uv_point(uv);
        send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          shelf("Sign Plus");
          workspace->roughness_control()->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(30);
          workspace->findChild<QScrollArea *>("propertiesScroll")
              ->ensureWidgetVisible(workspace->findChild<QWidget *>("alphaPanel"));
          break;
        case 2: {
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
          send_drag(*viewport,
                    {rect.left() + rect.width() * .35, rect.top() + rect.height() * .35},
                    {0, 0},
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 3:
          workspace->findChild<QCheckBox *>("alphaInvert")->setChecked(true);
          break;
        case 4:
          stamp({.78, .6});
          break;
        case 5:
          workspace->findChild<QPushButton *>("clearBrushAlpha")->click();
          break;
        case 6:
          workspace->findChild<QDoubleSpinBox *>("alphaUniform")->setValue(.25);
          stamp({.8, .2});
          break;
        case 7: {
          QTimer::singleShot(100, workspace, [&] {
            auto *dialog = window->findChild<QFileDialog *>("alphaImportDialog");
            if (!dialog)
              qFatal("Alpha import dialog missing");
            dialog->selectFile(harness.value("alpha-demo"));
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
          });
          workspace->findChild<QPushButton *>("importBrushAlpha")->click();
          break;
        }
        case 8:
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(40);
          stamp({.25, .2});
          break;
        case 9:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 10:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 11:
          send_key(*viewport, Qt::Key_2);
          shelf("Sign Plus");
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(20);
          break;
        case 12:
          stamp({.25, .2});
          break;
        case 13:
          save_document(directory + "/alpha.blend");
          break;
        case 14:
          open_document(directory + "/alpha.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
      }
    }
    else if (erase_demo) {
      const auto stroke = [&](QPointF start, QPointF end) {
        send_drag(*viewport,
                  viewport->uv_point(start),
                  viewport->uv_point(end) - viewport->uv_point(start),
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      const auto undo = [&] { workspace->findChild<QAction *>("undoDocument")->trigger(); };
      switch (sets_stage) {
        case 1:
          workspace->roughness_control()->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(15);
          for (double y : {.4, .5, .6})
            stroke({.15, y}, {.8, y});
          stroke({.7, .25}, {.7, .25});
          break;
        case 2:
          send_key(*viewport, Qt::Key_2);
          break;
        case 3: {
          const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
          send_drag(*viewport,
                    {rect.left() + rect.width() * .38, rect.top() + rect.height() * .2},
                    {0, rect.height() * .6},
                    Qt::LeftButton,
                    Qt::NoModifier);
          break;
        }
        case 4:
          undo();
          break;
        case 5:
          stroke({.4, .25}, {.4, .7});
          break;
        case 6:
          undo();
          break;
        case 7:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          workspace->findChild<QToolButton *>("brushChannel2")->click();
          break;
        case 9:
          stroke({.6, .25}, {.6, .7});
          break;
        case 10: {
          const auto start = viewport->uv_point({.25, .25}), end = viewport->uv_point({.25, .7});
          send_mouse_event(*viewport,
                           QEvent::MouseButtonPress,
                           start,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(
              *viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
          send_key(*viewport, Qt::Key_Escape);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonRelease,
                           end,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 11:
          send_key(*viewport, Qt::Key_1);
          break;
        case 12:
          send_key(*viewport, Qt::Key_2);
          break;
        case 13:
          save_document(directory + "/erased.blend");
          break;
        case 14:
          open_document(directory + "/erased.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
        case 15:
          send_key(*viewport, Qt::Key_2);
          stroke({.25, .25}, {.25, .7});
          break;
      }
    }
    else if (mask_demo) {
      const auto target = [&](bool mask, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        auto *list = workspace->layer_stack();
        const auto index = list->currentIndex();
        const auto rect = list->visualRect(index);
        const QPointF point = mask ? bts::layer_mask_rect(index, rect).center() :
                                     QPoint(bts::layer_thumbnail_x(index, rect) + 12,
                                            rect.center().y());
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         modifiers);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         modifiers);
      };
      const auto uv_stroke = [&] {
        const auto start = viewport->uv_point({.25, .3});
        send_drag(*viewport,
                  start,
                  viewport->uv_point({.65, .3}) - start,
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      if (invert_demo) {
        const auto invert = [&] {
          auto *list = workspace->layer_stack();
          const auto index = list->currentIndex();
          const auto point = bts::layer_mask_rect(index, list->visualRect(index)).center();
          QContextMenuEvent event(
              QContextMenuEvent::Mouse, point, list->viewport()->mapToGlobal(point));
          QApplication::sendEvent(list->viewport(), &event);
          auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
          if (!menu)
            qFatal("Mask context menu missing");
          auto *action = menu->findChild<QAction *>("invertMaskBackground");
          if (!action)
            qFatal("Mask inversion action missing");
          action->trigger();
          menu->close();
        };
        switch (sets_stage) {
          case 1:
            workspace->findChild<QAction *>("addBlackMask")->trigger();
            workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->setValue(.25);
            workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(25);
            uv_stroke();
            target(true, Qt::AltModifier);
            break;
          case 2:
            invert();
            break;
          case 3:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 4:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 5:
            invert();
            break;
          case 6:
            save_document(directory + "/inverted.blend");
            break;
          case 7:
            open_document(directory + "/inverted.blend");
            workspace->texture_sets()->setCurrentRow(1);
            target(true, Qt::AltModifier);
            break;
          case 8:
            invert();
            break;
        }
      }
      else if (isolate_demo) {
        switch (sets_stage) {
          case 1:
            workspace->findChild<QAction *>("addBlackMask")->trigger();
            workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(25);
            uv_stroke();
            break;
          case 2:
            target(true, Qt::AltModifier);
            break;
          case 3:
            target(true, Qt::AltModifier);
            break;
          case 4:
            workspace->findChild<QDoubleSpinBox *>("environmentExposure")->setValue(2);
            break;
          case 5:
            workspace->display_transform_control()->setCurrentText("Standard");
            break;
          case 6:
            workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->setValue(0);
            {
              const auto start = viewport->uv_point({.4, .2});
              send_drag(*viewport,
                        start,
                        viewport->uv_point({.4, .4}) - start,
                        Qt::LeftButton,
                        Qt::NoModifier);
            }
            break;
          case 7:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 8:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 9:
            target(true, Qt::ShiftModifier);
            break;
          case 10:
            target(true, Qt::ShiftModifier);
            break;
          case 11:
            send_key(*viewport, Qt::Key_M);
            break;
          case 12:
            target(true, Qt::AltModifier);
            save_document(directory + "/isolated.blend");
            send_key(*viewport, Qt::Key_M);
            break;
          case 13:
            open_document(directory + "/isolated.blend");
            workspace->texture_sets()->setCurrentRow(1);
            break;
          case 14:
            target(true, Qt::AltModifier);
            break;
        }
      }
      else if (bitmap_demo) {
        const auto choose = [&](int attempt) {
          QTimer::singleShot(100, workspace, [&, attempt] {
            auto *dialog = window->findChild<QDialog *>("bitmapMaskDialog");
            if (!dialog)
              qFatal("Bitmap dialog missing");
            auto *buttons = dialog->findChild<QDialogButtonBox *>("bitmapMaskButtons");
            if (attempt == 0) {
              buttons->button(QDialogButtonBox::Cancel)->click();
              return;
            }
            dialog->findChild<QLineEdit *>("bitmapMaskPath")
                ->setText(attempt == 1 ? directory + "/missing.png" :
                                         harness.value("bitmap-mask-demo"));
            buttons->button(QDialogButtonBox::Ok)->click();
            if (attempt == 1) {
              if (dialog->findChild<QLabel *>("bitmapMaskError")->text().isEmpty())
                qFatal("Invalid bitmap accepted");
              buttons->button(QDialogButtonBox::Cancel)->click();
            }
          });
          workspace->findChild<QAction *>("addBitmapMask")->trigger();
        };
        switch (sets_stage) {
          case 1:
            choose(0);
            break;
          case 2:
            choose(1);
            break;
          case 3:
            choose(2);
            break;
          case 4:
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
            break;
          case 5:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 6:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 7:
            save_document(directory + "/bitmap.blend");
            break;
          case 8:
            open_document(directory + "/bitmap.blend");
            workspace->texture_sets()->setCurrentRow(1);
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
            break;
          case 9:
            workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(25);
            workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->setValue(0);
            uv_stroke();
            break;
          case 10:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
        }
      }
      else
        switch (sets_stage) {
          case 1:
            workspace->findChild<QAction *>("addWhiteMask")->trigger();
            break;
          case 2:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 3:
            workspace->findChild<QAction *>("addBlackMask")->trigger();
            break;
          case 4: {
            workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(25);
            const auto rect = viewport->pane_rect(bts::ViewKind::Scene);
            send_drag(*viewport,
                      {rect.left() + rect.width() * .28, rect.top() + rect.height() * .4},
                      {rect.width() * .18, rect.height() * .04},
                      Qt::LeftButton,
                      Qt::NoModifier);
            break;
          }
          case 5:
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
            break;
          case 6:
            uv_stroke();
            break;
          case 7:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 8:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 9:
            target(false);
            break;
          case 10:
            target(true);
            break;
          case 11:
            target(true, Qt::ShiftModifier);
            break;
          case 12:
            target(true, Qt::ShiftModifier);
            break;
          case 13:
            save_document(directory + "/masked.blend");
            break;
          case 14:
            open_document(directory + "/masked.blend");
            workspace->texture_sets()->setCurrentRow(1);
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
            break;
          case 15:
            workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(25);
            workspace->findChild<QDoubleSpinBox *>("maskGrayscale")->setValue(0);
            uv_stroke();
            break;
          case 16:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
        }
    }
    else if (blend_demo) {
      const auto context = [&](int channel) {
        auto *control = workspace->findChild<QComboBox *>("layerChannelContext");
        control->setCurrentIndex(control->findData(channel));
      };
      const auto blend = [&](int mode, bool row_click = false) {
        if (row_click) {
          auto *list = workspace->layer_stack();
          const auto rect = list->visualItemRect(list->item(0));
          const QPointF point(rect.right() - 20, rect.top() + 10);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
        }
        else
          workspace->findChild<QAction *>("editLayerBlend")->trigger();
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
          qFatal("Blend menu missing");
        menu->findChild<QAction *>(QString("layerBlend%1").arg(mode))->trigger();
        menu->close();
      };
      const auto inspect = [&](int channel) {
        send_key(*viewport, Qt::Key_M);
        for (int c = 0; c <= channel; ++c)
          send_key(*viewport, Qt::Key_C);
      };
      if (replace_demo)
        switch (sets_stage) {
          case 1:
            blend(1, true);
            break;
          case 2:
            context(0);
            break;
          case 3:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 4:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 5:
            save_document(directory + "/replace.blend");
            break;
          case 6:
            open_document(directory + "/replace.blend");
            workspace->texture_sets()->setCurrentRow(1);
            inspect(2);
            break;
          case 7:
            context(2);
            blend(0);
            break;
        }
      else
        switch (sets_stage) {
          case 1:
            workspace->layer_stack()->setCurrentRow(1);
            blend(2, true);
            break;
          case 2:
            workspace->layer_stack()->setCurrentRow(0);
            context(0);
            break;
          case 3:
            blend(3);
            break;
          case 4:
            context(2);
            blend(3);
            break;
          case 5:
            blend(4);
            break;
          case 6:
            inspect(1);
            break;
          case 7:
            blend(0);
            inspect(2);
            break;
          case 8:
            workspace->findChild<QAction *>("undoDocument")->trigger();
            break;
          case 9:
            workspace->findChild<QAction *>("redoDocument")->trigger();
            break;
          case 10:
            save_document(directory + "/blending.blend");
            break;
          case 11:
            open_document(directory + "/blending.blend");
            workspace->texture_sets()->setCurrentRow(1);
            inspect(2);
            break;
          case 12:
            context(0);
            workspace->display_transform_control()->setCurrentText("Standard");
            inspect(0);
            break;
        }
    }
    else if (selection_demo) {
      const auto click = [&](int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        auto *list = workspace->layer_stack();
        const auto rect = list->visualItemRect(list->item(row));
        const QPointF point(140, rect.center().y());
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonPress,
                         point,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         modifiers);
        send_mouse_event(*list->viewport(),
                         QEvent::MouseButtonRelease,
                         point,
                         Qt::LeftButton,
                         Qt::NoButton,
                         modifiers);
      };
      switch (sets_stage) {
        case 1:
          click(3, Qt::ControlModifier);
          break;
        case 2:
          send_drag(*viewport,
                    viewport->pane_rect(bts::ViewKind::Uv).center(),
                    {45, 0},
                    Qt::LeftButton,
                    Qt::NoModifier);
          click(0);
          break;
        case 3:
          click(3, Qt::ShiftModifier);
          break;
        case 4:
          click(0);
          click(3, Qt::ControlModifier);
          break;
        case 5:
          click(3, Qt::ControlModifier);
          break;
        case 6:
          click(1, Qt::ControlModifier);
          break;
        case 7:
          workspace->findChild<QAction *>("groupLayers")->trigger();
          break;
        case 8:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 9:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 10:
          save_document(directory + "/grouped.blend");
          break;
        case 11:
          open_document(directory + "/grouped.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
      }
    }
    else if (folder_demo) {
      const auto select = [&](QUuid id) {
        for (int row = 0; row < workspace->layer_stack()->count(); ++row)
          if (QUuid(workspace->layer_stack()->item(row)->data(Qt::UserRole).toString()) == id) {
            workspace->layer_stack()->setCurrentRow(row);
            return;
          }
        qFatal("Folder demo child missing");
      };
      switch (sets_stage) {
        case 1:
          workspace->findChild<QToolButton *>("addLayerFolder")->click();
          break;
        case 2:
          workspace->findChild<QToolButton *>("addPaintLayer")->click();
          break;
        case 3:
          select(folder_demo_fill);
          workspace->findChild<QAction *>("groupLayers")->trigger();
          break;
        case 4:
        case 5:
          workspace->findChild<QAction *>("toggleLayerFolder")->trigger();
          break;
        case 6:
          workspace->layer_stack()->currentItem()->setCheckState(Qt::Unchecked);
          break;
        case 7:
          workspace->layer_stack()->currentItem()->setCheckState(Qt::Checked);
          break;
        case 8: {
          workspace->findChild<QAction *>("editLayerOpacity")->trigger();
          auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
          if (!popup)
            qFatal("Folder opacity popup missing");
          popup->findChild<QDoubleSpinBox *>("layerChannelOpacity")->setValue(50);
          popup->close();
          break;
        }
        case 9:
          workspace->findChild<QAction *>("groupLayers")->trigger();
          break;
        case 10:
          save_document(directory + "/folders.blend");
          break;
        case 11:
          open_document(directory + "/folders.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
        case 12:
          select(folder_demo_fill);
          workspace->roughness_control()->setValue(.6);
          break;
        case 13:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (layer_drag_demo) {
      auto *list = static_cast<bts::LayerStackList *>(workspace->layer_stack());
      const auto point = [&](bool top) {
        return QPoint(80,
                      top ? list->visualItemRect(list->item(0)).top() + 1 :
                            list->visualItemRect(list->item(list->count() - 1)).bottom() - 1);
      };
      const auto preview = [&](bool top) {
        layer_drag_mime = list->drag_data(
            QUuid(list->currentItem()->data(Qt::UserRole).toString()));
        QDragEnterEvent enter(
            point(top), Qt::MoveAction, layer_drag_mime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &enter);
        QDragMoveEvent move(
            point(top), Qt::MoveAction, layer_drag_mime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &move);
        if (!enter.isAccepted() || !move.isAccepted())
          qFatal("Internal layer drag was rejected");
      };
      const auto drop = [&](bool top) {
        QDropEvent event(
            point(top), Qt::MoveAction, layer_drag_mime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(list->viewport(), &event);
        if (!event.isAccepted())
          qFatal("Internal layer drop was rejected");
        layer_drag_mime.reset();
      };
      switch (sets_stage) {
        case 1:
          preview(true);
          break;
        case 2:
          drop(true);
          break;
        case 3:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 4:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 5:
          preview(false);
          break;
        case 6: {
          QDragLeaveEvent leave;
          QCoreApplication::sendEvent(list->viewport(), &leave);
          layer_drag_mime.reset();
          break;
        }
        case 7:
          preview(false);
          break;
        case 8:
          drop(false);
          break;
        case 9:
          preview(false);
          drop(false);
          break;
        case 10: {
          QMimeData invalid;
          invalid.setData("application/x-bts-layer", "foreign-document");
          QDragEnterEvent event(
              point(true), Qt::MoveAction, &invalid, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(list->viewport(), &event);
          if (event.isAccepted())
            qFatal("Foreign layer payload accepted");
          break;
        }
        case 11:
          save_document(directory + "/reordered.blend");
          break;
        case 12:
          open_document(directory + "/reordered.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 3; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
      }
    }
    else if (layer_opacity_demo) {
      auto *context = workspace->findChild<QComboBox *>("layerChannelContext");
      const auto channel = [&](int c) { context->setCurrentIndex(context->findData(c)); };
      const auto opacity = [&](double value, bool row_click = false) {
        if (row_click) {
          auto *list = workspace->layer_stack();
          const auto rect = list->visualItemRect(list->currentItem());
          const QPointF point(rect.right() - 22, rect.bottom() - 10);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*list->viewport(),
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
        }
        else
          workspace->findChild<QAction *>("editLayerOpacity")->trigger();
        auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup)
          qFatal("Layer opacity popup missing");
        auto *spin = popup->findChild<QDoubleSpinBox *>("layerChannelOpacity");
        if (!spin)
          qFatal("Layer opacity field missing");
        spin->setValue(value);
        popup->grab().save(directory + "/opacity-popup.png");
        popup->close();
      };
      switch (sets_stage) {
        case 1:
          opacity(20, true);
          break;
        case 2:
          channel(0);
          break;
        case 3:
          opacity(40);
          break;
        case 4:
          channel(2);
          break;
        case 5:
          opacity(10);
          break;
        case 6:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 7:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          save_document(directory + "/opacity.blend");
          break;
        case 9:
          open_document(directory + "/opacity.blend");
          workspace->texture_sets()->setCurrentRow(1);
          for (int c = 0; c < 4; ++c)
            send_key(*viewport, Qt::Key_C);
          break;
        case 10:
          channel(4);
          opacity(0);
          break;
        case 11:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
      }
    }
    else if (layer_context_demo) {
      auto *context = workspace->findChild<QComboBox *>("layerChannelContext");
      const auto channel = [&](int c) { context->setCurrentIndex(context->findData(c)); };
      switch (sets_stage) {
        case 1:
          channel(2);
          break;
        case 2:
          break;
        case 3:
          channel(4);
          break;
        case 4:
          channel(0);
          break;
        case 5:
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          channel(2);
          break;
        case 6:
          channel(0);
          break;
        case 7:
          workspace->findChild<QToolButton *>("removeChannel0")->click();
          break;
        case 8:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 9:
          workspace->texture_sets()->setCurrentRow(1);
          break;
      }
    }
    else if (alignment_demo) {
      auto *angle = workspace->findChild<QDoubleSpinBox *>("environmentRotation");
      auto *alignment = workspace->findChild<QComboBox *>("environmentAlignment");
      switch (sets_stage) {
        case 1:
          angle->setValue(0);
          break;
        case 2:
          alignment->setCurrentIndex(1);
          break;
        case 3:
          angle->setValue(90);
          break;
        case 4:
          alignment->setCurrentIndex(0);
          break;
        case 5:
          alignment->setCurrentIndex(1);
          angle->setValue(30);
          break;
        case 6:
          save_document(directory + "/alignment.blend");
          break;
        case 7:
          open_document(directory + "/alignment.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 8:
          send_drag(*viewport,
                    viewport->pane_rect(bts::ViewKind::Scene).center(),
                    {80, 35},
                    Qt::LeftButton,
                    Qt::AltModifier);
          break;
        case 9:
          alignment->setCurrentIndex(0);
          break;
        case 10:
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          break;
        case 11:
          alignment->setCurrentIndex(1);
          break;
      }
    }
    else if (resource_demo) {
      switch (sets_stage) {
        case 1:
          change_environment_resource(harness.value("alternative-environment"));
          break;
        case 2: {
          const auto before = host.environment_state();
          bool rejected = false;
          const auto bad = directory + "/invalid.hdr";
          QFile file(bad);
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Cannot create invalid HDR fixture");
          file.write("invalid HDR bytes");
          file.close();
          try {
            change_environment_resource(bad);
          }
          catch (const std::exception &) {
            rejected = true;
          }
          if (!rejected || host.environment_state() != before)
            qFatal("Invalid environment changed the active resource");
          break;
        }
        case 3:
          save_document(directory + "/resource.blend");
          break;
        case 4:
          open_document(directory + "/resource.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 5:
          change_environment_resource(parser.value("environment"));
          break;
      }
    }
    else if (appearance_demo) {
      const auto change = [&](const char *name, double value) {
        workspace->findChild<QDoubleSpinBox *>(name)->setValue(value);
      };
      switch (sets_stage) {
        case 1:
          change("environmentExposure", 2);
          break;
        case 2:
          change("environmentExposure", 0);
          break;
        case 3:
          change("environmentOpacity", 100);
          break;
        case 4:
          change("environmentBlur", 0);
          break;
        case 5:
          change("environmentOpacity", 0);
          change("environmentBlur", 50);
          break;
        case 6:
          change("environmentExposure", 1);
          change("environmentOpacity", 40);
          change("environmentBlur", 15);
          break;
        case 7:
          save_document(directory + "/appearance.blend");
          break;
        case 8:
          open_document(directory + "/appearance.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 9:
          change("environmentOpacity", 0);
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          break;
        case 10:
          change("environmentExposure", -1);
          break;
      }
    }
    else if (environment_demo) {
      auto *rotation = workspace->findChild<QDoubleSpinBox *>("environmentRotation");
      switch (sets_stage) {
        case 1:
          rotation->setValue(0);
          break;
        case 2:
          rotation->setValue(90);
          break;
        case 3:
          rotation->setValue(30);
          break;
        case 4:
          save_document(directory + "/environment.blend");
          break;
        case 5:
          open_document(directory + "/environment.blend");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 6:
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          break;
        case 7:
          rotation->setValue(-30);
          break;
      }
    }
    else if (udim_demo) {
      const auto drag = [&](float height) {
        const auto start = viewport->scene_point({-.65f, 0, height}),
                   end = viewport->scene_point({.65f, 0, height});
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          drag(0);
          break;
        case 2:
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          break;
        case 3:
          undo_action->trigger();
          break;
        case 4:
          redo_action->trigger();
          break;
        case 5:
          save_document(directory + "/painted.blend");
          break;
        case 6:
          open_document(directory + "/painted.blend");
          viewport->surface_demo_camera();
          viewport->frame_uv_document();
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          break;
        case 7:
          workspace->findChild<QToolButton *>("brushChannel2")->click();
          send_drag(*viewport,
                    viewport->uv_point({.8, .75}),
                    viewport->uv_point({1.2, .75}) - viewport->uv_point({.8, .75}),
                    Qt::LeftButton,
                    Qt::NoModifier);
          save_document(directory + "/edited.blend");
          break;
      }
    }
    else if (surface_demo) {
      if (sets_stage == 1) {
        const auto start = viewport->scene_point({-.1f, 0, -.15f}),
                   end = viewport->scene_point({-.1f, 0, .15f});
        send_drag(*viewport, start, end - start, Qt::LeftButton, Qt::NoModifier);
      }
      else if (sets_stage == 2)
        undo_action->trigger();
      else if (sets_stage == 3)
        redo_action->trigger();
    }
    else if (pressure_demo) {
      const auto point = [&](double x = .5, double y = .5) {
        for (const auto &p : viewport->visible_panes())
          if (p.kind == bts::ViewKind::Uv)
            return QPointF(p.x + p.width * x, p.y + p.height * y) / viewport->devicePixelRatioF();
        return QPointF{};
      };
      const auto dot = [&](double pressure) {
        const auto p = point();
        send_tablet_event(*viewport, QEvent::TabletPress, p, pressure);
        send_tablet_event(*viewport, QEvent::TabletRelease, p, 0);
      };
      const auto toggle = [&](const char *name) {
        workspace->findChild<QToolButton *>(name)->click();
      };
      switch (sets_stage) {
        case 1:
          dot(.25);
          break;
        case 2:
        case 5:
        case 7:
          undo_action->trigger();
          break;
        case 3:
          dot(1);
          break;
        case 4:
          toggle("toolbarPressureSize");
          toggle("brushPressureFlow");
          break;
        case 6:
          dot(.25);
          break;
        case 8:
          dot(1);
          break;
        case 9:
          undo_action->trigger();
          toggle("brushPressureSize");
          dot(.25);
          break;
        case 10:
          send_tablet_event(*viewport, QEvent::TabletPress, point(.4, .3), .5);
          send_tablet_event(*viewport, QEvent::TabletMove, point(.6, .3), 1);
          send_key(*viewport, Qt::Key_Escape);
          send_tablet_event(*viewport, QEvent::TabletRelease, point(.6, .3), 0);
          break;
        case 11:
          workspace->findChild<QDoubleSpinBox *>("brushMinimumSize")->setValue(20);
          workspace->findChild<QDoubleSpinBox *>("brushMinimumFlow")->setValue(10);
          toggle("brushPressureSize");
          toggle("toolbarPressureFlow");
          break;
        case 12:
          undo_action->trigger();
          toggle("toolbarPressureSize");
          toggle("toolbarPressureFlow");
          send_mouse_event(*viewport,
                           QEvent::MouseButtonPress,
                           point(),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonRelease,
                           point(),
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        case 13:
          send_tablet_event(*viewport, QEvent::TabletPress, point(.3, .7), .25);
          for (int step = 1; step <= 8; ++step)
            send_tablet_event(*viewport,
                              QEvent::TabletMove,
                              point(.3 + .4 * step / 8, .7),
                              .25 + .75 * step / 8);
          send_tablet_event(*viewport, QEvent::TabletRelease, point(.7, .7), 0);
          break;
      }
    }
    else if (spacing_demo) {
      const auto stroke = [&](bts::ViewKind kind, double y, bool coarse = false) {
        for (const auto &p : viewport->visible_panes())
          if (p.kind == kind) {
            const double scale = viewport->devicePixelRatioF();
            const QPointF start((p.x + p.width * .28) / scale, (p.y + p.height * y) / scale),
                delta(p.width * .44 / scale, 0);
            if (!coarse)
              send_drag(*viewport, start, delta, Qt::LeftButton, Qt::NoModifier);
            else {
              send_mouse_event(*viewport,
                               QEvent::MouseButtonPress,
                               start,
                               Qt::LeftButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*viewport,
                               QEvent::MouseMove,
                               start + delta,
                               Qt::NoButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
              send_mouse_event(*viewport,
                               QEvent::MouseButtonRelease,
                               start + delta,
                               Qt::LeftButton,
                               Qt::NoButton,
                               Qt::NoModifier);
            }
          }
      };
      switch (sets_stage) {
        case 1:
          stroke(bts::ViewKind::Uv, .35);
          break;
        case 2:
          workspace->findChild<QDoubleSpinBox *>("brushSpacing")->setValue(150);
          break;
        case 3:
          stroke(bts::ViewKind::Uv, .65);
          break;
        case 4:
        case 6:
          undo_action->trigger();
          break;
        case 5:
          redo_action->trigger();
          break;
        case 7:
          stroke(bts::ViewKind::Uv, .65, true);
          break;
        case 8:
          stroke(bts::ViewKind::Scene, .50);
          break;
      }
    }
    else if (opacity_demo) {
      const auto stroke = [&](double y, int loops, bool cancel = false) {
        for (const auto &p : viewport->visible_panes())
          if (p.kind == bts::ViewKind::Uv) {
            const double scale = viewport->devicePixelRatioF();
            const QPointF start((p.x + p.width * .4) / scale, (p.y + p.height * y) / scale),
                end = start + QPointF(p.width * .18 / scale, 0);
            send_mouse_event(*viewport,
                             QEvent::MouseButtonPress,
                             start,
                             Qt::LeftButton,
                             Qt::LeftButton,
                             Qt::NoModifier);
            for (int repeat = 0; repeat < loops; ++repeat) {
              send_mouse_event(
                  *viewport, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
              send_mouse_event(*viewport,
                               QEvent::MouseMove,
                               start,
                               Qt::NoButton,
                               Qt::LeftButton,
                               Qt::NoModifier);
            }
            if (cancel)
              send_key(*viewport, Qt::Key_Escape);
            send_mouse_event(*viewport,
                             QEvent::MouseButtonRelease,
                             start,
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
          }
      };
      switch (sets_stage) {
        case 1:
          workspace->findChild<QDoubleSpinBox *>("brushStrokeOpacity")->setValue(20);
          stroke(.5, 4);
          break;
        case 2:
          stroke(.5, 2);
          break;
        case 3:
          workspace->findChild<QDoubleSpinBox *>("toolbarStrokeOpacity")->setValue(100);
          break;
        case 4:
          stroke(.7, 1);
          break;
        case 5:
          workspace->findChild<QDoubleSpinBox *>("brushStrokeOpacity")->setValue(0);
          break;
        case 6:
          stroke(.3, 2);
          break;
        case 7:
          undo_action->trigger();
          break;
        case 8:
          redo_action->trigger();
          break;
        case 9:
          workspace->findChild<QDoubleSpinBox *>("brushStrokeOpacity")->setValue(20);
          stroke(.3, 2, true);
          break;
      }
    }
    else if (flow_demo) {
      const auto drag = [&](bts::ViewKind kind, QPointF relative) {
        for (const auto &p : viewport->visible_panes())
          if (p.kind == kind) {
            const double scale = viewport->devicePixelRatioF();
            send_drag(*viewport,
                      QPointF(p.x + p.width * relative.x(), p.y + p.height * relative.y()) / scale,
                      QPointF(p.width * .18, p.height * .02) / scale,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
      };
      switch (sets_stage) {
        case 1:
          workspace->findChild<QDoubleSpinBox *>("brushFlow")->setValue(20);
          drag(bts::ViewKind::Scene, {.28, .35});
          break;
        case 2:
          workspace->findChild<QDoubleSpinBox *>("toolbarBrushFlow")->setValue(100);
          drag(bts::ViewKind::Scene, {.28, .50});
          break;
        case 3:
          workspace->findChild<QDoubleSpinBox *>("brushFlow")->setValue(0);
          break;
        case 4:
          drag(bts::ViewKind::Scene, {.28, .65});
          break;
        case 5:
          workspace->findChild<QDoubleSpinBox *>("brushFlow")->setValue(20);
          drag(bts::ViewKind::Scene, {.28, .35});
          break;
        case 6:
          undo_action->trigger();
          break;
        case 7:
          redo_action->trigger();
          break;
        case 8:
          drag(bts::ViewKind::Uv, {.4, .65});
          break;
      }
    }
    else if (paint_demo) {
      const auto pane = [&](bts::ViewKind kind) {
        for (const auto &p : viewport->visible_panes())
          if (p.kind == kind)
            return p;
        throw std::runtime_error("Paint demo pane missing");
      };
      const auto drag = [&](bts::ViewKind kind, QPointF relative, QPointF delta) {
        const auto p = pane(kind);
        const double scale = viewport->devicePixelRatioF();
        send_drag(*viewport,
                  QPointF(p.x + p.width * relative.x(), p.y + p.height * relative.y()) / scale,
                  QPointF(p.width * delta.x(), p.height * delta.y()) / scale,
                  Qt::LeftButton,
                  Qt::NoModifier);
      };
      switch (sets_stage) {
        case 1:
          workspace->roughness_control()->setValue(1);
          drag(bts::ViewKind::Scene, {.28, .40}, {.18, .04});
          break;
        case 2:
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(30);
          drag(bts::ViewKind::Scene, {.28, .56}, {.18, .04});
          break;
        case 3:
          workspace->findChild<QToolButton *>("brushChannel2")->click();
          workspace->metallic_control()->setValue(1);
          workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(10);
          break;
        case 4:
          drag(bts::ViewKind::Scene, {.28, .34}, {.18, .02});
          break;
        case 5:
          send_key(*viewport, Qt::Key_M);
          send_key(*viewport, Qt::Key_C);
          send_key(*viewport, Qt::Key_C);
          break;
        case 6:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 7:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          drag(bts::ViewKind::Uv, {.4, .45}, {.15, 0});
          break;
        case 9: {
          workspace->findChild<QAction *>("redoDocument")->trigger();
          const auto p = pane(bts::ViewKind::Uv);
          const QPointF point((p.x + p.width * .4) / viewport->devicePixelRatioF(),
                              (p.y + p.height * .7) / viewport->devicePixelRatioF());
          send_mouse_event(*viewport,
                           QEvent::MouseButtonPress,
                           point,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
          send_key(*viewport, Qt::Key_Escape);
          send_mouse_event(*viewport,
                           QEvent::MouseButtonRelease,
                           point,
                           Qt::LeftButton,
                           Qt::NoButton,
                           Qt::NoModifier);
          break;
        }
        case 10:
          workspace->layer_stack()->item(0)->setCheckState(Qt::Unchecked);
          break;
        case 11:
          workspace->layer_stack()->item(0)->setCheckState(Qt::Checked);
          break;
        case 12:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 13:
          workspace->texture_sets()->setCurrentRow(1);
          send_key(*viewport, Qt::Key_M);
          break;
        case 14: {
          save_document(directory + "/painted.blend");
          QFile saved(directory + "/painted.blend");
          if (!saved.open(QIODevice::ReadOnly))
            qFatal("Saved project missing");
          const auto original = saved.readAll();
          saved.close();
          const auto before = document->to_json();
          bool rejected = false;
          try {
            save_document(directory + "/nonexistent-parent/fail.blend");
          }
          catch (const std::exception &) {
            rejected = true;
          }
          if (!saved.open(QIODevice::ReadOnly) || !rejected || saved.readAll() != original ||
              document->to_json() != before)
            qFatal("Failed Save damaged the current project");
          std::puts("BTS_FAILED_SAVE_PRESERVED");
          break;
        }
        case 15: {
          const auto before = document->to_json();
          try {
            open_document(directory + "/missing.blend");
            qFatal("Missing project accepted");
          }
          catch (const std::exception &) {
          }
          if (document->to_json() != before)
            qFatal("Invalid Open changed the document");
          open_document(directory + "/painted.blend");
          if (undo_action->isEnabled() || redo_action->isEnabled() || brush.diameter != 10)
            qFatal("Open retained session history or brush size");
          workspace->texture_sets()->setCurrentRow(1);
          break;
        }
        case 16:
          drag(bts::ViewKind::Uv, {.4, .62}, {.15, 0});
          save_document(directory + "/edited.blend");
          break;
      }
    }
    else if (inspection_demo) {
      switch (sets_stage) {
        case 1:
        case 2:
        case 3:
        case 8:
        case 10:
          send_key(*viewport, Qt::Key_C);
          break;
        case 4:
          workspace->display_transform_control()->setCurrentText("Standard");
          break;
        case 5:
          workspace->roughness_control()->setValue(.8);
          break;
        case 6:
          workspace->layer_stack()->item(0)->setCheckState(Qt::Unchecked);
          break;
        case 7:
          workspace->layer_stack()->item(0)->setCheckState(Qt::Checked);
          break;
        case 9:
          workspace->display_transform_control()->setCurrentText("ACES 1.3");
          break;
        case 11:
          send_key(*viewport, Qt::Key_M);
          break;
      }
    }
    else if (fill_demo) {
      switch (sets_stage) {
        case 1:
        case 3:
          workspace->findChild<QToolButton *>("addFillLayer")->click();
          break;
        case 2:
          workspace->roughness_control()->setValue(.8);
          break;
        case 4:
          workspace->roughness_control()->setValue(.2);
          break;
        case 5:
          workspace->layer_stack()->setCurrentRow(1);
          break;
        case 6:
          workspace->layer_stack()->item(0)->setCheckState(Qt::Unchecked);
          break;
        case 7:
          workspace->layer_stack()->item(0)->setCheckState(Qt::Checked);
          break;
        case 8:
          workspace->findChild<QAction *>("moveLayerUp")->trigger();
          break;
        case 9:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 10:
          workspace->texture_sets()->setCurrentRow(0);
          break;
      }
    }
    else if (color_demo) {
      workspace->display_transform_control()->setCurrentText(sets_stage == 1 ? "Standard" :
                                                                               "ACES 1.3");
    }
    else if (channels_demo) {
      switch (sets_stage) {
        case 1:
          workspace->metallic_control()->setValue(.8);
          break;
        case 2:
          workspace->findChild<QToolButton *>("removeChannel1")->click();
          break;
        case 3:
          workspace->findChild<QAction *>("addChannel1")->trigger();
          break;
        case 4:
          workspace->findChild<QToolButton *>("removeChannel2")->click();
          break;
        case 5:
        case 7:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 6:
        case 9:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 8:
          workspace->metallic_control()->setValue(.2);
          break;
        case 10:
          workspace->texture_sets()->setCurrentRow(1);
          break;
        case 11:
          workspace->texture_sets()->setCurrentRow(0);
          break;
        case 12:
          workspace->findChild<QAction *>("addScatteringChannel")->trigger();
          break;
        case 13:
          workspace->findChild<QComboBox *>("userChannelPrecision0")
              ->setCurrentIndex(int(bts::ChannelPrecision::L16F));
          break;
        case 14:
          workspace->findChild<QAction *>("undoDocument")->trigger();
          break;
        case 15:
          workspace->findChild<QAction *>("redoDocument")->trigger();
          break;
        case 16:
          workspace->findChild<QToolButton *>("removeUserChannel0")->click();
          break;
        case 17:
          workspace->findChild<QAction *>("addScatteringChannel")->trigger();
          break;
      }
    }
    else {
      if (sets_stage == 1)
        workspace->roughness_control()->setValue(0.25);
      else if (sets_stage == 2)
        workspace->texture_sets()->setCurrentRow(1);
      else if (sets_stage == 3)
        workspace->roughness_control()->setValue(0.75);
      else if (sets_stage == 4)
        workspace->texture_sets()->setCurrentRow(0);
    }
    sets_after_frame = viewport->frame_count + 3;
  }
  catch (const std::exception &error) {
    qFatal("BTS_FEATURE_DEMO_FAILED: %s", error.what());
  }
});
