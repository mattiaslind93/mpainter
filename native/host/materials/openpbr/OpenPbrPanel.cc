#include "Theme.hh"
#include "NumericLocale.hh"
#include "OpenPbrPanel.hh"
#include "OpenPbrCompatibility.hh"
#include "ColorPicker.hh"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLocale>
namespace bts {
OpenPbrPanel::OpenPbrPanel(QWidget *parent) : QWidget(parent)
{
  setObjectName("openpbrParameters");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 4, 0, 0);
  auto *notice = new QLabel(
      "OpenPBR 1.1 parameters — portable Principled approximation. This is not a conformant OpenPBR BSDF. Base Color, Metalness and Roughness come from the GPU layer stack. Grey fields are unavailable in this backend; their non-default values are rejected.",
      this);
  notice->setWordWrap(true);
  notice->setObjectName("openpbrBackendNotice");
  layout->addWidget(notice);
  error_ = new QLabel(this);
  error_->setObjectName("openpbrParameterError");
  error_->setWordWrap(true);
  error_->setStyleSheet(bts::theme::qss("color:@Error@"));
  error_->hide();
  layout->addWidget(error_);
  QString group;
  QFormLayout *form = nullptr;
  for (const auto &p : openpbr_parameters) {
    if (group != p.group) {
      group = p.group;
      auto *title = new QLabel(group, this);
      layout->addWidget(title);
      form = new QFormLayout;
      layout->addLayout(form);
    }
    QWidget *control = nullptr;
    if (openpbr_layer_parameter(p.id)) {
      auto *label = new QLabel("GPU layer stack", this);
      label->setToolTip("Edit this channel in the selected paint/fill layer.");
      control = label;
    }
    else if (!openpbr_compatibility_parameter(p.id)) {
      auto *label = new QLabel("Unavailable in Principled preview", this);
      label->setWordWrap(true);
      label->setEnabled(false);
      label->setToolTip(
          "Requires an OpenPBR BSDF backend; this renderer will not silently ignore an authored value.");
      control = label;
    }
    else if (p.type == OpenPbrType::Boolean || std::string_view(p.id) == "thin_film_weight") {
      auto *check = new QCheckBox(this);
      if (std::string_view(p.id) == "thin_film_weight")
        check->setToolTip(
            "Portable backend supports film off/on only; fractional film coverage requires the full BSDF.");
      connect(check, &QCheckBox::toggled, this, [this, id = std::string(p.id)](bool value) {
        publish(id, {value ? 1.f : 0.f, 0, 0}, true);
      });
      control = check;
    }
    else if (p.type == OpenPbrType::Color) {
      auto *picker = new ColorPicker(this);
      picker->findChild<QPushButton *>("colorEyedropper")->hide();
      picker->changed = [this, id = std::string(p.id)](const auto &v) {
        publish(id, {v[0], v[1], v[2]}, false);
      };
      picker->finished = [this] {
        if (!syncing_ && finished)
          finished();
      };
      control = picker;
    }
    else {
      auto *number = new bts::NumericSpinBox(this);
      number->setLocale(QLocale::c());
      number->setDecimals(6);
      number->setRange(
          p.positive ? 1e-6 : p.minimum,
          std::min<double>(p.maximum, std::string_view(p.id) == "specular_weight" ? 2 : 1e6));
      number->setSingleStep(.05);
      connect(number,
              &QDoubleSpinBox::valueChanged,
              this,
              [this, id = std::string(p.id)](double v) { publish(id, {float(v), 0, 0}, false); });
      connect(number, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!syncing_ && finished)
          finished();
      });
      control = number;
    }
    control->setObjectName(QString("openpbrParameter_%1").arg(p.id));
    controls_[p.id] = control;
    form->addRow(p.label, control);
  }
}
void OpenPbrPanel::synchronize(const OpenPbrParameters &state, bool editable)
{
  syncing_ = true;
  if (state_ != state)
    error_->hide();
  state_ = state;
  setEnabled(editable);
  for (const auto &p : openpbr_parameters) {
    auto *control = controls_.at(p.id);
    const auto value = state.get(p.id);
    if (auto *check = qobject_cast<QCheckBox *>(control))
      check->setChecked(value[0] != 0);
    else if (auto *number = qobject_cast<QDoubleSpinBox *>(control))
      number->setValue(value[0]);
    else if (auto *color = dynamic_cast<ColorPicker *>(control))
      color->set_color({value[0], value[1], value[2], 1});
  }
  syncing_ = false;
}
void OpenPbrPanel::publish(std::string_view id, OpenPbrParameters::Value value, bool immediate)
{
  if (syncing_)
    return;
  try {
    auto next = state_;
    next.set(id, value);
    validate_openpbr_compatibility(next);
    error_->hide();
    if (next == state_)
      return;
    state_ = next;
    if (changed)
      changed(next, immediate);
  }
  catch (const std::exception &e) {
    const auto message = QString::fromUtf8(e.what());
    error_->setText(message);
    error_->show();
    synchronize(state_, isEnabled());
    if (error)
      error(message);
  }
}
}  // namespace bts
