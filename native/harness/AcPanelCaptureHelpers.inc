// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers for the AC panel capture and the shelf-navigation test (render, save and the fill
// chrome checks). Test code; WorkspaceShell.cc includes it inside namespace bts.
namespace {
QImage render_panel(QWidget *widget, int width)
{
  widget->ensurePolished();
  const int height = std::max(48, std::max(widget->sizeHint().height(), widget->height()));
  const QSize previous = widget->size();
  widget->resize(width, height);
  QImage image(width, height, QImage::Format_RGB32);
  image.fill(QColor(bts::theme::Surface));
  QPainter painter(&image);
  widget->render(&painter);
  widget->resize(previous);
  return image;
}
void save_properties(WorkspaceShell &shell, const QString &path)
{
  auto *properties = shell.findChild<QWidget *>("properties");
  auto *header = properties->findChild<QWidget *>("dockHeader");
  auto *body = shell.findChild<QWidget *>("materialProperties");
  const int width = 320;
  const auto head = render_panel(header, width);
  body->setMinimumWidth(width);
  body->setMaximumWidth(width);
  body->adjustSize();
  const auto content = render_panel(body, width);
  QImage image(width, head.height() + content.height(), QImage::Format_RGB32);
  image.fill(QColor(bts::theme::Surface));
  QPainter painter(&image);
  painter.drawImage(0, 0, head);
  painter.drawImage(0, head.height(), content);
  if (!image.save(path))
    qFatal("Could not write %s", qPrintable(path));
}
void require_fill_chrome(WorkspaceShell &shell)
{
  auto fail = [](const char *message) { qFatal("AC fill chrome: %s", message); };
  auto *title = shell.findChild<QLabel *>("propertiesDockTitle");
  if (!title || title->text() != "PROPERTIES - FILL")
    fail("dock title");
  auto *lock = shell.findChild<QCheckBox *>("fillScaleLock");
  auto *scale_v = shell.findChild<QDoubleSpinBox *>("fillScaleV");
  auto *slider = shell.findChild<QSlider *>("fillScaleSlider1");
  if (!lock || !lock->isChecked() || !scale_v || scale_v->isEnabled() || !slider ||
      slider->isEnabled())
    fail("linked scale");
  if (scale_v->text().contains(',') ||
      shell.findChild<QDoubleSpinBox *>("roughnessControl")->text().contains(','))
    fail("decimal comma");
  if (!shell.findChild<QWidget *>("fillChannelRow6")->isHidden())
    fail("scattering block visible");
  if (shell.findChild<QWidget *>("fillChannelRow14")->isHidden())
    fail("displacement block hidden");
  if (shell.findChild<QWidget *>("fillChannelRow0")->isHidden())
    fail("color block hidden");
  if (shell.findChild<QWidget *>("fillChannelRow4")->isHidden())
    fail("channel 4 bump block hidden");
  auto *scatt = shell.findChild<QCheckBox *>("fillChannelEnabled6");
  if (!scatt || scatt->isHidden() || scatt->isChecked() || scatt->text() != "scatt")
    fail("scatt toggle");
  auto *normal_header = shell.findChild<QToolButton *>("fillChannelHeader3");
  auto *normal_toggle = shell.findChild<QCheckBox *>("fillChannelEnabled3");
  auto *normal_row = shell.findChild<QWidget *>("fillChannelRow3");
  if (!normal_header || !normal_header->isHidden() || !normal_toggle ||
      !normal_toggle->isHidden() || normal_toggle->text() != "nrm" || !normal_row ||
      !normal_row->isHidden())
    fail("normal channel visible");
  auto *bump4 = shell.findChild<QToolButton *>("fillChannelHeader4");
  auto *bump4_toggle = shell.findChild<QCheckBox *>("fillChannelEnabled4");
  if (!bump4 || bump4->isHidden() || !bump4->text().startsWith("Bump") || !bump4_toggle ||
      bump4_toggle->isHidden() || bump4_toggle->text() != "bump")
    fail("channel 4 named Bump");
  shell.findChild<QWidget *>("materialProperties")->adjustSize();
  QApplication::processEvents();
  const auto toggle_at = [&](const char *name) {
    auto *box = shell.findChild<QCheckBox *>(name);
    if (!box || box->isHidden())
      fail(name);
    return box->mapTo(shell.findChild<QWidget *>("fillChannelToggles"), QPoint(0, 0));
  };
  const auto color = toggle_at("fillChannelEnabled0"), metal = toggle_at("fillChannelEnabled1"),
             rough = toggle_at("fillChannelEnabled2"),
             bump_button = toggle_at("fillChannelEnabled4"),
             disp = toggle_at("fillChannelEnabled14"), scatt_at = toggle_at("fillChannelEnabled6");
  if (!(color.y() == metal.y() && metal.y() == rough.y() && color.x() < metal.x() &&
        metal.x() < rough.x()))
    fail("channel row color metal rough");
  if (!(bump_button.y() == disp.y() && disp.y() == scatt_at.y() && bump_button.y() > color.y() &&
        bump_button.x() < disp.x() && disp.x() < scatt_at.x()))
    fail("channel row bump disp scatt");
  auto *bar = shell.findChild<QWidget *>("fillTangentBump");
  auto *xyz = shell.findChild<QWidget *>("tangentBumpValues");
  if (!bar || bar->isVisible() || !xyz || xyz->isVisible())
    fail("normal swatch visible");
  if (!shell.findChild<QSlider *>("metallicSlider")->isHidden() ||
      !shell.findChild<QSlider *>("roughnessSlider")->isHidden() ||
      !shell.findChild<QSlider *>("bumpSlider")->isHidden() ||
      !shell.findChild<QSlider *>("displacementSlider")->isHidden())
    fail("scalar sliders still visible");
  if (!shell.findChild<QDoubleSpinBox *>("metallicControl")->isHidden() ||
      !shell.findChild<QDoubleSpinBox *>("materialBump")->isHidden() ||
      !shell.findChild<QDoubleSpinBox *>("materialDisplacement")->isHidden())
    fail("scalar values still visible");
  const auto gradient_ok = [&](const char *name, const char *text) {
    auto *widget = shell.findChild<QWidget *>(name);
    if (!widget || widget->isHidden())
      fail(name);
    QImage image(
        std::max(widget->width(), 8), std::max(widget->height(), 8), QImage::Format_RGB32);
    image.fill(QColor(bts::theme::Surface));
    widget->render(&image);
    if (widget->accessibleName() != text)
      fail(name);
    const int y = std::clamp(image.height() * 3 / 4, 0, image.height() - 1);
    const auto left = image.pixelColor(std::clamp(image.width() / 8, 1, image.width() - 1), y);
    const auto right = image.pixelColor(std::clamp(image.width() * 5 / 8, 1, image.width() - 1),
                                        y);
    if (right.red() + right.green() + right.blue() - left.red() - left.green() - left.blue() < 80)
      fail(name);
  };
  gradient_ok("fillGradientMetallic", "0.000");
  gradient_ok("fillGradientRoughness", "0.800");
  gradient_ok("fillGradientBump", "0.500");
  gradient_ok("fillGradientDisplacement", "0.0000");
  auto *mode = shell.findChild<QToolButton *>("fillMaterialMode");
  if (!mode || !mode->text().contains("No Resource Selected"))
    fail("material mode");
  auto *wrap = shell.findChild<QComboBox *>("fillWrap");
  if (!wrap || wrap->isHidden() || wrap->currentText() != "Repeat")
    fail("uv wrap");
  if (!shell.findChild<QLabel *>("sectionTitle")->isHidden())
    fail("inner fill title");
  if (!shell.findChild<QComboBox *>("fillProjectionMode") ||
      !shell.findChild<QComboBox *>("fillBaseColorResource"))
    fail("projection controls");
  if (!shell.findChild<QToolButton *>("propertiesUndock") ||
      !shell.findChild<QToolButton *>("propertiesClose") ||
      !shell.findChild<QAction *>("toggleProperties"))
    fail("dock controls");
  const auto place = [&](const char *name) {
    auto *widget = shell.findChild<QWidget *>(name);
    if (!widget || widget->isHidden())
      fail(name);
    return widget->mapTo(shell.findChild<QWidget *>("materialProperties"), QPoint(0, 0)).y();
  };
  const int offset = place("fillOffset0"), scale_mode = place("fillScaleMode"),
            physical = place("fillPhysicalSizeX"), projection = place("fill3dDisclosure"),
            warp = place("fillWarpDisclosure"), symmetry = place("fillSymmetryPanel"),
            frame = place("fillShowManipulator");
  if (!(offset < scale_mode && scale_mode < physical && physical < projection &&
        projection < warp && warp < symmetry && symmetry < frame))
    fail("controls after offset");
  auto *sphere = shell.findChild<QWidget *>("fillMaterialSphere");
  if (!sphere || !sphere->isHidden())
    fail("sphere hidden on settings tab");
}
}  // namespace
#include "ShelfNavigationTest.inc"
