// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Pure Qt metadata boundary: usable by catalog/project readers without linking
// native graph/GPU objects. Source resolution remains in the worker backend.
#include "ResourceAvailability.hh"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace bts {
namespace native_image_binding_metadata {
[[noreturn]] inline void invalid()
{
  throw std::invalid_argument("Invalid native image binding metadata");
}
inline QString text(const QJsonValue &v)
{
  if (!v.isString())
    invalid();
  const auto s = v.toString();

  if (s.isEmpty() || s.contains(QChar::Null) || QString::fromUtf8(s.toUtf8()) != s)
    invalid();
  return s;
}
inline int integer(const QJsonValue &v, bool positive = false)
{
  if (!v.isDouble() || !std::isfinite(v.toDouble()) ||
      v.toDouble() < std::numeric_limits<int>::min() ||
      v.toDouble() > std::numeric_limits<int>::max())
    invalid();
  const int n = v.toInt();
  if (v.toDouble() != n || (positive && n <= 0))
    invalid();
  return n;
}
inline void keys(const QJsonObject &o, std::initializer_list<const char *> names)
{
  if (o.size() != qsizetype(names.size()))
    invalid();
  for (auto name : names)
    if (!o.contains(name))
      invalid();
}
inline void settings(const QJsonObject &o)
{
  keys(o,
       {"image_source",
        "image_type",
        "alpha_mode",
        "high_precision",
        "multiview",
        "source_space",
        "interpolation",
        "projection",
        "projection_blend",
        "extension"});
  for (auto name :
       {"image_source", "image_type", "alpha_mode", "interpolation", "projection", "extension"})
    integer(o[name]);
  for (auto name : {"high_precision", "multiview"})
    if (!o[name].isBool())
      invalid();
  // Empty source-space is retained as unknown; the replacement must choose an
  // explicit known interpretation before it can become a prepared variant.
  if (!o["source_space"].isString())
    invalid();
  const auto space = o["source_space"].toString();

  if (space.contains(QChar::Null) || QString::fromUtf8(space.toUtf8()) != space ||
      !o["projection_blend"].isDouble() || !std::isfinite(o["projection_blend"].toDouble()) ||
      std::abs(o["projection_blend"].toDouble()) > std::numeric_limits<float>::max())
    invalid();
}
}  // namespace native_image_binding_metadata
inline QJsonObject native_image_binding_json(const NativeImageBinding &v)
{
  using namespace native_image_binding_metadata;
  if (v.node_identifier <= 0 || v.consumer_identifier <= 0)
    invalid();
  settings(v.source_settings);
  auto units = resource_sum(resource_sum(v.output.size(), v.input.size()),
                            resource_sum(v.consumer_type.size(), v.image.size()));
  units = resource_sum(units, v.source_settings["source_space"].toString().size());
  for (const auto &step : v.groups)
    units = resource_sum(units, step.group.size());

  QJsonArray groups;
  for (const auto &step : v.groups) {
    if (step.node_identifier <= 0)
      invalid();
    groups.append(QJsonObject{{"node", step.node_identifier}, {"group", text(step.group)}});
  }
  return {{"version", 1},
          {"groups", groups},
          {"node", v.node_identifier},
          {"output", text(v.output)},
          {"consumer", v.consumer_identifier},
          {"input", text(v.input)},
          {"consumer_type", text(v.consumer_type)},
          {"image", text(v.image)},
          {"settings", v.source_settings}};
}
inline NativeImageBinding read_native_image_binding(const QJsonObject &o)
{
  using namespace native_image_binding_metadata;
  keys(o,
       {"version",
        "groups",
        "node",
        "output",
        "consumer",
        "input",
        "consumer_type",
        "image",
        "settings"});
  if (integer(o["version"]) != 1 || !o["groups"].isArray() || !o["settings"].isObject())
    invalid();
  const auto groups = o["groups"].toArray();
  NativeImageBinding value;
  for (const auto &entry : groups) {
    if (!entry.isObject())
      invalid();
    const auto step = entry.toObject();
    keys(step, {"node", "group"});
    value.groups.push_back({integer(step["node"], true), text(step["group"])});
  }
  value.node_identifier = integer(o["node"], true);
  value.consumer_identifier = integer(o["consumer"], true);
  value.output = text(o["output"]);
  value.input = text(o["input"]);
  value.consumer_type = text(o["consumer_type"]);
  value.image = text(o["image"]);
  value.source_settings = o["settings"].toObject();
  native_image_binding_json(value);
  return value;
}
inline QString NativeImageBinding::key() const
{
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(native_image_binding_json(*this)).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
}

}  // namespace bts
