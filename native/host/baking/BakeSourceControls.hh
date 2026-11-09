// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BakeSource.hh"
#include "NumericLocale.hh"
#include <QCheckBox>
#include <QFormLayout>
#include <QSignalBlocker>
namespace bts {
class BakeSourceControls final : public QWidget {
  QCheckBox *specific_;
  NumericSpinBox *frame_, *scale_;

 public:
  std::function<void()> changed;
  explicit BakeSourceControls(QWidget *parent, const QString &prefix, const QJsonObject &recipe)
      : QWidget(parent)
  {
    auto *layout = new QFormLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    specific_ = new QCheckBox("Use specific source frame", this);
    specific_->setObjectName(prefix + "SourceFrameEnabled");
    specific_->setToolTip(
        "Off: use the first authored frame in each ABC/USD file. On: freeze high-poly and cage at the selected frame.");
    layout->addRow(specific_);
    frame_ = new NumericSpinBox(this);
    frame_->setObjectName(prefix + "SourceFrame");
    frame_->setRange(-1e6, 1e6);
    frame_->setDecimals(3);
    layout->addRow("Source frame", frame_);
    scale_ = new NumericSpinBox(this);
    scale_->setObjectName(prefix + "SourceScale");
    scale_->setRange(1e-6, 1e6);
    scale_->setDecimals(6);
    scale_->setToolTip(
        "Additional uniform high-poly/cage scale in project coordinates. USD metadata units are converted by Blender first. For centimetre ABC coordinates in a metre project use 0.01.");
    layout->addRow("Source scale", scale_);
    synchronize(recipe);
    QObject::connect(specific_, &QCheckBox::toggled, this, [this](bool checked) {
      frame_->setEnabled(checked);
      if (changed)
        changed();
    });
    for (auto *spin : {frame_, scale_})
      QObject::connect(spin, &QDoubleSpinBox::editingFinished, this, [this] {
        if (changed)
          changed();
      });
  }
  BakeSourceOptions options() const
  {
    BakeSourceOptions value;
    value.scale = scale_->value();
    if (specific_->isChecked())
      value.frame = frame_->value();
    value.validate();
    return value;
  }
  void synchronize(const QJsonObject &recipe)
  {
    const auto value = bake_source_options(recipe);
    const QSignalBlocker a(specific_), b(frame_), c(scale_);
    specific_->setChecked(value.frame.has_value());
    if (!frame_->hasFocus())
      frame_->setValue(value.frame.value_or(1));
    if (!scale_->hasFocus())
      scale_->setValue(value.scale);
    frame_->setEnabled(value.frame.has_value());
  }
};
}  // namespace bts
