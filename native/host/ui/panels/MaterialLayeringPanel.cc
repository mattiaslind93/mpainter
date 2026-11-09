// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialLayeringPanel.hh"
#include "ColorPicker.hh"
#include "NumericLocale.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
namespace bts {
MaterialLayeringPanel::MaterialLayeringPanel(QWidget *parent) : QDialog(parent)
{
  setObjectName("uniformMaterialLayeringDialog");
  setWindowTitle("Four materials");
  setWindowModality(Qt::NonModal);
  resize(380, 650);
  auto *layout = new QVBoxLayout(this);
  editor_ = new QWidget(this);
  auto *edit = new QVBoxLayout(editor_);
  edit->setContentsMargins(0, 0, 0, 0);
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(editor_);
  layout->addWidget(scroll);
  enabled_ = new QCheckBox("Enable native four-material mode", this);
  enabled_->setObjectName("uniformMaterialLayeringEnabled");
  edit->addWidget(enabled_);
  auto *scope = new QLabel(
      "Three mesh opacity stacks mix four repeating material inputs. Main is retained. Color supports linear AP1/Rec.709 and sRGB (8-bit images); scalar maps are data. Bump maps read grayscale height (R); clearing a Bump map restores neutral 0.5. Normal stays internal. Linear Color and raw scalar UDIM sequences require the complete mapped source footprint; sRGB and other nonlinear Color sequences are unavailable.",
      this);
  scope->setWordWrap(true);
  edit->addWidget(scope);
  input_ = new QComboBox(this);
  input_->setObjectName("uniformMaterialInput");
  for (int i = 0; i < 4; ++i)
    input_->addItem(QString("Input %1").arg(i + 1));
  edit->addWidget(input_);
  auto *form = new QFormLayout;
  color_ = new ColorPicker(this);
  color_->setObjectName("uniformMaterialColor");
  color_->findChild<QPushButton *>("colorEyedropper")->hide();
  form->addRow("Base Color", color_);
  color_->changed = [this](const auto &value) {
    if (syncing_ || retired_)
      return;
    inputs_[current_].values.base_color = value;
    apply_->setEnabled(true);
  };
  const char *labels[]{"Metallic", "Roughness", "Ambient occlusion"};
  for (int i = 0; i < 3; ++i) {
    auto *field = fields_[i] = new QDoubleSpinBox(this);
    field->setObjectName(QString("uniformMaterialScalar%1").arg(i));
    field->setRange(0, 1);
    field->setDecimals(6);
    field->setSingleStep(.01);
    field->setKeyboardTracking(false);
    form->addRow(QString::fromLatin1(labels[i]), field);
    connect(field, &QDoubleSpinBox::valueChanged, this, [this] {
      if (!syncing_ && !retired_) {
        store();
        apply_->setEnabled(true);
      }
    });
  }
  edit->addLayout(form);
  channel_ = new QComboBox(this);
  channel_->setObjectName("materialInputSourceChannel");
  for (const auto &[name, role] : {std::pair{"Color", 0},
                                   {"Metallic", 1},
                                   {"Roughness", 2},
                                   {"Bump", 4},
                                   {"Ambient occlusion", 5}})
    channel_->addItem(name, role);
  edit->addWidget(channel_);
  source_ = new QLabel(this);
  source_->setObjectName("materialInputSource");
  source_->setWordWrap(true);
  edit->addWidget(source_);
  space_ = new QComboBox(this);
  space_->setObjectName("materialInputColorSpace");
  space_->setToolTip(
      "Input space for the next source; existing maps keep their recorded interpretation.");
  space_->addItem("Next Color source: automatic metadata", QString());
  space_->addItem("Next Color source: linear AP1 (ACEScg)", "ACEScg");
  space_->addItem("Next Color source: linear Rec.709", "Linear Rec.709");
  space_->addItem("Next Color source: sRGB (8-bit images)", "sRGB");
  edit->addWidget(space_);
  auto *sources = new QHBoxLayout;
  for (const auto &[label, name] : {std::pair{"File…", "materialInputFile"},
                                    {"Shelf…", "materialInputShelf"},
                                    {"Clear", "materialInputClear"}})
  {
    auto *button = new QPushButton(label, this);
    button->setAutoDefault(false);
    button->setDefault(false);
    button->setObjectName(name);
    sources->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, name] {
      if (retired_ || applying_)
        return;
      const int channel = channel_->currentData().toInt();
      if (QString::fromLatin1(name) == "materialInputClear") {
        if (source_cleared)
          source_cleared(current_, channel);
        inputs_[current_].textures[channel].reset();
        loading_[current_][channel] = false;
        apply_->setEnabled(true);
        show_source();
      }
      else if (source_requested)
        source_requested(current_,
                         channel,
                         QString::fromLatin1(name) == "materialInputFile",
                         channel == 0 ? spaces_[current_] : std::string{});
    });
  }
  edit->addLayout(sources);
  auto *uv = new QFormLayout;
  const char *uv_labels[]{"Tiling X", "Tiling Y", "Offset X", "Offset Y", "Rotation (degrees)"};
  for (int i = 0; i < 5; ++i) {
    auto *field = mapping_[i] = new QDoubleSpinBox(this);
    field->setObjectName(QString("materialInputMapping%1").arg(i));
    field->setRange(i == 4 ? -360000 : -1000, i == 4 ? 360000 : 1000);
    field->setDecimals(6);
    field->setSingleStep(i == 4 ? 1 : .1);
    field->setKeyboardTracking(false);
    uv->addRow(uv_labels[i], field);
    connect(field, &QDoubleSpinBox::valueChanged, this, [this] {
      if (!syncing_ && !retired_) {
        store();
        apply_->setEnabled(true);
      }
    });
  }
  edit->addLayout(uv);
  message_ = new QLabel(this);
  message_->setObjectName("uniformMaterialLayeringMessage");
  message_->setWordWrap(true);
  layout->addWidget(message_);
  auto *buttons = new QHBoxLayout;
  auto *cancel = new QPushButton("Cancel", this);
  cancel->setObjectName("uniformMaterialLayeringCancel");
  apply_ = new QPushButton("Apply", this);
  apply_->setObjectName("uniformMaterialLayeringApply");
  buttons->addStretch();
  buttons->addWidget(cancel);
  buttons->addWidget(apply_);
  layout->addLayout(buttons);
  for (auto *button : {cancel, apply_}) {
    button->setAutoDefault(false);
    button->setDefault(false);
  }
  apply_poll_ = new QTimer(this);
  apply_poll_->setInterval(16);
  connect(apply_poll_, &QTimer::timeout, this, [this] { finish_apply(); });
  connect(cancel, &QPushButton::clicked, this, &MaterialLayeringPanel::reject);
  connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
  connect(enabled_, &QCheckBox::toggled, this, [this] {
    if (!syncing_ && !retired_)
      apply_->setEnabled(true);
  });
  connect(input_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (syncing_ || retired_)
      return;
    store();
    show_input(index);
  });
  connect(channel_, &QComboBox::currentIndexChanged, this, [this] { show_source(); });
  connect(space_, &QComboBox::currentIndexChanged, this, [this] {
    if (!syncing_ && !retired_)
      spaces_[current_] = space_->currentData().toString().toStdString();
  });
}
void MaterialLayeringPanel::load(const TextureSet &set, const QString &space)
{
  syncing_ = true;
  color_->set_working_space_label(space);
  inputs_ = material_layering_inputs(set);
  for (int i = 0; i < 4; ++i)
    spaces_[i] = inputs_[i].textures[0] ? inputs_[i].textures[0]->source_space : std::string{};
  enabled_->setChecked(set.material_layering.enabled);
  input_->setCurrentIndex(0);
  current_ = 0;
  syncing_ = false;
  show_input(0);
  message_->setText(
      QString(
          "Constants use recorded linear %1. Maps stay buffered until Apply; each input has its own SRT. Normal stays flat; Bump uses a raw grayscale map or neutral 0.5.")
          .arg(space));
  apply_->setEnabled(false);
}
void MaterialLayeringPanel::retire_callbacks() noexcept
{
  if (retired_)
    return;
  retired_ = true;
  apply_poll_->stop();
  auto finish = std::move(completed);
  if (finish)
    finish();
  const auto block = [](auto &&self, QObject *object) noexcept -> void {
    object->blockSignals(true);
    for (QObject *child : object->children())
      self(self, child);
  };
  block(block, this);
  apply_requested = {};
  apply_inputs_requested = {};
  source_requested = {};
  source_cleared = {};
  color_->retire_callbacks();
}
MaterialLayeringPanel::~MaterialLayeringPanel()
{
  retire_callbacks();
}
void MaterialLayeringPanel::reject()
{
  retire_callbacks();
  QDialog::reject();
}
void MaterialLayeringPanel::accept()
{
  retire_callbacks();
  QDialog::accept();
}
void MaterialLayeringPanel::store()
{
  auto &v = inputs_.at(current_).values;
  v.base_color = color_->color();
  v.metallic = float(fields_[0]->value());
  v.roughness = float(fields_[1]->value());
  v.ambient_occlusion = float(fields_[2]->value());
  auto &uv = inputs_[current_];
  uv.tiling = {mapping_[0]->value(), mapping_[1]->value()};
  uv.offset = {mapping_[2]->value(), mapping_[3]->value()};
  uv.rotation = mapping_[4]->value();
}
void MaterialLayeringPanel::show_input(int index)
{
  if (index < 0 || index >= 4)
    return;
  syncing_ = true;
  current_ = index;
  const auto &input = inputs_[index];
  const auto &v = input.values;
  color_->set_color(v.base_color);
  fields_[0]->setValue(v.metallic);
  fields_[1]->setValue(v.roughness);
  fields_[2]->setValue(v.ambient_occlusion);
  mapping_[0]->setValue(input.tiling[0]);
  mapping_[1]->setValue(input.tiling[1]);
  mapping_[2]->setValue(input.offset[0]);
  mapping_[3]->setValue(input.offset[1]);
  mapping_[4]->setValue(input.rotation);
  space_->setCurrentIndex(std::max(0, space_->findData(QString::fromStdString(spaces_[index]))));
  syncing_ = false;
  show_source();
}
void MaterialLayeringPanel::show_source()
{
  if (!source_)
    return;
  const int channel = channel_->currentData().toInt();
  space_->setVisible(channel == 0);
  const auto &texture = inputs_[current_].textures[channel];
  source_->setText(loading_[current_][channel] ? "Loading buffered source…" :
                   texture                     ? QString::fromStdString(texture->name) :
                                                 "Uniform constant (no map)");
}
void MaterialLayeringPanel::install_texture(int input,
                                            int channel,
                                            std::shared_ptr<const FillTexture> texture)
{
  if (retired_)
    return;
  if (input < 0 || input >= 4 ||
      (channel != 0 && channel != 1 && channel != 2 && channel != 4 && channel != 5))
    throw std::invalid_argument("Unsupported material map target");
  inputs_[input].textures[channel] = std::move(texture);
  loading_[input][channel] = false;
  apply_->setEnabled(true);
  show_source();
}
void MaterialLayeringPanel::load_status(
    int input, int channel, const QString &text, bool loading, bool error)
{
  if (retired_ || input < 0 || input >= 4 || channel < 0 || channel >= 6)
    return;
  loading_[input][channel] = loading;
  if (current_ == input && channel_->currentData().toInt() == channel) {
    show_source();
    if (loading || error)
      message_->setText(text);
  }
}
void MaterialLayeringPanel::show_error(const QString &text)
{
  if (!retired_)
    message_->setText(text);
}
void MaterialLayeringPanel::apply()
{
  if (retired_ || applying_)
    return;
  try {
    for (auto *field : color_->findChildren<QDoubleSpinBox *>())
      field->interpretText();
    for (auto *field : fields_)
      field->interpretText();
    for (auto *field : mapping_)
      field->interpretText();
    store();
    for (const auto &row : loading_)
      for (bool loading : row)
        if (loading)
          throw std::runtime_error("Finish or cancel source loading before Apply");
    applying_ = true;
    editor_->setEnabled(false);
    apply_->setEnabled(false);
    finish_apply();
  }
  catch (const std::exception &error) {
    show_error(QString::fromUtf8(error.what()));
  }
}
void MaterialLayeringPanel::finish_apply()
{
  try {
    if (retired_)
      return;
    if (apply_inputs_requested) {
      if (!apply_inputs_requested(enabled_->isChecked(), inputs_)) {
        message_->setText("Preparing material source resources…");
        apply_poll_->start();
        return;
      }
    }
    else if (apply_requested) {
      std::array<MaterialValues, 4> values;
      for (int i = 0; i < 4; ++i)
        values[i] = inputs_[i].values;
      apply_requested(enabled_->isChecked(), values);
    }
    else
      throw std::logic_error("Material editor has no application target");
    accept();
  }
  catch (const std::exception &error) {
    apply_poll_->stop();
    applying_ = false;
    editor_->setEnabled(true);
    apply_->setEnabled(true);
    show_error(QString::fromUtf8(error.what()));
  }
}
}  // namespace bts
