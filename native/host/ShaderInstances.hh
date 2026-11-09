// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#pragma once
#include "SubsurfaceSettings.hh"
#include "OpenPbrCompatibility.hh"
#include <optional>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace bts {
struct ShaderParameters {
  float normal_intensity = 1, ao_intensity = 1;
  SubsurfaceParameters subsurface;
  std::optional<OpenPbrParameters>
      openpbr;  // Explicit portable compatibility workflow; legacy projects remain Principled.
  bool operator==(const ShaderParameters &) const = default;
  void validate() const
  {
    if (openpbr)
      validate_openpbr_compatibility(*openpbr);
    subsurface.validate();
    if (!std::isfinite(normal_intensity) || normal_intensity < 0 || normal_intensity > 4 ||
        !std::isfinite(ao_intensity) || ao_intensity < 0 || ao_intensity > 1)
      throw std::invalid_argument("Invalid Principled shader parameters");
  }
};
struct ShaderInstance {
  QUuid id;
  QString name;
  ShaderParameters parameters;
  bool operator==(const ShaderInstance &) const = default;
};
struct ShaderInstances {
  QUuid default_instance;
  std::map<QUuid, ShaderInstance> instances;
  std::map<QUuid, QUuid> assignments;
  bool operator==(const ShaderInstances &) const = default;
  static QString key(const QUuid &id)
  {
    return id.toString(QUuid::WithoutBraces);
  }
  static void validate_name(const QString &name, bool admit = true)
  {
    if (name.isEmpty() || name.trimmed() != name || name.contains(QChar(0)) ||
        QString::fromUtf8(name.toUtf8()) != name)
      throw std::invalid_argument(
          "Use a nonempty shader instance name without surrounding whitespace or NUL");
  }
  static ShaderInstances defaults(const QUuid &document, const std::set<QUuid> &sets)
  {
    if (document.isNull())
      throw std::invalid_argument("Shader instances require a project identity");
    ShaderInstances value;
    const auto id = QUuid::createUuidV5(document, QByteArray("BTS Main shader"));
    value.default_instance = id;
    value.instances.emplace(id, ShaderInstance{id, "Main shader", {}});
    for (const auto &set : sets)
      value.assignments.emplace(set, id);
    value.validate();
    return value;
  }
  void validate() const
  {
    std::uint64_t text_bytes = 0;
    for (const auto &[id, value] : instances)
      text_bytes = resource_sum(text_bytes, resource_product(value.name.size(), 8));
    if (instances.empty() || !instances.contains(default_instance))
      throw std::invalid_argument("Invalid shader instance library");
    std::set<QString> names;
    for (const auto &[id, value] : instances) {
      if (id.isNull() || id != value.id)
        throw std::invalid_argument("Invalid shader instance identity");
      validate_name(value.name, false);
      if (!names.insert(value.name).second)
        throw std::invalid_argument("Shader instance names must be unique");
      value.parameters.validate();
    }
    for (const auto &[set, id] : assignments)
      if (set.isNull() || !instances.contains(id))
        throw std::invalid_argument("Shader assignment refers to a missing instance");
  }
  void validate_sets(const std::set<QUuid> &sets) const
  {
    validate();
    if (sets.size() != assignments.size() ||
        std::any_of(
            sets.begin(), sets.end(), [&](const auto &id) { return !assignments.contains(id); }))
      throw std::invalid_argument("Shader assignments do not match the project's Texture Sets");
  }
  const ShaderInstance &for_set(const QUuid &id) const
  {
    return instances.at(assignments.at(id));
  }
  void rename(const QUuid &id, const QString &name)
  {
    validate_name(name);
    for (const auto &[other, value] : instances)
      if (other != id && value.name == name)
        throw std::invalid_argument("Shader instance name is already in use");
    instances.at(id).name = name;
  }
  QUuid create(const QUuid &parent, const QString &name)
  {
    validate_name(name);
    for (const auto &[id, value] : instances)
      if (value.name == name)
        throw std::invalid_argument("Shader instance name is already in use");
    auto next = instances.at(parent);
    next.id = QUuid::createUuid();
    next.name = name;
    const auto id = next.id;
    instances.emplace(id, std::move(next));
    return id;
  }
  void assign(const QUuid &set, const QUuid &id)
  {
    if (!instances.contains(id))
      throw std::invalid_argument("Shader instance is missing");
    assignments.at(set) = id;
  }
  void parameters(const QUuid &id, ShaderParameters value)
  {
    value.validate();
    instances.at(id).parameters = value;
  }
  void remove(const QUuid &id)
  {
    if (!instances.contains(id) || instances.size() == 1 ||
        std::any_of(assignments.begin(), assignments.end(), [&](const auto &entry) {
          return entry.second == id;
        }))
      throw std::invalid_argument("Only unused shader instances can be deleted");
    instances.erase(id);
    if (default_instance == id)
      default_instance = instances.begin()->first;
  }
  // Mesh reimport/paint history can change the set graph independently of shader history.
  // Retain current bindings; restore an older binding only for a returning set and
  // only while its shader still exists in the current library.
  void reconcile(const std::set<QUuid> &sets, const ShaderInstances *previous = nullptr)
  {
    for (auto it = assignments.begin(); it != assignments.end();)
      if (!sets.contains(it->first))
        it = assignments.erase(it);
      else
        ++it;
    for (const auto &set : sets)
      if (!assignments.contains(set)) {
        const auto old = previous ? previous->assignments.find(set) : assignments.end();
        assignments[set] = previous && old != previous->assignments.end() &&
                                   instances.contains(old->second) ?
                               old->second :
                               default_instance;
      }
    validate_sets(sets);
  }
  QJsonObject to_json() const
  {
    validate();
    QJsonArray list;
    for (const auto &[id, value] : instances) {
      const auto &p = value.parameters;
      QJsonObject fields{{"normal_intensity", p.normal_intensity},
                         {"ao_intensity", p.ao_intensity},
                         {"subsurface", p.subsurface.to_json()}};
      if (p.openpbr)
        fields["openpbr"] = p.openpbr->to_json();
      list.append(QJsonObject{
          {"id", key(id)},
          {"name", value.name},
          {"shader",
           p.openpbr ? "openpbr-1.1-principled-compatibility" : "principled-metallic-roughness"},
          {"parameters", fields}});
    }
    QJsonObject bindings;
    for (const auto &[set, id] : assignments)
      bindings[key(set)] = key(id);
    return {{"version", 3},
            {"default", key(default_instance)},
            {"instances", list},
            {"texture_sets", bindings}};
  }
  static ShaderInstances from_json(const QJsonValue &json)
  {
    if (!json.isObject())
      throw std::invalid_argument("Invalid shader instance metadata");
    const auto root = json.toObject();
    if (root.size() != 4 ||
        (root["version"].toDouble() != 1 && root["version"].toDouble() != 2 &&
         root["version"].toDouble() != 3) ||
        !root["instances"].isArray() || !root["texture_sets"].isObject())
      throw std::invalid_argument("Unsupported shader instance metadata");
    const auto uuid = [](const QJsonValue &value) {
      const auto text = value.toString();
      const QUuid id(text);
      if (!value.isString() || id.isNull() || key(id) != text)
        throw std::invalid_argument("Invalid shader UUID");
      return id;
    };
    ShaderInstances result;
    result.default_instance = uuid(root["default"]);
    for (const auto &entry : root["instances"].toArray()) {
      if (!entry.isObject())
        throw std::invalid_argument("Invalid shader instance");
      const auto o = entry.toObject();
      const auto shader = o["shader"].toString();
      const bool compatibility = shader == "openpbr-1.1-principled-compatibility";
      if (o.size() != 4 ||
          (shader != "principled-metallic-roughness" &&
           !(root["version"] == 3 && compatibility)) ||
          !o["parameters"].isObject())
        throw std::invalid_argument("Unsupported shader file");
      const auto p = o["parameters"].toObject();
      if (p.size() != (root["version"] == 1 ? 2 :
                       compatibility        ? 4 :
                                              3) ||
          !p["normal_intensity"].isDouble() || !p["ao_intensity"].isDouble())
        throw std::invalid_argument("Invalid shader parameters");
      ShaderInstance value{
          uuid(o["id"]),
          o["name"].toString(),
          {float(p["normal_intensity"].toDouble()), float(p["ao_intensity"].toDouble())}};
      if (root["version"] != 1)
        value.parameters.subsurface = SubsurfaceParameters::from_json(p["subsurface"]);
      if (compatibility)
        value.parameters.openpbr = OpenPbrParameters::from_json(p["openpbr"]);
      if (!o["name"].isString() || !result.instances.emplace(value.id, value).second)
        throw std::invalid_argument("Duplicate shader identity");
    }
    const auto bindings = root["texture_sets"].toObject();
    for (auto it = bindings.begin(); it != bindings.end(); ++it)
      result.assignments.emplace(uuid(it.key()), uuid(it.value()));
    result.validate();
    return result;
  }
};
}  // namespace bts
