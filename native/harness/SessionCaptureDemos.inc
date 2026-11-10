// SPDX-License-Identifier: GPL-3.0-or-later
// Harness: the new-project, open, capture, layout and navigation demo sequences. Part of
// WorkspaceSession::run() (app/WorkspaceSession.inc), included in order inside its body; it shares
// that function's locals and is not a standalone file.
QTimer new_demo;
int new_demo_after_frame = 3;
bool new_created = false;
QJsonObject new_before;
bool invalid_import_preserved = false;
if (harness.is_set("new-mesh")) {
  QObject::connect(&new_demo, &QTimer::timeout, window.get(), [&] {
    if (viewport->frame_count < new_demo_after_frame)
      return;
    try {
      if (!new_created) {
        new_before = native_document_state(host);
        if (harness.is_set("new-demo-dir")) {
          try {
            host.new_project("/nonexistent-bts-fixture.obj",
                             parser.value("environment").toStdString());
            qFatal("Invalid OBJ unexpectedly succeeded");
          }
          catch (const std::exception &) {
            invalid_import_preserved = new_before == native_document_state(host);
          }
        }
        bts::NewProjectOptions options;
        options.absolute_mesh_path =
            QFileInfo(harness.value("new-mesh")).absoluteFilePath().toStdString();
        if (udim_demo || harness.is_set("selective-bake-udim"))
          options.uv_workflow = bts::UvWorkflow::PreserveTiles;
        create_project(options);
        if (harness.is_set("texture-sets-demo") || channels_demo || color_demo || fill_demo ||
            inspection_demo || paint_demo || flow_demo || opacity_demo || spacing_demo ||
            pressure_demo || surface_demo || udim_demo || environment_demo || layer_context_demo ||
            layer_opacity_demo || layer_drag_demo || folder_demo || selection_demo || blend_demo ||
            mask_demo)
          send_key(*viewport, Qt::Key_F1);
        if (channels_demo)
          workspace->layer_settings_tabs()->setCurrentIndex(1);
        if (fill_demo || inspection_demo || paint_demo || flow_demo || opacity_demo ||
            spacing_demo || pressure_demo || environment_demo || layer_context_demo ||
            layer_opacity_demo || layer_drag_demo || folder_demo || selection_demo || blend_demo ||
            mask_demo)
        {
          workspace->texture_sets()->setCurrentRow(
              std::min(1, workspace->texture_sets()->count() - 1));
          send_key(*viewport, Qt::Key_F);
        }
        new_created = true;
        new_demo_after_frame = viewport->frame_count + 3;
        return;
      }
      new_demo.stop();
      if (harness.is_set("new-demo-dir")) {
        const QString directory = harness.value("new-demo-dir");
        if (!QDir().mkpath(directory))
          throw std::runtime_error("Cannot create New demo directory");
        const QImage rendered = viewport->grabFramebuffer();
        const QPixmap presented = window->screen()->grabWindow(window->winId());
        rendered.save(directory + "/new-viewport.png");
        presented.save(directory + "/new-window.png");
        const QPoint origin = viewport->mapTo(window.get(), QPoint{});
        const double scale = double(presented.width()) / window->width();
        const QJsonObject result{{"pid", app.applicationPid()},
                                 {"frames", viewport->frame_count},
                                 {"invalid_import_preserved", invalid_import_preserved},
                                 {"before", new_before},
                                 {"native", native_document_state(host)},
                                 {"document", document->to_json()},
                                 {"profile", host.project_profile()},
                                 {"view", viewport->view_state()},
                                 {"workspace", workspace_state(*workspace)},
                                 {"viewport_rect",
                                  QJsonArray{qRound(origin.x() * scale),
                                             qRound(origin.y() * scale),
                                             qRound(viewport->width() * scale),
                                             qRound(viewport->height() * scale)}}};
        QFile file(directory + "/new.json");
        if (!file.open(QIODevice::WriteOnly))
          throw std::runtime_error("Cannot write New demo result");
        file.write(QJsonDocument(result).toJson());
        if (harness.is_set("texture-sets-demo") || channels_demo || color_demo || fill_demo ||
            inspection_demo || paint_demo || flow_demo || opacity_demo || spacing_demo ||
            pressure_demo || surface_demo || udim_demo || environment_demo || layer_context_demo ||
            layer_opacity_demo || layer_drag_demo || folder_demo || selection_demo || blend_demo ||
            mask_demo)
        {
          if (harness.is_set("texture-sets-demo")) {
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            workspace->texture_sets()->setCurrentRow(1);
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            workspace->texture_sets()->setCurrentRow(0);
          }
          if ((inspection_demo || paint_demo || flow_demo || opacity_demo || spacing_demo ||
               pressure_demo || channels_demo || environment_demo || layer_context_demo ||
               layer_opacity_demo || layer_drag_demo || folder_demo || selection_demo ||
               blend_demo || mask_demo) &&
              !(paint_demo && harness.is_set("paint-no-fill")))
            workspace->findChild<QToolButton *>("addFillLayer")->click();
          if (paint_demo || flow_demo || opacity_demo || spacing_demo || pressure_demo) {
            workspace->findChild<QToolButton *>("addPaintLayer")->click();
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
            if (flow_demo || opacity_demo || spacing_demo || pressure_demo) {
              workspace->roughness_control()->setValue(1);
              for (int c : {0, 1, 3, 4})
                workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c))->click();
            }
          }
          if (fill_uv_demo) {
            workspace->texture_sets()->setCurrentRow(0);
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            edit_material([](bts::TextureSet &set) {
              bts::TextureDocument::edit_values(set).base_color = {.2, .2, .2, 1};
            });
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            edit_material([](bts::TextureSet &set) {
              auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
              layer.values.base_color = {1, 1, 1, 1};
              layer.blending[0] = bts::LayerBlend::Screen;
            });
            update_inspection(bts::InspectionMode::BaseColor);
            workspace->inspector_splitter()->setSizes({90, 200, 540, 0});
          }
          if (triplanar_demo) {
            workspace->findChild<QComboBox *>("fillBaseColorResource")->setCurrentIndex(1);
            workspace->findChild<QDoubleSpinBox *>("fillScaleU")->setValue(4);
            workspace->findChild<QDoubleSpinBox *>("fillUvRotation")->setValue(30);
            workspace->findChild<QDoubleSpinBox *>("fillOffset0")->setValue(.1);
          }
          if (polygon_demo) {
            workspace->texture_sets()->setCurrentRow(0);
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            workspace->roughness_control()->setValue(.2);
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            workspace->roughness_control()->setValue(.8);
            workspace->findChild<QAction *>("addBlackMask")->trigger();
            update_inspection(bts::InspectionMode::Mask);
            send_key(*viewport, Qt::Key_4);
          }
          if (picker_demo) {
            edit_material([](bts::TextureSet &set) {
              set.layers[1].values.base_color = {.15, .4, .7, 1};
              set.layers[1].values.metallic = .6;
              set.layers[1].values.roughness = .2;
              set.layers[1].values.normal = {.1, -.2, .97};
              set.layers[1].values.bump = .15;
            });
            brush.values.base_color = {.8, .9, .3, 1};
            brush.values.metallic = .1;
            brush.values.roughness = 1;
            brush.values.normal = {-.2, .1, .95};
            brush.values.bump = .5;
            brush.hardness = 1;
            brush.diameter = 20;
            select_material(selected_material);
            auto point = viewport->uv_point({.3, .5});
            send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
            brush.stroke_opacity = 50;
            point = viewport->uv_point({.55, .5});
            send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
            brush.stroke_opacity = 100;
            brush.values.roughness = .1;
            select_material(selected_material);
          }
          if (padding_demo) {
            edit_material([](bts::TextureSet &set) {
              set.layers[1].values.base_color = {.2, .2, .2, 1};
              set.layers[1].values.roughness = .2;
            });
            brush.values.base_color = {.8, .1, .1, 1};
            brush.values.roughness = .8;
            brush.values.normal = {1, 0, 0};
            brush.alpha.kind = bts::AlphaKind::Uniform;
            brush.diameter = 50;
            select_material(selected_material);
            const auto point = viewport->uv_point({.75, .5});
            send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
            viewport->cursor_visible_ = false;
            viewport->focus_uv({.45, .5}, .06);
            workspace->layer_settings_tabs()->setCurrentIndex(1);
          }
          if (mask_demo) {
            edit_material([](bts::TextureSet &set) {
              auto &value = bts::TextureDocument::edit_values(set);
              value.base_color = {.2, .2, .2, 1};
              value.roughness = .2;
              value.metallic = 0;
            });
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            edit_material([](bts::TextureSet &set) {
              auto &value = bts::TextureDocument::edit_values(set);
              value.base_color = {.8, .8, .8, 1};
              value.roughness = .8;
              value.metallic = 1;
            });
          }
          if (blend_demo) {
            workspace->roughness_control()->setValue(.3);
            if (replace_demo) {
              workspace->findChild<QToolButton *>("addPaintLayer")->click();
              workspace->roughness_control()->setValue(1);
              workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(25);
              workspace->findChild<QDoubleSpinBox *>("brushStrokeOpacity")->setValue(50);
              for (int c : {0, 1, 3, 4})
                workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c))->click();
              const auto point = viewport->uv_point({.5, .5});
              send_drag(*viewport, point, {0, 0}, Qt::LeftButton, Qt::NoModifier);
              viewport->cursor_visible_ = false;
            }
            else {
              workspace->findChild<QToolButton *>("addFillLayer")->click();
              workspace->roughness_control()->setValue(.5);
              workspace->metallic_control()->setValue(1);
            }
            auto *context = workspace->findChild<QComboBox *>("layerChannelContext");
            context->setCurrentIndex(context->findData(2));
            for (int c = 0; c < 3; ++c)
              send_key(*viewport, Qt::Key_C);
          }
          if (selection_demo) {
            workspace->findChild<QToolButton *>("addPaintLayer")->click();
            workspace->findChild<QAction *>("groupLayers")->trigger();
            workspace->findChild<QToolButton *>("addPaintLayer")->click();
            workspace->layer_stack()->currentItem()->setCheckState(Qt::Unchecked);
            workspace->findChild<QToolButton *>("addPaintLayer")->click();
            workspace->inspector_splitter()->setSizes({100, 350, 250, 100});
            for (int c = 0; c < 3; ++c)
              send_key(*viewport, Qt::Key_C);
          }
          if (folder_demo) {
            workspace->roughness_control()->setValue(.2);
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            workspace->roughness_control()->setValue(.8);
            folder_demo_fill = QUuid(
                workspace->layer_stack()->currentItem()->data(Qt::UserRole).toString());
            auto *context = workspace->findChild<QComboBox *>("layerChannelContext");
            context->setCurrentIndex(context->findData(2));
            for (int c = 0; c < 3; ++c)
              send_key(*viewport, Qt::Key_C);
          }
          if (layer_drag_demo) {
            for (int i = 0; i < 3; ++i) {
              if (i)
                workspace->findChild<QToolButton *>("addFillLayer")->click();
              workspace->roughness_control()->setValue(.2 + .3 * i);
            }
            workspace->layer_stack()->setCurrentRow(2);
            for (int c = 0; c < 3; ++c)
              send_key(*viewport, Qt::Key_C);
          }
          if (layer_opacity_demo) {
            edit_material([](bts::TextureSet &set) {
              auto &values = bts::TextureDocument::edit_values(set);
              values.base_color = {1, 0, 0, 1};
              values.roughness = .2;
              values.bump = .2;
            });
            workspace->findChild<QToolButton *>("addFillLayer")->click();
            edit_material([](bts::TextureSet &set) {
              auto &values = bts::TextureDocument::edit_values(set);
              values.base_color = {0, 0, 1, 1};
              values.roughness = .8;
              values.bump = 1;
            });
            auto *context = workspace->findChild<QComboBox *>("layerChannelContext");
            context->setCurrentIndex(context->findData(4));
            for (int c = 0; c < 4; ++c)
              send_key(*viewport, Qt::Key_C);
          }
          if (environment_demo) {
            if (alignment_demo) {
              host.set_environment_alignment(true);
              workspace->set_environment(host.environment_state());
            }  // Explicit World fixture; product New defaults to follow.
            workspace->roughness_control()->setValue(.25);
            workspace->metallic_control()->setValue(.6);
            auto *panel = workspace->findChild<QDialog *>("displaySettings");
            panel->show();
            panel->grab().save(directory + "/settings.png");
            panel->hide();
          }
          if (udim_demo) {
            viewport->surface_demo_camera();
            viewport->frame_uv_document();
            workspace->findChild<QDoubleSpinBox *>("brushSize")->setValue(20);
            workspace->findChild<QToolButton *>("brushChannel2")->click();
            send_key(*viewport, Qt::Key_C);
          }
          if (surface_demo) {
            viewport->surface_demo_camera();
            workspace->roughness_control()->setValue(1);
            workspace->findChild<QDoubleSpinBox *>("brushSize")
                ->setValue(volume_demo ? 50 : 37.037);
            if (volume_demo) {
              workspace->findChild<QDoubleSpinBox *>("alphaHardness")->setValue(1);
              workspace->findChild<QCheckBox *>("brushBackfaceCulling")->setChecked(false);
              for (int c : {0, 1, 3, 4})
                workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c))->click();
            }
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
            send_key(*viewport, Qt::Key_C);
          }
          sets_after_frame = viewport->frame_count + 3;
          sets_demo.start(600);
        }
        else
          QTimer::singleShot(300, &app, &QCoreApplication::quit);
      }
    }
    catch (const std::exception &error) {
      qFatal("BTS_NEW_FAILED: %s", error.what());
    }
  });
  new_demo.start(500);
}

QTimer open_demo;
int open_stage = 0, open_after_frame = 3;
QJsonArray open_captures;
if (harness.is_set("open-document")) {
  QObject::connect(&open_demo, &QTimer::timeout, window.get(), [&] {
    if (viewport->frame_count < open_after_frame)
      return;
    try {
      if (open_stage == 0) {
        if (!harness.is_set("open-demo-dir") && harness.values("reject-document").isEmpty()) {
          begin_open_document(harness.value("open-document"));
          open_demo.stop();
          return;
        }
        open_document(harness.value("open-document"));
        send_key(*viewport, Qt::Key_F1);
        for (const auto &rejected_path : harness.values("reject-document")) {
          const auto before = document->to_json(), native_before = native_document_state(host);
          bool rejected = false;
          try {
            open_document(rejected_path);
          }
          catch (const std::exception &) {
            rejected = true;
          }
          if (!rejected || document->to_json() != before ||
              native_document_state(host) != native_before)
            qFatal("Invalid project did not preserve the live document");
          std::puts("BTS_INVALID_PROJECT_PRESERVED");
        }
        ++open_stage;
        open_after_frame = viewport->frame_count + 3;
        if (!harness.is_set("open-demo-dir"))
          open_demo.stop();
        return;
      }
      const auto directory = harness.value("open-demo-dir");
      QDir().mkpath(directory);
      const QString name = open_stage == 1 ? "opened" : "edited";
      if (viewport->cursor_visible_) {
        viewport->cursor_visible_ = false;
        viewport->update();
        open_after_frame = viewport->frame_count + 1;
        return;
      }
      viewport->grabFramebuffer().save(directory + "/" + name + "-viewport.png");
      window->screen()->grabWindow(window->winId()).save(directory + "/" + name + "-window.png");
      open_captures.append(QJsonObject{{"stage", name},
                                       {"document", document->to_json()},
                                       {"profile", host.project_profile()},
                                       {"undo_enabled", undo_action->isEnabled()},
                                       {"redo_enabled", redo_action->isEnabled()},
                                       {"brush_size", brush.diameter}});
      if (open_stage == 1) {
        workspace->texture_sets()->setCurrentRow(
            std::min(1, workspace->texture_sets()->count() - 1));
        viewport->frame_uv_document();
        if (document->settings().uv_workflow == bts::UvWorkflow::PreserveTiles)
          for (int c : {1, 2, 3, 4})
            workspace->findChild<QToolButton *>(QString("brushChannel%1").arg(c))->click();
        for (const auto &pane : viewport->visible_panes())
          if (pane.kind == bts::ViewKind::Uv) {
            const double scale = viewport->devicePixelRatioF();
            send_drag(*viewport,
                      QPointF(pane.x + pane.width * .4, pane.y + pane.height * .62) / scale,
                      QPointF(pane.width * .15, 0) / scale,
                      Qt::LeftButton,
                      Qt::NoModifier);
          }
        save_document(directory + "/edited.blend");
        ++open_stage;
        open_after_frame = viewport->frame_count + 3;
      }
      else {
        QFile file(directory + "/open.json");
        if (!file.open(QIODevice::WriteOnly))
          throw std::runtime_error("Cannot write Open proof");
        file.write(
            QJsonDocument(QJsonObject{{"pid", app.applicationPid()}, {"captures", open_captures}})
                .toJson());
        open_demo.stop();
        QTimer::singleShot(300, &app, &QCoreApplication::quit);
      }
    }
    catch (const std::exception &error) {
      qFatal("BTS_OPEN_FAILED: %s", error.what());
    }
  });
  open_demo.start(500);
}

const QString demo_dir = harness.value("demo-dir");
QTimer demo;
QTimer layout_demo;
QTimer navigation_demo;
int stage = 0;
int after_frame = 3;
QJsonArray captures;
if (!demo_dir.isEmpty()) {
  if (!QDir().mkpath(demo_dir)) {
    throw std::runtime_error("Cannot create demo output directory");
  }
  QObject::connect(&demo, &QTimer::timeout, window.get(), [&] {
    if (viewport->frame_count < after_frame) {
      return;
    }
    const QStringList names{"initial", "roughness", "orbit", "resize"};
    const auto name = names.at(stage);
    const QImage rendered = viewport->grabFramebuffer();
    const QPixmap presented = window->screen()->grabWindow(window->winId());
    if (!rendered.save(demo_dir + "/" + name + "-viewport.png") ||
        !presented.save(demo_dir + "/" + name + "-window.png"))
    {
      qFatal("BTS_DEMO_CAPTURE_FAILED");
    }
    const QPoint viewport_origin = viewport->mapTo(window.get(), QPoint{});
    const double capture_scale = double(presented.width()) / window->width();
    QJsonObject capture{{"stage", name},
                        {"viewport_rect",
                         QJsonArray{qRound(viewport_origin.x() * capture_scale),
                                    qRound(viewport_origin.y() * capture_scale),
                                    qRound(viewport->width() * capture_scale),
                                    qRound(viewport->height() * capture_scale)}}};
    if (workspace) {
      capture.insert("workspace_state", workspace_state(*workspace));
    }
    captures.append(capture);
    std::printf("BTS_DEMO stage=%s pid=%lld frames=%d\n",
                name.toUtf8().constData(),
                static_cast<long long>(app.applicationPid()),
                viewport->frame_count);
    std::fflush(stdout);
    ++stage;
    after_frame = viewport->frame_count + 3;
    if (stage == 1) {
      if (workspace) {
        if (!workspace->roughness_control()->isEnabled()) {
          qFatal("BTS_DEMO_MATERIAL_UNAVAILABLE");
        }
        workspace->roughness_control()->setValue(0.8);
      }
      else {
        roughness->setValue(80);
      }
    }
    else if (stage == 2) {
      send_drag(*viewport,
                viewport->pane_rect(bts::ViewKind::Scene).center(),
                {60.0 / 0.35, 0},
                Qt::LeftButton,
                Qt::AltModifier);
    }
    else if (stage == 3) {
      if (workspace) {
        workspace->set_console_visible(false);
        workspace->layer_settings_tabs()->setCurrentIndex(1);
      }
      window->resize(use_workspace ? QSize(1600, 1000) : QSize(1320, 840));
    }
    else {
      QJsonObject result{{"pid", app.applicationPid()},
                         {"qt", qVersion()},
                         {"renderer", QString::fromStdString(host.renderer())},
                         {"frames", viewport->frame_count},
                         {"last_cpu_ms", viewport->last_ms},
                         {"workspace", use_workspace},
                         {"captures", captures},
                         {"presentation", "GPU framebuffer blit"}};
      if (workspace) {
        result.insert("workspace_state", workspace_state(*workspace));
      }
      QFile file(demo_dir + "/host.json");
      if (!file.open(QIODevice::WriteOnly)) {
        qFatal("BTS_DEMO_REPORT_FAILED");
      }
      file.write(QJsonDocument(result).toJson());
      demo.stop();
      QTimer::singleShot(500, &app, &QCoreApplication::quit);
    }
  });
  demo.start(750);
}

const QString layout_demo_dir = harness.value("layout-demo-dir");
const QStringList layout_stages{"3d-initial",
                                "split",
                                "swapped",
                                "3d-only",
                                "3d-swap-unchanged",
                                "2d-only",
                                "2d-swap-unchanged",
                                "split-restored",
                                "resize",
                                "roughness"};
int layout_stage = 0;
int layout_after_frame = 3;
bool layout_roughness_edited = false, layout_prepared = false;
QJsonArray layout_captures;
if (!layout_demo_dir.isEmpty()) {
  if (!QDir().mkpath(layout_demo_dir)) {
    throw std::runtime_error("Cannot create layout demo output directory");
  }
  QObject::connect(&layout_demo, &QTimer::timeout, window.get(), [&] {
    if (viewport->frame_count < layout_after_frame) {
      return;
    }
    if (!layout_prepared) {
      // --open-document is scheduled on startup; prepare only after it has run.
      viewport->set_layout({bts::ViewLayout::SceneOnly, false});
      viewport->setFocus(Qt::OtherFocusReason);
      if (document)
        if (auto *metal = workspace->findChild<QDoubleSpinBox *>("metallicControl"))
          metal->setValue(1);
      layout_prepared = true;
      layout_after_frame = viewport->frame_count + 96;
      return;
    }
    const QString name = layout_stages.at(layout_stage);
    const QImage rendered = viewport->grabFramebuffer();
    const QPixmap presented = window->screen()->grabWindow(window->winId());
    if (!rendered.save(layout_demo_dir + "/" + name + "-viewport.png") ||
        !presented.save(layout_demo_dir + "/" + name + "-window.png"))
    {
      qFatal("BTS_LAYOUT_CAPTURE_FAILED");
    }
    const QPoint origin = viewport->mapTo(window.get(), QPoint{});
    const double scale = double(presented.width()) / window->width();
    const QJsonArray viewport_rect{qRound(origin.x() * scale),
                                   qRound(origin.y() * scale),
                                   qRound(viewport->width() * scale),
                                   qRound(viewport->height() * scale)};
    QJsonObject view_state = viewport->view_state();
    QJsonArray pane_states;
    const double window_per_framebuffer_pixel = scale / viewport->devicePixelRatioF();
    for (const auto &value : view_state.value("panes").toArray()) {
      QJsonObject pane = value.toObject();
      const QJsonArray rect = pane.value("rect").toArray();
      pane.insert(
          "window_rect",
          QJsonArray{
              qRound(origin.x() * scale + rect.at(0).toInt() * window_per_framebuffer_pixel),
              qRound(origin.y() * scale + rect.at(1).toInt() * window_per_framebuffer_pixel),
              qRound(rect.at(2).toInt() * window_per_framebuffer_pixel),
              qRound(rect.at(3).toInt() * window_per_framebuffer_pixel)});
      pane_states.append(pane);
    }
    view_state.insert("panes", pane_states);
    layout_captures.append(QJsonObject{{"stage", name},
                                       {"viewport_rect", viewport_rect},
                                       {"view_state", view_state},
                                       {"workspace_state", workspace_state(*workspace)}});
    std::printf("BTS_LAYOUT_DEMO stage=%s pid=%lld frames=%d\n",
                name.toUtf8().constData(),
                static_cast<long long>(app.applicationPid()),
                viewport->frame_count);
    std::fflush(stdout);
    ++layout_stage;
    // Progressive EEVEE refines across frames; capture after sample convergence.
    layout_after_frame = viewport->frame_count + 96;
    int key = 0;
    switch (layout_stage) {
      case 1:
        key = Qt::Key_F1;
        break;
      case 2:
        key = Qt::Key_F4;
        break;
      case 3:
        key = Qt::Key_F2;
        break;
      case 4:
        key = Qt::Key_F4;
        break;
      case 5:
        key = Qt::Key_F3;
        break;
      case 6:
        key = Qt::Key_F4;
        break;
      case 7:
        key = Qt::Key_F1;
        break;
      case 8:
        window->resize(1660, 900);
        break;
      case 9:
        if (workspace->roughness_control()->isEnabled()) {
          workspace->roughness_control()->setValue(0.8);
          layout_roughness_edited = true;
        }
        break;
      default: {
        const QJsonObject result{
            {"pid", app.applicationPid()},
            {"qt", qVersion()},
            {"renderer", QString::fromStdString(host.renderer())},
            {"frames", viewport->frame_count},
            {"captures", layout_captures},
            {"roughness_edit_applied", layout_roughness_edited},
            {"input_delivery", "Qt key press/release events to the live viewport"},
            {"capture_surface", "Qt ARGB native window and explicit viewport framebuffer"},
            {"presentation", "One Qt OpenGL context, native EEVEE targets, GPU framebuffer blits"},
            {"single_view_f4", "No-op; product choice for behavior unobserved in the reference"}};
        QFile file(layout_demo_dir + "/layout.json");
        if (!file.open(QIODevice::WriteOnly)) {
          qFatal("BTS_LAYOUT_REPORT_FAILED");
        }
        file.write(QJsonDocument(result).toJson());
        layout_demo.stop();
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
        break;
      }
    }
    if (key) {
      const auto before = viewport->view_state();
      const auto prior = before["layout"].toString();
      const auto expected = key == Qt::Key_F1   ? (before["uv_first"].toBool() ? QString("2D/3D") :
                                                                                 QString("3D/2D")) :
                            key == Qt::Key_F2   ? QString("3D only") :
                            key == Qt::Key_F3   ? QString("2D only") :
                            prior.contains('/') ? (prior == "3D/2D" ? QString("2D/3D") :
                                                                      QString("3D/2D")) :
                                                  prior;
      QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
      QCoreApplication::sendEvent(viewport, &press);
      // QAction shortcut dispatch need not set the original key's accepted bit.
      // Assert the visible layout state instead of that implementation detail.
      const auto actual = viewport->view_state()["layout"].toString();
      if (actual != expected)
        qFatal("BTS_LAYOUT_KEY_STATE expected=%s actual=%s",
               qPrintable(expected),
               qPrintable(actual));
      QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
      QCoreApplication::sendEvent(viewport, &release);
    }
  });
  layout_demo.start(750);
}

const QString navigation_demo_dir = harness.value("navigation-demo-dir");
struct NavigationStep {
  QString name;
  QString input;
  std::function<void()> apply;
};
const auto scene_point = [&] { return viewport->pane_rect(bts::ViewKind::Scene).center(); };
const auto uv_point = [&] { return viewport->pane_rect(bts::ViewKind::Uv).center(); };
const auto frame_scene = [&] {
  send_mouse_event(
      *viewport, QEvent::MouseMove, scene_point(), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  send_key(*viewport, Qt::Key_F);
};
std::vector<NavigationStep> navigation_steps{
    {"initial", "No input", [] {}},
    {"orbit",
     "Alt + left drag",
     [&] { send_drag(*viewport, scene_point(), {90, 25}, Qt::LeftButton, Qt::AltModifier); }},
    {"snap-orbit",
     "Alt + Shift + left drag",
     [&] {
       send_drag(*viewport,
                 scene_point(),
                 {35, 20},
                 Qt::LeftButton,
                 Qt::AltModifier | Qt::ShiftModifier);
     }},
    {"pan",
     "Alt + middle drag",
     [&] { send_drag(*viewport, scene_point(), {48, -26}, Qt::MiddleButton, Qt::AltModifier); }},
    {"dolly",
     "Alt + right drag",
     [&] { send_drag(*viewport, scene_point(), {0, 40}, Qt::RightButton, Qt::AltModifier); }},
    {"wheel",
     "Positive Qt wheel angle over 3D",
     [&] { send_wheel(*viewport, scene_point(), 120); }},
    {"frame", "F over 3D", frame_scene},
    {"orthographic", "F6", [&] { send_key(*viewport, Qt::Key_F6); }},
    {"perspective", "F5", [&] { send_key(*viewport, Qt::Key_F5); }},
    {"split", "F1", [&] { send_key(*viewport, Qt::Key_F1); }},
    {"resize", "Resize workspace to 1660 by 1000", [&] { window->resize(1660, 1000); }},
    {"plain-drag",
     "Unmodified left drag",
     [&] { send_drag(*viewport, scene_point(), {90, 30}, Qt::LeftButton, Qt::NoModifier); }},
    {"uv-pan",
     "Alt + middle drag over 2D",
     [&] { send_drag(*viewport, uv_point(), {40, -25}, Qt::MiddleButton, Qt::AltModifier); }},
    {"uv-wheel",
     "Negative Qt wheel angle over 2D",
     [&] { send_wheel(*viewport, uv_point(), -120); }},
    {"pane-capture",
     "3D Alt + middle drag continues across the 2D pane",
     [&] {
       send_drag(*viewport,
                 scene_point(),
                 uv_point() - scene_point(),
                 Qt::MiddleButton,
                 Qt::AltModifier);
     }},
    {"gesture-capture",
     "Alt + left press; move and release after Alt is released",
     [&] {
       send_drag(
           *viewport, scene_point(), {45, -10}, Qt::LeftButton, Qt::AltModifier, Qt::NoModifier);
     }},
    {"plain-then-alt",
     "Unmodified left press; move after Alt is pressed",
     [&] {
       send_drag(
           *viewport, scene_point(), {45, -10}, Qt::LeftButton, Qt::NoModifier, Qt::AltModifier);
     }},
    {"released-gesture",
     "Mouse move with a button flag but no initiating press",
     [&] {
       send_mouse_event(*viewport,
                        QEvent::MouseMove,
                        scene_point() + QPointF(60, 20),
                        Qt::NoButton,
                        Qt::LeftButton,
                        Qt::AltModifier);
     }},
    {"frame-split", "F uses the current 3D pane aspect", frame_scene},
    {"projection-selector",
     "Select Orthographic view in the toolbar",
     [&] { workspace->projection_control()->setCurrentIndex(1); }},
};
const bool trackball_demo = qEnvironmentVariableIsSet("BTS_TRACKBALL_DEMO");
int trackball_pointer_clicks = 0;
if (trackball_demo) {
  const auto toggle = [&] {
    auto *control = workspace->findChild<QCheckBox *>("trackballNavigation");
    if (!control || !control->isVisible() || !control->isEnabled() ||
        control->visibleRegion().isEmpty())
      qFatal("Trackball checkbox is not accessible");
    const bool before = control->isChecked();
    control->setFocus(Qt::MouseFocusReason);
    if (!control->hasFocus())
      qFatal("Trackball checkbox did not acquire keyboard focus");
    const QPointF point = control->rect().center();
    send_mouse_event(
        *control, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    send_mouse_event(
        *control, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    send_mouse_event(
        *control, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    if (control->isChecked() == before || viewport->trackball_enabled() != control->isChecked())
      qFatal("Trackball pointer click did not reach the live navigation callback");
    ++trackball_pointer_clicks;
  };
  if (qEnvironmentVariableIsSet("BTS_TRACKBALL_RELOAD"))
    navigation_steps = {{"initial", "Reload persisted Trackball preference", [] {}},
                        {"disabled", "Click Trackball off", toggle}};
  else
    navigation_steps = {
        {"initial", "Absent preference preserves turntable", [] {}},
        {"enabled", "Click real Trackball checkbox", toggle},
        {"trackball",
         "Off-center Alt left drag",
         [&] {
           const auto r = viewport->pane_rect(bts::ViewKind::Scene);
           send_drag(*viewport,
                     r.center() + QPointF(-r.width() * .23, r.height() * .2),
                     {90, -65},
                     Qt::LeftButton,
                     Qt::AltModifier);
         }},
        {"released-idle", "Twelve ordinary frames without input after release", [] {}},
        {"noop-layout-drag",
         "F4 no-op preserves captured SceneOnly trackball drag",
         [&] {
           if (viewport->view_layout().mode != bts::ViewLayout::SceneOnly)
             qFatal("Trackball F4 no-op witness requires SceneOnly");
           const auto p = scene_point() + QPointF(-50, 60);
           send_mouse_event(*viewport,
                            QEvent::MouseButtonPress,
                            p,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           send_key(*viewport, Qt::Key_F4);
           send_mouse_event(*viewport,
                            QEvent::MouseMove,
                            p + QPointF(70, -35),
                            Qt::NoButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           send_mouse_event(*viewport,
                            QEvent::MouseButtonRelease,
                            p + QPointF(70, -35),
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::AltModifier);
         }},
        {"disabled", "Click Trackball off; preserve rolled view", toggle},
        {"turntable",
         "Legacy Alt left drag after free roll",
         [&] { send_drag(*viewport, scene_point(), {30, 20}, Qt::LeftButton, Qt::AltModifier); }},
        {"enabled-again", "Click Trackball on; preserve view", toggle},
        {"snap",
         "Shift retains existing 15-degree turntable snap",
         [&] {
           send_drag(*viewport,
                     scene_point(),
                     {20, 15},
                     Qt::LeftButton,
                     Qt::AltModifier | Qt::ShiftModifier);
         }},
        {"split", "Switch to split panes", [&] { send_key(*viewport, Qt::Key_F1); }},
        {"layout-cancel",
         "A layout change cancels captured trackball",
         [&] {
           const auto p = scene_point();
           send_mouse_event(*viewport,
                            QEvent::MouseButtonPress,
                            p,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           send_key(*viewport, Qt::Key_F4);
           send_mouse_event(*viewport,
                            QEvent::MouseMove,
                            p + QPointF(70, -35),
                            Qt::NoButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           send_mouse_event(*viewport,
                            QEvent::MouseButtonRelease,
                            p + QPointF(70, -35),
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::AltModifier);
         }},
        {"resize-cancel",
         "A pane size change cancels captured trackball",
         [&] {
           const auto p = scene_point();
           send_mouse_event(*viewport,
                            QEvent::MouseButtonPress,
                            p,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           window->resize(window->width() + 24, window->height() + 16);
           QCoreApplication::processEvents();
           send_mouse_event(*viewport,
                            QEvent::MouseMove,
                            p + QPointF(70, -35),
                            Qt::NoButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           send_mouse_event(*viewport,
                            QEvent::MouseButtonRelease,
                            p + QPointF(70, -35),
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::AltModifier);
         }},
        {"uv-orbit",
         "Alt left over UV does not orbit",
         [&] { send_drag(*viewport, uv_point(), {45, 20}, Qt::LeftButton, Qt::AltModifier); }},
        {"uv-pan",
         "Existing UV pan unaffected",
         [&] { send_drag(*viewport, uv_point(), {40, -25}, Qt::MiddleButton, Qt::AltModifier); }},
        {"focus-cancel",
         "Actual checkbox focus cancels a held viewport gesture",
         [&, toggle] {
           const auto p = scene_point() + QPointF(-50, 60);
           viewport->setFocus(Qt::MouseFocusReason);
           send_mouse_event(*viewport,
                            QEvent::MouseButtonPress,
                            p,
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           toggle();
           send_mouse_event(*viewport,
                            QEvent::MouseMove,
                            p + QPointF(70, -35),
                            Qt::NoButton,
                            Qt::LeftButton,
                            Qt::AltModifier);
           send_mouse_event(*viewport,
                            QEvent::MouseButtonRelease,
                            p + QPointF(70, -35),
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::AltModifier);
         }},
        {"next-turntable",
         "Next gesture uses the pointer-selected turntable mode",
         [&] { send_drag(*viewport, scene_point(), {30, 20}, Qt::LeftButton, Qt::AltModifier); }},
        {"persist-enabled", "Leave global preference enabled for restart", toggle}};
}
int navigation_stage = 0;
int navigation_after_frame = 3;
int navigation_before_frame = 0;
QJsonArray navigation_captures;
QJsonObject navigation_before_view;
QJsonObject navigation_before_document;
QJsonObject navigation_initial_document;
if (!navigation_demo_dir.isEmpty()) {
  if (!QDir().mkpath(navigation_demo_dir)) {
    throw std::runtime_error("Cannot create navigation demo output directory");
  }
  QObject::connect(&navigation_demo, &QTimer::timeout, window.get(), [&] {
    if (viewport->frame_count < navigation_after_frame) {
      return;
    }
    if (trackball_demo && !window->isActiveWindow()) {
      window->activateWindow();
      return;
    }
    if (trackball_demo && navigation_stage == 0) {
      const bool reload = qEnvironmentVariableIsSet("BTS_TRACKBALL_RELOAD");
      if (viewport->trackball_enabled() != reload ||
          workspace->findChild<QCheckBox *>("trackballNavigation")->isChecked() != reload)
        qFatal("Trackball startup preference mismatch");
    }
    if (trackball_demo && harness.is_set("new-mesh") && !new_created)
      return;
    const auto &step = navigation_steps.at(navigation_stage);
    const QImage rendered = viewport->grabFramebuffer();
    const QPixmap presented = window->screen()->grabWindow(window->winId());
    if (!rendered.save(navigation_demo_dir + "/" + step.name + "-viewport.png") ||
        !presented.save(navigation_demo_dir + "/" + step.name + "-window.png"))
    {
      qFatal("BTS_NAVIGATION_CAPTURE_FAILED");
    }
    const auto authoring_state = trackball_demo && document ? document->to_json() : QJsonObject{};
    QJsonObject document;
    try {
      document = native_document_state(host);
    }
    catch (const std::exception &error) {
      qFatal("BTS_NAVIGATION_INSPECTION_FAILED: %s", error.what());
    }
    if (navigation_stage == 0) {
      navigation_initial_document = document;
    }
    const QJsonObject view_state = viewport->view_state();
    QJsonObject trackball_control, trackball_pick;
    if (trackball_demo) {
      auto *control = workspace->findChild<QCheckBox *>("trackballNavigation");
      auto *symmetry = workspace->findChild<QWidget *>("symmetryManipulator");
      if (!control || !symmetry)
        qFatal("Trackball/Symmetry controls missing");
      const auto rect = control->geometry(), neighbor = symmetry->geometry();
      trackball_control = {
          {"visible", control->isVisible() && !control->visibleRegion().isEmpty()},
          {"enabled", control->isEnabled()},
          {"checked", control->isChecked()},
          {"rect", QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()}},
          {"symmetry_rect",
           QJsonArray{neighbor.x(), neighbor.y(), neighbor.width(), neighbor.height()}},
          {"same_toolbar", control->parentWidget() == symmetry->parentWidget()},
          {"pointer_clicks", trackball_pointer_clicks}};
      if (step.name == "trackball" || step.name == "turntable" || step.name == "persist-enabled" ||
          (step.name == "initial" && qEnvironmentVariableIsSet("BTS_TRACKBALL_RELOAD")))
      {
        const auto pane = viewport->pane_rect(bts::ViewKind::Scene);
        struct PickCandidate {
          std::size_t index;
          QVector3D center;
          QPointF expected, point;
          std::array<QPointF, 3> projected;
          double area2, min_edge, shape;
        };
        std::vector<PickCandidate> candidates;
        const auto interior = pane.adjusted(8, 8, -8, -8);
        for (std::size_t index = 0; index < snapshot.uv_triangles.size(); ++index) {
          const auto &triangle = snapshot.uv_triangles[index];
          if (triangle.material != std::size_t(selected_material))
            continue;
          PickCandidate candidate{};
          candidate.index = index;
          bool inside = true;
          for (int k = 0; k < 3; ++k) {
            const QVector3D position(
                triangle.positions[k][0], triangle.positions[k][1], triangle.positions[k][2]);
            candidate.center += position / 3;
            candidate.expected += QPointF(triangle.corners[k][0], triangle.corners[k][1]) / 3;
            candidate.projected[k] = viewport->scene_point(position);
            inside = inside && std::isfinite(candidate.projected[k].x()) &&
                     std::isfinite(candidate.projected[k].y()) &&
                     interior.contains(candidate.projected[k]);
          }
          if (!inside)
            continue;
          const auto ab = candidate.projected[1] - candidate.projected[0],
                     ac = candidate.projected[2] - candidate.projected[0],
                     bc = candidate.projected[2] - candidate.projected[1];
          const double ab2 = QPointF::dotProduct(ab, ab), ac2 = QPointF::dotProduct(ac, ac),
                       bc2 = QPointF::dotProduct(bc, bc);
          candidate.area2 = std::abs(ab.x() * ac.y() - ab.y() * ac.x());
          candidate.min_edge = std::sqrt(std::min({ab2, ac2, bc2}));
          candidate.shape = candidate.area2 / std::max({ab2, ac2, bc2});
          candidate.point = viewport->scene_point(candidate.center);
          if (candidate.area2 >= 16 && candidate.min_edge >= 2 && candidate.shape >= .1 &&
              interior.contains(candidate.point))
            candidates.push_back(candidate);
        }
        // Rank only projected geometry. Never use measured UV error to choose a witness.
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
          return a.area2 > b.area2;
        });
        for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
          const auto &candidate = candidates[rank];
          const auto &triangle = snapshot.uv_triangles[candidate.index];
          const auto hit = viewport->probe_surface(candidate.point);
          if (!hit || viewport->surface_triangle_id_ != triangle.triangle_id)
            continue;
          const double error = std::max(std::abs(hit->center[0] - candidate.expected.x()),
                                        std::abs(hit->center[1] - candidate.expected.y()));
          QJsonArray projected, positions, uv;
          for (int k = 0; k < 3; ++k) {
            projected.append(QJsonArray{candidate.projected[k].x(), candidate.projected[k].y()});
            positions.append(QJsonArray{
                triangle.positions[k][0], triangle.positions[k][1], triangle.positions[k][2]});
            uv.append(QJsonArray{triangle.corners[k][0], triangle.corners[k][1]});
          }
          trackball_pick = {
              {"triangle", qint64(triangle.triangle_id)},
              {"screen", QJsonArray{candidate.point.x(), candidate.point.y()}},
              {"expected_uv", QJsonArray{candidate.expected.x(), candidate.expected.y()}},
              {"actual_uv", QJsonArray{hit->center[0], hit->center[1]}},
              {"max_error", error},
              {"projected_vertices", projected},
              {"original_positions", positions},
              {"original_uv", uv},
              {"projected_area2", candidate.area2},
              {"minimum_edge", candidate.min_edge},
              {"area2_over_max_edge_squared", candidate.shape},
              {"geometric_rank", qint64(rank)},
              {"geometric_candidates", qint64(candidates.size())},
              {"selection",
               "Descending projected area; all vertices inside pane; area2>=16, min edge>=2, area2/max edge squared>=.1; first production-visible same triangle"},
              {"method",
               "Projected original triangle centroid -> production surface ray -> barycentric UV"}};
          break;
        }
        if (trackball_pick.isEmpty())
          qFatal("No visible original triangle for trackball picking witness");
      }
    }
    const QPoint origin = viewport->mapTo(window.get(), QPoint{});
    const double scale = double(presented.width()) / window->width();
    navigation_captures.append(
        QJsonObject{{"stage", step.name},
                    {"input", step.input},
                    {"viewport_rect",
                     QJsonArray{qRound(origin.x() * scale),
                                qRound(origin.y() * scale),
                                qRound(viewport->width() * scale),
                                qRound(viewport->height() * scale)}},
                    {"before_view_state", navigation_before_view},
                    {"view_state", view_state},
                    {"before_frame", navigation_before_frame},
                    {"frame", viewport->frame_count},
                    {"trackball_control", trackball_control},
                    {"trackball_pick", trackball_pick},
                    {"before_document_state", navigation_before_document},
                    {"document_state", document},
                    {"undo_count", int(undo_history.size())},
                    {"redo_count", int(redo_history.size())},
                    {"authoring_document", authoring_state},
                    {"document_revision", qint64(document_revision)},
                    {"dirty", dirty},
                    {"document_matches_initial", document == navigation_initial_document},
                    {"workspace_state", workspace_state(*workspace)}});
    if (trackball_demo) {
      QFile partial(navigation_demo_dir + "/navigation-partial.json");
      if (!partial.open(QIODevice::WriteOnly))
        qFatal("Trackball incremental evidence write failed");
      partial.write(
          QJsonDocument(QJsonObject{{"complete", false}, {"captures", navigation_captures}})
              .toJson());
    }
    std::printf("BTS_NAVIGATION_DEMO stage=%s pid=%lld frames=%d document_unchanged=%s\n",
                step.name.toUtf8().constData(),
                static_cast<long long>(app.applicationPid()),
                viewport->frame_count,
                document == navigation_initial_document ? "true" : "false");
    std::fflush(stdout);
    ++navigation_stage;
    if (navigation_stage < int(navigation_steps.size())) {
      navigation_before_view = view_state;
      navigation_before_frame = viewport->frame_count;
      navigation_before_document = document;
      navigation_steps[navigation_stage].apply();
      navigation_after_frame =
          viewport->frame_count +
          (trackball_demo && navigation_steps[navigation_stage].name == "released-idle" ? 12 : 3);
      return;
    }
    const QJsonObject result{
        {"pid", app.applicationPid()},
        {"qt", qVersion()},
        {"renderer", QString::fromStdString(host.renderer())},
        {"frames", viewport->frame_count},
        {"captures", navigation_captures},
        {"input_delivery",
         "Qt mouse press/move/release, wheel and key events to the live viewport"},
        {"capture_surface", "Qt ARGB native window and explicit viewport framebuffer"},
        {"snap_degrees", bts::OrbitCamera::snap_degrees},
        {"snap_policy",
         "15 degrees is a product choice; the reference did not establish an increment"},
        {"wheel_policy",
         "Positive Qt wheel angle zooms in; no macOS physical-wheel equivalence claimed"},
        {"gesture_policy",
         "Initiating pane, button and gesture retained until release or focus/grab loss"}};
    QFile file(navigation_demo_dir + "/navigation.json");
    if (!file.open(QIODevice::WriteOnly)) {
      qFatal("BTS_NAVIGATION_REPORT_FAILED");
    }
    file.write(QJsonDocument(result).toJson());
    navigation_demo.stop();
    QTimer::singleShot(500, &app, &QCoreApplication::quit);
  });
  navigation_demo.start(750);
}
