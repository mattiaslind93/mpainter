#include "ResourceAvailability.hh"
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
namespace bts {
struct GeometryMask {
  int type = 0;  // 0 mesh names, 1 UV tiles; independent retained selections.
  bool tiles_default_include = true;
  std::map<int, bool> tiles;
  bool includes_tile(int tile) const
  {
    const auto found = tiles.find(tile);
    return found == tiles.end() ? tiles_default_include : found->second;
  }
  void set_tile(int tile, bool include)
  {
    if (include == tiles_default_include)
      tiles.erase(tile);
    else
      tiles[tile] = include;
  }
  bool default_include = true;
  std::map<std::string, bool> meshes;
  bool active() const
  {
    return type == 1 ? (!tiles_default_include || !tiles.empty()) :
                       (!default_include || !meshes.empty());
  }
  bool includes(const std::string &name) const
  {
    const auto found = meshes.find(name);
    return found == meshes.end() ? default_include : found->second;
  }
  void set(const std::string &name, bool include)
  {
    if (include == default_include)
      meshes.erase(name);
    else
      meshes[name] = include;
  }
};
struct GeometryOwnershipTile {
  int width = 0, height = 0;
  std::vector<std::uint32_t> owners;
};
inline QJsonObject geometry_mask_json(const GeometryMask &mask)
{
  QJsonArray meshes;
  for (const auto &[name, include] : mask.meshes)
    meshes.append(QJsonObject{{"name", QString::fromStdString(name)}, {"included", include}});
  QJsonArray tiles;
  for (const auto &[tile, include] : mask.tiles)
    tiles.append(QJsonObject{{"tile", tile}, {"included", include}});
  return {{"type", mask.type},
          {"tiles_default_include", mask.tiles_default_include},
          {"tiles", tiles},
          {"default_include", mask.default_include},
          {"meshes", meshes}};
}
// Mesh keys are native Object.id.name+2: 255 UTF-8 bytes plus its NUL.
inline GeometryMask read_geometry_mask(const QJsonValue &value)
{
  if (value.isUndefined())
    return {};
  const auto fail = []() { throw std::invalid_argument("Invalid geometry mask"); };
  if (!value.isObject())
    fail();
  const auto o = value.toObject();
  if (!o["default_include"].isBool() || !o["meshes"].isArray())
    fail();
  GeometryMask mask;
  if (o.contains("type")) {
    const double type = o["type"].toDouble(-1);
    if (!o["type"].isDouble() || (type != 0 && type != 1) ||
        !o["tiles_default_include"].isBool() || !o["tiles"].isArray() ||
        o["tiles"].toArray().size() > 1000)
      fail();
    mask.type = int(type);
    mask.tiles_default_include = o["tiles_default_include"].toBool();
    for (const auto &entry : o["tiles"].toArray()) {
      if (!entry.isObject())
        fail();
      const auto e = entry.toObject();
      const double tile = e["tile"].toDouble(-1);
      if (!e["tile"].isDouble() || tile < 1001 || tile > 2000 || tile != int(tile) ||
          !e["included"].isBool() || e["included"].toBool() == mask.tiles_default_include ||
          !mask.tiles.emplace(int(tile), e["included"].toBool()).second)
        fail();
    }
  }
  mask.default_include = o["default_include"].toBool();
  for (const auto &entry : o["meshes"].toArray()) {
    if (!entry.isObject())
      fail();
    const auto e = entry.toObject();
    const auto name = e["name"].toString();
    if (!e["name"].isString() || name.isEmpty() || name.size() > 255 ||
        name.toUtf8().size() > 255 || QString::fromUtf8(name.toUtf8()) != name ||
        name.contains(QChar::Null) || !e["included"].isBool() ||
        e["included"].toBool() == mask.default_include ||
        !mask.meshes.emplace(name.toStdString(), e["included"].toBool()).second)
      fail();
  }
  return mask;
}
}  // namespace bts
