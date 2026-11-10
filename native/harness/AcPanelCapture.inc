// SPDX-License-Identifier: GPL-3.0-or-later
// --ac-ui-capture-demo: renders the property panels to PNG files for the AC review. Test code;
// WorkspaceShell.cc includes it inside namespace bts, after AcPanelCaptureHelpers.inc.
int capture_ac_panels(const QString &directory)
{
  if (directory.isEmpty())
    return 2;
  QDir().mkpath(directory);
  auto *viewport = new QWidget;
  viewport->resize(640, 480);
  int shelf_applications = 0;
  WorkspaceShell::Callbacks capture_callbacks;
  capture_callbacks.asset_selected = [&](const StudioAsset &) { ++shelf_applications; };
  if (qEnvironmentVariableIsSet("BTS_SHELF_NAV_TEST"))
    capture_callbacks.baked_maps = [] {
      return std::vector<BakedShelfItem>{
          {"baked:6a0b35a4-a916-42c3-a496-a3c7efdb1e13:3", "A Fixture baked AO"}};
    };
  WorkspaceShell shell(viewport, std::move(capture_callbacks));
  shell.resize(1280, 860);
  shell.show();
  shell.set_console_visible(false);
  shell.set_shelf_visible(true);
  shell.reset_reference_layout();
  QApplication::processEvents();
  NewProjectOptions options;
  BrushSettings brush;
  TextureSet set;
  set.uniform_editable = true;
  set.name = "Default";
  set.channels = {true, true, true, true, true};
  set.displacement.channel = true;
  set.displacement.allocated = true;
  set.users[0].present = true;
  set.users[0].scattering = true;
  set.users[0].name = "Scattering";
  TextureLayer fill;
  fill.kind = LayerKind::Fill;
  fill.name = "Fill layer 1";
  fill.channels = {true, true, true, true, true};
  fill.values.base_color = {.75f, .75f, .75f, 1};
  fill.values.metallic = 0;
  fill.values.roughness = .8f;
  fill.values.normal = {0, 0, 1};
  fill.displacement.enabled = true;
  fill.displacement.value = 0;
  fill.users[0].enabled = false;
  set.layers = {fill};
  set.selected_layer = fill.id;
  const auto show = [&] {
    shell.show_texture_set(&set, &options, &brush);
    QApplication::processEvents();
  };
  show();
  require_fill_chrome(shell);
  if (qEnvironmentVariableIsSet("BTS_SHELF_NAV_TEST"))
    return verify_shelf_navigation(shell, set, directory, shelf_applications);
  save_properties(shell, directory + "/fill-properties.png");
  if (qEnvironmentVariableIsSet("BTS_MASK_FILL_UI_TEST")) {
    verify_shared_mask_fill_ui(
        shell,
        *static_cast<MaskFillPanel *>(shell.findChild<QWidget *>("maskFillProperties")),
        set,
        directory,
        show,
        [&](QString path) { save_properties(shell, path); });
    return 0;
  }
  shell.findChild<QToolButton *>("fillMaterialTab")->click();
  QApplication::processEvents();
  auto *sphere = shell.findChild<QWidget *>("fillMaterialSphere");
  if (!sphere || sphere->isHidden() || sphere->width() < 64)
    qFatal("AC material sphere missing");
  QImage sphere_image(sphere->size(), QImage::Format_RGB32);
  sphere->render(&sphere_image);
  int inside = 0, lo = 999, hi = 0;
  for (int y = 0; y < sphere_image.height(); ++y)
    for (int x = 0; x < sphere_image.width(); ++x) {
      const auto px = sphere_image.pixelColor(x, y);
      if (px.red() > 45 && px.red() < 60 && px.green() > 45 && px.green() < 60 && px.blue() > 45 &&
          px.blue() < 60)
        continue;
      const int lum = px.red() + px.green() + px.blue();
      ++inside;
      lo = std::min(lo, lum);
      hi = std::max(hi, lum);
    }
  const auto corner = sphere_image.pixelColor(2, 2),
             south = sphere_image.pixelColor(sphere_image.width() / 2,
                                             sphere_image.height() * 3 / 4);
  if (inside < 8000 || hi - lo < 80 || corner.red() > 70 || south.red() < 70)
    qFatal("AC material sphere is not shaded");
  if (!sphere_image.save(directory + "/fill-material-sphere.png"))
    qFatal("sphere grab failed");
  shell.findChild<QToolButton *>("fillSettingsTab")->click();
  QApplication::processEvents();
  shell.findChild<QToolButton *>("propertiesUndock")->click();
  QApplication::processEvents();
  auto *floating = shell.findChild<QDialog *>("propertiesFloat");
  if (!floating || floating->isHidden() || floating->windowTitle() != "PROPERTIES - FILL")
    qFatal("AC properties did not undock");
  shell.findChild<QToolButton *>("propertiesUndock")->click();
  QApplication::processEvents();
  if (auto *still = shell.findChild<QDialog *>("propertiesFloat"); still && still->isVisible())
    qFatal("AC properties did not dock");
  if (shell.findChild<QWidget *>("propertiesSlot")->isHidden())
    qFatal("AC properties did not dock");
  shell.findChild<QToolButton *>("propertiesClose")->click();
  QApplication::processEvents();
  if (!shell.findChild<QWidget *>("propertiesSlot")->isHidden())
    qFatal("AC properties did not close");
  shell.findChild<QAction *>("toggleProperties")->trigger();
  QApplication::processEvents();
  if (shell.findChild<QWidget *>("propertiesSlot")->isHidden())
    qFatal("AC properties did not return");
  show();
  fill.kind = LayerKind::Paint;
  set.layers = {fill};
  set.selected_layer = fill.id;
  set.selected_mask = false;
  set.selected_effect = QUuid();
  show();
  if (auto *normal = shell.findChild<QToolButton *>("brushChannel3");
      !normal || !normal->isHidden())
    qFatal("AC paint still shows nrm");
  if (auto *bump = shell.findChild<QToolButton *>("brushChannel4");
      !bump || bump->isHidden() || bump->text() != "bump")
    qFatal("AC paint bump missing");
  if (auto *context = shell.findChild<QComboBox *>("layerChannelContext");
      !context || context->findData(3) >= 0 || context->findText("Normal") >= 0)
    qFatal("AC layer list still shows Normal");
  save_properties(shell, directory + "/paint-properties.png");
  fill.kind = LayerKind::Fill;
  LayerMask mask;
  MaskEffect mask_fill;
  mask_fill.kind = MaskEffectKind::Fill;
  mask_fill.fill = .5f;
  mask.effects = {mask_fill};
  fill.mask = mask;
  fill.effects.clear();
  set.layers = {fill};
  set.selected_layer = fill.id;
  set.selected_mask = true;
  set.selected_effect = mask_fill.id;
  show();
  save_properties(shell, directory + "/mask-fill-properties.png");
  MaskEffect generator;
  generator.kind = MaskEffectKind::Generator;
  generator.generator = 2;
  fill.mask->effects = {generator};
  set.layers = {fill};
  set.selected_effect = generator.id;
  show();
  save_properties(shell, directory + "/generator-properties.png");
  MaskEffect filter_mask;
  filter_mask.kind = MaskEffectKind::Filter;
  filter_mask.filter.resource = 5;
  fill.mask->effects = {filter_mask};
  set.layers = {fill};
  set.selected_effect = filter_mask.id;
  show();
  save_properties(shell, directory + "/filter-properties.png");
  MaskEffect anchor;
  anchor.kind = MaskEffectKind::Anchor;
  anchor.anchor_name = "Fill layer 1 mask";
  fill.mask->effects = {anchor};
  set.layers = {fill};
  set.selected_effect = anchor.id;
  show();
  save_properties(shell, directory + "/anchor-properties.png");
  fill.mask.reset();
  LevelsEffect levels;
  levels.kind = ContentEffectKind::Levels;
  levels.channel = 0;
  fill.effects = {levels};
  set.layers = {fill};
  set.selected_mask = false;
  set.selected_effect = levels.id;
  show();
  save_properties(shell, directory + "/levels-properties.png");
  shell.layer_settings_tabs()->setCurrentIndex(0);
  QApplication::processEvents();
  if (!shell.findChild<QWidget *>("layerSettingsTabs")->grab().save(directory + "/layers.png"))
    qFatal("layers grab failed");
  shell.layer_settings_tabs()->setCurrentIndex(1);
  QApplication::processEvents();
  if (!shell.findChild<QWidget *>("layerSettingsTabs")
           ->grab()
           .save(directory + "/texture-set-settings.png"))
    qFatal("settings grab failed");
  if (!shell.findChild<QWidget *>("assetShelf")->grab().save(directory + "/shelf.png"))
    qFatal("shelf grab failed");
  if (!shell.findChild<QWidget *>("toolRail")->grab().save(directory + "/tool-rail.png"))
    qFatal("tool rail grab failed");
  if (!shell.findChild<QWidget *>("contextToolbar")
           ->grab()
           .save(directory + "/context-toolbar.png"))
    qFatal("toolbar grab failed");
  auto *display = shell.findChild<QDialog *>("displaySettings");
  display->show();
  QApplication::processEvents();
  auto *opacity = display->findChild<QDoubleSpinBox *>("environmentRotation");
  auto *exposure = display->findChild<QDoubleSpinBox *>("environmentExposure");
  opacity->setValue(40.5);
  exposure->setValue(1.5);
  QApplication::processEvents();
  if (opacity->text().contains(',') || exposure->text().contains(',') ||
      !opacity->text().contains('.') || !exposure->text().contains('.'))
    qFatal(
        "AC display decimals: %s %s", qPrintable(opacity->text()), qPrintable(exposure->text()));
  if (!display->grab().save(directory + "/display-settings.png"))
    qFatal("display grab failed");
  display->hide();
  shell.resize(1400, 900);
  shell.reset_reference_layout();
  show();
  QApplication::processEvents();
  if (!shell.grab().save(directory + "/workspace.png"))
    qFatal("workspace grab failed");
  std::printf("BTS_AC_CAPTURE directory=%s\n", qPrintable(directory));
  std::fflush(stdout);
  return 0;
}
