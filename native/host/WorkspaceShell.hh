// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ThumbnailMetadata.hh"
#include "BakedMapPresentation.hh"
#include "HistogramMetadata.hh"

#include "ViewLayout.hh"
#include "TextureDocument.hh"
#include "BrushSettings.hh"
#include "Symmetry.hh"
#include "AssetCatalog.hh"
#include "OrbitCamera.hh"

#include <functional>

#include <QByteArray>
#include <QMainWindow>
#include <QAction>
#include <QToolButton>
#include <QStringList>
#include <QSet>
#include <QMap>
#include <QVariant>

class QDialog;
class QAction;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QListWidget;
class QTreeWidget;
class QPlainTextEdit;
class QShowEvent;
class QSlider;
class QSpinBox;
class QVBoxLayout;
class QProgressBar;
class QPushButton;
class QLabel;
class QLineEdit;
class QToolButton;
class QSplitter;
class QTabWidget;

namespace bts {
class PropertySection;
class NativeMaterialPanel;
class LayerThumbnailJobs;
class DisplacementPanel;
class ColorPicker;
class BrushPreview;
class StencilPanel;
class ProjectionPanel;
class ImportedEffectPanel;
class EnvironmentPanel;
class LevelsPanel;
class ColorSelectionPanel;
class MaskFillPanel;
class MaskGeneratorPanel;
class CompareMaskPanel;
class FilterPanel;
class AnchorPanel;
class GeometryMaskPanel;
class LayerInstancePanel;
class AlphaPanel;
class FillPanel;
class MeshMapsPanel;
class NativeMaterialImagePanel;

int capture_ac_panels(const QString &directory);
class WorkspaceShell final : public QMainWindow {
 public:
  void set_baking_tools(bool baking, bool editing, const BrushSettings &brush);
  void set_path_active(bool active)
  {
    path_active_ = active;
    path_tool_->setChecked(active);
  }
  void set_quick_mask_state(bool enabled, bool editing)
  {
    quick_mask_edit_ = editing;
    quick_mask_enable_->setChecked(enabled);
    quick_mask_edit_action_->setEnabled(enabled);
    quick_mask_edit_action_->setChecked(editing);
    quick_mask_invert_->setEnabled(enabled);
    quick_mask_clear_->setEnabled(enabled);
  }
  struct Callbacks {
    std::function<void(bool)> displacement_channel_changed, displacement_brush_changed;
    std::function<void(double)> displacement_value_changed;
    std::function<void(const DisplacementSettings &)> displacement_changed;
    std::function<void(bool)> tool_selected;
    std::function<void(const SymmetrySettings &)> symmetry_changed;
    std::function<void(bool)> trackball_changed;
    std::function<void(const BrushAlpha &)> alpha_selected;
    std::function<void(const DynamicStrokeParameters &)> dynamic_stroke_changed;
    std::function<void(int, bool, double)> brush_projection_changed;
    std::function<void()> alpha_import_requested, save_brush_preset;
    std::function<void(const BrushSettings &, std::function<void(QImage)>)>
        brush_preview_requested;
    std::function<void(const QString &, const QUuid &)> brush_preset_action;
    std::function<void(bool, double, double)> alpha_parameters_changed;
    std::function<void(double)> roughness_changed;
    std::function<void()> roughness_started, roughness_finished;
    std::function<void(double)> metallic_changed;
    std::function<void(int, double)> base_color_changed;
    std::function<void()> color_started, color_finished, color_pick_requested,
        secondary_pick_requested;
    std::function<void(const std::array<float, 4> &)> color_changed, secondary_color_changed;
    std::function<std::uint64_t(const TextureSet &, const ThumbnailRequest &)> thumbnail_requested;
    std::function<std::optional<ThumbnailMetadata>()> thumbnail_result;
    std::function<std::uint64_t(const TextureSet &, const HistogramRequest &)> histogram_requested;
    std::function<std::optional<HistogramMetadata>(const QUuid &)> histogram_result;
    std::function<void()> add_fill;
    std::function<void(int)> add_levels, add_filter_effect;
    std::function<void()> add_mask_anchor;
    std::function<void(QUuid)> geometry_selected, copy_geometry, paste_geometry;
    std::function<void(QUuid, const QString &, bool)> geometry_mesh_changed;
    std::function<void(QUuid, bool)> geometry_all_changed;
    std::function<void(bool)> hide_excluded_changed;
    std::function<void()> copy_layer_reference, paste_layer_instance;
    std::function<void(QUuid)> instance_selected;
    std::function<void(QUuid, QUuid)> instance_navigate;
    std::function<void(QUuid, int)> geometry_type_changed;
    std::function<void(QUuid, int, bool)> geometry_tile_changed;
    std::function<void(QUuid)> anchor_requested;
    std::function<void(QUuid, const QString &)> anchor_renamed;
    std::function<void()> add_color_selection, add_mask_fill, add_mask_generator, add_compare_mask,
        add_paint_effect;
    std::function<void(int, int)> generator_input_requested;
    std::function<void(int)> move_effect;
    std::function<void(QUuid)> copy_mask, paste_mask, create_smart_mask, create_smart_material,
        create_folder_material, remove_mask;
    std::function<void(const MaskEffect &)> mask_effect_changed;
    std::function<void(float, LayerBlend)> paint_effect_mix_changed;
    std::function<void(const FilterSettings &, int, float, LayerBlend)> filter_changed;
    std::function<void(bool)> id_pick_changed;
    std::function<void(QUuid, QUuid)> effect_selected, effect_removed;
    std::function<void(const std::vector<std::pair<QUuid, QUuid>> &,
                       const std::vector<QUuid> &,
                       const std::vector<QUuid> &)>
        delete_selection;
    std::function<void(QUuid, QUuid, bool)> effect_visibility_changed;
    std::function<void(const LevelsEffect &)> effect_changed;
    std::function<void(QUuid, int)> effect_component_selected;
    std::function<void()> scattering_added;
    std::function<void(int, bool)> user_channel_changed, user_brush_changed;
    std::function<void(int, const QString &)> user_renamed;
    std::function<void(int, int)> user_precision_changed;
    std::function<void(int, double)> user_value_changed;
    std::function<void(int, int)> channel_precision_changed;
    std::function<void(int)> tile_selected;
    std::function<void(std::uint32_t, int, bool)> residency_changed;
    std::function<void(int, int)> tile_resolution_changed;
    std::function<void(int)> texture_resolution_changed;
    std::function<void(int)> texture_height_changed;
    std::function<void(bool)> texture_size_linked;
    std::function<void()> add_paint;
    std::function<void()> add_adjustment;
    std::function<void(QUuid, bool)> layer_adjustment_changed;
    std::function<void()> add_folder;
    std::function<void()> group_layers;
    std::function<void(QUuid)> material_id_requested;
    std::function<void()> flatten_layer;
    std::function<void(bool)> precompose_layer;
    std::function<void()> release_precompose;
    std::function<void(bool)> export_flattened;
    std::function<void(float)> add_mask;
    std::function<void()> add_bitmap_mask;
    std::function<void(QUuid, bool)> mask_selected;
    std::function<void(QUuid)> mask_bypassed;
    std::function<void(QUuid)> mask_isolated;
    std::function<void(QUuid)> mask_background_inverted;
    std::function<void(double)> mask_value_changed;
    std::function<void(double)> brush_size_changed;
    std::function<void(double)> brush_flow_changed;
    std::function<void(double)> brush_opacity_changed;
    std::function<void(double)> brush_spacing_changed;
    std::function<void(int, double)> brush_jitter_changed;
    std::function<void(double)> brush_angle_changed;
    std::function<void(bool)> brush_follow_path_changed;
    std::function<void()> projection_selected, smudge_selected, clone_selected, path_selected;
    std::function<void(bool)> clone_mode_changed;
    std::function<void(int)> quick_mask_action;
    std::function<void(const ProjectionMaterial &)> projection_material_changed;
    std::function<void(bool)> projection_resource_selected;
    std::function<void(const BrushStencil &)> stencil_changed;
    std::function<void()> stencil_import_requested;
    std::function<bool(QUuid)> stencil_asset_requested;
    std::function<void(bool, double)> lazy_mouse_changed;
    std::function<void(bool)> picker_changed;
    std::function<void()> apply_smart_material;
    std::function<void()> bake_mesh_maps;
    std::function<void(int)> remove_mesh_map, inspect_mesh_map;
    std::function<void()> resource_import_requested, shelf_preferences_requested;
    std::function<void(const QStringList &)> texture_assets_import_requested;
    std::function<void(const StudioAsset &)> asset_selected, asset_export_requested;
    std::function<void(const StudioAsset &, QUuid, bool, bool)> asset_dropped;
    std::function<void(const FillProjection &)> fill_projection_changed;
    std::function<void(std::vector<int>)> warp_selection_changed;
    std::function<void(int)> warp_split_requested;
    std::function<void(bool)> warp_space_changed;
    std::function<void(const QString &)> property_edit_started;
    std::function<void()> property_edit_finished;
    std::function<void()> fill_texture_file_requested, layer_texture_file_requested,
        cancel_texture_loads;
    std::function<std::vector<BakedShelfItem>()> baked_maps;
    std::function<StudioAsset(const QString &)> baked_asset;
    std::function<void(int)> fill_texture_cancelled;
    std::function<void(int, bool)> fill_channel_enabled;
    std::function<void(int, const QStringList &)> fill_texture_files_dropped;
    std::function<void(int, const QString &)> fill_texture_asset_dropped;
    std::function<void(const QStringList &)> layer_texture_files_dropped;
    std::function<void(const QString &)> fill_color_space_changed;
    std::function<void(bool)> polygon_tool_changed;
    std::function<void(int, double)> polygon_fill_changed;
    std::function<void(int, bool)> brush_pressure_changed;
    std::function<void(int, double)> brush_minimum_changed;
    std::function<void(int, bool)> brush_channel_changed;
    std::function<void(QUuid, QUuid, const QString &)> layer_item_renamed;
    std::function<void(std::uint32_t, const QString &)> texture_set_renamed;
    std::function<void(QUuid, const QString &)> asset_renamed;
    std::function<void(QUuid)> layer_selected;
    std::function<void()> layer_context_changed;
    std::function<void()> layer_selection_changed;
    std::function<void(QUuid, int, double)> layer_opacity_changed;
    std::function<void(QUuid, int, LayerBlend)> layer_blend_changed;
    std::function<void(QUuid, bool)> layer_visibility_changed;
    std::function<void(QUuid, int)> layer_moved;
    std::function<void(QUuid, int)> layer_dropped;
    std::function<void(const std::vector<QUuid> &, QUuid, bool, bool)> layer_hierarchy_dropped;
    std::function<void(int, bool)> channel_changed;
    std::function<void(bool)> padding_changed;
    std::function<void(int)> normal_mixing_changed;
    std::function<void(int)> bump_method_changed;
    std::function<void(int)> ao_mixing_changed;
    std::function<void(double)> ao_changed;
    std::function<void(double)> bump_changed;
    std::function<void(int, double)> normal_changed;
    std::function<void(const QString &)> display_transform_changed;
    std::function<void(const QString &, double)> environment_changed;
    std::function<void()> environment_resource_requested;
    std::function<void(int, const QString &)> texture_set_selected;
    std::function<void(ViewLayoutState)> view_layout_changed;
    std::function<void(OrbitCamera::Projection)> projection_changed;
    std::function<void(bool)> reference_image_toggled;
    std::function<void()> open_reference_image_requested;
  };

  // Takes QWidget ownership of a non-null viewport. Keep the renderer alive until the window is
  // destroyed. Callback captures must remain valid; callbacks run synchronously on the GUI thread.
  explicit WorkspaceShell(QWidget *viewport, Callbacks callbacks = {}, QWidget *parent = nullptr);
  ~WorkspaceShell() override;
  // Terminal only: block nested editor signals before any hide/focus change.
  // Does not commit pending text or invoke application callbacks.
  void retire_editor_callbacks() noexcept;
  std::function<bool()> close_requested;
  void closeEvent(QCloseEvent *) override;

  // These synchronize caller-owned state without invoking callbacks.
  void set_texture_sets(const QStringList &names, int selected_index = 0);
  void set_roughness(double value);
  void set_roughness_editable(bool editable, const QString &reason = {});
  void set_view_layout(ViewLayoutState layout);
  void set_projection(OrbitCamera::Projection projection);
  void set_reference_image_enabled(bool enabled);
  void set_reference_image_path(const QString &path);
  void show_texture_set(const TextureSet *set,
                        const NewProjectOptions *settings,
                        const BrushSettings *brush = nullptr);
  void set_environment(const QJsonObject &state);
  void set_symmetry(const SymmetrySettings &);
  void set_trackball(bool enabled);
  void set_picker_active(bool);
  void set_id_picker_active(bool);
  void set_mask_clipboard_available(bool ready);
  void set_instance_hierarchy_target(QUuid set = {}, QUuid layer = {})
  {
    instance_hierarchy_set_ = set;
    instance_hierarchy_layer_ = layer;
  }
  void set_instance_clipboard_available(bool ready)
  {
    instance_clipboard_available_ = ready;
  }
  void set_geometry_clipboard_available(bool ready)
  {
    geometry_clipboard_available_ = ready;
  }
  void set_polygon_fill(bool active, int mode, double value);
  void add_alpha_resource(const BrushAlpha &alpha);
  void refresh_alpha_shelf();
  int layer_channel_context() const
  {
    const auto opacity = property("materialOpacityChannel");
    if (opacity.isValid())
      return opacity.toInt();
    return layer_channel_ >= 7 && layer_channel_ <= 14 ? layer_channel_ - 1 :
           layer_channel_ == 16                        ? 14 :
           layer_channel_ == 6                         ? 3 :
           layer_channel_ < 6                          ? layer_channel_ :
                                                         0;
  }
  void set_asset_catalog(AssetCatalog *);
  void set_texture_load_status(QUuid set,
                               QUuid layer,
                               int channel,
                               const QString &,
                               int completed,
                               int total,
                               bool loading,
                               bool error);
  void set_display_transforms(const QStringList &choices, const QString &selected);
  QComboBox *display_transform_control() const
  {
    return display_control_;
  }
  void set_status(const QString &message);
  void set_error_status(const QString &message);
  void append_console(const QString &message);

  void set_shelf_visible(bool visible);
  void show_texture_shelf();
  NativeMaterialImagePanel *native_image_panel() const;
  NativeMaterialPanel *native_material_panel() const;
  void set_console_visible(bool visible);
  void reset_reference_layout();
  void float_properties();
  void dock_properties();
  // Properties lives in the tab bar beside Layers and Texture Sets. Closing it
  // takes the tab away rather than leaving an empty page behind.
  void show_properties_tab();
  void hide_properties_tab();
  // Every section is registered so that one pass can settle them all.
  PropertySection *make_section(const QString &title, QWidget *parent, bool folded = false);
  // Shown only once a newer release has actually been confirmed; the row is
  // empty and hidden otherwise.
  void show_update_available(const QString &release, const QUrl &page);
  QByteArray save_layout() const;
  bool restore_layout(const QByteArray &state);

  QDoubleSpinBox *metallic_control() const
  {
    return metallic_control_;
  }
  QDoubleSpinBox *roughness_control() const
  {
    return roughness_control_;
  }
  QSlider *roughness_slider() const
  {
    return roughness_slider_;
  }
  QComboBox *view_layout_control() const
  {
    return view_layout_control_;
  }
  QComboBox *projection_control() const
  {
    return projection_control_;
  }
  int fill_resource_channel() const;
  void set_fill_color_spaces(const QStringList &);
  void edit_fill_projection(const FillProjection &);
  void set_warp_selection(std::vector<int>);
  void cancel_warp_split();
  bool layer_thumbnails_pending() const;
  QListWidget *layer_stack() const
  {
    return layers_;
  }
  int active_tile() const
  {
    return active_tile_;
  }
  void request_texture_residency(std::uint32_t uid, int tile, bool loaded)
  {
    if (callbacks_.residency_changed)
      callbacks_.residency_changed(uid, tile, loaded);
  }
  QListWidget *texture_sets() const
  {
    return texture_sets_;
  }
  QTabWidget *layer_settings_tabs() const
  {
    return layer_settings_tabs_;
  }
  QSplitter *workspace_splitter() const
  {
    return workspace_splitter_;
  }
  QSplitter *viewport_shelf_splitter() const
  {
    return viewport_shelf_splitter_;
  }
  QSplitter *inspector_splitter() const
  {
    return inspector_splitter_;
  }
  bool console_visible() const
  {
    return console_->isVisible();
  }

 protected:
  void showEvent(QShowEvent *event) override;

 private:
  std::unique_ptr<LayerThumbnailJobs> thumbnails_;
  Callbacks callbacks_;
  bool mask_clipboard_available_ = false, geometry_clipboard_available_ = false;
  QWidget *paint_effect_mix_ = nullptr;
  QDoubleSpinBox *paint_effect_opacity_ = nullptr;
  QComboBox *paint_effect_blend_ = nullptr;
  LevelsPanel *levels_panel_ = nullptr;
  ColorSelectionPanel *color_selection_panel_ = nullptr;
  MaskFillPanel *mask_fill_panel_ = nullptr;
  MaskGeneratorPanel *mask_generator_panel_ = nullptr;
  CompareMaskPanel *compare_mask_panel_ = nullptr;
  FilterPanel *filter_panel_ = nullptr;
  ImportedEffectPanel *imported_effect_panel_ = nullptr;
  AnchorPanel *anchor_panel_ = nullptr;
  GeometryMaskPanel *geometry_panel_ = nullptr;
  LayerInstancePanel *instance_panel_ = nullptr;
  QLabel *instance_empty_ = nullptr;
  QUuid instance_hierarchy_set_, instance_hierarchy_layer_;
  bool instance_clipboard_available_ = false;
  QAction *copy_instance_ = nullptr, *paste_instance_ = nullptr;
  QAction *effect_up_ = nullptr, *effect_down_ = nullptr;
  QWidget *property_content_ = nullptr;
  QToolButton *effects_button_ = nullptr;
  QSplitter *workspace_splitter_ = nullptr;
  QSplitter *viewport_shelf_splitter_ = nullptr;
  QSplitter *inspector_splitter_ = nullptr;
  QWidget *properties_slot_ = nullptr, *properties_panel_ = nullptr;
  PropertySection *content_section_ = nullptr, *blending_section_ = nullptr,
                  *projection_section_ = nullptr;
  std::vector<PropertySection *> property_sections_;
  QWidget *update_notice_ = nullptr;
  QLabel *update_label_ = nullptr;
  QUrl update_page_;
  QDialog *properties_float_ = nullptr;
  QAction *properties_action_ = nullptr;
  QToolButton *properties_undock_ = nullptr;
  QTabWidget *layer_settings_tabs_ = nullptr;
  EnvironmentPanel *environment_panel_ = nullptr;
  QTreeWidget *uv_tiles_ = nullptr;
  int active_tile_ = 0;
  QWidget *settings_general_ = nullptr, *settings_tile_ = nullptr;
  QComboBox *tile_resolution_ = nullptr;
  QListWidget *texture_sets_ = nullptr;
  QListWidget *layers_ = nullptr;
  std::array<QComboBox *, 6> channel_formats_{};
  QComboBox *resolution_ = nullptr, *resolution_height_ = nullptr;
  QToolButton *resolution_lock_ = nullptr;
  QComboBox *layer_context_ = nullptr;
  int layer_channel_ = 0;
  QDoubleSpinBox *roughness_control_ = nullptr;
  QDoubleSpinBox *metallic_control_ = nullptr;
  std::array<QDoubleSpinBox *, 3> color_controls_{};
  ColorPicker *color_picker_, *secondary_color_picker_;
  QWidget *secondary_color_row_;
  std::array<QWidget *, 5> channel_rows_{};
  std::array<QAction *, 5> channel_add_actions_{};
  QWidget *roughness_row_ = nullptr;
  QWidget *metallic_row_ = nullptr;
  QWidget *color_row_ = nullptr;
  QLabel *normal_label_ = nullptr;
  QWidget *normal_row_ = nullptr;
  std::array<QDoubleSpinBox *, 3> normal_values_{};
  QLabel *bump_label_ = nullptr;
  bool path_active_ = false;
  QToolButton *path_tool_ = nullptr;
  QAction *path_action_ = nullptr;
  bool quick_mask_edit_ = false;
  QAction *quick_mask_enable_ = nullptr, *quick_mask_edit_action_ = nullptr,
          *quick_mask_invert_ = nullptr, *quick_mask_clear_ = nullptr;
  QLabel *material_label_ = nullptr;
  QToolButton *fill_button_ = nullptr;
  QToolButton *paint_button_ = nullptr;
  QAction *add_adjustment_action_ = nullptr;
  QToolButton *folder_button_ = nullptr;
  QToolButton *mask_button_ = nullptr;
  QDoubleSpinBox *mask_value_ = nullptr;
  QWidget *mask_value_row_ = nullptr;
  QSet<QUuid> collapsed_folders_;
  QUuid displayed_set_;
  bool selection_refresh_pending_ = false;
  QToolButton *paint_tool_ = nullptr;
  QToolButton *eraser_tool_ = nullptr;
  bool polygon_active_ = false;
  QToolButton *polygon_tool_ = nullptr;
  QAction *polygon_action_ = nullptr;
  QWidget *brush_toolbar_ = nullptr, *polygon_toolbar_ = nullptr, *polygon_controls_ = nullptr;
  std::array<std::array<QToolButton *, 5>, 2> polygon_modes_{};
  std::array<QDoubleSpinBox *, 2> polygon_values_{};
  std::array<QSlider *, 2> polygon_sliders_{};
  QToolButton *picker_tool_ = nullptr;
  QAction *picker_action_ = nullptr;
  QAction *paint_tool_action_ = nullptr, *eraser_tool_action_ = nullptr;
  QWidget *brush_controls_ = nullptr;
  ProjectionPanel *projection_panel_ = nullptr;
  QToolButton *projection_tool_ = nullptr;
  QAction *projection_action_ = nullptr;
  QToolButton *clone_tool_ = nullptr;
  QAction *clone_action_ = nullptr, *clone_relative_ = nullptr, *clone_absolute_ = nullptr;
  QToolButton *smudge_tool_ = nullptr;
  QAction *smudge_action_ = nullptr;
  bool asset_projection_ready_ = false;
  StencilPanel *stencil_panel_ = nullptr;
  BrushPreview *brush_preview_ = nullptr;
  QDoubleSpinBox *brush_angle_ = nullptr;
  QToolButton *brush_follow_path_ = nullptr;
  std::array<QDoubleSpinBox *, 4> brush_jitter_{};
  std::array<QSlider *, 4> brush_jitter_sliders_{};
  QDoubleSpinBox *brush_size_ = nullptr;
  std::array<QToolButton *, 2> brush_pressure_{}, toolbar_pressure_{};
  std::array<QDoubleSpinBox *, 2> brush_minimum_{};
  QToolButton *lazy_toggle_ = nullptr;
  QAction *lazy_action_ = nullptr;
  QWidget *lazy_controls_ = nullptr;
  QDoubleSpinBox *lazy_distance_ = nullptr;
  QSlider *lazy_slider_ = nullptr;
  QDoubleSpinBox *brush_spacing_ = nullptr, *toolbar_spacing_ = nullptr;
  QSlider *brush_spacing_slider_ = nullptr, *toolbar_spacing_slider_ = nullptr;
  QDoubleSpinBox *brush_opacity_ = nullptr, *toolbar_opacity_ = nullptr;
  QSlider *brush_opacity_slider_ = nullptr, *toolbar_opacity_slider_ = nullptr;
  QDoubleSpinBox *brush_flow_ = nullptr, *toolbar_flow_ = nullptr;
  QSlider *brush_flow_slider_ = nullptr, *toolbar_flow_slider_ = nullptr;
  QDoubleSpinBox *toolbar_size_ = nullptr;
  QSlider *toolbar_size_slider_ = nullptr;
  std::array<QWidget *, custom_channel_count> user_rows_{}, user_value_rows_{};
  std::array<QLineEdit *, custom_channel_count> user_names_{};
  std::array<QComboBox *, custom_channel_count> user_formats_{};
  QAction *scattering_add_ = nullptr;
  std::array<QAction *, custom_channel_count> user_add_{};
  std::array<QToolButton *, custom_channel_count> user_brush_{}, user_fill_enabled_{};
  std::array<QLabel *, custom_channel_count> user_value_labels_{};
  std::array<QDoubleSpinBox *, custom_channel_count> user_values_{};
  std::array<QSlider *, custom_channel_count> user_sliders_{};
  std::array<QToolButton *, 5> brush_channels_{};
  QToolButton *brush_ao_ = nullptr;
  QWidget *ao_channel_row_ = nullptr, *ao_value_row_ = nullptr;
  QAction *ao_add_action_ = nullptr;
  QComboBox *ao_mixing_ = nullptr;
  QDoubleSpinBox *ao_value_ = nullptr;
  QAction *move_layer_up_ = nullptr;
  QAction *move_layer_down_ = nullptr;
  QSlider *roughness_slider_ = nullptr;
  QComboBox *view_layout_control_ = nullptr;
  QComboBox *projection_control_ = nullptr;
  QCheckBox *reference_image_toggle_ = nullptr;
  QPushButton *open_reference_image_button_ = nullptr;
  QComboBox *display_control_ = nullptr;
  QComboBox *padding_control_ = nullptr, *normal_mixing_ = nullptr, *bump_method_ = nullptr;
  QDoubleSpinBox *bump_control_ = nullptr;
  ViewLayoutState view_layout_;
  QPlainTextEdit *console_text_ = nullptr;
  QWidget *shelf_ = nullptr;
  SymmetrySettings symmetry_;
  QToolButton *symmetry_toggle_ = nullptr, *symmetry_manipulator_ = nullptr;
  QCheckBox *trackball_toggle_ = nullptr;
  QComboBox *symmetry_mode_ = nullptr, *symmetry_axis_ = nullptr;
  QDoubleSpinBox *symmetry_offset_ = nullptr;
  std::array<QDoubleSpinBox *, 3> symmetry_center_{};
  QSpinBox *symmetry_copies_ = nullptr;
  QCheckBox *symmetry_intersection_ = nullptr;
  FillProjection displayed_fill_;
  QWidget *fill_toolbar_ = nullptr, *fill_spatial_toolbar_ = nullptr;
  QToolButton *warp_geometry_toggle_ = nullptr;
  QToolButton *fill_manipulator_ = nullptr;
  std::array<QToolButton *, 3> fill_gizmos_{};
  std::array<QAction *, 3> fill_gizmo_actions_{};
  QDoubleSpinBox *fill_gizmo_size_ = nullptr;
  MeshMapsPanel *mesh_maps_panel_ = nullptr;
  FillPanel *fill_panel_ = nullptr;
  QWidget *material_body_ = nullptr, *texture_loading_bar_ = nullptr;
  QVBoxLayout *material_layout_ = nullptr;
  QLabel *texture_loading_notice_ = nullptr;
  QProgressBar *texture_loading_progress_ = nullptr;
  QPushButton *texture_loading_cancel_ = nullptr;
  QUuid shown_texture_set_, shown_texture_layer_;
  QMap<QString, QVariantList> texture_load_states_;
  AlphaPanel *alpha_panel_ = nullptr;
  QComboBox *brush_alignment_ = nullptr;
  QCheckBox *brush_cull_ = nullptr;
  QDoubleSpinBox *brush_cull_angle_ = nullptr;
  QSlider *brush_cull_slider_ = nullptr;
  QListWidget *alpha_assets_ = nullptr, *shelf_categories_ = nullptr;
  QString alpha_search_;
  bool asset_project_ready_ = false, asset_paint_ready_ = false, asset_fill_ready_ = false,
       asset_smart_ready_ = false;
  QToolButton *smart_material_button_ = nullptr;
  AssetCatalog *asset_catalog_ = nullptr;
  int asset_scope_ = -1;
  QComboBox *asset_scope_control_ = nullptr;
  QLineEdit *asset_search_ = nullptr;
  QToolButton *asset_reset_ = nullptr, *asset_category_chip_ = nullptr;
  std::vector<BrushAlpha> alpha_resources_;
  QWidget *console_ = nullptr;
  QAction *shelf_action_ = nullptr;
  QAction *console_action_ = nullptr;
  DisplacementPanel *displacement_panel_ = nullptr;
  bool reference_layout_pending_ = true;
};

}  // namespace bts
