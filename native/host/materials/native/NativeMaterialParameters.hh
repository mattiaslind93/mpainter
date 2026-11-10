// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceAvailability.hh"
#include "NativeMaterialMapping.hh"
#include "NativeMaterialHeightPolicy.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>
namespace bts {
enum class NativeParameterType { Scalar, Vector, Color };
struct NativeMaterialParameter {
  QString identifier, name, source_node, source_socket;
  NativeParameterType type = NativeParameterType::Scalar;
  std::array<double, 4> value{};
  double minimum = 0, maximum = 1;
  bool mapping = false, editable = true;
  // Linked ramps retain their complete stop/interpolation description.
  QJsonObject ramp;
  NativeMaterialMapping mapping_transform;
  bool mapping_transform_supported() const
  {
    return mapping && editable && type == NativeParameterType::Vector &&
           ((source_node == "ShaderNodeTexCoord" &&
             (source_socket == "UV" || source_socket == "Generated" ||
              source_socket == "Object")) ||
            ((source_node == "ShaderNodeMapping" || source_node == "ShaderNodeVectorMath") &&
             source_socket == "Vector"));
  }
  int components() const
  {
    return type == NativeParameterType::Scalar ? 1 : type == NativeParameterType::Vector ? 3 : 4;
  }
  void validate() const
  {
    if (identifier.isEmpty() || name.isEmpty() || int(type) < 0 || int(type) > 2 ||
        !std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum)
      throw std::invalid_argument("Invalid native material parameter");
    for (int i = 0; i < components(); ++i)
      if (!std::isfinite(value[i]))
        throw std::invalid_argument("Non-finite native material parameter");
    mapping_transform.validate();
    if (!mapping_transform.identity() && !mapping_transform_supported())
      throw std::invalid_argument(
          "Native Mapping override requires a retained UV/Generated/Object, Blender Mapping or supported Vector Math vector output");
  }
};
struct NativeMaterialDescription {
  QString material, group, surface_type;
  std::vector<NativeMaterialParameter> parameters;
  QStringList node_types, warnings;
  std::vector<int> channels;  // Empty only for legacy/inspection-only interfaces.
  // Authored instance state. Immutable catalog/source descriptions require None.
  NativeHeightPolicy height_policy = NativeHeightPolicy::None;
  std::array<float, 9> color_to_aces{1, 0, 0, 0, 1, 0, 0, 0, 1}, color_from_aces = color_to_aces;
  void validate() const
  {
    validate_native_height_policy(height_policy);  // No allocation in the ordinary None path.
    if (material.isEmpty())
      throw std::invalid_argument("Invalid native material description");
    std::set<int> channel_ids;
    for (int channel : channels)
      if (channel < 0 || channel > 16 || !channel_ids.insert(channel).second)
        throw std::invalid_argument("Invalid native material channel capabilities");
    for (const auto &matrix : {color_to_aces, color_from_aces})
      for (float value : matrix)
        if (!std::isfinite(value) || std::abs(value) > 1e6f)
          throw std::invalid_argument("Invalid native color conversion matrix");
    // This transport description also stores application aliases and warnings.
    // Fixed native ID/socket buffers are checked where those values enter Blender.
    auto bytes = resource_product(parameters.size(),
                                  sizeof(NativeMaterialParameter) + 4 * sizeof(void *));
    bytes = resource_sum(
        bytes,
        resource_product(resource_sum(node_types.size(), warnings.size()), sizeof(QString)));
    bytes = resource_sum(bytes, resource_product(channels.size(), sizeof(int)));
    std::uint64_t text_units = 0;
    const auto text = [&](const QString &value) {
      text_units = resource_sum(text_units, value.size());
    };
    const auto json_text = [&](auto &&self, const QJsonValue &value) -> void {
      bytes = resource_sum(bytes, 2 * sizeof(QJsonValue));
      if (value.isString())
        text(value.toString());
      else if (value.isArray())
        for (const auto &child : value.toArray())
          self(self, child);
      else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
          text(it.key());
          self(self, it.value());
        }
      }
    };
    text(material);
    text(group);
    text(surface_type);
    for (const auto &value : node_types)
      text(value);
    for (const auto &value : warnings)
      text(value);
    for (const auto &p : parameters) {
      text(p.identifier);
      text(p.name);
      text(p.source_node);
      text(p.source_socket);
      json_text(json_text, p.ramp);
    }
    // Query available memory once for the aggregate, never once per parameter.

    std::set<QString> identifiers;
    for (const auto &p : parameters) {
      p.validate();
      if (!identifiers.insert(p.identifier).second)
        throw std::invalid_argument("Duplicate native material input identifier");
    }
  }
};
inline QJsonObject native_material_description_json(const NativeMaterialDescription &d)
{
  d.validate();
  QJsonArray parameters;
  bool transformed = false;
  for (const auto &p : d.parameters) {
    QJsonArray value;
    for (int i = 0; i < p.components(); ++i)
      value.append(p.value[i]);
    QJsonObject parameter{{"id", p.identifier},
                          {"name", p.name},
                          {"type", int(p.type)},
                          {"value", value},
                          {"min", p.minimum},
                          {"max", p.maximum},
                          {"mapping", p.mapping},
                          {"editable", p.editable},
                          {"source_node", p.source_node},
                          {"source_socket", p.source_socket},
                          {"ramp", p.ramp}};
    if (!p.mapping_transform.identity()) {
      parameter["mapping_transform"] = native_material_mapping_json(p.mapping_transform);
      transformed = true;
    }
    parameters.append(parameter);
  }
  QJsonArray channels, to, from;
  for (int channel : d.channels)
    channels.append(channel);
  for (float value : d.color_to_aces)
    to.append(value);
  for (float value : d.color_from_aces)
    from.append(value);
  QJsonObject result{
      {"version", d.height_policy == NativeHeightPolicy::None ? (transformed ? 3 : 2) : 4},
      {"channels", channels},
      {"color_to_aces", to},
      {"color_from_aces", from},
      {"material", d.material},
      {"group", d.group},
      {"surface_type", d.surface_type},
      {"parameters", parameters},
      {"node_types", QJsonArray::fromStringList(d.node_types)},
      {"warnings", QJsonArray::fromStringList(d.warnings)}};
  if (d.height_policy != NativeHeightPolicy::None)
    result["height_policy"] = native_height_policy_name(d.height_policy);
  return result;
}
inline NativeMaterialDescription read_native_material_description(const QJsonObject &o)
{
  if ((o["version"] != 1 && o["version"] != 2 && o["version"] != 3 && o["version"] != 4) ||
      !o["parameters"].isArray())
    throw std::invalid_argument("Unsupported native material parameter schema");
  NativeMaterialDescription d;
  d.material = o["material"].toString();
  d.group = o["group"].toString();
  d.surface_type = o["surface_type"].toString();
  if (o["version"] == 4) {
    if (!o["height_policy"].isString())
      throw std::invalid_argument("Native Height interpretation missing");
    d.height_policy = read_native_height_policy(o["height_policy"].toString());
  }
  else if (o.contains("height_policy"))
    throw std::invalid_argument("Native Height interpretations require parameter schema4");
  if (o["version"] == 2 || o["version"] == 3 || o["version"] == 4) {
    if (!o["channels"].isArray() || o["channels"].toArray().size() > 17)
      throw std::invalid_argument("Native channel capabilities missing");
    for (const auto &channel : o["channels"].toArray()) {
      if (!channel.isDouble() || channel.toDouble() != channel.toInt(-1))
        throw std::invalid_argument("Invalid native channel capability");
      d.channels.push_back(channel.toInt(-1));
    }
    const auto read_matrix = [&](const char *name, auto &matrix) {
      const auto values = o[name].toArray();
      if (!o[name].isArray() || values.size() != 9)
        throw std::invalid_argument("Native color conversion matrix missing");
      for (int i = 0; i < 9; ++i) {
        if (!values[i].isDouble())
          throw std::invalid_argument("Invalid native color conversion matrix component");
        matrix[i] = float(values[i].toDouble());
      }
    };
    read_matrix("color_to_aces", d.color_to_aces);
    read_matrix("color_from_aces", d.color_from_aces);
  }
  for (const auto &entry : o["parameters"].toArray()) {
    const auto p = entry.toObject();
    NativeMaterialParameter value;
    value.identifier = p["id"].toString();
    value.name = p["name"].toString();
    const int type = p["type"].toInt(-1);
    if (!p["type"].isDouble() || p["type"].toDouble() != type || type < 0 || type > 2)
      throw std::invalid_argument("Invalid native material parameter type");
    value.type = NativeParameterType(type);
    const auto data = p["value"].toArray();
    if (data.size() != value.components() || !p["min"].isDouble() || !p["max"].isDouble())
      throw std::invalid_argument("Invalid native material parameter values");
    for (int i = 0; i < data.size(); ++i) {
      if (!data[i].isDouble())
        throw std::invalid_argument("Invalid native material component");
      value.value[i] = data[i].toDouble();
    }
    value.minimum = p["min"].toDouble();
    value.maximum = p["max"].toDouble();
    value.mapping = p["mapping"].toBool();
    value.editable = p["editable"].toBool(true);
    value.source_node = p["source_node"].toString();
    value.source_socket = p["source_socket"].toString();
    value.ramp = p["ramp"].toObject();
    if (p.contains("mapping_transform")) {
      if (o["version"] != 3 && o["version"] != 4)
        throw std::invalid_argument("Native Mapping transforms require parameter schema3 or4");
      value.mapping_transform = read_native_material_mapping(p["mapping_transform"]);
    }
    d.parameters.push_back(std::move(value));
  }
  for (const auto &s : o["node_types"].toArray())
    d.node_types.append(s.toString());
  for (const auto &s : o["warnings"].toArray())
    d.warnings.append(s.toString());
  d.validate();
  return d;
}
inline void require_native_source_policy(const NativeMaterialDescription &description)
{
  if (description.height_policy != NativeHeightPolicy::None)
    throw std::invalid_argument(
        "Native source assets cannot carry an instance Height interpretation");
}
}  // namespace bts
