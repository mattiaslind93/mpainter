#include "NumericLocale.hh"
#pragma once
#include "TextureDocument.hh"
#include "GeneratorAnchorPanel.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <cmath>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <string_view>
namespace bts {
class MaskGeneratorPanel final : public QWidget {
 public:
  explicit MaskGeneratorPanel(QWidget *parent) : QWidget(parent)
  {
    setObjectName("maskGeneratorProperties");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("GENERATOR", this));
    resource_ = new QToolButton(this);
    resource_->setObjectName("maskGeneratorResource");
    resource_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    resource_->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(resource_);
    resource_->setMenu(menu);
    const std::array<const char *, 10> names{"generatorNone",
                                             "generatorWorldHeight",
                                             "generatorCurvature",
                                             "generatorAmbientOcclusion",
                                             "generatorThickness",
                                             "generatorDirt",
                                             "generatorWorldNormalLight",
                                             "generatorNativeGrunge",
                                             "generatorNativeEdgeWear",
                                             "generatorNativeCavityDirt"};
    for (int i = 0; i < int(mask_generator_names.size()); ++i) {
      auto *action = menu->addAction(mask_generator_names[i]);
      action->setObjectName(names[i]);
      connect(action, &QAction::triggered, this, [this, i] {
        auto e = effect_;
        e.generator = i;
        if (i >= 7) {
          e.triplanar_grunge = true;
          e.surface_fallback = true;
        }
        publish(e);
      });
    }
    layout->addWidget(resource_);
    controls_ = new QWidget(this);
    auto *form = new QFormLayout(controls_);
    form->setContentsMargins(0, 0, 0, 0);
    channel_ = new QComboBox(controls_);
    channel_->setObjectName("generatorChannel");
    form->addRow("Channel", channel_);
    connect(channel_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (index >= 0 && content_channel_ >= 0) {
        content_channel_ = channel_->currentData().toInt();
        publish(effect_);
      }
    });
    input_ = new QLabel(controls_);
    input_->setObjectName("generatorInputResource");
    input_->setWordWrap(true);
    form->addRow(input_);
    input_role_ = new QComboBox(controls_);
    input_role_->setObjectName("generatorInputRole");
    form->addRow("Input map", input_role_);
    tile_ = new QComboBox(controls_);
    tile_->setObjectName("generatorInputTile");
    form->addRow("UV tile", tile_);
    auto *replace = new QPushButton("Replace input…", controls_);
    replace->setObjectName("generatorReplaceInput");
    form->addRow(replace);
    connect(replace, &QPushButton::clicked, this, [this] {
      if (input_requested && tile_->currentIndex() >= 0 && input_role_->currentIndex() >= 0)
        input_requested(input_role_->currentData().toInt(), tile_->currentData().toInt());
    });
    bake_ = new QPushButton("Bake Mesh Maps…", controls_);
    bake_->setObjectName("generatorBakeMaps");
    form->addRow(bake_);
    connect(bake_, &QPushButton::clicked, this, [this] {
      if (bake_requested)
        bake_requested();
    });
    fallback_ = new QCheckBox("Use 3D fallback while maps are missing", controls_);
    fallback_->setObjectName("generatorSurfaceFallback");
    form->addRow(fallback_);
    fallback_->setToolTip(
        "Provisional position/normal-based coverage. Bake replaces these proxies with the actual mesh maps.");
    connect(fallback_, &QCheckBox::toggled, this, [this](bool enabled) {
      auto e = effect_;
      e.surface_fallback = enabled;
      publish(e);
    });
    for (int slot = 0; slot < 2; ++slot) {
      anchor_inputs_[slot] = new GeneratorAnchorPanel(slot, controls_);
      form->addRow(anchor_inputs_[slot]);
      anchor_inputs_[slot]->changed = [this, slot](const AnchorReference &ref) {
        auto e = effect_;
        e.input_anchors[slot] = ref;
        publish(e);
      };
      anchor_inputs_[slot]->source_requested = [this](const QUuid &id) {
        if (anchor_requested)
          anchor_requested(id);
      };
    }
    const auto section = [&](QFormLayout *target, const char *text, const char *name) {
      auto *label = new QLabel(text, controls_);
      label->setObjectName(name);
      auto font = label->font();
      font.setBold(true);
      label->setFont(font);
      target->addRow(label);
    };
    section(form, "Parameters", "generatorParametersSection");
    level_ = spin("generatorLevel", 0, 1, .05, 3);
    form->addRow("Level", slider_field(level_));
    connect(level_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.bias = e.response ? float(value) - .5f : .5f - float(value);
      publish(e);
    });
    contrast_ = spin("generatorContrast", 0, 8, .1, 4);
    form->addRow("Contrast", slider_field(contrast_));
    invert_ = new QCheckBox("Invert", controls_);
    invert_->setObjectName("generatorInvert");
    form->addRow(invert_);
    axis_ = new QComboBox(controls_);
    axis_->setObjectName("generatorPositionAxis");
    axis_->addItems({"X", "Y", "Z"});
    axis_label_ = new QLabel("Position axis", controls_);
    form->addRow(axis_label_, axis_);
    light_ = new QWidget(controls_);
    light_->setObjectName("generatorLightDirection");
    auto *direction = new QHBoxLayout(light_);
    direction->setContentsMargins(0, 0, 0, 0);
    const std::array<const char *, 3> axes{"X", "Y", "Z"};
    for (int c = 0; c < 3; ++c) {
      direction->addWidget(new QLabel(axes[c], light_));
      light_components_[c] = spin(
          (QString("generatorLight") + axes[c]).toLatin1().constData(), -1, 1, .1, 3);
      direction->addWidget(light_components_[c]);
      connect(light_components_[c], &QDoubleSpinBox::valueChanged, this, [this, c](double value) {
        auto e = effect_;
        e.light_direction[c] = float(value);
        publish(e);
      });
    }
    light_label_ = new QLabel("Light direction (world)", controls_);
    form->addRow(light_label_, light_);
    dirt_ = spin("generatorDirtCurvature", 0, 1, .05, 3);
    dirt_label_ = new QLabel("Curvature influence", controls_);
    form->addRow(dirt_label_, dirt_);
    connect(dirt_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.dirt_curvature = float(value);
      publish(e);
    });
    curve_ = new QComboBox(controls_);
    curve_->setObjectName("generatorCurve");
    curve_->addItems({"Linear", "Smooth", "Square"});
    form->addRow("Curve", curve_);
    map_filter_ = new QComboBox(controls_);
    map_filter_->setObjectName("generatorMapFiltering");
    map_filter_->addItems({"Nearest", "Linear"});
    form->addRow("Map filtering", map_filter_);
    bias_ = spin("generatorBias", -1, 1, .05, 3);
    form->addRow("Bias", bias_);
    native_controls_ = new QWidget(controls_);
    auto *native_form = new QFormLayout(native_controls_);
    native_form->setContentsMargins(0, 0, 0, 0);
    const auto parameter = [&](const char *label,
                               const char *name,
                               double low,
                               double high,
                               float MeshGeneratorSettings::*member) {
      auto *control = spin(name, low, high, .05, 3);
      native_form->addRow(label,
                          slider_field(control, std::string_view(name) == "generatorGrungeScale"));
      native_parameters_.push_back({control, member});
      connect(control, &QDoubleSpinBox::valueChanged, this, [this, member](double value) {
        auto e = effect_;
        e.*member = float(value);
        publish(e);
      });
    };
    section(native_form, "Grunge", "generatorGrungeSection");
    grunge_projection_ = new QComboBox(native_controls_);
    grunge_projection_->setObjectName("generatorGrungeProjection");
    grunge_projection_->addItems({"UV (legacy)", "Tri-planar (3D)"});
    native_form->addRow("Projection", grunge_projection_);
    connect(grunge_projection_, &QComboBox::currentIndexChanged, this, [this](int index) {
      if (index >= 0) {
        auto e = effect_;
        e.triplanar_grunge = index == 1;
        publish(e);
      }
    });
    parameter("Amount", "generatorGrungeAmount", 0, 1, &MeshGeneratorSettings::grunge_amount);
    parameter("Scale", "generatorGrungeScale", .01, 4096, &MeshGeneratorSettings::grunge_scale);
    section(native_form, "Mesh Map Influences", "generatorMapInfluencesSection");
    parameter(
        "Ambient occlusion", "generatorAoInfluence", 0, 1, &MeshGeneratorSettings::ao_influence);
    parameter("Curvature influence",
              "generatorCurvatureInfluence",
              0,
              1,
              &MeshGeneratorSettings::curvature_influence);
    parameter("Top/down gradient",
              "generatorPositionInfluence",
              0,
              1,
              &MeshGeneratorSettings::position_influence);
    parameter("World normal influence",
              "generatorNormalInfluence",
              0,
              1,
              &MeshGeneratorSettings::normal_influence);
    parameter("Thickness influence",
              "generatorThicknessInfluence",
              0,
              1,
              &MeshGeneratorSettings::thickness_influence);
    form->addRow(native_controls_);
    opacity_ = spin("generatorOpacity", 0, 100, 1, 1);
    opacity_->setSuffix(" %");
    form->addRow("Opacity", opacity_);
    blend_ = new QComboBox(controls_);
    blend_->setObjectName("generatorBlend");
    for (int i = 0; i < 5; ++i)
      blend_->addItem(layer_blend_names[i], i);
    form->addRow("Blending", blend_);
    layout->addWidget(controls_);
    connect(curve_, &QComboBox::currentIndexChanged, this, [this](int value) {
      if (value >= 0) {
        auto e = effect_;
        e.curve = value;
        publish(e);
      }
    });
    connect(map_filter_, &QComboBox::currentIndexChanged, this, [this](int value) {
      if (value >= 0) {
        auto e = effect_;
        e.map_filter = value;
        publish(e);
      }
    });
    connect(axis_, &QComboBox::currentIndexChanged, this, [this](int value) {
      if (value >= 0) {
        auto e = effect_;
        e.axis = value;
        publish(e);
      }
    });
    connect(contrast_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.contrast = e.response ? 1 + 7 * float(value) : float(value);
      publish(e);
    });
    connect(bias_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.bias = float(value);
      publish(e);
    });
    connect(invert_, &QCheckBox::toggled, this, [this](bool value) {
      auto e = effect_;
      e.invert = value;
      publish(e);
    });
    connect(opacity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      auto e = effect_;
      e.opacity = float(value / 100);
      publish(e);
    });
    connect(blend_, &QComboBox::currentIndexChanged, this, [this](int value) {
      if (value >= 0) {
        auto e = effect_;
        e.blending = LayerBlend(blend_->currentData().toInt());
        publish(e);
      }
    });
  }
  std::function<void(const MaskEffect &)> changed;
  std::function<void(const MaskEffect &, int)> content_changed;
  std::function<void(int, int)> input_requested;
  std::function<void()> bake_requested;
  std::function<void(const QUuid &)> anchor_requested;
  void synchronize(const TextureSet *set, const MaskEffect *effect, int channel = -1)
  {
    setVisible(effect);
    if (!effect || !set)
      return;
    effect_ = *effect;
    content_channel_ = channel;
    for (int slot = 0; slot < 2; ++slot) {
      anchor_inputs_[slot]->setVisible(slot < generator_input_count(effect->generator));
      if (slot < generator_input_count(effect->generator))
        anchor_inputs_[slot]->synchronize(*set,
                                          set->selected_layer,
                                          effect->input_anchors[slot],
                                          generator_input_role(effect->generator, slot));
    }
    resource_->setText(effect->generator == 0 ? "Generator\nNo generator selected" :
                                                mask_generator_names[effect->generator]);
    controls_->setVisible(effect->generator != 0);
    {
      const QSignalBlocker block(channel_);
      channel_->clear();
      for (int c = 0; c < 5; ++c)
        if (c != 3 && set->channels[c])
          channel_->addItem(channel_names[c], c);
      if (set->ao_channel)
        channel_->addItem("Ambient occlusion", 5);
      for (int i = 0; i < 8; ++i)
        if (set->users[i].present)
          channel_->addItem(QString::fromStdString(set->users[i].name), 6 + i);
      if (set->displacement.channel)
        channel_->addItem("Displacement", 14);
      channel_->setCurrentIndex(channel_->findData(channel));
      channel_->setVisible(channel >= 0);
      if (auto *label = static_cast<QFormLayout *>(controls_->layout())->labelForField(channel_))
        label->setVisible(channel >= 0);
    }
    auto *form = static_cast<QFormLayout *>(controls_->layout());
    const unsigned required = mask_generator_inputs(effect->generator);
    QStringList descriptions;
    bool missing = false;
    {
      const QSignalBlocker block(input_role_);
      const int previous = input_role_->currentData().toInt();
      input_role_->clear();
      for (int role = 0; role < 7; ++role)
        if (required & (1u << role)) {
          int input_slot = -1;
          for (int slot = 0; slot < generator_input_count(effect->generator); ++slot)
            if (generator_input_role(effect->generator, slot) == role)
              input_slot = slot;
          const auto ref = input_slot >= 0 ? effect->input_anchors[input_slot] : AnchorReference{};
          const bool anchored = !ref.source.isNull();
          if (!anchored)
            input_role_->addItem(mesh_map_names[role], role);
          bool available = anchored ? TextureDocument::anchor_reference_problem(
                                          *set, set->selected_layer, ref)
                                          .isEmpty() :
                                      bool(set->mesh_maps[role]);
          if (available && !anchored)
            for (int number : set->tiles) {
              const auto found = set->mesh_maps[role]->tiles.find(number);
              available &= found != set->mesh_maps[role]->tiles.end() &&
                           bool(found->second.pixels);
            }
          missing |= !available && !anchored;
          const QString name =
              anchored  ? QString("%1 input: anchor").arg(mesh_map_names[role]) :
              available ? QString::fromStdString(set->mesh_maps[role]->name) :
                          QString("%1 map required — Bake Mesh Maps…").arg(mesh_map_names[role]);
          descriptions.append(name);
        }
      const int index = input_role_->findData(previous);
      input_role_->setCurrentIndex(index >= 0 ? index : input_role_->count() ? 0 : -1);
    }
    auto *replace = findChild<QPushButton *>("generatorReplaceInput");
    replace->setEnabled(input_role_->count() > 0);
    replace->setToolTip("Replace a baked input map. Change anchor inputs using Input source.");
    if (missing && effect->surface_fallback)
      descriptions.append("Provisional 3D surface fallback — bake for actual mesh-map response");
    input_->setText(descriptions.join("\n"));
    bake_->setVisible(missing);
    input_role_->setVisible(input_role_->count() > 1);
    if (auto *label = form->labelForField(input_role_))
      label->setVisible(input_role_->count() > 1);
    {
      const QSignalBlocker block(fallback_);
      fallback_->setChecked(effect->surface_fallback);
    }
    {
      const QSignalBlocker block(tile_);
      const int previous = tile_->currentData().toInt();
      tile_->clear();
      for (int number : set->tiles)
        tile_->addItem(QString::number(number), number);
      const int index = tile_->findData(previous);
      tile_->setCurrentIndex(index >= 0 ? index : 0);
    }
    tile_->setVisible(set->tiles.size() > 1 && input_role_->count() > 0);
    if (auto *label = form->labelForField(tile_))
      label->setVisible(set->tiles.size() > 1 && input_role_->count() > 0);
    map_filter_->setEnabled(input_role_->count() > 0);
    map_filter_->setToolTip(
        "Baked map filtering; anchors sample matching UVs at Texture Set resolution.");
    const auto index = [](QComboBox *control, int value) {
      const QSignalBlocker block(control);
      control->setCurrentIndex(value);
    };
    const auto value = [](QDoubleSpinBox *control, double v) {
      const QSignalBlocker block(control);
      control->setValue(v);
    };
    index(curve_, effect->curve);
    index(map_filter_, effect->map_filter);
    index(axis_, effect->axis);
    index(blend_, blend_->findData(int(effect->blending)));
    index(grunge_projection_, effect->triplanar_grunge ? 1 : 0);
    {
      const QSignalBlocker block(contrast_);
      contrast_->setRange(0, effect->response ? 1 : 8);
      contrast_->setSingleStep(effect->response ? .05 : .1);
    }
    value(contrast_, effect->response ? (effect->contrast - 1) / 7 : effect->contrast);
    value(bias_, effect->bias);
    value(opacity_, effect->opacity * 100);
    value(dirt_, effect->dirt_curvature);
    for (int c = 0; c < 3; ++c)
      value(light_components_[c], effect->light_direction[c]);
    {
      const QSignalBlocker block(invert_);
      invert_->setChecked(effect->invert);
    }
    native_controls_->setVisible(effect->generator >= 7 || effect->response);
    for (const auto &[control, member] : native_parameters_)
      value(control, effect->*member);
    value(level_, std::clamp(.5f + (effect->response ? effect->bias : -effect->bias), 0.f, 1.f));
    for (const auto &field : slider_fields_)
      synchronize_slider(field);
    axis_->setVisible(effect->generator == 1);
    axis_label_->setVisible(effect->generator == 1);
    light_->setVisible(effect->generator == 6);
    light_label_->setVisible(effect->generator == 6);
    dirt_->setVisible(effect->generator == 5);
    dirt_label_->setVisible(effect->generator == 5);
  }

 private:
  struct SliderField {
    QDoubleSpinBox *spin;
    QSlider *slider;
    bool logarithmic;
  };
  std::vector<SliderField> slider_fields_;
  static int slider_position(const SliderField &field)
  {
    const double low = field.spin->minimum(), high = field.spin->maximum(),
                 value = field.spin->value();
    return qRound(1000 * (field.logarithmic ? std::log(value / low) / std::log(high / low) :
                                              (value - low) / (high - low)));
  }
  static void synchronize_slider(const SliderField &field)
  {
    const QSignalBlocker block(field.slider);
    field.slider->setValue(slider_position(field));
  }
  QWidget *slider_field(QDoubleSpinBox *control, bool logarithmic = false)
  {
    auto *field = new QWidget(controls_);
    auto *row = new QHBoxLayout(field);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto *slider = new QSlider(Qt::Horizontal, field);
    slider->setObjectName(control->objectName() + "Slider");
    slider->setRange(0, 1000);
    control->setMaximumWidth(82);
    row->addWidget(slider, 1);
    row->addWidget(control);
    const SliderField metadata{control, slider, logarithmic};
    slider_fields_.push_back(metadata);
    connect(control, &QDoubleSpinBox::valueChanged, this, [metadata] {
      synchronize_slider(metadata);
    });
    connect(slider, &QSlider::valueChanged, this, [metadata](int position) {
      const double low = metadata.spin->minimum(), high = metadata.spin->maximum(),
                   fraction = position / 1000.;
      metadata.spin->setValue(metadata.logarithmic ? low * std::pow(high / low, fraction) :
                                                     low + (high - low) * fraction);
    });
    return field;
  }
  QWidget *native_controls_ = nullptr;
  QDoubleSpinBox *level_ = nullptr;
  QComboBox *grunge_projection_ = nullptr;
  QCheckBox *fallback_ = nullptr;
  std::vector<std::pair<QDoubleSpinBox *, float MeshGeneratorSettings::*>> native_parameters_;
  std::array<GeneratorAnchorPanel *, 2> anchor_inputs_{};
  MaskEffect effect_;
  int content_channel_ = -1;
  QComboBox *channel_;
  QToolButton *resource_;
  QWidget *controls_, *light_;
  QLabel *input_, *axis_label_, *light_label_, *dirt_label_;
  QComboBox *input_role_, *tile_, *axis_, *blend_, *curve_, *map_filter_;
  QDoubleSpinBox *contrast_, *bias_, *opacity_, *dirt_;
  std::array<QDoubleSpinBox *, 3> light_components_{};
  QCheckBox *invert_;
  QPushButton *bake_;
  QDoubleSpinBox *spin(const char *name, double low, double high, double step, int decimals)
  {
    auto *control = new bts::NumericSpinBox(controls_);
    control->setObjectName(name);
    control->setLocale(QLocale::c());
    control->setRange(low, high);
    control->setDecimals(decimals);
    control->setSingleStep(step);
    control->setKeyboardTracking(false);
    return control;
  }
  void publish(const MaskEffect &effect)
  {
    effect_ = effect;
    if (content_channel_ >= 0) {
      if (content_changed)
        content_changed(effect, content_channel_);
    }
    else if (changed)
      changed(effect);
  }
};
}  // namespace bts
