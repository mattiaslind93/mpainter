// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialLayering.hh"
#include "FillTextureJSON.hh"
#include "MaterialValuesJson.hh"
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
inline QJsonObject layer_material_json(const LayerMaterialInput &v)
{
  v.validate();
  QJsonArray maps;
  for (const auto &texture : v.textures)
    maps.append(fill_texture_json(texture));
  return {{"name", QString::fromStdString(v.name)},
          {"resource", QString::fromStdString(v.resource)},
          {"values", material_values_json(v.values)},
          {"textures", maps},
          {"tiling", QJsonArray{v.tiling[0], v.tiling[1]}},
          {"offset", QJsonArray{v.offset[0], v.offset[1]}},
          {"rotation", v.rotation}};
}
inline LayerMaterialInput read_layer_material(const QJsonValue &json)
{
  if (!json.isObject())
    throw std::invalid_argument("Invalid shader material input");
  const auto o = json.toObject();
  if (o.size() != 7 || !o["name"].isString() || !o["resource"].isString() ||
      !o["textures"].isArray() || o["textures"].toArray().size() != 6)
    throw std::invalid_argument("Invalid shader material fields");
  LayerMaterialInput v;
  v.name = o["name"].toString().toStdString();
  v.resource = o["resource"].toString().toStdString();
  v.values = read_material_values(o["values"]);
  const auto maps = o["textures"].toArray();
  for (int c = 0; c < 6; ++c)
    v.textures[c] = read_fill_texture_json(maps[c]);
  const auto pair = [](const QJsonValue &value) {
    if (!value.isArray() || value.toArray().size() != 2)
      throw std::invalid_argument("Invalid material transform pair");
    const auto a = value.toArray();
    if (!a[0].isDouble() || !a[1].isDouble())
      throw std::invalid_argument("Invalid material transform number");
    return std::array<double, 2>{a[0].toDouble(), a[1].toDouble()};
  };
  v.tiling = pair(o["tiling"]);
  v.offset = pair(o["offset"]);
  if (!o["rotation"].isDouble())
    throw std::invalid_argument("Invalid material rotation");
  v.rotation = o["rotation"].toDouble();
  v.validate();
  return v;
}
inline QJsonObject material_layering_json(const MaterialLayering &v)
{
  v.validate();
  QJsonArray inputs, stacks, selections;
  for (const auto &id : v.mask_selections)
    selections.append(id.toString(QUuid::WithoutBraces));
  for (const auto &input : v.materials)
    inputs.append(layer_material_json(input));
  for (int i = 0; i < 3; ++i)
    stacks.append(QJsonObject{{"id", v.mask_roots[i].toString(QUuid::WithoutBraces)},
                              {"channel", v.mask_slots[i]}});
  return {{"version", 1},
          {"enabled", v.enabled},
          {"materials", inputs},
          {"opacity_stacks", stacks},
          {"active_mask", v.active_mask},
          {"main_selection", v.main_selection.toString(QUuid::WithoutBraces)},
          {"mask_selections", selections}};
}
inline MaterialLayering read_material_layering(const QJsonValue &json)
{
  if (json.isNull() || json.isUndefined())
    return {};
  if (!json.isObject())
    throw std::invalid_argument("Invalid material layering metadata");
  const auto o = json.toObject();
  if (o.size() != 7 || o["version"] != 1 || !o["enabled"].isBool() || !o["materials"].isArray() ||
      o["materials"].toArray().size() != 4 || !o["opacity_stacks"].isArray() ||
      o["opacity_stacks"].toArray().size() != 3 || !o["active_mask"].isDouble() ||
      o["active_mask"].toDouble() != o["active_mask"].toInt(-99) ||
      !o["main_selection"].isString() || !o["mask_selections"].isArray() ||
      o["mask_selections"].toArray().size() != 3)
    throw std::invalid_argument("Invalid material layering fields");
  MaterialLayering v;
  v.enabled = o["enabled"].toBool();
  v.active_mask = o["active_mask"].toInt();
  const auto uuid = [](const QJsonValue &value) {
    if (!value.isString())
      throw std::invalid_argument("Invalid material substack UUID");
    const auto text = value.toString();
    const QUuid id(text);
    if (id.toString(QUuid::WithoutBraces) != text)
      throw std::invalid_argument("Invalid material substack UUID");
    return id;
  };
  v.main_selection = uuid(o["main_selection"]);
  for (int i = 0; i < 3; ++i)
    v.mask_selections[i] = uuid(o["mask_selections"].toArray()[i]);
  const auto inputs = o["materials"].toArray(), stacks = o["opacity_stacks"].toArray();
  for (int i = 0; i < 4; ++i)
    v.materials[i] = read_layer_material(inputs[i]);
  for (int i = 0; i < 3; ++i) {
    const auto s = stacks[i].toObject();
    if (s.size() != 2 || !s["channel"].isDouble() ||
        s["channel"].toDouble() != s["channel"].toInt(-99))
      throw std::invalid_argument("Invalid material opacity stack");
    v.mask_roots[i] = uuid(s["id"]);
    v.mask_slots[i] = s["channel"].toInt();
  }
  v.validate();
  return v;
}
}  // namespace bts
