#pragma once
#include "FillProjection.hh"
#include <QWidget>
#include <functional>
class QSpinBox;class QDoubleSpinBox;class QComboBox;class QCheckBox;class QLabel;
namespace bts {
class ColorPicker;
class WarpPanel final:public QWidget {
 public:
  explicit WarpPanel(QWidget *parent=nullptr);
  void synchronize(const FillProjection &);
  void set_selection(std::vector<int>);
  void cancel_split();
  std::function<void(const FillProjection &)> changed;
  std::function<void()> edit_started,edit_finished;
  std::function<void(std::vector<int>)> selection_changed;
  std::function<void(int)> split_requested;
  std::function<void(bool)> space_changed;
 private:
  FillProjection state_;std::vector<int> selected_;int point_width_=0,point_height_=0;
  QSpinBox *columns_,*rows_,*handle_;
  QDoubleSpinBox *depth_,*split_u_,*split_v_;
  QComboBox *edition_,*point_,*split_,*space_;
  QCheckBox *automatic_,*surface_;
  std::array<std::array<QDoubleSpinBox *,3>,3> vectors_{};
  QLabel *status_;ColorPicker *color_;
  void modify(const std::function<void(FillProjection &)> &,bool atomic=false);
};
}
