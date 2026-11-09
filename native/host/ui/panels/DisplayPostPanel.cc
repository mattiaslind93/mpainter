// SPDX-License-Identifier: GPL-3.0-or-later
#include "NumericLocale.hh"
#include "DisplayPostPanel.hh"
#include "DisplayPostEffects.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
namespace bts {
namespace {
double slider_value(const DisplayPostNumber &n, int value)
{
  return QString(n.key) == "focus_distance" ? n.low * std::pow(n.high / n.low, value / 1000.) :
                                              n.low + (n.high - n.low) * value / 1000.;
}
int slider_position(const DisplayPostNumber &n, double value)
{
  return qRound(QString(n.key) == "focus_distance" ?
                    std::log(std::clamp(value, double(n.low), double(n.high)) / n.low) /
                        std::log(n.high / n.low) * 1000 :
                    (value - n.low) / (n.high - n.low) * 1000);
}
}  // namespace
void DisplayPostPanel::publish(const QString &key, double value)
{
  if (changed)
    changed(key, value);
}
QVBoxLayout *DisplayPostPanel::group(QVBoxLayout *layout,
                                     const char *title,
                                     const char *prefix,
                                     const char *key,
                                     const char *family)
{
  auto *header = new QWidget(groups_);
  auto *line = new QHBoxLayout(header);
  line->setContentsMargins(0, 0, 0, 0);
  auto *expand = new QToolButton(header);
  expand->setObjectName(QString(prefix) + "Expand");
  expand->setArrowType(Qt::RightArrow);
  expand->setCheckable(true);
  line->addWidget(expand);
  line->addWidget(new QLabel(title, header), 1);
  auto *enabled = new QCheckBox(header);
  enabled->setObjectName(QString(prefix) + "Enabled");
  line->addWidget(enabled);
  toggles_[key] = enabled;
  layout->addWidget(header);
  connect(enabled, &QCheckBox::toggled, this, [this, key](bool on) { publish(key, on); });
  auto *body = new QWidget(groups_);
  auto *fields = new QVBoxLayout(body);
  fields->setContentsMargins(12, 0, 0, 0);
  layout->addWidget(body);
  body->hide();
  connect(expand, &QToolButton::toggled, this, [body, expand](bool open) {
    body->setVisible(open);
    expand->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
  });
  for (const auto &n : DisplayPostSettings::numbers())
    if (QString(family) == n.group) {
      auto *row = new QHBoxLayout;
      row->addWidget(new QLabel(n.label, body), 1);
      auto *number = new bts::NumericSpinBox(body);
      number->setObjectName(n.object);
      number->setLocale(QLocale::c());
      number->setRange(n.low, n.high);
      number->setDecimals(n.decimals);
      number->setSingleStep(n.step);
      number->setKeyboardTracking(false);
      row->addWidget(number);
      fields->addLayout(row);
      auto *slider = new QSlider(Qt::Horizontal, body);
      slider->setObjectName(QString(n.object) + "Slider");
      slider->setRange(0, 1000);
      fields->addWidget(slider);
      values_[n.key] = number;
      sliders_[n.key] = slider;
      connect(slider, &QSlider::valueChanged, this, [number, n](int v) {
        number->setValue(slider_value(n, v));
      });
      connect(number, &QDoubleSpinBox::valueChanged, this, [this, n, slider](double value) {
        const QSignalBlocker block(slider);
        slider->setValue(slider_position(n, value));
        publish(n.key, value);
      });
    }
  auto *restore = new QPushButton("Restore defaults", body);
  restore->setObjectName(QString(prefix) + "Restore");
  fields->addWidget(restore);
  connect(restore, &QPushButton::clicked, this, [this, family] {
    publish(QString(family) + "_reset", 1);
  });
  return fields;
}
DisplayPostPanel::DisplayPostPanel(QWidget *parent) : QWidget(parent)
{
  setObjectName("displayPostEffects");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(new QLabel("Camera / Post Effects", this));
  enabled_ = new QCheckBox("Activate Post Effects", this);
  enabled_->setObjectName("postEffectsEnabled");
  layout->addWidget(enabled_);
  groups_ = new QWidget(this);
  auto *fields = new QVBoxLayout(groups_);
  fields->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(groups_);
  group(fields, "Color Correction", "postColor", "color_correction", "color");
  auto *dof = group(fields, "DOF", "postDof", "dof", "dof");
  auto *hint = new QLabel("Aperture 0 keeps the view sharp. Preview blur is limited to 16 pixels.",
                          this);
  hint->setWordWrap(true);
  dof->addWidget(hint);
  auto *tone = group(fields, "Tone Mapping", "postTone", "tone_mapping", "tone");
  auto *row = new QHBoxLayout;
  row->addWidget(new QLabel("Function", this));
  tone_ = new QComboBox(this);
  tone_->setObjectName("postToneFunction");
  tone_->addItem("Linear", 0);
  tone_->addItem("Reinhard", 1);
  tone_->addItem("Reinhard/Devlin", 2);
  row->addWidget(tone_);
  tone->insertLayout(0, row);
  connect(tone_, &QComboBox::currentIndexChanged, this, [this] {
    publish("tone_function", tone_->currentData().toInt());
  });
  auto *glare = group(fields, "Glare", "postGlare", "glare", "glare");
  auto *shape = new QHBoxLayout;
  shape->addWidget(new QLabel("Shape", this));
  glare_ = new QComboBox(this);
  glare_->setObjectName("postGlareShape");
  glare_->addItem("Filter Cross Screen Spectral", 0);
  glare_->addItem("Fog Glow", 1);
  glare_->addItem("Ghosts", 2);
  shape->addWidget(glare_);
  glare->insertLayout(0, shape);
  connect(glare_, &QComboBox::currentIndexChanged, this, [this] {
    publish("glare_shape", glare_->currentData().toInt());
  });
  group(fields, "Vignette", "postVignette", "vignette", "vignette");
  group(fields, "Lens Distortion", "postLens", "lens", "lens");
  auto *restore = new QPushButton("Restore all defaults", this);
  restore->setObjectName("postEffectsRestoreAll");
  layout->addWidget(restore);
  connect(restore, &QPushButton::clicked, this, [this] { publish("reset", 1); });
  connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
    groups_->setEnabled(on);
    publish("enabled", on);
  });
  synchronize({});
}
void DisplayPostPanel::synchronize(const QJsonObject &json)
{
  const auto state = DisplayPostSettings::from_json(json);
  const auto data = state.to_json();
  const QSignalBlocker master(enabled_), function(tone_), shape(glare_);
  enabled_->setChecked(state.enabled);
  groups_->setEnabled(state.enabled);
  tone_->setCurrentIndex(tone_->findData(state.tone_function));
  glare_->setCurrentIndex(glare_->findData(state.glare_shape));
  for (auto &[key, control] : toggles_) {
    const QSignalBlocker blocker(control);
    control->setChecked(data[key].toBool());
  }
  for (const auto &n : DisplayPostSettings::numbers()) {
    auto *number = values_.at(n.key);
    auto *slider = sliders_.at(n.key);
    const QSignalBlocker block(number), block_slider(slider);
    const float v = state.*n.member;
    number->setValue(v);
    slider->setValue(slider_position(n, v));
  }
  values_.at("glare_remap")->setEnabled(state.glare_shape != 1);
  sliders_.at("glare_remap")->setEnabled(state.glare_shape != 1);
  values_.at("mapping_factor")->setEnabled(state.tone_function != 0);
  sliders_.at("mapping_factor")->setEnabled(state.tone_function != 0);
}
}  // namespace bts
