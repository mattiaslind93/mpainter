#pragma once
#include "FillSymmetry.hh"
#include <QWidget>
#include <functional>
class QCheckBox;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
namespace bts {
class FillSymmetryPanel final : public QWidget {
 public:
  explicit FillSymmetryPanel(QWidget *);
  void synchronize(const std::shared_ptr<const FillSymmetry> &, bool supported);
  std::function<void(std::shared_ptr<const FillSymmetry>)> changed;

 private:
  std::shared_ptr<const FillSymmetry> state_;
  QCheckBox *enabled_, *flip_u_, *flip_v_;
  QComboBox *mode_, *axis_;
  QSpinBox *copies_;
  QDoubleSpinBox *offset_;
  std::array<QDoubleSpinBox *, 3> center_{};
  QWidget *options_;
  void publish();
};
}  // namespace bts
