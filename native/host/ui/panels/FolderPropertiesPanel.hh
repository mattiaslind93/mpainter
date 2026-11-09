// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "NumericLocale.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <functional>
namespace bts {
class FolderPropertiesPanel final : public QWidget {
  QUuid layer_;
  int channel_ = 0;
  QLabel *name_, *children_;
  QCheckBox *visible_;
  QDoubleSpinBox *opacity_;
  QComboBox *blend_;

 public:
  std::function<void(QUuid, bool)> visibility_changed;
  std::function<void(QUuid, int, double)> opacity_changed;
  std::function<void(QUuid, int, LayerBlend)> blend_changed;
  explicit FolderPropertiesPanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("folderProperties");
    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    name_ = new QLabel(this);
    name_->setWordWrap(true);
    name_->setObjectName("folderPropertiesName");
    form->addRow(name_);
    children_ = new QLabel(this);
    children_->setWordWrap(true);
    form->addRow(children_);
    visible_ = new QCheckBox("Visible", this);
    visible_->setObjectName("folderVisible");
    form->addRow(visible_);
    opacity_ = new NumericSpinBox(this);
    opacity_->setObjectName("folderOpacity");
    opacity_->setRange(0, 100);
    opacity_->setDecimals(0);
    opacity_->setSuffix(" %");
    opacity_->setKeyboardTracking(false);
    form->addRow("Opacity", opacity_);
    blend_ = new QComboBox(this);
    blend_->setObjectName("folderBlending");
    for (int i = 0; i < int(layer_blend_names.size()); ++i)
      blend_->addItem(layer_blend_names[i], i);
    form->addRow("Blending", blend_);
    connect(visible_, &QCheckBox::toggled, this, [this](bool value) {
      if (visibility_changed)
        visibility_changed(layer_, value);
    });
    connect(opacity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      if (opacity_changed)
        opacity_changed(layer_, channel_, value / 100);
    });
    connect(blend_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (index >= 0 && blend_changed)
        blend_changed(layer_, channel_, LayerBlend(blend_->currentData().toInt()));
    });
  }
  void synchronize(const TextureSet *set, const TextureLayer *folder, int channel)
  {
    setVisible(set && folder);
    if (!set || !folder)
      return;
    layer_ = folder->id;
    channel_ = channel;
    name_->setText(QString::fromStdString(folder->name));
    int count = 0;
    bool inside = false;
    for (auto it = set->layers.rbegin(); it != set->layers.rend(); ++it) {
      if (it->id == folder->id) {
        inside = true;
        continue;
      }
      if (inside) {
        if (it->depth <= folder->depth)
          break;
        ++count;
      }
    }
    children_->setText(QString("%1 layer%2 in this group").arg(count).arg(count == 1 ? "" : "s"));
    const QSignalBlocker a(visible_), b(opacity_), c(blend_);
    visible_->setChecked(folder->visible);
    const auto opacity = channel == 14 ? folder->displacement.opacity :
                         channel >= 6  ? folder->users.at(channel - 6).opacity :
                         channel == 5  ? folder->ao_opacity :
                                         folder->opacity.at(channel);
    const auto blend = channel == 14 ? folder->displacement.blending :
                       channel >= 6  ? folder->users.at(channel - 6).blending :
                       channel == 5  ? folder->ao_blending :
                                       folder->blending.at(channel);
    opacity_->setValue(opacity * 100);
    blend_->setCurrentIndex(blend_->findData(int(blend)));
    setEnabled(set->uniform_editable && !folder->instance);
  }
};
}  // namespace bts
