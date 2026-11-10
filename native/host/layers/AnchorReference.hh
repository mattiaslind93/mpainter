// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QUuid>
#include <QJsonObject>
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts {
struct TextureSet;
struct FillSurfaceAtlas;
struct AnchorReference {
  QUuid source;
  int channel = 0, component = 1;  // 0 RGB; 1 R; 2 G; 3 B; 4 source coverage.
  bool coverage = true;
  bool operator==(const AnchorReference &) const = default;
};
struct AnchorSample {
  std::array<float, 4> rgba{0, 0, 0, 1};
  bool valid = false, mask = false;
};
struct AnchorSampleContext {
  const TextureSet *set = nullptr;
  QUuid consumer;
  int x = 0, y = 0, tile = 1001;
  AnchorSample (*resolve)(const AnchorSampleContext &, const AnchorReference &) = nullptr;
  const FillSurfaceAtlas *projection = nullptr;  // Optional immutable generator surface.
};
inline std::array<float, 4> anchor_components(const AnchorSample &sample,
                                              const AnchorReference &ref)
{
  if (!sample.valid || ref.component < 0 || ref.component > 4)
    return {0, 0, 0, 1};
  if (sample.mask)
    return {sample.rgba[0], sample.rgba[0], sample.rgba[0], 1};
  auto value = sample.rgba;
  if (ref.component > 0) {
    const float scalar = value[ref.component - 1];
    for (int c = 0; c < 3; ++c)
      value[c] = scalar;
  }
  value[3] = ref.coverage && ref.component != 4 ? std::clamp(sample.rgba[3], 0.f, 1.f) : 1;
  return value;
}
inline QJsonObject anchor_reference_json(const AnchorReference &ref)
{
  return {{"source", ref.source.isNull() ? QString{} : ref.source.toString(QUuid::WithoutBraces)},
          {"channel", ref.channel},
          {"component", ref.component},
          {"coverage", ref.coverage}};
}
inline AnchorReference read_anchor_reference(const QJsonValue &value)
{
  const auto fail = []() { throw std::invalid_argument("Invalid anchor reference"); };
  if (!value.isObject())
    fail();
  const auto o = value.toObject();
  AnchorReference ref;
  if (!o["source"].isString())
    fail();
  const auto source = o["source"].toString();
  if (!source.isEmpty()) {
    ref.source = QUuid(source);
    if (ref.source.isNull())
      fail();
  }
  const auto integer = [&](const char *key, int max) {
    const auto v = o[key];
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() != v.toInt(-1) ||
        v.toInt() < 0 || v.toInt() > max)
      fail();
    return v.toInt();
  };
  if (o.contains("channel"))
    ref.channel = integer("channel", 14);
  if (o.contains("component"))
    ref.component = integer("component", 4);
  if (o.contains("coverage")) {
    if (!o["coverage"].isBool())
      fail();
    ref.coverage = o["coverage"].toBool();
  }
  return ref;
}
}  // namespace bts
