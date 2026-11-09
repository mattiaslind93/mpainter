#pragma once
#include "BlendLibrary.hh"
#include <QWidget>
#include <functional>

class QLineEdit;
class QLabel;

namespace bts {
class BlendLibraryPanel final : public QWidget {
 public:
  BlendLibraryPanel(QLineEdit *, const BlendMeshSelection &, QWidget *, std::string fallback = {});
  BlendMeshSelection options() const;
  std::function<void()> changed;

 private:
  QLineEdit *path_;
  QLabel *summary_;
  std::string fallback_, source_;
  BlendMeshSelection selection_;
  void refresh_path();
  void refresh_summary();
  void choose();
};
}  // namespace bts
