#pragma once
#include "SkewField.hh"
#include <QWidget>
#include <QJsonObject>
#include <functional>
#include <array>
#include <QUuid>
class QToolButton;class QComboBox;class QCheckBox;class QDoubleSpinBox;class QSpinBox;class QLineEdit;class QLabel;class QPushButton;
namespace bts {
class BakeSourceControls;
class SkewPanel final:public QWidget {
 public:
 explicit SkewPanel(QWidget *parent=nullptr);
 void synchronize(const QJsonObject &recipe,const SkewSettings &,bool editing,bool auto_normal,QUuid set={},int auto_role=-1,int preview=-1,int set_count=1,const QString &set_name={});
 void progress(bool running,const QString &message);
 std::function<void()> paint_mode,configure,paint_skew,rebake,bake_selected;
 std::function<void(const QJsonObject &)> recipe_changed;
 std::function<void(const SkewSettings &)> settings_changed;
 std::function<void(float)> value_changed;
 std::function<void(bool)> auto_changed;
 std::function<void(int)> auto_role_changed,bake_map,preview_map,sync_map;
 std::function<void(int,const QJsonObject &)> quality_changed;
 private:
 BakeSourceControls *source_controls_;
 bool updating_=false,previous_editing_=false;QJsonObject recipe_;SkewSettings settings_;
 QUuid set_;
 std::array<QCheckBox *,7> map_selected_{};
 std::array<QToolButton *,7> map_preview_{},map_bake_{},map_auto_{},map_sync_{};
 QComboBox *quality_preset_;QCheckBox *ignore_backface_,*denoise_;
 QComboBox *quality_role_,*samples_,*width_,*height_,*common_samples_=nullptr,*common_rays_preset_=nullptr;QSpinBox *dilation_,*rays_,*common_rays_=nullptr;QCheckBox *diffusion_,*locked_;
 QLineEdit *cage_path_;
 QLineEdit *high_;QCheckBox *project_,*cage_,*average_,*relative_,*enabled_,*edge_,*vectors_,*automatic_;
 QDoubleSpinBox *front_,*rear_,*distance_,*contrast_,*length_,*opacity_,*value_;QSpinBox *density_;
 QLabel *reason_,*status_,*context_,*selected_count_;QPushButton *paint_,*rebake_,*bake_selected_;QToolButton *skew_section_;
 void emit_recipe();void emit_settings();void refresh_quality();void emit_quality();
};
}
