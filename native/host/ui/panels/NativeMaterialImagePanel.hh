// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialImageVariant.hh"
#include <QWidget>
#include <functional>
class QComboBox;
class QLabel;
class QPushButton;
namespace bts {
// Original nested image links, separate from editable top-group parameters and
// ordinary fill textures. All inspection/preparation is delegated to workers.
class NativeMaterialImagePanel final : public QWidget {
 public:
  explicit NativeMaterialImagePanel(QWidget *parent = nullptr);
  void loading(const QString &source);
  void synchronize(std::shared_ptr<const NativeImageBindingAvailability>, bool can_reset);
  void set_color_spaces(const QStringList &);
  void set_busy(bool, const QString &message = {});
  void error(const QString &);
  void source_context(const QString &resource,
                      bool native,
                      NativeHeightPolicy policy = NativeHeightPolicy::None);
  void target_context(const QString &token);
  std::function<void(const NativeImageBinding &, bool file, const NativeImageReplacementPolicy &)>
      requested;
  std::function<void()> reset_requested, cancel_requested, source_changed, target_changed;

 private:
  void refresh();
  void choose(bool file);
  std::shared_ptr<const NativeImageBindingAvailability> availability_;
  QComboBox *slot_, *space_;
  QLabel *source_, *status_;
  QPushButton *file_, *shelf_, *reset_, *cancel_;
  QStringList spaces_;
  bool busy_ = false, can_reset_ = false;
};
}  // namespace bts
