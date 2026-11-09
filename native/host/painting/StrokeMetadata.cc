// SPDX-License-Identifier: GPL-3.0-or-later
#include "EvaluationScope.hh"
#include "StrokeMetadata.hh"
#include "TextureMemory.hh"
#include <limits>
#include "SurfacePathIO.hh"
#include <QJsonArray>
namespace bts {
namespace {
[[noreturn]] void fail()
{
  throw std::invalid_argument("Invalid stroke metadata");
}
QJsonArray hashes(const StrokeHashes &hash)
{
  QJsonArray result;
  for (auto value : hash)
    result.append(QString::number(value, 16));
  return result;
}
int count(const QJsonValue &value, int maximum)
{
  if (!value.isDouble() || value.toDouble() != value.toInt(-1) || value.toInt() < 0 ||
      value.toInt() > maximum)
    fail();
  return value.toInt();
}
}  // namespace
QJsonValue stroke_history_json(const std::shared_ptr<const StrokeHistory> &history,
                               const StrokeRaster &flat)
{
  if (!history)
    return QJsonValue();
  if (edit_storage_identity)
    return QJsonObject{{"immutable_storage",
                        QString::number(reinterpret_cast<std::uintptr_t>(history.get()), 16)}};
  if (!history->matches(flat)) {
    if (has_surface_paths(history))
      throw std::logic_error("Editable path pixels do not match their operation history");
    return QJsonValue();
  }
  const int operations = history->loaded ? int(history->operations.size()) :
                                           history->declared_operations;
  int spatial = history->declared_spatial;
  if (history->loaded) {
    spatial = 0;
    for (const auto &op : history->operations)
      spatial += op->spatial;
  }
  if (!operations)
    return QJsonValue();
  QJsonObject object{{"version", 1},
                     {"operations", operations},
                     {"spatial", spatial},
                     {"flat_hash", hashes(history->final_hashes())}};
  QJsonArray paths;
  if (history->loaded) {
    for (int i = 0; i < int(history->operations.size()); ++i)
      if (history->operations[i]->path)
        paths.append(
            QJsonObject{{"index", i}, {"path", surface_path_json(*history->operations[i]->path)}});
  }
  else
    for (const auto &[index, path] : history->declared_paths)
      paths.append(QJsonObject{{"index", index}, {"path", surface_path_json(*path)}});
  if (!paths.isEmpty())
    object["paths"] = paths;
  return object;
}
std::shared_ptr<const StrokeHistory> read_stroke_history_json(const QJsonValue &value)
{
  if (value.isNull())
    return {};
  if (!value.isObject())
    fail();
  const auto object = value.toObject();
  if (object["version"].toInt() != 1)
    fail();
  auto history = std::make_shared<StrokeHistory>();
  history->loaded = false;
  history->declared_operations = count(object["operations"], std::numeric_limits<int>::max());
  history->declared_spatial = count(object["spatial"], history->declared_operations);
  const auto hash = object["flat_hash"].toArray();
  if (hash.size() != 6 || history->declared_operations < 1)
    fail();
  for (int c = 0; c < 6; ++c) {
    bool valid = false;
    if (!hash[c].isString() || hash[c].toString().size() > 16)
      fail();
    history->final_hash[c] = hash[c].toString().toULongLong(&valid, 16);
    if (!valid)
      fail();
  }
  if (object.contains("paths")) {
    if (!object["paths"].isArray() ||
        object["paths"].toArray().size() > history->declared_operations)
      fail();
    std::set<std::string> ids;
    for (const auto &entry : object["paths"].toArray()) {
      if (!entry.isObject())
        fail();
      const auto row = entry.toObject();
      const int index = count(row["index"], history->declared_operations - 1);
      auto path = read_surface_path(row["path"]);
      if (!history->declared_paths.emplace(index, path).second || !ids.insert(path->id).second)
        fail();
    }
  }
  return history;
}
}  // namespace bts
