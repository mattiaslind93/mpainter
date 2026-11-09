// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Displacement.hh"
#include <QWidget>
#include <functional>
#include <optional>
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
namespace bts {
class DisplacementPanel final : public QWidget {
 public:
  explicit DisplacementPanel(QWidget *parent = nullptr);
  void synchronize(const DisplacementSettings &,
                   bool editable,
                   std::optional<bool> layer_channel = {});
  std::function<void(bool)> channel_changed;
  std::function<void(const DisplacementSettings &)> changed;

 private:
  DisplacementSettings state_;
  bool syncing_ = false;
  QCheckBox *channel_, *enabled_;
  QWidget *fields_;
  QDoubleSpinBox *strength_, *midlevel_, *max_length_;
  QSpinBox *subdivision_;
  QComboBox *mode_;
  void publish();
};
}  // namespace bts
