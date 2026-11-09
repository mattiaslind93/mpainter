#pragma once
#include "MaterialValues.hh"
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace bts {
inline QJsonObject material_values_json(const MaterialValues &v)
{
  return {{"base_color",
           QJsonArray{v.base_color[0], v.base_color[1], v.base_color[2], v.base_color[3]}},
          {"normal", QJsonArray{v.normal[0], v.normal[1], v.normal[2]}},
          {"metallic", v.metallic},
          {"roughness", v.roughness},
          {"bump", v.bump},
          {"ao", v.ambient_occlusion}};
}
inline MaterialValues read_material_values(const QJsonValue &value)
{
  if (!value.isObject())
    throw std::invalid_argument("Invalid native material values");
  const auto o = value.toObject();
  const auto number = [](const QJsonValue &v, double low, double high) {
    const double n = v.toDouble(low - 1);
    if (!v.isDouble() || !std::isfinite(n) || n < low || n > high)
      throw std::invalid_argument("Native material value is outside its channel range");
    return float(n);
  };
  MaterialValues result;
  if (!o["base_color"].isArray() || o["base_color"].toArray().size() != 4 ||
      !o["normal"].isArray() || o["normal"].toArray().size() != 3)
    throw std::invalid_argument("Invalid native material components");
  for (int c = 0; c < 4; ++c)
    result.base_color[c] = number(
        o["base_color"].toArray()[c], c == 3 ? 0 : -65504, c == 3 ? 1 : 65504);
  for (int c = 0; c < 3; ++c)
    result.normal[c] = number(o["normal"].toArray()[c], -1, 1);
  result.metallic = number(o["metallic"], 0, 1);
  result.roughness = number(o["roughness"], 0, 1);
  result.bump = number(o["bump"], -65504, 65504);
  result.ambient_occlusion = number(o["ao"], 0, 1);
  return result;
}
}  // namespace bts
