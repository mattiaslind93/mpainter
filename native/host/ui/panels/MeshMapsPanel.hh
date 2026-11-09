#pragma once
#include "TextureDocument.hh"
#include <QWidget>
#include <functional>
class QTreeWidget;
class QPushButton;
namespace bts {
class MeshMapsPanel final : public QWidget {
 public:
  explicit MeshMapsPanel(QWidget *parent = nullptr);
  void synchronize(const TextureSet *);
  std::function<void()> bake;
  std::function<void(int)> remove, inspect;

 private:
  QTreeWidget *maps_ = nullptr;
  QPushButton *bake_ = nullptr;
  QString signature_;
};
}  // namespace bts
