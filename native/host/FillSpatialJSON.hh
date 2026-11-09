#pragma once
#include "WarpIO.hh"
#include "FillSymmetryJSON.hh"
namespace bts {
inline QJsonObject fill_spatial_json(const FillProjection &p)
{
  const auto vector = [](const auto &v) { return QJsonArray{v[0], v[1], v[2]}; };
  return {{"bump_bias", p.bump_bias},
          {"mode", p.mode},
          {"hardness", p.hardness},
          {"offset3", vector(p.offset3)},
          {"rotation3", vector(p.rotation3)},
          {"scale3", vector(p.scale3)},
          {"linked3", p.linked3},
          {"filtering", p.filtering},
          {"wrap", p.wrap},
          {"crop", p.shape_crop},
          {"depth", p.depth_culling},
          {"depth_hardness", p.depth_hardness},
          {"backface", p.backface_culling},
          {"backface_angle", p.backface_angle},
          {"backface_hardness", p.backface_hardness},
          {"cylinder_angle", p.cylinder_angle},
          {"physical", p.physical},
          {"custom_size", p.custom_size},
          {"asset_size_cm", vector(p.asset_size_cm)},
          {"custom_size_cm", vector(p.custom_size_cm)},
          {"warp", warp_settings_json(p)},
          {"symmetry", fill_symmetry_json(p.symmetry)}};
}
inline void read_fill_spatial(FillProjection &p, const QJsonValue &value)
{
  if (value.isUndefined())
    return;
  if (!value.isObject())
    throw std::invalid_argument("Invalid spatial fill settings");
  const auto data = value.toObject();
  const auto number = [](const QJsonValue &v) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
      throw std::invalid_argument("Invalid spatial fill number");
    return v.toDouble();
  };
  const auto integer = [&](const char *key, int maximum) {
    const double n = number(data[key]);
    if (n != std::floor(n) || n < 0 || n > maximum)
      throw std::invalid_argument("Invalid spatial fill index");
    return int(n);
  };
  const auto boolean = [&](const char *key) {
    if (!data[key].isBool())
      throw std::invalid_argument("Invalid spatial fill flag");
    return data[key].toBool();
  };
  const auto vector = [&](const char *key, auto &destination) {
    if (!data[key].isArray() || data[key].toArray().size() != 3)
      throw std::invalid_argument("Invalid spatial fill vector");
    for (int i = 0; i < 3; ++i)
      destination[i] = number(data[key].toArray()[i]);
  };
  auto candidate = p;
  candidate.bump_bias = data.contains("bump_bias") ? float(number(data["bump_bias"])) : 0;
  if (candidate.bump_bias != -.5f && candidate.bump_bias != 0 && candidate.bump_bias != .5f)
    throw std::invalid_argument("Invalid Bump binding coordinate");
  candidate.mode = integer("mode", 6);
  candidate.hardness = number(data["hardness"]);
  vector("offset3", candidate.offset3);
  vector("rotation3", candidate.rotation3);
  vector("scale3", candidate.scale3);
  candidate.linked3 = boolean("linked3");
  candidate.filtering = integer("filtering", 2);
  candidate.wrap = integer("wrap", 3);
  candidate.shape_crop = boolean("crop");
  candidate.depth_culling = boolean("depth");
  candidate.depth_hardness = number(data["depth_hardness"]);
  candidate.backface_culling = boolean("backface");
  candidate.backface_angle = number(data["backface_angle"]);
  candidate.backface_hardness = number(data["backface_hardness"]);
  candidate.cylinder_angle = number(data["cylinder_angle"]);
  candidate.physical = boolean("physical");
  candidate.custom_size = boolean("custom_size");
  vector("asset_size_cm", candidate.asset_size_cm);
  vector("custom_size_cm", candidate.custom_size_cm);
  read_warp_settings(candidate, data["warp"]);
  candidate.symmetry = read_fill_symmetry(data["symmetry"]);
  validate_fill_projection(candidate);
  p = std::move(candidate);
}
}  // namespace bts
