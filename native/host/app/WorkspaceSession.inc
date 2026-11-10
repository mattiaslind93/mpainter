// SPDX-License-Identifier: GPL-3.0-or-later
// The Painter session: everything main() does once the Blender host is up, from the workspace
// and its callbacks to the event loop and an orderly shutdown. main.cc includes this file inside
// its anonymous namespace, after Viewport and the input helpers, so it is one translation unit
// with main(). That is deliberate: the reviewed live-render drivers in native/tests compile
// main.cc with a renamed main() and a start_gui_fix_demo() macro that names this function's
// locals (host, viewport, edit_material).
int run_workspace_session(bts::ProfileApplication &app,
                          const QCommandLineParser &parser,
                          const bts::harness::HarnessContext &harness,
                          bts::BrandingStartup &branding,
                          bts::BlenderHost &host,
                          const bool numeric_locale_invalid)
{
#include "WorkspaceState.inc"
#include "WorkspaceViewport.inc"
#include "WorkspaceStrokes.inc"
#include "WorkspaceResources.inc"
#include "WorkspaceCallbacks.inc"
#include "WorkspacePaths.inc"
#include "WorkspaceProject.inc"
#include "WorkspaceMenus.inc"
#include "WorkspaceAutomation.inc"
#include "WorkspaceWindow.inc"
#include "SessionDemoFlags.inc"
#include "SessionDemoSequence.inc"
#include "SessionCaptureDemos.inc"
#include "SessionDemoActions.inc"
  // Declared after every callback capture: normal return and exceptions from
  // the event loop retire/delete widgets while all referenced locals are alive.
  bool ui_shutdown_done = false, ui_shutdown_failed = false;
  const auto shutdown_ui = [&]() noexcept {
    if (ui_shutdown_done)
      return;
    ui_shutdown_done = true;
    const auto attempt = [&](const char *stage, auto &&operation) noexcept {
      try {
        operation();
      }
      catch (const std::exception &error) {
        ui_shutdown_failed = true;
        std::fprintf(stderr, "BTS_UI_CLEANUP_FAILED stage=%s detail=%s\n", stage, error.what());
      }
      catch (...) {
        ui_shutdown_failed = true;
        std::fprintf(stderr, "BTS_UI_CLEANUP_FAILED stage=%s detail=unknown\n", stage);
      }
    };
    // Never disable/hide/clear focus first: Qt can interpret pending editor
    // text during each of those operations. No new edit may enter shutdown.
    const auto block = [](auto &&self, QObject *object) noexcept -> void {
      object->blockSignals(true);
      for (QObject *child : object->children())
        self(self, child);
    };
    if (window)
      block(block, window.get());  // Includes the bare-window controls.
    pending_new->retire();
    clear_pending_new_ui();  // Before Save's event-pumping drain.
    if (program_warmup_retire)
      program_warmup_retire();
    if (pd149_actor_retire)
      pd149_actor_retire();  // Before widget hide or Host teardown.
    if (workspace)
      workspace->retire_editor_callbacks();
    recovery_timer.stop();
    material_pick_timer.stop();
    resource_ui_timer.stop();
    sets_demo.stop();
    new_demo.stop();
    open_demo.stop();
    demo.stop();
    layout_demo.stop();
    navigation_demo.stop();
    attempt("project-open-height-import", [&] {
      if (parameter_import)
        parameter_import->shutdown();
      project_opener.shutdown();
      height_ui.shutdown();
    });
    // Save shutdown pumps events. Cancel Source application timers first.
    attempt("source-applications", [&] { bts::finish_spsm_applications(workspace); });
    save_poll_timer.stop();
    // Preserve completion of accepted model edits and captured writers while
    // the host and callback state still exist. Pending editor text is retired.
    attempt("accepted-saves", [&] {
      while (active_save || requested_save) {
        if (requested_save && host.texture_updates_pending())
          host.flush_texture_updates();
        poll_save();
        if (active_save || requested_save) {
          QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
          QThread::msleep(1);
        }
      }
    });
    property_frame_timer.stop();
    property_idle_timer.stop();
    history_spill_poll.stop();
    attempt("python-automation", [&] {
      python_console.reset();
      automation_jobs.reset();
    });
    // These owners are QObjects parented to the shell/window. Release each
    // unique owner before deleting that parent; do not leave a second delete.
    attempt("shader-render", [&] {
      shader.reset();
      render_controller.reset();
    });
    attempt("resource-monitor", [&] { resource_monitor.reset(); });
#ifdef BTS_WITH_MATERIALX
    attempt("material-importer", [&] { material_importer.reset(); });
#endif
    attempt("native-image-source", [&] {
      material_layering_ui.reset();
      parameter_import.reset();
      native_image_ui.reset();
      native_material_loader.reset();
      texture_loader.reset();
    });
    attempt("thumbnails", [&] {
      bts::stop_asset_thumbnail_jobs();
      brush_thumbnails.reset();
      smart_mask_thumbnails.reset();
    });
    attempt("skew", [&] { skew_owner.reset(); });
    // Stack Open/Height controllers and QTimers have no window parent; their
    // terminal drains above make later stack destruction safe after Host close.
    // Their existing QPointer hook is no longer needed by Viewport destruction.
    if (viewport)
      viewport->before_native_close = {};
    window.reset();
    workspace = nullptr;
    viewport = nullptr;
  };
  const auto retire_window = qScopeGuard([&]() noexcept { shutdown_ui(); });
  // Explicit headless regression entry point; ordinary launches never run it.
  QTimer material_filter_test;
  QElapsedTimer material_filter_timeout;
  int material_filter_stage = 0;
  if (qEnvironmentVariableIsSet("BTS_MATERIAL_FILTER_UI_TEST")) {
    material_filter_timeout.start();
    material_filter_test.setInterval(100);
    QObject::connect(&material_filter_test, &QTimer::timeout, window.get(), [&] {
      try {
        if (material_filter_timeout.elapsed() > 120000)
          throw std::runtime_error("Material Filter UI timed out");
        if (material_filter_stage && (!document || host.texture_updates_pending() ||
                                      uniform_before || pending_property_edit))
          return;
        const auto directory = qEnvironmentVariable("BTS_MATERIAL_FILTER_UI_TEST");
        QDir().mkpath(directory);
        if (material_filter_stage == 0) {
          bts::NewProjectOptions options;
          options.absolute_mesh_path =
              qEnvironmentVariable("BTS_MATERIAL_FILTER_MESH").toStdString();
          options.resolution = bts::DocumentResolution::Pixels128;
          create_project(options);
        }
        else if (material_filter_stage == 1) {
          edit_material([](bts::TextureSet &set) {
            bts::TextureDocument::add_fill(set);
            auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
            layer.values.base_color = {.2, .4, .8, 1};
            set.precision[0] = bts::ChannelPrecision::RGB32F;
          });
        }
        else if (material_filter_stage == 2) {
          viewport->grabFramebuffer().save(directory + "/before.png");
          workspace->findChild<QAction *>("addFilterEffect")->trigger();
        }
        else if (material_filter_stage == 3) {
          workspace->findChild<QAction *>("filterHsv")->trigger();
        }
        else if (material_filter_stage == 4) {
          workspace->findChild<QDoubleSpinBox *>("filterHue")->setValue(.5);
        }
        else {
          auto &set = document->set(snapshot.materials[selected_material].session_id);
          const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
          const std::array<std::array<int, 2>, 1> points{{{64, 64}}};
          const auto cpu = bts::TextureDocument::evaluate(set, 64, 64).authored.base_color;
          const auto gpu = host.diagnostic_gpu_pixels(set.material_session_uid, 0, points);
          const std::array<float, 3> expected{.8, .6, .2};
          for (int c = 0; c < 3; ++c)
            if (std::abs(cpu[c] - expected[c]) > 1e-5 || gpu.empty() ||
                std::abs(gpu[0][c] - expected[c]) > 1e-5)
              throw std::runtime_error("Material Filter UI did not publish Hue to CPU/GPU");
          if (layer.effects.empty() || layer.effects.back().filter.resource != 4 ||
              layer.effects.back().filter.hue != .5)
            throw std::runtime_error("Material Filter UI lost authored settings");
          viewport->grabFramebuffer().save(directory + "/after.png");
          QFile proof(directory + "/ui.json");
          if (!proof.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot write Filter UI proof");
          proof.write(
              QJsonDocument(QJsonObject{{"hue", .5},
                                        {"cpu", QJsonArray{cpu[0], cpu[1], cpu[2]}},
                                        {"gpu", QJsonArray{gpu[0][0], gpu[0][1], gpu[0][2]}},
                                        {"renderer", QString::fromStdString(host.renderer())}})
                  .toJson());
          material_filter_test.stop();
          app.exit(0);
          return;
        }
        ++material_filter_stage;
      }
      catch (const std::exception &error) {
        std::fprintf(stderr, "MATERIAL_FILTER_UI_FAILED %s\n", error.what());
        material_filter_test.stop();
        app.exit(1);
      }
    });
    material_filter_test.start();
  }
  // A plain launch with nothing to open starts on the bundled Shaderball, so
  // the app opens on something to paint instead of on an empty viewport. The
  // New Project dialog does the work unshown, which is what keeps the mesh
  // lookup, the validation and every default in one place. Demo and test
  // launches always pass options of their own, and are left alone.
  QTimer startup_project;
  if (workspace && !qEnvironmentVariableIsSet("BTS_HEADLESS_WORKSPACE")) {
    const auto given = parser.optionNames();
    const bool plain = parser.positionalArguments().isEmpty() &&
                       std::all_of(given.begin(), given.end(), [](const QString &name) {
                         return name == "workspace" || name == "no-splash";
                       });
    if (plain) {
      startup_project.setSingleShot(true);
      QObject::connect(&startup_project, &QTimer::timeout, window.get(), [&] {
        if (document || opening_project || project_opener.pending())
          return;  // Something else got there first.
        try {
          bts::NewProjectDialog dialog(workspace);
          auto *ball = dialog.findChild<QPushButton *>("useShaderballMesh");
          auto *create = dialog.findChild<QPushButton *>("createProject");
          if (!ball || !ball->isEnabled() || !create)
            return;  // No bundled mesh: leave the viewport empty.
          ball->click();
          create->click();
          if (const auto options = dialog.options())
            create_project(*options);
        }
        catch (const std::exception &error) {
          workspace->append_console("Could not open the starting project: " +
                                    QString::fromUtf8(error.what()));
        }
      });
      startup_project.start(400);
    }
  }
  const int result = app.exec();
  shutdown_ui();
  return ui_shutdown_failed ? 1 : result;
}
