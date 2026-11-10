// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ColorPicker.hh"
#include "NumericLocale.hh"
#include "IMB_colormanagement.hh"
#include <QJsonObject>
#include <QScopeGuard>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace bts {
inline QJsonObject verify_color_picker_enter()
{
  const auto require = [](bool condition, const char *message) {
    if (!condition)
      throw std::runtime_error(message);
  };
  const std::string prior = blender::IMB_colormanagement_working_space_get();
  const auto restore = qScopeGuard(
      [&] { blender::IMB_colormanagement_working_space_set_from_name(prior.c_str()); });
  blender::IMB_colormanagement_working_space_set_from_name("ACEScg");
  require(std::string(blender::IMB_colormanagement_working_space_get()) == "ACEScg",
          "Color Enter test needs the actual owner ACEScg context");
  auto &app = *QApplication::instance();
  ColorPicker picker;
  picker.set_color({1, 1, 1, 1});
  picker.show();
  app.processEvents();
  auto *popup = picker.findChild<QDialog *>("colorPickerDialog");
  auto *swatch = picker.findChild<QPushButton *>("colorSwatch");
  QPushButton *previous = nullptr;
  for (auto *button : popup->findChildren<QPushButton *>())
    if (button->text() == "Previous")
      previous = button;
  require(previous, "Missing Previous button");
  int starts = 0, changes = 0, ends = 0, previous_clicks = 0;
  picker.started = [&] { ++starts; };
  picker.changed = [&](const auto &value) {
    ++changes;
    std::printf("COLOR_CHANGED red=%.9g\n", value[0]);
  };
  picker.finished = [&] { ++ends; };
  QObject::connect(previous, &QPushButton::clicked, [&] {
    ++previous_clicks;
    std::printf("PREVIOUS_CLICK default=%d auto_default=%d\n",
                previous->isDefault(),
                previous->autoDefault());
  });
  const auto open = [&] {
    swatch->click();
    app.processEvents();
    picker.findChild<QCheckBox *>("colorAdvanced")->setChecked(true);
    app.processEvents();
  };
  const auto enter = [&](double wanted) {
    starts = changes = ends = previous_clicks = 0;
    auto *field = picker.linear_fields()[0];
    auto *line = field->findChild<QLineEdit *>();
    require(field->isVisible() && field->isEnabled(),
            "Linear Color editor is not visible/enabled");
    field->setFocus();
    QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, "a");
    QApplication::sendEvent(line, &select);
    QKeyEvent text(QEvent::KeyPress, Qt::Key_0, Qt::NoModifier, QString::number(wanted));
    QApplication::sendEvent(line, &text);
    std::printf("COLOR_TYPED wanted=%.9g value=%.9g text=%s\n",
                wanted,
                field->value(),
                line->text().toUtf8().constData());
    QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(line, &commit);
    std::printf(
        "COLOR_COMMITTED wanted=%.9g value=%.9g color=%.9g text=%s start=%d change=%d finish=%d previous=%d\n",
        wanted,
        field->value(),
        picker.color()[0],
        line->text().toUtf8().constData(),
        starts,
        changes,
        ends,
        previous_clicks);
    require(!previous_clicks, "Enter activated Previous after committing the numeric Color value");
    require(std::abs(field->value() - wanted) < 1e-6 &&
                std::abs(picker.color()[0] - wanted) < 1e-6,
            "Enter did not retain the signed/HDR Color value");
    require(starts == 1 && changes == 1 && ends == 1,
            "Numeric Enter did not produce exactly one complete Color gesture");
    require(popup->isVisible(), "Numeric Enter unexpectedly closed the Color popup");
  };
  open();
  enter(-.25);
  // Existing Escape behavior closes the popup and retains an already completed
  // numeric edit. It must not introduce another edit or a Previous activation.
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(popup, &escape);
  app.processEvents();
  require(!popup->isVisible() && picker.color()[0] == -.25f && changes == 1 && ends == 1,
          "Escape changed an already committed Color gesture");
  open();
  enter(2.5);
  previous->click();
  require(picker.color()[0] == -.25f && previous_clicks == 1 && starts == 2 && changes == 2 &&
              ends == 2,
          "Explicit Previous no longer restores the prior Color");
  for (auto *button : popup->findChildren<QPushButton *>())
    require(!button->autoDefault() && !button->isDefault(),
            "Color popup retains an implicit default action");
  popup->hide();
  // The same ColorPicker can be reparented into the real floating Properties
  // QDialog. Return in another numeric field must not open its outer swatch.
  QDialog undocked;
  QVBoxLayout layout(&undocked);
  NumericSpinBox outside;
  outside.setRange(0, 1);
  outside.setDecimals(3);
  outside.setKeyboardTracking(false);
  ColorPicker embedded;
  layout.addWidget(&outside);
  layout.addWidget(&embedded);
  undocked.show();
  app.processEvents();
  auto *outer_swatch = embedded.findChild<QPushButton *>("colorSwatch");
  auto *outer_popup = embedded.findChild<QDialog *>("colorPickerDialog");
  outside.setFocus();
  outside.selectAll();
  QKeyEvent number(QEvent::KeyPress, Qt::Key_0, Qt::NoModifier, "0.5");
  QApplication::sendEvent(&outside, &number);
  QKeyEvent outer_commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QApplication::sendEvent(&outside, &outer_commit);
  app.processEvents();
  require(outside.value() == .5 && !outer_popup->isVisible(),
          "Unrelated numeric Enter opened ColorPicker in floating Properties");
  require(!outer_swatch->autoDefault() && !outer_swatch->isDefault(),
          "Outer Color swatch retains an implicit dialog action");
  outer_swatch->click();
  app.processEvents();
  require(outer_popup->isVisible(), "Explicit outer Color swatch click no longer opens its popup");
  outer_popup->hide();
  std::puts("ColorPicker Enter: signed/HDR single gesture, Escape and explicit Previous PASS");
  return {{"signed_hdr_enter", true},
          {"one_gesture_per_enter", true},
          {"implicit_previous_clicks", 0},
          {"explicit_previous", true},
          {"escape_retains_committed_value", true},
          {"undocked_outer_swatch_enter", true},
          {"owner_context", "ACEScg"},
          {"working_space_restored", true}};
}
}  // namespace bts
