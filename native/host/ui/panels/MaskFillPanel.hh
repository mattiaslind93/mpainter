// SPDX-License-Identifier: GPL-3.0-or-later
#include "NumericLocale.hh"
#pragma once
#include "TextureDocument.hh"
#include "FillPanel.hh"
#include "FillGradient.hh"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <functional>
namespace bts {
// A mask is one scalar consumer of the ordinary fill editor. Mapping, resources,
// warp and symmetry belong to FillPanel; only the authored value is mask-specific.
class MaskFillPanel final : public QWidget {
 public:
  explicit MaskFillPanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("maskFillProperties");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    shared_ = new FillPanel(this);
    shared_->set_mask_mode();
    shared_->setObjectName("maskSpatialFillPanel");
    std::array<bool, 15> channels{};
    channels[0] = true;
    shared_->set_channels(channels, {"Grayscale"});
    for (auto *child : shared_->findChildren<QObject *>())
      if (!child->objectName().isEmpty() && !child->objectName().startsWith("qt_"))
        child->setObjectName("maskSpatial_" + child->objectName());
    const auto alias = [&](const char *from, const char *to) {
      if (auto *x = shared_->findChild<QObject *>(QString("maskSpatial_") + from))
        x->setObjectName(to);
    };
    alias("fillProjectionMode", "maskFillProjection");
    alias("fillScaleU", "maskFillScale0");
    alias("fillScaleV", "maskFillScale1");
    alias("fillUvRotation", "maskFillRotation");
    alias("fillOffset0", "maskFillOffset0");
    alias("fillOffset1", "maskFillOffset1");
    alias("fillFiltering", "maskFillFiltering");
    alias("fillWrap", "maskFillWrap");
    alias("fillScaleLock", "maskFillScaleLock");
    alias("fillResetUvTransforms", "maskFillResetMapping");
    alias("fillBaseColorResource", "maskFillResource");
    alias("fillBaseColorHeader", "maskFillResourceButton");
    alias("fillAnchorChannel0", "anchorSourceChannel");
    alias("fillAnchorComponent0", "anchorSourceComponent");
    alias("fillAnchorCoverage0", "anchorUseSourceCoverage");
    alias("fillGoToAnchor0", "goToMaskAnchor");
    alias("fillAnchorWarning0", "anchorReferenceWarning");
    auto *uniform = new QWidget(this);
    auto *values = new QVBoxLayout(uniform);
    values->setContentsMargins(0, 0, 0, 0);
    values->setSpacing(0);
    value_ = new bts::NumericSpinBox(uniform);
    value_->setObjectName("maskEffectFillValue");
    value_->setLocale(QLocale::c());
    value_->setRange(0, 1);
    value_->setDecimals(3);
    value_->setSingleStep(.01);
    value_->setKeyboardTracking(false);
    value_->setProperty("fillHideInFill", true);
    values->addWidget(value_);
    slider_ = new QSlider(Qt::Horizontal, uniform);
    slider_->setObjectName("maskFillValueSlider");
    slider_->setRange(0, 1000);
    slider_->setProperty("fillHideInFill", true);
    values->addWidget(slider_);
    auto *gradient = new FillGradient(slider_, value_, uniform);
    gradient->setObjectName("maskFillGradient");
    values->addWidget(gradient);
    shared_->register_uniform_controls(0, {uniform});
    layout->addWidget(shared_);
    auto *mix = new QFormLayout;
    mode_ = new QComboBox(this);
    mode_->setObjectName("maskEffectFillBlend");
    for (int i = 0; i < int(layer_blend_names.size()); ++i)
      mode_->addItem(layer_blend_names[i], i);
    mix->addRow("Blending", mode_);
    opacity_ = new bts::NumericSpinBox(this);
    opacity_->setObjectName("maskEffectFillOpacity");
    opacity_->setLocale(QLocale::c());
    opacity_->setRange(0, 100);
    opacity_->setSuffix(" %");
    opacity_->setKeyboardTracking(false);
    mix->addRow("Opacity", opacity_);
    layout->addLayout(mix);
    connect(
        slider_, &QSlider::valueChanged, value_, [this](int n) { value_->setValue(n / 1000.); });
    connect(value_, &QDoubleSpinBox::valueChanged, this, [this](double n) {
      {
        const QSignalBlocker b(slider_);
        slider_->setValue(qRound(n * 1000));
      }
      auto e = effect_;
      e.fill = n;
      publish(e);
    });
    connect(slider_, &QSlider::sliderPressed, this, [this] {
      if (edit_started)
        edit_started();
    });
    connect(slider_, &QSlider::sliderReleased, this, [this] {
      if (edit_finished)
        edit_finished();
    });
    connect(value_, &QDoubleSpinBox::editingFinished, this, [this] {
      if (edit_finished)
        edit_finished();
    });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0) {
        auto e = effect_;
        e.blending = LayerBlend(mode_->itemData(i).toInt());
        publish(e);
      }
    });
    connect(opacity_, &QDoubleSpinBox::valueChanged, this, [this](double n) {
      auto e = effect_;
      e.opacity = n / 100;
      publish(e);
    });
    shared_->changed = [this](const FillProjection &p) {
      auto e = effect_;
      e.mapping.projection = p;
      e.mapping.filtering = p.filtering == 2 ? 1 : 0;
      const auto ref = p.anchors[0];
      e.anchor_source = ref.source;
      e.anchor_channel = ref.channel;
      e.anchor_component = ref.source != effect_.anchor_source && !ref.source.isNull() &&
                                   ref.component == 0 ?
                               1 :
                               ref.component;
      e.anchor_coverage = ref.coverage;
      e.mapping.projection.anchors = {};
      publish(e);
    };
    shared_->wrap_edited = [this](int mode) { effect_.mapping.wrap = mode == 0 ? 0 : mode + 1; };
    shared_->edit_started = [this] {
      if (edit_started)
        edit_started();
    };
    shared_->edit_finished = [this] {
      if (edit_finished)
        edit_finished();
    };
    shared_->anchor_requested = [this](QUuid id) {
      if (anchor_requested)
        anchor_requested(id);
    };
    shared_->texture_requested = [this](bool file, int channel) {
      if (texture_requested)
        texture_requested(file, channel);
    };
    shared_->files_dropped = [this](int channel, const QStringList &files) {
      if (files_dropped)
        files_dropped(channel, files);
    };
    shared_->asset_dropped = [this](int channel, const QString &id) {
      if (asset_dropped)
        asset_dropped(channel, id);
    };
    shared_->texture_cancelled = [this](int channel) {
      if (texture_cancelled)
        texture_cancelled(channel);
    };
    shared_->color_space_changed = [this](const QString &space) {
      if (color_space_changed)
        color_space_changed(space);
    };
  }
  std::function<void(const MaskEffect &)> changed;
  std::function<void()> edit_started, edit_finished;
  std::function<void(QUuid)> anchor_requested;
  std::function<void(bool, int)> texture_requested;
  std::function<void(int, const QStringList &)> files_dropped;
  std::function<void(int, const QString &)> asset_dropped;
  std::function<void(int)> texture_cancelled;
  std::function<void(const QString &)> color_space_changed;
  WarpPanel *warp_panel() const
  {
    return shared_->warp_panel();
  }
  int resource_channel() const
  {
    return shared_->resource_channel();
  }
  void set_color_spaces(const QStringList &)
  {
    shared_->set_color_spaces({});
    if (auto *space = shared_->findChild<QComboBox *>("maskSpatial_fillTextureColorSpace")) {
      space->setItemText(0, "Non-Color/raw");
      space->setEnabled(false);
      space->setToolTip("Grayscale textures use their raw red channel.");
    }
  }
  void set_load_status(
      int channel, const QString &text, int completed, int total, bool loading, bool error)
  {
    shared_->set_load_status(channel, text, completed, total, loading, error);
  }
  void synchronize(const MaskEffect *effect, const TextureSet *set = nullptr)
  {
    setVisible(effect);
    if (!effect)
      return;
    effect_ = *effect;
    auto p = effect->mapping.projection;
    if (effect->mapping.filtering == 1)
      p.filtering = 2;
    if (p.mode == 0)
      p.wrap = effect->mapping.wrap < 2 ? 0 : effect->mapping.wrap - 1;
    if (set) {
      p.native_per_uv = set->native_per_uv;
      p.cm_per_native = set->cm_per_native;
    }
    p.anchors[0] = effect->anchor_reference();
    shared_->set_anchor_context(set, set ? set->selected_layer : QUuid{});
    shared_->synchronize(p);
    shared_->set_legacy_clamp(p.mode == 0 && effect->mapping.wrap == 1);
    if (auto *settings = shared_->findChild<QWidget *>("maskSpatial_fillSettingsPage")) {
      settings->setEnabled(effect->anchor_source.isNull());
      settings->setToolTip(effect->anchor_source.isNull() ?
                               QString{} :
                               QString("Anchor references use matching Texture Set UVs."));
    }
    const QSignalBlocker a(value_), b(slider_), c(mode_), d(opacity_);
    value_->setValue(effect->fill);
    slider_->setValue(qRound(effect->fill * 1000));
    mode_->setCurrentIndex(mode_->findData(int(effect->blending)));
    opacity_->setValue(effect->opacity * 100);
  }

 private:
  FillPanel *shared_;
  MaskEffect effect_;
  QDoubleSpinBox *value_, *opacity_;
  QSlider *slider_;
  QComboBox *mode_;
  void publish(const MaskEffect &e)
  {
    validate_fill_projection(e.mapping.projection);
    effect_ = e;
    if (changed)
      changed(e);
  }
};
}  // namespace bts
