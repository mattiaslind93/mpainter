#include "Theme.hh"
#include "SliderHitArea.hh"
#include "NativeMaterialPanel.hh"
#include "ColorPicker.hh"
#include "NativeMaterialColor.hh"
#include "NativeMaterialRamp.hh"
#include "BKE_colorband.hh"
#include "NumericLocale.hh"
#include "FillGradient.hh"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <numbers>
namespace bts {
namespace {
class NativeRamp final : public QWidget {
 public:
  QJsonObject value;
  int selected = 0;
  bool dragging = false;
  std::function<PickerColor(const PickerColor &)> to_canonical;
  std::function<void(int)> selected_changed;
  std::function<void()> started, finished;
  std::function<void(const QJsonObject &)> changed;
  explicit NativeRamp(QWidget *parent) : QWidget(parent)
  {
    new SliderHandleGeometry(this, [this] {
      QList<QRectF> result;
      for (const auto &stop : value["stops"].toArray()) {
        const double x = 8 + stop.toObject()["position"].toDouble() * (width() - 16);
        result.append(QRectF(x - 6.5, 31.5, 13, 14));
      }
      return result;
    });
    setMinimumHeight(58);
    setMinimumWidth(140);
    setCursor(Qt::PointingHandCursor);
    setObjectName("nativeColorRamp");
  }
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect rectangle(8, 5, std::max(1, width() - 16), 28);
    const int samples = std::max(1, qRound(rectangle.width() * devicePixelRatioF()));
    QImage strip(samples, 1, QImage::Format_ARGB32);
    const auto band = native_material_ramp(value);
    // The GPU lookup builder calls this same native evaluator. Interpolate in
    // source space, then apply the existing canonical/display color hooks.
    // Converting stops first would change HSV/HSL and nonlinear display blends.
    for (int x = 0; x < samples; ++x) {
      PickerColor rgba;
      blender::BKE_colorband_evaluate(&band, float(x + .5f) / samples, rgba.data());
      strip.setPixelColor(x, 0, picker_to_srgb(to_canonical ? to_canonical(rgba) : rgba));
    }
    p.drawImage(rectangle, strip);
    p.setPen(QColor(bts::theme::Muted));
    p.drawRect(rectangle);
    const auto stops = value["stops"].toArray();
    for (int i = 0; i < stops.size(); ++i) {
      const double x = 8 + stops[i].toObject()["position"].toDouble() * (width() - 16);
      p.setPen(QColor(bts::theme::Canvas));
      p.setBrush(i == selected ? QColor(bts::theme::Accent) : QColor(bts::theme::Heading));
      p.drawPolygon(QPolygonF{QPointF(x, 32), QPointF(x - 6, 45), QPointF(x + 6, 45)});
    }
  }
  void place(qreal x)
  {
    auto stops = value["stops"].toArray();
    if (selected < 0 || selected >= stops.size())
      return;
    auto stop = stops[selected].toObject();
    const double minimum = selected ? stops[selected - 1].toObject()["position"].toDouble() : 0,
                 maximum = selected + 1 < stops.size() ?
                               stops[selected + 1].toObject()["position"].toDouble() :
                               1;
    stop["position"] = std::clamp((x - 8) / std::max(1, width() - 16), minimum, maximum);
    stops[selected] = stop;
    value["stops"] = stops;
    update();
    if (changed)
      changed(value);
  }
  void mousePressEvent(QMouseEvent *e) override
  {
    if (e->button() != Qt::LeftButton)
      return;
    const auto stops = value["stops"].toArray();
    if (stops.isEmpty())
      return;
    selected = std::clamp(selected, 0, int(stops.size()) - 1);
    double nearest = std::abs(e->position().x() - 8 -
                              stops[selected].toObject()["position"].toDouble() * (width() - 16));
    for (int i = 0; i < stops.size(); ++i) {
      const double distance = std::abs(
          e->position().x() - 8 - stops[i].toObject()["position"].toDouble() * (width() - 16));
      if (distance < nearest) {
        nearest = distance;
        selected = i;
      }
    }
    if (selected_changed)
      selected_changed(selected);
    dragging = true;
    if (started)
      started();
    place(e->position().x());
    e->accept();
  }
  void mouseMoveEvent(QMouseEvent *e) override
  {
    if (dragging) {
      place(e->position().x());
      e->accept();
    }
  }
  void mouseReleaseEvent(QMouseEvent *e) override
  {
    if (dragging && e->button() == Qt::LeftButton) {
      place(e->position().x());
      dragging = false;
      if (finished)
        finished();
      e->accept();
    }
  }
};
}  // namespace
NativeMaterialPanel::NativeMaterialPanel(QWidget *parent)
    : QWidget(parent), layout_(new QVBoxLayout(this))
{
  setObjectName("nativeMaterialPanel");
  layout_->setContentsMargins(0, 0, 0, 0);
  preview_.setSingleShot(true);
  preview_.setInterval(16);
  idle_.setSingleShot(true);
  idle_.setInterval(150);
  connect(&preview_, &QTimer::timeout, this, [this] { publish(); });
  connect(&idle_, &QTimer::timeout, this, [this] {
    if (!dragging_)
      finish();
  });
}
void NativeMaterialPanel::begin()
{
  if (updating_ || gesture_)
    return;
  gesture_ = true;
  if (edit_started)
    edit_started();
}
void NativeMaterialPanel::publish()
{
  if (!updating_ && gesture_ && changed)
    changed(state_);
}
void NativeMaterialPanel::finish()
{
  idle_.stop();
  if (!gesture_)
    return;
  if (preview_.isActive()) {
    preview_.stop();
    publish();
  }
  gesture_ = false;
  if (edit_finished)
    edit_finished();
}
void NativeMaterialPanel::schedule()
{
  if (updating_)
    return;
  begin();
  if (!preview_.isActive())
    preview_.start();
  if (!dragging_)
    idle_.start();
}
void NativeMaterialPanel::rebuild_topology()
{
  // Retire the clicked button after its event returns. Ordinary value/drag
  // edits keep their existing widgets; topology changes need new stop rows.
  QTimer::singleShot(0, this, [this] {
    finish();
    updating_ = true;
    rebuild();
    updating_ = false;
  });
}
void NativeMaterialPanel::synchronize(const NativeMaterialDescription &value)
{
  value.validate();
  for (const auto &parameter : value.parameters)
    if (!parameter.ramp.isEmpty())
      native_material_ramp(parameter.ramp);
  if (!state_.material.isEmpty() &&
      native_material_description_json(value) == native_material_description_json(state_))
    return;
  finish();
  updating_ = true;
  std::erase_if(ramp_selection_, [&](const auto &entry) {
    return std::none_of(
        value.parameters.begin(), value.parameters.end(), [&](const auto &parameter) {
          return !parameter.ramp.isEmpty() && parameter.identifier == entry.first;
        });
  });
  state_ = value;
  rebuild();
  updating_ = false;
}
void NativeMaterialPanel::set_resource_inputs_enabled(bool value)
{
  if (resources_enabled_ == value)
    return;
  finish();
  resources_enabled_ = value;
  if (!state_.material.isEmpty()) {
    updating_ = true;
    rebuild();
    updating_ = false;
  }
}
void NativeMaterialPanel::set_height_busy(bool busy)
{
  height_busy_ = busy;
  if (auto *choice = findChild<QComboBox *>("nativeHeightPolicy"))
    choice->setEnabled(!busy && bool(height_requested));
  if (auto *cancel = findChild<QPushButton *>("cancelNativeHeight"))
    cancel->setVisible(busy);
}
PickerColor NativeMaterialPanel::picker_color(const PickerColor &value) const
{
  return to_canonical ? to_canonical(value) : native_material_color(state_.color_to_aces, value);
}
PickerColor NativeMaterialPanel::material_color(const PickerColor &value) const
{
  return from_canonical ? from_canonical(value) :
                          native_material_color(state_.color_from_aces, value);
}
void NativeMaterialPanel::rebuild()
{
  while (auto *item = layout_->takeAt(0)) {
    if (auto *widget = item->widget())
      delete widget;
    delete item;
  }
  auto *heading = new QLabel(state_.group.isEmpty() ? state_.material : state_.group, this);
  heading->setWordWrap(true);
  heading->setObjectName("nativeMaterialHeading");
  layout_->addWidget(heading);
  auto *height = new QComboBox(this);
  height->setObjectName("nativeHeightPolicy");
  height->addItem("Authored Height: Off", int(NativeHeightPolicy::None));
  height->addItem("Raw authored Height — scalar Float",
                  int(NativeHeightPolicy::RawAuthoredFloatV1));
  height->setCurrentIndex(state_.height_policy == NativeHeightPolicy::None ? 0 : 1);
  height->setEnabled(!height_busy_ && bool(height_requested));
  layout_->addWidget(height);
  auto *height_note = new QLabel(
      "With Authored Height Off, Normal Map and Bump nodes render directly through the layer stack. Source strength controls, layer opacity and masks affect the result. Texture export converts the normal contribution to Bump using the hybrid method. Raw authored Height uses scalar H, unchanged, and disables that source Normal contribution. Our Bump neutral is 0.5. Strength, Distance, Filter Width and Invert are not applied; this does not reproduce native Bump shading. Procedural scalar or retained Non-Color image Red; eligibility is checked before enabling.",
      this);
  height_note->setObjectName("nativeHeightPolicyWarning");
  height_note->setWordWrap(true);
  layout_->addWidget(height_note);
  auto *height_cancel = new QPushButton("Cancel Height preparation", this);
  height_cancel->setObjectName("cancelNativeHeight");
  height_cancel->setVisible(height_busy_);
  layout_->addWidget(height_cancel);
  connect(height_cancel, &QPushButton::clicked, this, [this] {
    if (height_cancelled)
      height_cancelled();
  });
  connect(height, &QComboBox::activated, this, [this, height](int index) {
    if (updating_ || height_busy_)
      return;
    const auto policy = NativeHeightPolicy(height->itemData(index).toInt());
    finish();
    {
      QSignalBlocker block(height);
      height->setCurrentIndex(state_.height_policy == NativeHeightPolicy::None ? 0 : 1);
    }
    if (height_requested)
      height_requested(policy);
  });
  const auto start = [this] {
    dragging_ = true;
    begin();
  };
  const auto stop = [this] {
    dragging_ = false;
    finish();
  };
  for (std::size_t index = 0; index < state_.parameters.size(); ++index) {
    const auto &parameter = state_.parameters[index];
    auto *row = new QWidget(this);
    row->setObjectName("nativeParameter_" + parameter.identifier);
    auto *form = new QVBoxLayout(row);
    form->setContentsMargins(0, bts::theme::Space, 0, bts::theme::Space);
    auto *caption = new QLabel(parameter.name, row);
    auto *header = new QWidget(row);
    auto *header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(bts::theme::Space);
    header_layout->addWidget(caption, 1);
    form->addWidget(header);
    if (parameter.mapping_transform_supported()) {
      auto *source = new QLabel(
          parameter.source_node == "ShaderNodeMapping" ?
              QString(
                  "After retained Mapping output — extra Point transform, origin 0; original mode and linked inputs unchanged") :
          parameter.source_node == "ShaderNodeVectorMath" ?
              QString(
                  "After retained Vector Math output — extra Point transform, origin 0; original Add/Multiply operation and linked inputs unchanged") :
              QString("Linked " + parameter.source_socket + " — transform after source, origin 0"),
          row);
      source->setWordWrap(true);
      form->addWidget(source);
      const std::array<const char *, 3> names{"Offset", "Rotation", "Scale"};
      for (int component = 0; component < 3; ++component) {
        const auto &values = component == 0 ? parameter.mapping_transform.offset :
                             component == 1 ? parameter.mapping_transform.rotation_xyz_radians :
                                              parameter.mapping_transform.scale;
        const double units = component == 1 ? 180 / std::numbers::pi : 1;
        auto *line = new QHBoxLayout;
        line->addWidget(new QLabel(names[component], row));
        for (int c = 0; c < 3; ++c) {
          auto *number = new NumericSpinBox(row);
          number->setObjectName(QString("nativeMapping_%1_%2_%3")
                                    .arg(parameter.identifier)
                                    .arg(names[component])
                                    .arg(c));
          number->setPrefix(QString("XYZ")[c] + QString(" "));
          number->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
          number->setMinimumWidth(0);
          const double bound = double(std::numeric_limits<float>::max()) * units;
          number->setRange(-bound, bound);
          number->setDecimals(6);
          number->setValue(values[c] * units);
          number->setToolTip(
              component == 1 ?
                  "Euler XYZ rotation in degrees; stored in radians" :
                  "Native POINT transform after the original linked coordinate value");
          line->addWidget(number);
          connect(number,
                  &QDoubleSpinBox::valueChanged,
                  this,
                  [this, index, component, c, units](double v) {
                    auto &mapping = state_.parameters[index].mapping_transform;
                    auto &values = component == 0 ? mapping.offset :
                                   component == 1 ? mapping.rotation_xyz_radians :
                                                    mapping.scale;
                    values[c] = v / units;
                    schedule();
                  });
          connect(number, &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
        }
        form->addLayout(line);
      }
      auto *reset = new QPushButton("Reset linked transform", row);
      reset->setObjectName("nativeMappingReset_" + parameter.identifier);
      form->addWidget(reset);
      connect(reset, &QPushButton::clicked, this, [this, index] {
        if (state_.parameters[index].mapping_transform.identity())
          return;
        begin();
        state_.parameters[index].mapping_transform = {};
        schedule();
        finish();
        rebuild_topology();
      });
    }
    else if (parameter.mapping) {
      auto *text = new QLabel("Linked coordinate source preserved; this override is unavailable",
                              row);
      text->setWordWrap(true);
      form->addWidget(text);
    }
    else if (!parameter.ramp.isEmpty()) {
      auto *ramp = new NativeRamp(row);
      ramp->setToolTip(
          "Drag the stops; changes update the native GPU lookup without shader recompilation");
      ramp->value = parameter.ramp;
      ramp->selected = std::clamp(ramp_selection_[parameter.identifier],
                                  0,
                                  int(parameter.ramp["stops"].toArray().size()) - 1);
      ramp->selected_changed = [this, id = parameter.identifier](int value) {
        ramp_selection_[id] = value;
      };
      ramp->to_canonical = [this](const auto &color) { return picker_color(color); };
      ramp->started = start;
      ramp->finished = stop;
      ramp->changed = [this, index, row](const auto &value) {
        state_.parameters[index].ramp = value;
        const auto stops = value["stops"].toArray();
        for (int n = 0; n < stops.size(); ++n)
          if (auto *position = row->findChild<QDoubleSpinBox *>(
                  QString("nativeRampPosition_%1_%2")
                      .arg(state_.parameters[index].identifier)
                      .arg(n)))
          {
            QSignalBlocker block(position);
            position->setValue(stops[n].toObject()["position"].toDouble());
          }
        schedule();
      };
      form->addWidget(ramp);
      auto *buttons = new QHBoxLayout;
      auto *duplicate = new QPushButton("Duplicate stop", row);
      duplicate->setObjectName("nativeRampDuplicate_" + parameter.identifier);
      duplicate->setToolTip(
          "Copy the selected stop's exact position and color, then drag the new stop");
      auto *remove = new QPushButton("Remove stop", row);
      remove->setObjectName("nativeRampRemove_" + parameter.identifier);
      duplicate->setEnabled(parameter.ramp["stops"].toArray().size() < 32);
      remove->setEnabled(parameter.ramp["stops"].toArray().size() > 1);
      buttons->addWidget(duplicate);
      buttons->addWidget(remove);
      form->addLayout(buttons);
      const auto topology = [this, index, ramp, duplicate, remove](bool add) {
        auto stops = state_.parameters[index].ramp["stops"].toArray();
        const int selected = std::clamp(ramp->selected, 0, int(stops.size()) - 1);
        if ((add && stops.size() >= 32) || (!add && stops.size() <= 1))
          return;  // Native ColorBand DNA stores 32 stops.
        begin();
        int next = selected;
        if (add) {
          stops.insert(selected + 1, stops[selected]);
          ++next;
        }
        else {
          stops.removeAt(selected);
          next = std::min(selected, int(stops.size()) - 1);
        }
        state_.parameters[index].ramp["stops"] = stops;
        ramp_selection_[state_.parameters[index].identifier] = next;
        duplicate->setEnabled(false);
        remove->setEnabled(false);
        schedule();
        finish();
        rebuild_topology();
      };
      connect(duplicate, &QPushButton::clicked, this, [topology] { topology(true); });
      connect(remove, &QPushButton::clicked, this, [topology] { topology(false); });
      const auto stops = parameter.ramp["stops"].toArray();
      for (int n = 0; n < stops.size(); ++n) {
        auto *controls = new QWidget(row);
        auto *line = new QHBoxLayout(controls);
        line->setContentsMargins(0, 0, 0, 0);
        auto *position = new NumericSpinBox(controls);
        position->setObjectName(
            QString("nativeRampPosition_%1_%2").arg(parameter.identifier).arg(n));
        position->setRange(0, 1);
        position->setDecimals(4);
        position->setValue(stops[n].toObject()["position"].toDouble());
        auto *color = new ColorPicker(controls);
        auto value = stops[n].toObject()["color"].toArray();
        PickerColor rgba{};
        for (int c = 0; c < 4; ++c)
          rgba[c] = value[c].toDouble();
        color->set_color(picker_color(rgba));
        color->started = start;
        color->finished = stop;
        connect(position,
                &QDoubleSpinBox::valueChanged,
                this,
                [this, index, n, ramp, position](double v) {
                  auto stops = state_.parameters[index].ramp["stops"].toArray();
                  auto stop = stops[n].toObject();
                  const double lo = n ? stops[n - 1].toObject()["position"].toDouble() : 0,
                               hi = n + 1 < stops.size() ?
                                        stops[n + 1].toObject()["position"].toDouble() :
                                        1;
                  stop["position"] = std::clamp(v, lo, hi);
                  {
                    QSignalBlocker block(position);
                    position->setValue(stop["position"].toDouble());
                  }
                  stops[n] = stop;
                  state_.parameters[index].ramp["stops"] = stops;
                  ramp->value = state_.parameters[index].ramp;
                  ramp->selected = n;
                  if (ramp->selected_changed)
                    ramp->selected_changed(n);
                  ramp->update();
                  schedule();
                });
        connect(position, &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
        color->changed = [this, index, n, ramp](const PickerColor &input) {
          const auto v = material_color(input);
          auto stops = state_.parameters[index].ramp["stops"].toArray();
          auto stop = stops[n].toObject();
          stop["color"] = QJsonArray{v[0], v[1], v[2], v[3]};
          stops[n] = stop;
          state_.parameters[index].ramp["stops"] = stops;
          ramp->value = state_.parameters[index].ramp;
          ramp->selected = n;
          if (ramp->selected_changed)
            ramp->selected_changed(n);
          ramp->update();
          schedule();
        };
        line->addWidget(position);
        line->addWidget(color);
        form->addWidget(controls);
      }
    }
    else if (parameter.source_node == "ShaderNodeTexImage") {
      auto *button = new QPushButton("Image Texture — Choose resource…", row);
      button->setObjectName("nativeTexture_" + parameter.identifier);
      button->setEnabled(resources_enabled_);
      button->setToolTip(
          resources_enabled_ ?
              QString() :
              QString(
                  "Native image input replacement requires source preparation — not available yet"));
      connect(button, &QPushButton::clicked, this, [this, id = parameter.identifier] {
        if (texture_requested)
          texture_requested(id);
      });
      form->addWidget(button);
    }
    else if (!parameter.editable) {
      auto *message = new QLabel(
          "Linked " + parameter.source_node + " — preserved in the native graph", row);
      message->setWordWrap(true);
      message->setEnabled(false);
      form->addWidget(message);
    }
    else if (parameter.type == NativeParameterType::Color) {
      auto *color = new ColorPicker(row);
      color->setObjectName("nativeColor_" + parameter.identifier);
      PickerColor v{};
      for (int c = 0; c < 4; ++c)
        v[c] = parameter.value[c];
      color->set_color(picker_color(v));
      color->started = start;
      color->finished = stop;
      color->changed = [this, index](const auto &input) {
        const auto v = material_color(input);
        for (int c = 0; c < 4; ++c)
          state_.parameters[index].value[c] = v[c];
        schedule();
      };
      form->addWidget(color);
    }
    else {
      const int count = parameter.components();
      for (int c = 0; c < count; ++c) {
        QWidget *line = header;
        QHBoxLayout *horizontal = header_layout;
        if (count > 1) {
          line = new QWidget(row);
          horizontal = new QHBoxLayout(line);
          horizontal->setContentsMargins(0, 0, 0, 0);
          horizontal->addWidget(new QLabel(QString("XYZ")[c], line));
        }
        auto *number = new NumericSpinBox(line);
        number->setObjectName(QString("nativeValue_%1_%2").arg(parameter.identifier).arg(c));
        number->setRange(parameter.minimum, parameter.maximum);
        number->setDecimals(4);
        number->setValue(parameter.value[c]);
        horizontal->addWidget(number);
        if (count > 1)
          form->addWidget(line);
        if (count == 1) {
          auto *slider = new QSlider(Qt::Horizontal, row);
          slider->setRange(0, 10000);
          slider->hide();
          double low = parameter.minimum, high = parameter.maximum;
          if (high - low > 1e6) {
            low = std::min(0.0, parameter.value[c] * 2);
            high = std::max(1.0, std::abs(parameter.value[c]) * 2);
          }
          const auto slide_value = [low, high](double v) {
            return high > low ? int(std::clamp((v - low) / (high - low), 0.0, 1.0) * 10000) : 0;
          };
          slider->setValue(slide_value(parameter.value[c]));
          auto *gradient = new FillGradient(slider, number, row);
          gradient->setObjectName("nativeGradient_" + parameter.identifier);
          gradient->show();
          form->addWidget(gradient);
          connect(slider, &QSlider::sliderPressed, this, start);
          connect(slider, &QSlider::sliderReleased, this, stop);
          connect(slider, &QSlider::valueChanged, this, [number, low, high](int value) {
            number->setValue(low + (high - low) * value / 10000);
          });
          connect(
              number, &QDoubleSpinBox::valueChanged, this, [slider, slide_value](double value) {
                const QSignalBlocker block(slider);
                slider->setValue(slide_value(value));
              });
        }
        connect(number, &QDoubleSpinBox::valueChanged, this, [this, index, c](double v) {
          state_.parameters[index].value[c] = v;
          schedule();
        });
        connect(number, &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
      }
    }
    layout_->addWidget(row);
  }
  for (const auto &warning : state_.warnings) {
    if (warning.startsWith("Bump unavailable: Native bump/normal extraction unavailable:"))
      continue;
    auto *text = new QLabel(warning, this);
    text->setWordWrap(true);
    text->setStyleSheet(bts::theme::qss("color:@Warning@"));
    layout_->addWidget(text);
  }
  layout_->addStretch();
}
}  // namespace bts
