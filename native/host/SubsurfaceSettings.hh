#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts {
struct SubsurfaceParameters {
  bool enabled = true;
  int type = 0;  // Skin / Translucent; native random-walk variants in Cycles.
  float scale = .5f;
  std::array<float, 4> color{
      1, .3f, .2f, 1};  // Scene-linear wavelength radii for Principled BSDF.
  bool operator==(const SubsurfaceParameters &) const = default;
  void validate() const
  {
    if (type < 0 || type > 1 || !std::isfinite(scale) || scale < 0 || scale > 100 ||
        color[3] != 1 || !std::all_of(color.begin(), color.end(), [](float v) {
          return std::isfinite(v) && v >= 0 && v <= 65504;
        }))
      throw std::invalid_argument("Invalid subsurface shader settings");
  }
  QJsonObject to_json() const
  {
    validate();
    return {{"enabled", enabled},
            {"type", type},
            {"scale", scale},
            {"color", QJsonArray{color[0], color[1], color[2], color[3]}}};
  }
  static SubsurfaceParameters from_json(const QJsonValue &json)
  {
    if (!json.isObject())
      throw std::invalid_argument("Missing subsurface shader settings");
    const auto o = json.toObject();
    if (o.size() != 4 || !o["enabled"].isBool() || !o["type"].isDouble() ||
        o["type"].toDouble() != o["type"].toInt() || !o["scale"].isDouble() ||
        !o["color"].isArray() || o["color"].toArray().size() != 4)
      throw std::invalid_argument("Invalid subsurface shader metadata");
    SubsurfaceParameters value;
    value.enabled = o["enabled"].toBool();
    value.type = o["type"].toInt();
    value.scale = float(o["scale"].toDouble());
    const auto color = o["color"].toArray();
    for (int i = 0; i < 4; ++i) {
      if (!color[i].isDouble())
        throw std::invalid_argument("Invalid subsurface color");
      value.color[i] = float(color[i].toDouble());
    }
    value.validate();
    return value;
  }
};
struct SubsurfacePreview {
  bool enabled = false;
  int samples = 16;  // EEVEE's real diffusion kernel supports 1..64 samples.
  bool operator==(const SubsurfacePreview &) const = default;
  void validate() const
  {
    if (samples < 1 || samples > 64)
      throw std::invalid_argument("Subsurface samples must be between 1 and 64");
  }
  QJsonObject to_json() const
  {
    validate();
    return {{"version", 1}, {"enabled", enabled}, {"samples", samples}};
  }
  static SubsurfacePreview from_json(const QJsonObject &o)
  {
    if (o.isEmpty())
      return {};
    if (o.size() != 3 || o["version"] != 1 || !o["enabled"].isBool() || !o["samples"].isDouble() ||
        o["samples"].toDouble() != o["samples"].toInt())
      throw std::invalid_argument("Invalid subsurface preview metadata");
    SubsurfacePreview v{o["enabled"].toBool(), o["samples"].toInt()};
    v.validate();
    return v;
  }
};
}  // namespace bts
