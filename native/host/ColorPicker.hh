// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PickerColor.hh"
#include <QWidget>
#include <QColor>
#include <QTimer>
#include <array>
#include <functional>
#include <vector>
class QDialog;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;
namespace bts {
class ColorWheel;
class ColorPicker final:public QWidget {
 public:
  explicit ColorPicker(QWidget *parent=nullptr);
  ~ColorPicker()override;
  void retire_callbacks()noexcept;
  void set_color(const PickerColor &);
  // UI label only; conversion continues to use the existing native working space.
  void set_working_space_label(const QString &);
  // Show the colour as a small filled rectangle beside its label rather than as
  // a panel-wide button with the hex printed on it. The hex moves to the tooltip
  // and stays in the picker. docs/design/ui.md §7.5.
  void set_compact(bool on=true);
  const PickerColor &color()const{return color_;}
  const std::array<QDoubleSpinBox *,3> &linear_fields()const{return linear_;}
  void accept_sample(const PickerColor &);
  std::function<void()> started,finished,pick_requested;
  std::function<void(const PickerColor &)> changed;
 private:
  PickerColor color_{0,0,0,1},old_=color_;
  bool gesture_=false,updating_=false,compact_=false;
  QDialog *popup_;
  ColorWheel *wheel_;
  QPushButton *swatch_,*new_swatch_,*old_swatch_;
  QLineEdit *hex_;
  QLabel *hdr_;
  std::array<QDoubleSpinBox *,3> rgb_{},hsv_{},linear_{};
  std::array<QPushButton *,8> recent_buttons_{};
  std::vector<PickerColor> recent_;
  void begin();
  void publish();
  void end();
  void edit(PickerColor);
  void synchronize();
  void save_recent();
};
}
