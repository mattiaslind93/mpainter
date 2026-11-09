// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PhysicalScale.hh"
#include <QWidget>
class QLineEdit;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
namespace bts {
class UnitsPanel final : public QWidget {
 public:
  UnitsPanel(QLineEdit *,
             const UnitSettings &,
             const SourceUnits &,
             QWidget *,
             std::string fallback = {});
  UnitSettings options() const;
  void set_options(const UnitSettings &);

 private:
  void describe_source();
  QLineEdit *path_;
  std::string fallback_;
  SourceUnits source_;
  QCheckBox *internal_, *automatic_;
  QDoubleSpinBox *custom_;
  QComboBox *unit_;
  QLabel *description_;
  QWidget *details_, *custom_row_;
};
}  // namespace bts
