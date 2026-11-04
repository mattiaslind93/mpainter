#pragma once
#include "FillProjection.hh"
#include <QWidget>
#include <QStringList>
#include <functional>
class QProgressBar;class QPushButton;class QLabel;class QSlider;class QComboBox;class QDoubleSpinBox;class QCheckBox;class QToolButton;
namespace bts {
struct TextureSet;class WarpPanel;class FillSymmetryPanel;class NativeMaterialPanel;class NativeMaterialImagePanel;
class FillPanel final : public QWidget {
 public:
  explicit FillPanel(QWidget *parent=nullptr);
  void synchronize(const FillProjection &);
  void set_anchor_context(const TextureSet *,const QUuid &);
  std::function<void(QUuid)> anchor_requested;
  void set_color_spaces(const QStringList &);
  std::function<void(const QString &)> color_space_changed;
  std::function<void(const FillProjection &)> changed;
  std::function<void()> edit_started,edit_finished;
  std::function<void(bool,int)> texture_requested;
  WarpPanel *warp_panel()const{return warp_;}
  void edit_projection(const FillProjection &p){emit_change(p);}
  int resource_channel()const{return resource_channel_;}
  void set_channels(const std::array<bool,15> &,const QStringList &);
  void set_displacement_midlevel(float value){displacement_midlevel_=value;}
  QWidget *channel_panel()const{return channel_panel_;}
  NativeMaterialImagePanel *native_image_panel()const;
  NativeMaterialPanel *native_material_panel()const{return native_panel_;}
  void register_uniform_controls(int,const std::vector<QWidget *> &);
  void set_fill_context(bool);
  void refresh_channel_visibility();
  void set_enabled_channels(const std::array<bool,15> &);
  void set_load_status(int,const QString &,int completed,int total,bool loading,bool error);
  void set_embedded(bool);
  void set_mask_mode();
  void set_legacy_clamp(bool);
  std::function<void(int)> wrap_edited;
  void set_material_mode_text(const QString &);
  void set_material_preview(const std::array<float,4> &color,float metallic,float roughness,const std::array<float,3> &normal);
  std::function<void()> material_requested;
  void reset_resource_channel(){resource_channel_=0;}
  std::function<void(int)> texture_cancelled;
  std::function<void(int,bool)> channel_enabled;
  std::function<void(int,const QStringList &)> files_dropped;
  std::function<void(int,const QString &)> asset_dropped;
 private:
  struct AnchorChoice {QUuid id;QString name,problem;bool mask=true;};
  std::vector<AnchorChoice> anchor_choices_;std::array<bool,15> anchor_channels_{};QStringList anchor_channel_names_;
  std::array<QWidget *,15> anchor_options_{};std::array<QComboBox *,15> anchor_source_channels_{},anchor_components_{};std::array<QCheckBox *,15> anchor_coverage_{};std::array<QPushButton *,15> anchor_jump_{};std::array<QLabel *,15> anchor_warning_{};
  void refresh_anchor_controls();QString anchor_name(const QUuid &)const;
  NativeMaterialPanel *native_panel_=nullptr;
  FillProjection state_;
  bool color_channel_visible_=true,fill_context_=true;
  QWidget *channel_panel_=nullptr;
  std::array<QWidget *,15> channel_rows_{},uniform_rows_{};
  std::array<std::vector<QWidget *>,15> uniform_controls_;
  std::array<QLabel *,15> channel_info_{},channel_status_{};
  std::array<QProgressBar *,15> channel_progress_{};
  std::array<QPushButton *,15> channel_cancel_{};
  std::array<QCheckBox *,15> channel_enabled_{},channel_alpha_{};
  float displacement_midlevel_=.5f;
  std::array<bool,15> active_channels_{};
  bool embedded_=false,mask_mode_=false;
  QString material_mode_text_;
  QWidget *tabs_=nullptr,*settings_page_=nullptr,*channel_toggles_=nullptr,*material_heading_=nullptr,*sphere_=nullptr;
  QToolButton *settings_tab_=nullptr,*material_tab_=nullptr,*material_mode_=nullptr,*warp_disclosure_=nullptr;
  QLabel *or_label_=nullptr;
  std::array<QToolButton *,15> channel_headers_{},channel_invert_{};
  std::array<QString,15> channel_titles_{},channel_shorts_{};
  void update_color_space_visibility();
  void layout_channel_toggles();
  void update_channel_headers();
  WarpPanel *warp_=nullptr;
  FillSymmetryPanel *symmetry_=nullptr;
  int resource_channel_=0;
  std::array<QWidget *,14> resource_rows_{};
  std::array<QComboBox *,14> channel_resources_{};
  std::array<QLabel *,14> resource_labels_{};
  QLabel *color_resource_label_=nullptr;
  QComboBox *filtering_=nullptr,*wrap_=nullptr,*crop_=nullptr;
  QWidget *planar_controls_=nullptr,*transforms_=nullptr,*filtering_label_=nullptr;
  QToolButton *uv_disclosure_=nullptr;
  QCheckBox *depth_culling_=nullptr,*backface_culling_=nullptr;
  QDoubleSpinBox *cylinder_angle_=nullptr,*depth_hardness_=nullptr,*backface_angle_=nullptr,*backface_hardness_=nullptr;
  std::array<QDoubleSpinBox *,2> scale_{},offset_{};
  std::array<QSlider *,2> scale_sliders_{};
  std::array<std::array<QDoubleSpinBox *,3>,3> spatial_{};
  std::array<std::array<QSlider *,3>,3> spatial_sliders_{};
  std::array<QWidget *,2> offset_rows_{};
  QComboBox *projection_=nullptr,*scale_mode_=nullptr;
  QCheckBox *custom_size_=nullptr;
  QWidget *physical_size_=nullptr;
  QLabel *asset_size_=nullptr;
  std::array<QDoubleSpinBox *,3> size_cm_{};
  QLabel *wrap_label_=nullptr;
  QWidget *hardness_row_=nullptr,*spatial_controls_=nullptr;
  QSlider *hardness_slider_=nullptr;
  QDoubleSpinBox *hardness_=nullptr;
  QCheckBox *linked3_=nullptr;
  QToolButton *spatial_disclosure_=nullptr;
  QDoubleSpinBox *rotation_=nullptr,*width_=nullptr;
  QCheckBox *linked_=nullptr,*invert_=nullptr,*border_=nullptr,*middle_=nullptr;
  QComboBox *resource_=nullptr,*color_space_=nullptr;
  QWidget *color_space_header_=nullptr,*color_space_group_=nullptr;
  QToolButton *manipulator_=nullptr,*color_space_disclosure_=nullptr;
  QWidget *parameters_=nullptr;
  void emit_change(FillProjection);
};
}
