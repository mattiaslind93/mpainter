// SPDX-License-Identifier: GPL-3.0-or-later
#include "NumericLocale.hh"
#pragma once
#include "BrushStencil.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class StencilPanel final : public QWidget {
 public:
  explicit StencilPanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("stencilProperties");
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        0, 0, 0, 0); /* The heading belongs to the PropertySection around this panel. */
    auto *form = new QFormLayout;
    tiling_ = new QComboBox(this);
    tiling_->setObjectName("stencilTiling");
    tiling_->addItems({"No Tiling", "Horizontal Tiling", "Vertical Tiling", "H and V Tiling"});
    form->addRow("Tiling mode", tiling_);
    layout->addLayout(form);
    connect(tiling_, &QComboBox::currentIndexChanged, this, [this](int value) {
      settings_.tiling = value;
      publish();
    });
    resource_ = new QToolButton(this);
    resource_->setObjectName("stencilResource");
    resource_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    resource_->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(resource_);
    resource_->setMenu(menu);
    layout->addWidget(resource_);
    clear_ = menu->addAction("No Resource Selected");
    clear_->setObjectName("clearStencil");
    connect(clear_, &QAction::triggered, this, [this] {
      settings_.resource.reset();
      publish();
    });
    auto *plus = menu->addAction("Sign Plus");
    plus->setObjectName("stencilSignPlus");
    connect(plus, &QAction::triggered, this, [this] {
      BrushAlpha alpha;
      alpha.kind = AlphaKind::Plus;
      settings_.resource = alpha;
      publish();
    });
    auto *import = menu->addAction("Import image…");
    import->setObjectName("importStencilImage");
    connect(import, &QAction::triggered, this, [this] {
      if (import_requested)
        import_requested();
    });
    controls_ = new QWidget(this);
    auto *parameters = new QFormLayout(controls_);
    parameters->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 2; ++i) {
      offset_[i] = new bts::NumericSpinBox(controls_);
      offset_[i]->setObjectName(i ? "stencilOffsetY" : "stencilOffsetX");
      offset_[i]->setRange(-10, 10);
      offset_[i]->setDecimals(3);
      offset_[i]->setSingleStep(.05);
      offset_[i]->setKeyboardTracking(false);
      parameters->addRow(i ? "Offset Y" : "Offset X", offset_[i]);
      connect(offset_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
        settings_.offset[i] = v;
        publish();
      });
    }
    scale_ = new bts::NumericSpinBox(controls_);
    scale_->setObjectName("stencilScale");
    scale_->setRange(.01, 100);
    scale_->setDecimals(3);
    scale_->setSingleStep(.05);
    scale_->setKeyboardTracking(false);
    parameters->addRow("Scale", scale_);
    connect(scale_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
      settings_.scale = v;
      publish();
    });
    angle_ = new bts::NumericSpinBox(controls_);
    angle_->setObjectName("stencilAngle");
    angle_->setRange(-180, 180);
    angle_->setDecimals(1);
    angle_->setKeyboardTracking(false);
    parameters->addRow("Rotation", angle_);
    connect(angle_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
      settings_.angle = v;
      publish();
    });
    overlay_ = new QCheckBox("Show stencil", controls_);
    overlay_->setObjectName("stencilOverlay");
    parameters->addRow(overlay_);
    connect(overlay_, &QCheckBox::toggled, this, [this](bool v) {
      settings_.overlay = v;
      publish();
    });
    auto *reset = new QToolButton(controls_);
    reset->setObjectName("stencilReset");
    reset->setText("Reset transformation");
    parameters->addRow(reset);
    connect(reset, &QToolButton::clicked, this, [this] {
      settings_.offset = {};
      settings_.scale = 1;
      settings_.angle = 0;
      publish();
    });
    layout->addWidget(controls_);
    synchronize({});
  }
  std::function<void(const BrushStencil &)> changed;
  std::function<void()> import_requested;
  std::function<bool(QUuid)> asset_requested;
  void synchronize(const BrushStencil &settings)
  {
    settings_ = settings;
    const QSignalBlocker a(tiling_), b(scale_), c(angle_), d(overlay_), e(offset_[0]),
        f(offset_[1]);
    tiling_->setCurrentIndex(settings.tiling);
    resource_->setText(
        settings.resource ?
            QString("Stencil\n%1").arg(QString::fromStdString(settings.resource->name())) :
            "Stencil\nNo Resource Selected");
    controls_->setVisible(bool(settings.resource));
    clear_->setEnabled(bool(settings.resource));
    scale_->setValue(settings.scale);
    angle_->setValue(settings.angle);
    overlay_->setChecked(settings.overlay);
    for (int i = 0; i < 2; ++i)
      offset_[i]->setValue(settings.offset[i]);
  }

 protected:
  void dragEnterEvent(QDragEnterEvent *event) override
  {
    if (event->mimeData()->hasFormat("application/x-bts-asset"))
      event->acceptProposedAction();
  }
  void dropEvent(QDropEvent *event) override
  {
    const QUuid id(QString::fromUtf8(event->mimeData()->data("application/x-bts-asset")));
    if (!id.isNull() && asset_requested && asset_requested(id))
      event->acceptProposedAction();
  }

 private:
  BrushStencil settings_;
  QComboBox *tiling_;
  QToolButton *resource_;
  QAction *clear_;
  QWidget *controls_;
  std::array<QDoubleSpinBox *, 2> offset_;
  QDoubleSpinBox *scale_, *angle_;
  QCheckBox *overlay_;
  void publish()
  {
    settings_.validate();
    if (changed)
      changed(settings_);
  }
};
}  // namespace bts
