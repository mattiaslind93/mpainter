#pragma once
#include "UsdScene.hh"
#include <QWidget>
class QLineEdit;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
namespace bts {
class UsdScenePanel final : public QWidget {
 public:
  UsdScenePanel(QLineEdit *, const UsdImportOptions &, QWidget *, std::string fallback = {});
  UsdImportOptions options() const;
  void set_options(const UsdImportOptions &);

 private:
  std::string source() const;
  void refresh();
  void choose_scope();
  QLineEdit *path_;
  std::string fallback_, last_path_;
  UsdImportOptions options_;
  UsdSceneInfo info_;
  QLabel *scope_, *error_;
  QPushButton *change_;
  QSpinBox *subdivision_;
  QDoubleSpinBox *frame_;
  bool refreshing_ = false;
};
}  // namespace bts
