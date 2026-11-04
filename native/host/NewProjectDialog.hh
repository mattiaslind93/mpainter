#pragma once

#include <optional>
#include <string>

#include <QDialog>
#include "ProjectSettings.hh"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QShowEvent;

namespace bts {
class UsdScenePanel;
class BlendLibraryPanel;
class UnitsPanel;

// Collects project settings on the GUI thread without importing or changing any scene.
// These defaults are product choices, not a claim about Painter's remembered preferences.
class NewProjectDialog final : public QDialog {
 public:
  explicit NewProjectDialog(QWidget *parent = nullptr);

  // Present only after the latest interaction accepted a readable regular .obj file.
  // The path is absolute UTF-8. Native parsing and post-selection file changes remain
  // the importer's responsibility. No settings are returned on Cancel, Escape or close.
  std::optional<NewProjectOptions> options() const;
  void done(int result) override;

 protected:
  void showEvent(QShowEvent *event) override;

 private:
  void choose_mesh();
  std::optional<NewProjectOptions> validate_options();
  void update_uv_description();

  QLineEdit *mesh_path_ = nullptr;
  QPushButton *select_mesh_ = nullptr;
  QComboBox *template_ = nullptr;
  QComboBox *resolution_ = nullptr;
  QComboBox *normal_format_ = nullptr;
  UsdScenePanel *usd_=nullptr;
  BlendLibraryPanel *blend_=nullptr;
  UnitsPanel *units_=nullptr;
  AutoUnwrapOptions unwrap_options_;
  QCheckBox *auto_unwrap_=nullptr;
  QCheckBox *import_cameras_=nullptr;
  QCheckBox *tangent_per_fragment_=nullptr;
  QCheckBox *preserve_tiles_ = nullptr;
  QListWidget *utility_maps_=nullptr;
  QLabel *uv_description_ = nullptr;
  QLabel *validation_error_ = nullptr;
  QPushButton *ok_ = nullptr;
  std::optional<NewProjectOptions> accepted_options_;
};

}  // namespace bts
