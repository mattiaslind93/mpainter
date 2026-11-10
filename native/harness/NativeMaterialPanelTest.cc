// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialPanelTest.hh"
#include "NativeMaterialPanel.hh"
#include "FillPanel.hh"
#include <QLabel>
#include <QUuid>
#include "ColorPicker.hh"
#include "ColorPickerTest.hh"
#include "NumericLocale.hh"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QScrollArea>
#include <QDir>
#include <QPainter>
#include <QScopeGuard>
#include "BKE_colorband.hh"
#include "DNA_colorband_types.h"
#include "IMB_colormanagement.hh"
#include <stdexcept>
#include <algorithm>
#include <numbers>
namespace bts {
namespace {
QJsonObject verify_native_ramp_preview()
{
  using namespace blender;
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  const std::string previous = IMB_colormanagement_working_space_get();
  const std::string source_space = IMB_colormanagement_working_space_get_default();
  const auto restore = qScopeGuard(
      [&] { IMB_colormanagement_working_space_set_from_name(previous.c_str()); });
  IMB_colormanagement_working_space_set_from_name("ACEScg");
  require(std::string(IMB_colormanagement_working_space_get()) == "ACEScg",
          "Native preview test needs the artist ACEScg owner context");
  NativeMaterialDescription description;
  description.material = "Native ramp preview";
  description.group = "Source-space ramp";
  description.surface_type = "ShaderNodeGroup";
  for (int c = 0; c < 3; ++c) {
    float to[4]{}, from[4]{};
    to[c] = from[c] = 1;
    to[3] = from[3] = 1;
    IMB_colormanagement_transform_v4(to, source_space.c_str(), "ACEScg");
    IMB_colormanagement_transform_v4(from, "ACEScg", source_space.c_str());
    for (int r = 0; r < 3; ++r) {
      description.color_to_aces[c * 3 + r] = to[r];
      description.color_from_aces[c * 3 + r] = from[r];
    }
  }
  NativeMaterialParameter parameter;
  parameter.identifier = "Ramp";
  parameter.name = "ColorRamp";
  parameter.type = NativeParameterType::Color;
  parameter.source_node = "ShaderNodeValToRGB";
  parameter.source_socket = "Color";
  parameter.maximum = 65504;
  description.parameters.push_back(parameter);
  struct Fixture {
    int interpolation, mode, hue, count;
    bool hdr = false, alpha = false;
  };
  std::vector<Fixture> fixtures{{4, 0, 0, 3}};  // Reproduce actual material constant mode first.
  for (int interpolation = 0; interpolation < 4; ++interpolation)
    fixtures.push_back({interpolation, 0, 0, 3});
  for (int mode : {1, 2})
    for (int hue = 0; hue < 4; ++hue)
      fixtures.push_back({0, mode, hue, 3});
  fixtures.push_back({0, 0, 0, 1, true});
  fixtures.push_back({2, 0, 0, 32, true});
  fixtures.push_back({4, 0, 0, 32});
  fixtures.push_back({0, 0, 0, 3, false, true});
  int pixels = 0, maximum = 0, edits = 0;
  QJsonArray cases;
  for (const auto &fixture : fixtures) {
    ColorBand band{};
    band.tot = short(fixture.count);
    band.ipotype = eColorBand_Interp(fixture.interpolation);
    band.color_mode = eColorBand_ColorMode(fixture.mode);
    band.ipotype_hue = eColorBand_HueInterp(fixture.hue);
    QJsonArray stops;
    const std::array<std::array<float, 3>, 3> colors{
        {{.03f, .06f, .14f}, {.7f, .2f, .05f}, {.02f, .5f, .08f}}};
    for (int i = 0; i < fixture.count; ++i) {
      const auto &rgb = colors[i % 3];
      const float position = fixture.count == 1 ? .4f :
                             fixture.count == 3 ? (i == 0 ? 0 :
                                                   i == 1 ? .35f :
                                                            1) :
                                                  float(i) / (fixture.count - 1);
      const float r = fixture.hdr ? i % 2 ? -1.25f : 2.f : rgb[0], g = fixture.hdr ? 2.5f : rgb[1],
                  b = fixture.hdr ? -.25f : rgb[2], a = fixture.alpha ? .25f + .25f * i : 1;
      band.data[i] = {r, g, b, a, position, i};
      stops.append(QJsonObject{{"position", position}, {"color", QJsonArray{r, g, b, a}}});
    }
    description.parameters[0].ramp = {{"interpolation", fixture.interpolation},
                                      {"color_mode", fixture.mode},
                                      {"hue_interpolation", fixture.hue},
                                      {"stops", stops}};
    const auto unchanged = native_material_description_json(description);
    NativeMaterialPanel panel;
    panel.edit_started = [&] { ++edits; };
    panel.edit_finished = [&] { ++edits; };
    panel.changed = [&](const auto &) { ++edits; };
    panel.synchronize(description);
    panel.resize(360, 2400);
    panel.show();
    QApplication::processEvents();
    auto *strip = panel.findChild<QWidget *>("nativeColorRamp");
    require(strip, "Native preview strip missing");
    const auto image = strip->grab().toImage();
    const double ratio = image.devicePixelRatio();
    const int left = qRound(8 * ratio), columns = qRound((strip->width() - 16) * ratio),
              y = qRound(18 * ratio);
    int case_maximum = 0;
    for (int x = 2; x < columns - 2; ++x) {
      float rgba[4];
      require(BKE_colorband_evaluate(&band, float(x + .5f) / columns, rgba),
              "Native preview reference failed");
      IMB_colormanagement_transform_v4(
          rgba, source_space.c_str(), IMB_colormanagement_srgb_colorspace_name_get());
      const auto color = QColor::fromRgbF(std::clamp(rgba[0], 0.f, 1.f),
                                          std::clamp(rgba[1], 0.f, 1.f),
                                          std::clamp(rgba[2], 0.f, 1.f),
                                          std::clamp(rgba[3], 0.f, 1.f));
      QImage reference(1, 1, QImage::Format_ARGB32_Premultiplied);
      reference.fill(image.pixelColor(left + x, qRound(2 * ratio)));
      {
        QPainter painter(&reference);
        painter.fillRect(reference.rect(), color);
      }
      const auto expected = reference.pixelColor(0, 0), actual = image.pixelColor(left + x, y);
      const int error = std::max({std::abs(actual.red() - expected.red()),
                                  std::abs(actual.green() - expected.green()),
                                  std::abs(actual.blue() - expected.blue()),
                                  std::abs(actual.alpha() - expected.alpha())});
      case_maximum = std::max(case_maximum, error);
      ++pixels;
    }
    if (case_maximum > 1)
      throw std::runtime_error(
          ("Native ramp preview differs from SOURCE-space ColorBand sampling: interpolation=" +
           std::to_string(fixture.interpolation) + " mode=" + std::to_string(fixture.mode) +
           " hue=" + std::to_string(fixture.hue) + " error=" + std::to_string(case_maximum))
              .c_str());
    maximum = std::max(maximum, case_maximum);
    require(native_material_description_json(panel.state()) == unchanged && edits == 0,
            "Painting a native ramp edited authored values");
    cases.append(QJsonObject{{"interpolation", fixture.interpolation},
                             {"color_mode", fixture.mode},
                             {"hue", fixture.hue},
                             {"stops", fixture.count},
                             {"hdr", fixture.hdr},
                             {"alpha", fixture.alpha},
                             {"maximum_byte_error", case_maximum}});
  }
  return {{"cases", cases},
          {"pixel_checks", pixels},
          {"maximum_byte_error", maximum},
          {"allowed_byte_error", 1},
          {"authored_edits", edits},
          {"source_space", QString::fromStdString(source_space)},
          {"owner_display_space", "ACEScg"},
          {"working_space_restored", true}};
}
}  // namespace
QJsonObject verify_native_material_panel(const QString &output)
{
  verify_color_picker_enter();  // Ordinary --cpu-native-material-test regression, no GPU.
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  NativeMaterialDescription source;
  source.material = "Native group fixture";
  source.group = "Artist inputs";
  source.surface_type = "ShaderNodeGroup";
  NativeMaterialParameter scalar;
  scalar.identifier = "Scale";
  scalar.name = "Scale";
  scalar.minimum = 0;
  scalar.maximum = 1;
  scalar.value[0] = .2;
  source.parameters.push_back(scalar);
  auto color = scalar;
  color.identifier = "Tint";
  color.name = "Tint";
  color.type = NativeParameterType::Color;
  color.value = {.2, .4, .8, 1};
  source.parameters.push_back(color);
  auto vector = scalar;
  vector.identifier = "Offset";
  vector.name = "Offset";
  vector.type = NativeParameterType::Vector;
  vector.value = {.1, .2, .3, 0};
  source.parameters.push_back(vector);
  auto ramp = color;
  ramp.identifier = "Ramp";
  ramp.name = "Color ramp";
  ramp.source_node = "ShaderNodeValToRGB";
  ramp.ramp = {{"color_mode", 0},
               {"hue_interpolation", 0},
               {"interpolation", 0},
               {"stops",
                QJsonArray{QJsonObject{{"position", 0}, {"color", QJsonArray{0, 0, 0, 1}}},
                           QJsonObject{{"position", 1}, {"color", QJsonArray{1, .5, .1, 1}}}}}};
  source.parameters.push_back(ramp);
  auto image = color;
  image.identifier = "Texture";
  image.name = "Texture";
  image.source_node = "ShaderNodeTexImage";
  source.parameters.push_back(image);
  auto unknown = scalar;
  unknown.identifier = "Linked";
  unknown.name = "Linked input";
  unknown.source_node = "ShaderNodeMix";
  unknown.editable = false;
  source.parameters.push_back(unknown);
  NativeMaterialPanel panel;
  int starts = 0, finishes = 0, changes = 0;
  QString requested;
  NativeMaterialDescription latest;
  panel.edit_started = [&] { ++starts; };
  panel.edit_finished = [&] { ++finishes; };
  panel.changed = [&](const auto &value) {
    ++changes;
    latest = value;
    panel.synchronize(value);
  };
  panel.texture_requested = [&](const QString &id) { requested = id; };
  panel.synchronize(source);
  panel.resize(420, 1100);
  panel.show();
  QApplication::processEvents();
  auto *height_choice = panel.findChild<QComboBox *>("nativeHeightPolicy");
  auto *height_warning = panel.findChild<QLabel *>("nativeHeightPolicyWarning");
  require(height_choice && height_choice->itemText(height_choice->findData(
                               int(NativeHeightPolicy::RawAuthoredFloatV1))) ==
                               QString::fromUtf8("Raw authored Height — scalar Float"),
          "Raw Height label incorrectly excludes retained scalar images");
  require(height_warning && height_warning->text().contains("retained Non-Color image Red") &&
              height_warning->text().contains("H, unchanged") &&
              height_warning->text().contains("neutral is 0.5") &&
              height_warning->text().contains("does not reproduce native Bump shading") &&
              height_warning->text().contains("eligibility is checked before enabling"),
          "Raw Height warning lost source eligibility or raw-H/neutral/non-parity semantics");
  const auto mouse = [](QWidget *widget,
                        QEvent::Type type,
                        QPointF at,
                        Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
    QMouseEvent event(
        type, at, widget->mapToGlobal(at.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
  };
  auto *gradient = panel.findChild<QWidget *>("nativeGradient_Scale");
  auto *number = panel.findChild<QDoubleSpinBox *>("nativeValue_Scale_0");
  require(gradient && number, "Native scalar gradient missing");
  const auto *identity = number;
  mouse(gradient, QEvent::MouseButtonPress, {30, 25}, Qt::LeftButton, Qt::LeftButton);
  for (int i = 0; i < 30; ++i)
    mouse(gradient, QEvent::MouseMove, {qreal(35 + i * 3), 25}, Qt::NoButton, Qt::LeftButton);
  mouse(gradient, QEvent::MouseButtonRelease, {160, 25}, Qt::LeftButton, Qt::NoButton);
  require(starts == 1 && finishes == 1 && changes == 1,
          "Native slider did not coalesce one gesture");
  require(latest.parameters[0].value[0] > .2 &&
              identity == panel.findChild<QDoubleSpinBox *>("nativeValue_Scale_0"),
          "Native preview rebuilt the active editor");
  auto *text = number->findChild<QLineEdit *>();
  require(text, "Native scalar text editor missing");
  text->setText("0,25");
  number->interpretText();
  QMetaObject::invokeMethod(number, "editingFinished", Qt::DirectConnection);
  require(std::abs(latest.parameters[0].value[0] - .25) < 1e-6 && number->text().contains('.') &&
              !number->text().contains(','),
          "Native numeric locale or comma input failed");
  auto *ramp_widget = panel.findChild<QWidget *>("nativeColorRamp");
  require(ramp_widget, "Native color ramp missing");
  mouse(ramp_widget, QEvent::MouseButtonPress, {10, 38}, Qt::LeftButton, Qt::LeftButton);
  mouse(ramp_widget,
        QEvent::MouseMove,
        {qreal(ramp_widget->width() * .4), 38},
        Qt::NoButton,
        Qt::LeftButton);
  mouse(ramp_widget,
        QEvent::MouseButtonRelease,
        {qreal(ramp_widget->width() * .4), 38},
        Qt::LeftButton,
        Qt::NoButton);
  const auto position =
      latest.parameters[3].ramp["stops"].toArray()[0].toObject()["position"].toDouble();
  require(position > .3 && position < .5, "Native color ramp handle did not move");
  auto *precise = panel.findChild<QDoubleSpinBox *>("nativeRampPosition_Ramp_0");
  require(precise && std::abs(precise->value() - position) < 1e-4,
          "Native ramp handle and precision field diverged");
  auto *picker = dynamic_cast<ColorPicker *>(panel.findChild<QWidget *>("nativeColor_Tint"));
  require(picker, "Native color picker missing");
  picker->accept_sample({.6, .3, .9, 1});
  require(std::abs(latest.parameters[1].value[0] - .6) < 1e-6,
          "Native color input did not publish");
  auto *button = panel.findChild<QPushButton *>("nativeTexture_Texture");
  require(button, "Native image resource input missing");
  button->click();
  require(requested == "Texture", "Native image chooser lost socket identity");
  require(starts == finishes && starts == 4, "Native property gestures were unbalanced");
  require(native_material_description_json(
              read_native_material_description(native_material_description_json(latest))) ==
              native_material_description_json(latest),
          "Native edited inputs did not roundtrip");
  auto malformed = latest;
  malformed.parameters[3].ramp["stops"] = QJsonArray{
      QJsonObject{{"position", 0}, {"color", QJsonArray{1}}}};
  bool rejected = false;
  try {
    panel.synchronize(malformed);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Malformed native ramp reached the editor");
  // The real FillPanel emits a new immutable descriptor through its ordinary
  // property gesture callbacks, without modifying the saved source/defaults.
  FillPanel fill;
  FillProjection projection;
  projection.material_resource = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  projection.native_material = std::make_shared<const NativeMaterialDescription>(source);
  const auto saved = projection.native_material;
  FillProjection result;
  int fill_begin = 0, fill_end = 0, fill_changes = 0;
  fill.edit_started = [&] { ++fill_begin; };
  fill.edit_finished = [&] { ++fill_end; };
  fill.changed = [&](const auto &value) {
    ++fill_changes;
    result = value;
  };
  fill.synchronize(projection);
  auto *editor = fill.findChild<QDoubleSpinBox *>("nativeValue_Scale_0");
  require(editor, "Native inputs were not attached to fill properties");
  editor->setValue(.7);
  QMetaObject::invokeMethod(editor, "editingFinished", Qt::DirectConnection);
  require(fill_begin == 1 && fill_end == 1 && fill_changes == 1 && result.native_material &&
              std::abs(result.native_material->parameters[0].value[0] - .7) < 1e-6 &&
              saved->parameters[0].value[0] == .2,
          "Native fill edit did not detach/balance through the property path");
  auto *native_image = fill.findChild<QPushButton *>("nativeTexture_Texture");
  require(native_image && !native_image->isEnabled(),
          "Unsupported native image rebinding was presented as editable");
  auto *fill_ramp = fill.findChild<QWidget *>("nativeColorRamp");
  require(fill_ramp && fill_ramp->isEnabled(),
          "Live native ramp remained disabled in fill properties");
  mouse(fill_ramp, QEvent::MouseButtonPress, {10, 38}, Qt::LeftButton, Qt::LeftButton);
  mouse(fill_ramp,
        QEvent::MouseMove,
        {qreal(fill_ramp->width() * .2), 38},
        Qt::NoButton,
        Qt::LeftButton);
  mouse(fill_ramp,
        QEvent::MouseButtonRelease,
        {qreal(fill_ramp->width() * .2), 38},
        Qt::LeftButton,
        Qt::NoButton);
  require(fill_begin == 2 && fill_end == 2 &&
              result.native_material->parameters[3].ramp != saved->parameters[3].ramp &&
              saved->parameters[3].ramp == source.parameters[3].ramp,
          "Native fill ramp did not detach its instance or balance a single gesture");

  // Stop topology is authored through the actual FillPanel property path. It
  // changes one instance and one gesture, while the shared source stays intact.
  const auto before_topology = native_material_description_json(*result.native_material);
  const auto click_stop = [&](const char *prefix) {
    auto *button = fill.findChild<QPushButton *>(QString::fromLatin1(prefix) + "Ramp");
    require(button && button->isEnabled(), "Native ColorRamp topology control missing/disabled");
    button->click();
    QApplication::processEvents();
  };
  click_stop("nativeRampDuplicate_");
  require(fill_begin == 3 && fill_end == 3 && fill_changes == 3 &&
              result.native_material->parameters[3].ramp["stops"].toArray().size() == 3,
          "Duplicating a native stop was not one property gesture");
  auto duplicates = result.native_material->parameters[3].ramp["stops"].toArray();
  require(duplicates[0] == duplicates[1] && saved->parameters[3].ramp == source.parameters[3].ramp,
          "Duplicate stop altered its value or shared source");
  const auto duplicated = native_material_description_json(*result.native_material);
  // Coincident handles must keep the selected new duplicate on a distance tie.
  auto *duplicate_handle = fill.findChild<QWidget *>("nativeColorRamp");
  const auto original_position = duplicates[0].toObject()["position"].toDouble();
  const qreal handle_x = 8 + original_position * (duplicate_handle->width() - 16),
              moved_x = 8 + (original_position + .2) * (duplicate_handle->width() - 16);
  mouse(
      duplicate_handle, QEvent::MouseButtonPress, {handle_x, 38}, Qt::LeftButton, Qt::LeftButton);
  mouse(duplicate_handle, QEvent::MouseMove, {moved_x, 38}, Qt::NoButton, Qt::LeftButton);
  mouse(duplicate_handle, QEvent::MouseButtonRelease, {moved_x, 38}, Qt::LeftButton, Qt::NoButton);
  const auto moved = result.native_material->parameters[3].ramp["stops"].toArray();
  require(moved[0] == duplicates[0] &&
              moved[1].toObject()["position"].toDouble() > original_position + .1,
          "Dragging a duplicate moved the original coincident stop");
  click_stop("nativeRampRemove_");
  require(fill_begin == 5 && fill_end == 5 && fill_changes == 5 &&
              native_material_description_json(*result.native_material) == before_topology,
          "Removing the selected duplicate failed exact topology restoration");
  // Restoring immutable edit-state descriptors (the Undo/Redo boundary) rebuilds
  // the stop rows, without publishing another authored gesture.
  auto restored = projection;
  restored.native_material = std::make_shared<const NativeMaterialDescription>(
      read_native_material_description(duplicated));
  fill.synchronize(restored);
  require(fill.findChild<QDoubleSpinBox *>("nativeRampPosition_Ramp_2"),
          "Restored duplicate has no editor row");
  restored.native_material = std::make_shared<const NativeMaterialDescription>(
      read_native_material_description(before_topology));
  fill.synchronize(restored);
  require(!fill.findChild<QDoubleSpinBox *>("nativeRampPosition_Ramp_2") && fill_changes == 5,
          "Restoring ramp topology emitted a new property edit");
  for (int n = 2; n < 32; ++n)
    click_stop("nativeRampDuplicate_");
  auto *full = fill.findChild<QPushButton *>("nativeRampDuplicate_Ramp");
  require(full && !full->isEnabled() &&
              result.native_material->parameters[3].ramp["stops"].toArray().size() == 32,
          "Native ColorBand's 32-stop bound was not enforced");
  const auto full_state = native_material_description_json(*result.native_material);
  full->click();
  require(native_material_description_json(*result.native_material) == full_state,
          "Disabled duplicate changed a full ColorBand");
  for (int n = 32; n > 1; --n)
    click_stop("nativeRampRemove_");
  auto *last = fill.findChild<QPushButton *>("nativeRampRemove_Ramp");
  require(last && !last->isEnabled() &&
              result.native_material->parameters[3].ramp["stops"].toArray().size() == 1,
          "The final ColorRamp stop could be removed");
  require(fill_begin == fill_end && fill_begin == 66 && fill_changes == 66,
          "Topology property gestures did not remain balanced");
  require(native_material_description_json(read_native_material_description(
              native_material_description_json(*result.native_material))) ==
                  native_material_description_json(*result.native_material) &&
              saved->parameters[3].ramp == source.parameters[3].ramp,
          "Native topology persistence changed source or metadata");

  auto converted = source;
  converted.channels = {0, 1, 2};
  converted.color_to_aces = {2, 0, 0, 0, 3, 0, 0, 0, 4};
  converted.color_from_aces = {.5, 0, 0, 0, 1.f / 3, 0, 0, 0, .25};
  NativeMaterialPanel color_panel;
  color_panel.synchronize(converted);
  auto *working_picker = dynamic_cast<ColorPicker *>(
      color_panel.findChild<QWidget *>("nativeColor_Tint"));
  require(working_picker && std::abs(working_picker->color()[0] - .4) < 1e-6 &&
              std::abs(working_picker->color()[2] - 3.2) < 1e-6,
          "Native picker omitted linear primary conversion");
  NativeMaterialDescription color_edit;
  color_panel.changed = [&](const auto &value) { color_edit = value; };
  working_picker->accept_sample({2, 3, 4, .4});
  require(std::abs(color_edit.parameters[1].value[0] - 1) < 1e-6 &&
              std::abs(color_edit.parameters[1].value[1] - 1) < 1e-6 &&
              std::abs(color_edit.parameters[1].value[3] - .4) < 1e-6,
          "Native picker converted its canonical sample incorrectly");
  auto capabilities = projection;
  capabilities.native_material = std::make_shared<const NativeMaterialDescription>(converted);
  fill.set_fill_context(true);
  fill.synchronize(capabilities);
  auto *bump_toggle = fill.findChild<QCheckBox *>("fillChannelEnabled4"),
       *disp_toggle = fill.findChild<QCheckBox *>("fillChannelEnabled14");
  require(bump_toggle && bump_toggle->isEnabled(),
          "Native source shading gate was disabled because scalar Height is unavailable");
  require(disp_toggle && !disp_toggle->isEnabled(),
          "Unsupported native Displacement can be enabled");
  require(fill.findChild<QCheckBox *>("fillChannelEnabled0")->isEnabled(),
          "Supported native Color was disabled");
  require(bump_toggle->toolTip().contains("Normal Map and Bump shading") &&
              bump_toggle->toolTip().contains(
                  "Height export requires a separate Authored Height setting"),
          "Native Normal switch obscures shading or Height export behavior");
  require(bump_toggle->text() == "normal" && bump_toggle->accessibleName() == "Normal / Bump" &&
              !bump_toggle->isHidden(),
          "Native Normal shading switch is hidden or mislabeled");
  const auto scalar_capabilities = native_material_description_json(*capabilities.native_material);
  int shading_gate_events = 0;
  fill.channel_enabled = [&](int channel, bool) {
    require(channel == 4, "Native shading gate changed another channel");
    ++shading_gate_events;
  };
  const bool was_enabled = bump_toggle->isChecked();
  bump_toggle->setChecked(!was_enabled);
  bump_toggle->setChecked(was_enabled);
  fill.channel_enabled = {};
  require(
      shading_gate_events == 2 && fill_changes == 66 &&
          native_material_description_json(*capabilities.native_material) == scalar_capabilities &&
          capabilities.native_material->height_policy == NativeHeightPolicy::None &&
          capabilities.native_material->channels == std::vector<int>({0, 1, 2}),
      "Native shading gate changed authored H/policy or advertised unsupported scalar channels");
  auto raw_height = capabilities;
  auto raw_description = std::make_shared<NativeMaterialDescription>(*raw_height.native_material);
  raw_description->height_policy = NativeHeightPolicy::RawAuthoredFloatV1;
  raw_height.native_material = raw_description;
  fill.synchronize(raw_height);
  require(bump_toggle->text() == "bump" && bump_toggle->accessibleName() == "Bump",
          "Raw authored Height is mislabeled as source Normal");
  fill.synchronize(FillProjection{});
  require(bump_toggle->text() == "bump" && bump_toggle->accessibleName() == "Bump",
          "Ordinary Fill retained the native Normal label");
  fill.synchronize(capabilities);
  // Linked Mapping controls author a separate immutable instance transform. The
  // source vector, coordinate socket, and other instances remain untouched.
  auto mapped_source = source;
  auto mapping = vector;
  mapping.identifier = "Input_2";
  mapping.name = "Mapping";
  mapping.mapping = true;
  mapping.source_node = "ShaderNodeTexCoord";
  mapping.source_socket = "UV";
  mapped_source.parameters.push_back(mapping);
  FillPanel mapped_fill;
  auto mapped_projection = projection;
  mapped_projection.native_material = std::make_shared<const NativeMaterialDescription>(
      mapped_source);
  const auto mapped_original = mapped_projection.native_material;
  FillProjection mapped_result;
  int mapped_begin = 0, mapped_end = 0, mapped_changes = 0;
  mapped_fill.edit_started = [&] { ++mapped_begin; };
  mapped_fill.edit_finished = [&] { ++mapped_end; };
  mapped_fill.changed = [&](const auto &value) {
    ++mapped_changes;
    mapped_result = value;
    mapped_fill.synchronize(value);
  };
  mapped_fill.synchronize(mapped_projection);
  auto *mapping_x = mapped_fill.findChild<QDoubleSpinBox *>("nativeMapping_Input_2_Offset_0");
  auto *mapping_rotation = mapped_fill.findChild<QDoubleSpinBox *>(
      "nativeMapping_Input_2_Rotation_2");
  auto *mapping_scale = mapped_fill.findChild<QDoubleSpinBox *>("nativeMapping_Input_2_Scale_1");
  require(mapping_x && mapping_rotation && mapping_scale, "Linked Mapping XYZ controls missing");
  const auto *mapping_identity = mapping_x;
  mapping_x->setValue(-.25);
  QMetaObject::invokeMethod(mapping_x, "editingFinished", Qt::DirectConnection);
  require(mapped_changes == 1 && mapped_begin == 1 && mapped_end == 1 &&
              mapped_result.native_material->parameters.back().mapping_transform.offset[0] ==
                  -.25 &&
              mapping_identity ==
                  mapped_fill.findChild<QDoubleSpinBox *>("nativeMapping_Input_2_Offset_0"),
          "Mapping property gesture rebuilt editor or lost transform");
  mapping_rotation->setValue(90);
  QMetaObject::invokeMethod(mapping_rotation, "editingFinished", Qt::DirectConnection);
  mapping_scale->setValue(-2);
  QMetaObject::invokeMethod(mapping_scale, "editingFinished", Qt::DirectConnection);
  const auto &mapped_parameter = mapped_result.native_material->parameters.back();
  require(std::abs(mapped_parameter.mapping_transform.rotation_xyz_radians[2] -
                   std::numbers::pi / 2) < 1e-12 &&
              mapped_parameter.mapping_transform.scale[1] == -2,
          "Mapping UI degrees/negative scale conversion failed");
  require(mapped_parameter.value == mapping.value && mapped_parameter.source_socket == "UV" &&
              mapped_original->parameters.back().mapping_transform.identity(),
          "Mapping edit altered source/defaults");
  require(native_material_description_json(*mapped_result.native_material)["version"] == 3,
          "Authored Mapping did not persist as v3");
  auto *mapping_reset = mapped_fill.findChild<QPushButton *>("nativeMappingReset_Input_2");
  require(mapping_reset, "Mapping reset control missing");
  mapping_reset->click();
  QApplication::processEvents();
  require(mapped_begin == 4 && mapped_end == 4 && mapped_changes == 4 &&
              native_material_description_json(*mapped_result.native_material) ==
                  native_material_description_json(mapped_source),
          "Mapping reset did not restore exact v2 in one gesture");
  const auto mapping_before_restore = mapped_changes;
  mapped_fill.synchronize(mapped_projection);
  require(mapped_changes == mapping_before_restore,
          "Mapping restoration emitted an authored edit");
  auto upstream = mapped_source;
  upstream.parameters.back().source_node = "ShaderNodeMapping";
  upstream.parameters.back().source_socket = "Vector";
  NativeMaterialPanel upstream_panel;
  upstream_panel.synchronize(upstream);
  require(upstream_panel.findChild<QDoubleSpinBox *>("nativeMapping_Input_2_Offset_0"),
          "Retained Mapping output has no extra Point controls");
  const auto upstream_labels = upstream_panel.findChildren<QLabel *>();
  require(std::any_of(upstream_labels.begin(),
                      upstream_labels.end(),
                      [](const auto *label) {
                        return label->text().contains("extra Point transform") &&
                               label->text().contains("original mode");
                      }) &&
              !upstream_panel.findChild<QDoubleSpinBox *>("nativeValue_Input_2_0"),
          "Retained Mapping controls mislabel original source as an editable constant");
  auto math_source = mapped_source;
  math_source.parameters.back().source_node = "ShaderNodeVectorMath";
  math_source.parameters.back().source_socket = "Vector";
  NativeMaterialPanel math_panel;
  int math_begin = 0, math_end = 0, math_changes = 0;
  math_panel.edit_started = [&] { ++math_begin; };
  math_panel.edit_finished = [&] { ++math_end; };
  NativeMaterialDescription math_result;
  math_panel.changed = [&](auto value) {
    math_result = std::move(value);
    ++math_changes;
  };
  math_panel.synchronize(math_source);
  auto *math_offset = math_panel.findChild<QDoubleSpinBox *>("nativeMapping_Input_2_Offset_0");
  require(math_offset && !math_panel.findChild<QDoubleSpinBox *>("nativeValue_Input_2_0"),
          "Vector Math source was replaced by a constant editor");
  math_offset->setValue(.375);
  QMetaObject::invokeMethod(math_offset, "editingFinished", Qt::DirectConnection);
  QApplication::processEvents();
  require(math_begin == 1 && math_end == 1 && math_changes == 1 &&
              math_result.parameters.back().mapping_transform.offset[0] == .375 &&
              math_source.parameters.back().mapping_transform.identity(),
          "Vector Math extra POINT edit did not remain per instance/one gesture");
  auto *math_reset = math_panel.findChild<QPushButton *>("nativeMappingReset_Input_2");
  require(math_reset, "Vector Math reset missing");
  math_reset->click();
  QApplication::processEvents();
  require(math_begin == 2 && math_end == 2 && math_changes == 2 &&
              native_material_description_json(math_result) ==
                  native_material_description_json(math_source),
          "Vector Math reset did not restore exact source metadata");
  auto unsupported = mapped_source;
  unsupported.parameters.back().source_node = "ShaderNodeMapping";
  NativeMaterialPanel unsupported_panel;
  unsupported_panel.synchronize(unsupported);
  require(!unsupported_panel.findChild<QDoubleSpinBox *>("nativeMapping_Input_2_Offset_0"),
          "Unknown upstream Mapping was silently overridden");
  const std::string prior_space = blender::IMB_colormanagement_working_space_get();
  const auto ramp_preview = verify_native_ramp_preview();
  require(std::string(blender::IMB_colormanagement_working_space_get()) == prior_space,
          "Ramp image checks changed the owner working space");
  QDir().mkpath(output);
  require(panel.grab().save(output + "/native-material-panel.png"),
          "Native material panel screenshot failed");
  return {{"vector_math_mapping_gestures", math_changes},
          {"linked_mapping_gestures", mapped_changes},
          {"linked_mapping_cow", true},
          {"linked_mapping_reset_exact", true},
          {"linked_mapping_degrees_to_radians", true},
          {"gestures", starts},
          {"ramp_preview", ramp_preview},
          {"preview_events", changes},
          {"gradient", true},
          {"ramp_handles", true},
          {"live_fill_ramp", true},
          {"ramp_duplicate_remove", true},
          {"duplicate_handle_drag", true},
          {"topology_restore_exact", true},
          {"colorband_stop_bounds", QJsonArray{1, 32}},
          {"topology_fill_gestures", fill_changes},
          {"ramp_precision_synced", true},
          {"color", true},
          {"vector", true},
          {"comma_input", true},
          {"editor_reused", true},
          {"image_socket", requested},
          {"serialization", true},
          {"fill_binding", true},
          {"fill_cow", true},
          {"unsupported_resources_disabled", true},
          {"linear_color_conversion", true},
          {"unsupported_displacement_disabled", true},
          {"native_bump_shading_gate_enabled", true},
          {"native_scalar_capabilities_unchanged", true},
          {"native_shading_gate_preserves_source", true}};
}
}  // namespace bts
