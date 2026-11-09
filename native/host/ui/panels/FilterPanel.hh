// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "NumericLocale.hh"
#pragma once
#include "TextureDocument.hh"
#include "LevelsRampEditor.hh"
#include <map>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class FilterPanel final : public QWidget {
 public:
  explicit FilterPanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("filterProperties");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("FILTER", this));
    resource_ = new QToolButton(this);
    resource_->setObjectName("filterResource");
    resource_->setStyleSheet(bts::theme::qss(
        "QToolButton {background:@Surface@;border:1px solid @Border@;padding:6px;}"));
    resource_->setPopupMode(QToolButton::InstantPopup);
    resource_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *menu = new QMenu(resource_);
    resource_->setMenu(menu);
    const std::array<const char *, 11> labels{"No filter selected",
                                              "Invert",
                                              "Clamp",
                                              "Posterize",
                                              "Hue/Saturation/Value",
                                              "Contrast/Brightness",
                                              "Levels/Ramp",
                                              "Blur",
                                              "Sharpen",
                                              "Native Warp",
                                              "Native Slope Blur"},
        names{"filterNone",
              "filterInvert",
              "filterClamp",
              "filterPosterize",
              "filterHsv",
              "filterContrast",
              "filterLevels",
              "filterBlur",
              "filterSharpen",
              "filterWarp",
              "filterSlopeBlur"};
    for (int i = 0; i < 11; ++i) {
      auto *action = menu->addAction(labels[i]);
      action->setObjectName(names[i]);
      connect(action, &QAction::triggered, this, [this, i] {
        filter_.resource = i;
        publish();
      });
    }
    layout->addWidget(resource_);
    ramp_ = new LevelsRampEditor(this);
    ramp_->setObjectName("filterLevelsRamp");
    ramp_->maximum_gamma = 8;
    ramp_->input->setObjectName("filterInputRamp");
    ramp_->output->setObjectName("filterOutputRamp");
    ramp_->set_histogram_visible(false);
    layout->addWidget(ramp_);
    ramp_->hide();
    ramp_->changed = [this](const LevelCurve &curve) {
      filter_.input_low = curve.input_low;
      filter_.input_high = curve.input_high;
      filter_.gamma = curve.gamma;
      filter_.output_low = curve.output_low;
      filter_.output_high = curve.output_high;
      sync_curve_fields();
      if (changed)
        changed(filter_, target_, opacity_value_, blending_);
    };
    ramp_->edit_started = [this] {
      if (edit_started)
        edit_started();
    };
    ramp_->edit_finished = [this] {
      if (edit_finished)
        edit_finished();
    };
    controls_ = new QWidget(this);
    auto *form = new QFormLayout(controls_);
    form->setContentsMargins(0, 0, 0, 0);
    channel_ = new QComboBox(controls_);
    channel_->setObjectName("filterChannel");
    channel_->setMinimumWidth(0);
    channel_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    channel_label_ = new QLabel("Channel", controls_);
    form->addRow(channel_label_, channel_);
    steps_ = new QSpinBox(controls_);
    steps_->setObjectName("filterSteps");
    steps_->setRange(2, 64);
    steps_label_ = new QLabel("Levels", controls_);
    form->addRow(steps_label_, steps_);
    const auto decimal = [&](const char *name, double low, double high) {
      auto *box = new bts::NumericSpinBox(controls_);
      box->setObjectName(name);
      box->setLocale(QLocale::c());
      box->setRange(low, high);
      box->setDecimals(3);
      box->setSingleStep(.05);
      box->setKeyboardTracking(false);
      return box;
    };
    hue_ = decimal("filterHue", -1, 1);
    saturation_ = decimal("filterSaturation", 0, 4);
    value_scale_ = decimal("filterValue", 0, 4);
    form->addRow(hue_label_ = new QLabel("Hue", controls_), hue_);
    form->addRow(saturation_label_ = new QLabel("Saturation", controls_), saturation_);
    form->addRow(value_label_ = new QLabel("Value", controls_), value_scale_);
    brightness_ = decimal("filterBrightness", -1, 1);
    contrast_ = decimal("filterContrast", 0, 4);
    form->addRow(brightness_label_ = new QLabel("Brightness", controls_), brightness_);
    form->addRow(contrast_label_ = new QLabel("Contrast", controls_), contrast_);
    input_low_ = decimal("filterInputLow", -8, 8);
    input_high_ = decimal("filterInputHigh", -8, 8);
    gamma_ = decimal("filterGamma", .01, 8);
    output_low_ = decimal("filterOutputLow", -8, 8);
    output_high_ = decimal("filterOutputHigh", -8, 8);
    form->addRow(input_low_label_ = new QLabel("Input low", controls_), input_low_);
    form->addRow(input_high_label_ = new QLabel("Input high", controls_), input_high_);
    form->addRow(gamma_label_ = new QLabel("Gamma", controls_), gamma_);
    form->addRow(output_low_label_ = new QLabel("Output low", controls_), output_low_);
    form->addRow(output_high_label_ = new QLabel("Output high", controls_), output_high_);
    radius_ = new QSpinBox(controls_);
    radius_->setObjectName("filterRadius");
    radius_->setRange(1, 4);
    radius_label_ = new QLabel("Radius", controls_);
    form->addRow(radius_label_, radius_);
    opacity_ = new bts::NumericSpinBox(controls_);
    opacity_->setObjectName("filterOpacity");
    opacity_->setLocale(QLocale::c());
    opacity_->setRange(0, 100);
    opacity_->setSuffix(" %");
    opacity_->setKeyboardTracking(false);
    form->addRow("Opacity", opacity_);
    blend_ = new QComboBox(controls_);
    blend_->setObjectName("filterBlend");
    for (int i : {0, 2, 3})
      blend_->addItem(layer_blend_names[i], i);
    form->addRow("Blending", blend_);
    layout->addWidget(controls_);
    auto *description = new QLabel(
        "Choose a filter to transform the effect's input. Blur and Sharpen run on the GPU.", this);
    description->setWordWrap(true);
    layout->addWidget(description);
    connect(channel_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0) {
        target_ = channel_->currentData().toInt();
        publish();
      }
    });
    connect(steps_, &QSpinBox::valueChanged, this, [this](int value) {
      filter_.steps = value;
      publish();
    });
    connect(radius_, &QSpinBox::valueChanged, this, [this](int value) {
      filter_.radius = float(value);
      publish();
    });
    connect(opacity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      opacity_value_ = value / 100;
      publish();
    });
    connect(blend_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0) {
        blending_ = LayerBlend(blend_->currentData().toInt());
        publish();
      }
    });
    const auto add_ramp = [&](auto *box, double low, double high) {
      int row;
      QFormLayout::ItemRole role;
      form->getWidgetPosition(box, &row, &role);
      auto *slider = new HandleRamp(controls_);
      slider->setObjectName(box->objectName() + "Ramp");
      slider->setAccessibleName(box->toolTip().isEmpty() ? box->objectName() : box->toolTip());
      slider->formatted = [low, high](float t) {
        return QString::number(low + t * (high - low), 'g', 4);
      };
      slider->positions = {float((box->value() - low) / (high - low))};
      form->insertRow(row + 1, slider);
      scalar_ramps_[box] = slider;
      connect(box,
              &std::remove_pointer_t<decltype(box)>::valueChanged,
              this,
              [slider, low, high](auto value) {
                slider->positions = {float((value - low) / (high - low))};
                slider->update();
              });
      slider->moved = [box, low, high](int, float value) {
        box->setValue(low + value * (high - low));
      };
      slider->started = [this] {
        if (edit_started)
          edit_started();
      };
      slider->finished = [this] {
        if (edit_finished)
          edit_finished();
      };
    };
    add_ramp(steps_, 2, 64);
    add_ramp(hue_, -1, 1);
    add_ramp(saturation_, 0, 4);
    add_ramp(value_scale_, 0, 4);
    add_ramp(brightness_, -1, 1);
    add_ramp(contrast_, 0, 4);
    add_ramp(radius_, 1, 4);
    add_ramp(opacity_, 0, 100);
    for (auto *box : std::initializer_list<QDoubleSpinBox *>{hue_,
                                                             saturation_,
                                                             value_scale_,
                                                             brightness_,
                                                             contrast_,
                                                             input_low_,
                                                             input_high_,
                                                             gamma_,
                                                             output_low_,
                                                             output_high_})
      connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        read_controls();
        sync_ramp();
        if (changed)
          changed(filter_, target_, opacity_value_, blending_);
      });
  }
  std::function<void(const FilterSettings &, int, float, LayerBlend)> changed;
  std::function<void()> edit_started, edit_finished;
  void synchronize(const TextureSet *set,
                   const FilterSettings *filter,
                   int channel = -1,
                   float opacity = 1,
                   LayerBlend blending = LayerBlend::Normal)
  {
    setVisible(set && filter);
    if (!set || !filter)
      return;
    filter_ = *filter;
    target_ = channel;
    opacity_value_ = opacity;
    blending_ = blending;
    resource_->setText(filter->resource ? QString::fromLatin1(filter_names[filter->resource]) :
                                          "Filter\nNo filter selected");
    controls_->setVisible(filter->resource != 0);
    const QSignalBlocker a(channel_), b(steps_), c(opacity_), d(blend_), e(hue_), f(saturation_),
        g(value_scale_), h(brightness_), i(contrast_), j(input_low_), k(input_high_), l(gamma_),
        m(output_low_), n(output_high_), o(radius_);
    channel_->clear();
    const std::array<const char *, 6> names{
        "Base Color", "Metallic", "Roughness", "Normal", "Bump", "Ambient occlusion"};
    for (int index = 0; index < 6; ++index)
      if (index != 3 && (index == 5 ? set->ao_channel : set->channels[index]))
        channel_->addItem(names[index], index);
    for (int index = 0; index < 8; ++index)
      if (set->users[index].present)
        channel_->addItem(QString::fromStdString(set->users[index].name), 6 + index);
    if (set->displacement.channel)
      channel_->addItem("Displacement", 14);
    channel_->setCurrentIndex(channel_->findData(channel));
    channel_->setVisible(channel >= 0);
    channel_label_->setVisible(channel >= 0);
    steps_->setValue(filter->steps);
    const int resource = filter->resource;
    const auto show = [&](QWidget *label, QWidget *field, bool visible) {
      label->setVisible(visible);
      field->setVisible(visible);
    };
    show(steps_label_, steps_, resource == 3);
    hue_label_->setText(resource == 9 ? "Direction" : "Hue");
    show(hue_label_, hue_, resource == 4 || resource == 9);
    show(saturation_label_, saturation_, resource == 4);
    value_label_->setText(resource >= 9 ? "Strength" : "Value");
    show(value_label_, value_scale_, resource == 4 || resource >= 9);
    show(brightness_label_, brightness_, resource == 5);
    show(contrast_label_, contrast_, resource == 5);
    show(input_low_label_, input_low_, resource == 6);
    show(input_high_label_, input_high_, resource == 6);
    show(gamma_label_, gamma_, resource == 6);
    show(output_low_label_, output_low_, resource == 6);
    show(output_high_label_, output_high_, resource == 6);
    show(radius_label_, radius_, resource >= 7);
    hue_->setValue(filter->hue);
    saturation_->setValue(filter->saturation);
    value_scale_->setValue(filter->value_scale);
    brightness_->setValue(filter->brightness);
    contrast_->setValue(filter->contrast);
    input_low_->setValue(filter->input_low);
    input_high_->setValue(filter->input_high);
    gamma_->setValue(filter->gamma);
    output_low_->setValue(filter->output_low);
    output_high_->setValue(filter->output_high);
    radius_->setValue(int(std::lround(filter->radius)));
    opacity_->setValue(opacity * 100);
    blend_->setCurrentIndex(blend_->findData(int(blending)));
    sync_ramp();
    sync_scalar_visibility();
  }

 private:
  LevelsRampEditor *ramp_;
  std::map<QWidget *, HandleRamp *> scalar_ramps_;
  void sync_ramp()
  {
    ramp_->setVisible(filter_.resource == 6);
    LevelCurve curve;
    curve.input_low = filter_.input_low;
    curve.gamma = filter_.gamma;
    curve.input_high = filter_.input_high;
    curve.output_low = filter_.output_low;
    curve.output_high = filter_.output_high;
    ramp_->set_curve(curve);
  }
  void sync_scalar_visibility()
  {
    for (const auto &[field, ramp] : scalar_ramps_) {
      ramp->setVisible(!field->isHidden());
      double value = 0, low = 0, high = 1;
      if (auto *spin = qobject_cast<QDoubleSpinBox *>(field)) {
        value = spin->value();
        low = spin->minimum();
        high = spin->maximum();
      }
      else if (auto *spin = qobject_cast<QSpinBox *>(field)) {
        value = spin->value();
        low = spin->minimum();
        high = spin->maximum();
      }
      ramp->positions = {float((value - low) / (high - low))};
      ramp->update();
    }
  }
  void sync_curve_fields()
  {
    const QSignalBlocker a(input_low_), b(input_high_), c(gamma_), d(output_low_), e(output_high_);
    input_low_->setValue(filter_.input_low);
    input_high_->setValue(filter_.input_high);
    gamma_->setValue(filter_.gamma);
    output_low_->setValue(filter_.output_low);
    output_high_->setValue(filter_.output_high);
  }
  FilterSettings filter_;
  int target_ = -1;
  float opacity_value_ = 1;
  LayerBlend blending_ = LayerBlend::Normal;
  QToolButton *resource_;
  QWidget *controls_;
  QComboBox *channel_, *blend_;
  QLabel *channel_label_, *steps_label_, *hue_label_, *saturation_label_, *value_label_,
      *brightness_label_, *contrast_label_, *input_low_label_, *input_high_label_, *gamma_label_,
      *output_low_label_, *output_high_label_, *radius_label_;
  QSpinBox *steps_, *radius_;
  QDoubleSpinBox *opacity_, *hue_, *saturation_, *value_scale_, *brightness_, *contrast_,
      *input_low_, *input_high_, *gamma_, *output_low_, *output_high_;
  void read_controls()
  {
    filter_.hue = float(hue_->value());
    filter_.saturation = float(saturation_->value());
    filter_.value_scale = float(value_scale_->value());
    filter_.brightness = float(brightness_->value());
    filter_.contrast = float(contrast_->value());
    filter_.input_low = float(input_low_->value());
    filter_.input_high = float(input_high_->value());
    filter_.gamma = float(gamma_->value());
    filter_.output_low = float(output_low_->value());
    filter_.output_high = float(output_high_->value());
    filter_.radius = float(radius_->value());
    filter_.steps = steps_->value();
  }
  void publish()
  {
    read_controls();
    resource_->setText(filter_.resource ? QString::fromLatin1(filter_names[filter_.resource]) :
                                          "Filter\nNo filter selected");
    controls_->setVisible(filter_.resource != 0);
    synchronize_visibility();
    if (changed)
      changed(filter_, target_, opacity_value_, blending_);
  }
  void synchronize_visibility()
  {
    const int resource = filter_.resource;
    const auto show = [&](QWidget *label, QWidget *field, bool visible) {
      label->setVisible(visible);
      field->setVisible(visible);
    };
    show(steps_label_, steps_, resource == 3);
    hue_label_->setText(resource == 9 ? "Direction" : "Hue");
    show(hue_label_, hue_, resource == 4 || resource == 9);
    show(saturation_label_, saturation_, resource == 4);
    value_label_->setText(resource >= 9 ? "Strength" : "Value");
    show(value_label_, value_scale_, resource == 4 || resource >= 9);
    show(brightness_label_, brightness_, resource == 5);
    show(contrast_label_, contrast_, resource == 5);
    show(input_low_label_, input_low_, resource == 6);
    show(input_high_label_, input_high_, resource == 6);
    show(gamma_label_, gamma_, resource == 6);
    show(output_low_label_, output_low_, resource == 6);
    show(output_high_label_, output_high_, resource == 6);
    show(radius_label_, radius_, resource >= 7);
    sync_ramp();
    sync_scalar_visibility();
  }
};
}  // namespace bts
