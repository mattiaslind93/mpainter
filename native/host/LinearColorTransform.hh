// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <cmath>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>

namespace bts {
// Explicit destination interpretation, never a translation of a source ordinal.
enum class LinearColorInput { Rec709, ACEScg };
struct LinearColorTransform {
  LinearColorInput input = LinearColorInput::ACEScg;
  std::string input_space = "ACEScg";
  // Column major. Stored coefficients preserve the chosen interpretation on Open.
  std::array<float, 9> to_aces{1, 0, 0, 0, 1, 0, 0, 0, 1};
  void validate() const
  {
    if ((input != LinearColorInput::Rec709 && input != LinearColorInput::ACEScg) ||
        input_space.empty() || input_space.find('\0') != std::string::npos)
      throw std::invalid_argument("Invalid named linear color transform");
    for (unsigned char c : input_space)
      if (c < 32 || c == 127)
        throw std::invalid_argument("Invalid linear input-space name");
    for (float value : to_aces)
      if (!std::isfinite(value))
        throw std::invalid_argument("Nonfinite linear color matrix");
  }
};
inline QString linear_color_transform_label(const LinearColorTransform &transform)
{
  return QString::fromStdString(transform.input_space) + " → ACEScg";
}
// Worker preparation only; resolves explicit named spaces without changing the
// active scene-linear role. No image is read, copied or converted here.
std::shared_ptr<const LinearColorTransform> prepare_linear_color_transform(LinearColorInput);
inline std::array<float, 3> linear_color_value(const LinearColorTransform &transform,
                                               std::array<float, 3> value,
                                               std::array<float, 3> tint = {1, 1, 1},
                                               bool enabled = true)
{
  auto result = value;
  if (enabled)
    for (int row = 0; row < 3; ++row) {
      result[row] = 0;
      for (int column = 0; column < 3; ++column)
        result[row] += transform.to_aces[column * 3 + row] * value[column];
    }
  for (int c = 0; c < 3; ++c)
    result[c] *= tint[c];
  return result;
}
inline QJsonObject linear_color_transform_json(const LinearColorTransform &transform)
{
  transform.validate();
  QJsonArray matrix;
  for (float value : transform.to_aces)
    matrix.append(value);
  return {{"version", 1},
          {"input", transform.input == LinearColorInput::Rec709 ? "linear-rec709" : "acescg"},
          {"input_space", QString::fromStdString(transform.input_space)},
          {"target", "ACEScg"},
          {"raw_linear_source", true},
          {"matrix", matrix}};
}
inline std::shared_ptr<const LinearColorTransform> read_linear_color_transform(
    const QJsonValue &value)
{
  const auto fail = []() {
    throw std::invalid_argument("Invalid linear color transform metadata");
  };
  if (!value.isObject())
    fail();
  const auto object = value.toObject();
  for (auto i = object.begin(); i != object.end(); ++i)
    if (i.key() != "version" && i.key() != "input" && i.key() != "input_space" &&
        i.key() != "target" && i.key() != "raw_linear_source" && i.key() != "matrix")
      fail();
  if (object["version"] != 1 || object["target"] != "ACEScg" ||
      object["raw_linear_source"] != true || !object["input_space"].isString() ||
      !object["matrix"].isArray() || object["matrix"].toArray().size() != 9)
    fail();
  auto result = std::make_shared<LinearColorTransform>();
  if (object["input"] == "linear-rec709")
    result->input = LinearColorInput::Rec709;
  else if (object["input"] == "acescg")
    result->input = LinearColorInput::ACEScg;
  else
    fail();
  result->input_space = object["input_space"].toString().toStdString();
  for (int i = 0; i < 9; ++i) {
    const auto component = object["matrix"].toArray()[i];
    if (!component.isDouble() || !std::isfinite(component.toDouble()) ||
        std::abs(component.toDouble()) > std::numeric_limits<float>::max())
      fail();
    result->to_aces[i] = float(component.toDouble());
  }
  result->validate();
  return result;
}
}  // namespace bts
