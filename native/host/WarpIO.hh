#pragma once
#include "FillProjection.hh"
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
inline QJsonArray warp_vector_json(const WarpVector &value)
{
  return {value[0], value[1], value[2]};
}
inline QJsonValue warp_grid_json(const std::shared_ptr<const WarpGrid> &grid)
{
  if (!grid)
    return QJsonValue(QJsonValue::Null);
  grid->validate();
  QJsonArray columns, rows, nodes;
  for (double value : grid->columns)
    columns.append(value);
  for (double value : grid->rows)
    rows.append(value);
  for (const auto &node : grid->nodes)
    nodes.append(QJsonArray{warp_vector_json(node.position),
                            warp_vector_json(node.du),
                            warp_vector_json(node.dv),
                            warp_vector_json(node.duv)});
  return QJsonObject{{"columns", columns},
                     {"rows", rows},
                     {"nodes", nodes},
                     {"automatic_tangents", grid->automatic_tangents}};
}
inline std::shared_ptr<const WarpGrid> read_warp_grid(const QJsonValue &value)
{
  if (value.isNull())
    return {};
  if (!value.isObject())
    throw std::invalid_argument("Invalid Warp grid");
  const auto data = value.toObject();
  const auto columns = data["columns"].toArray(), rows = data["rows"].toArray(),
             nodes = data["nodes"].toArray();
  if (columns.size() < 2 || columns.size() > 9 || rows.size() < 2 || rows.size() > 9 ||
      nodes.size() != columns.size() * rows.size())
    throw std::invalid_argument("Invalid Warp grid size");
  const auto number = [](const QJsonValue &v, double low, double high) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < low || v.toDouble() > high)
      throw std::invalid_argument("Invalid Warp numeric value");
    return v.toDouble();
  };
  auto grid = std::make_shared<WarpGrid>(int(columns.size()), int(rows.size()));
  for (int i = 0; i < columns.size(); ++i)
    grid->columns[i] = number(columns[i], 0, 1);
  for (int j = 0; j < rows.size(); ++j)
    grid->rows[j] = number(rows[j], 0, 1);
  if (!data["automatic_tangents"].isBool())
    throw std::invalid_argument("Invalid Warp tangent flag");
  grid->automatic_tangents = data["automatic_tangents"].toBool();
  for (int i = 0; i < nodes.size(); ++i) {
    if (!nodes[i].isArray() || nodes[i].toArray().size() != 4)
      throw std::invalid_argument("Invalid Warp node");
    const auto vectors = nodes[i].toArray();
    std::array<WarpVector *, 4> targets{
        &grid->nodes[i].position, &grid->nodes[i].du, &grid->nodes[i].dv, &grid->nodes[i].duv};
    for (int k = 0; k < 4; ++k) {
      const auto vector = vectors[k].toArray();
      if (vector.size() != 3)
        throw std::invalid_argument("Invalid Warp node vector");
      for (int c = 0; c < 3; ++c)
        (*targets[k])[c] = number(vector[c], -10000, 10000);
    }
  }
  grid->validate();
  return grid;
}
inline QJsonValue warp_geometry_json(const std::shared_ptr<const WarpGeometrySettings> &settings)
{
  if (!settings)
    return QJsonValue(QJsonValue::Null);
  settings->validate();
  return QJsonObject{{"enabled", settings->enabled},
                     {"authored_reference", warp_grid_json(settings->authored_reference)},
                     {"frozen_reference", warp_grid_json(settings->frozen_reference)}};
}
inline std::shared_ptr<const WarpGeometrySettings> read_warp_geometry(const QJsonValue &value)
{
  if (value.isUndefined() || value.isNull())
    return {};
  if (!value.isObject() || !value.toObject()["enabled"].isBool())
    throw std::invalid_argument("Invalid Warp geometry mode");
  const auto data = value.toObject();
  auto result = std::make_shared<WarpGeometrySettings>();
  result->enabled = data["enabled"].toBool();
  result->authored_reference = read_warp_grid(data["authored_reference"]);
  result->frozen_reference = read_warp_grid(data["frozen_reference"]);
  result->validate();
  return result;
}
inline QJsonObject warp_settings_json(const FillProjection &p)
{
  return {{"grid", warp_grid_json(p.warp)},
          {"geometry", warp_geometry_json(p.warp_geometry)},
          {"depth", p.warp_depth},
          {"edit", p.warp_edit},
          {"surface", p.warp_surface},
          {"handle_size", p.warp_handle_size},
          {"color", QJsonArray{p.warp_color[0], p.warp_color[1], p.warp_color[2]}}};
}
inline void read_warp_settings(FillProjection &p, const QJsonValue &value)
{
  if (!value.isObject())
    throw std::invalid_argument("Warp settings must be an object");
  const auto object = value.toObject();
  const auto number = [](const QJsonValue &v, double low, double high) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < low || v.toDouble() > high)
      throw std::invalid_argument("Invalid Warp numeric value");
    return v.toDouble();
  };
  const auto boolean = [](const QJsonValue &v) {
    if (!v.isBool())
      throw std::invalid_argument("Invalid Warp flag");
    return v.toBool();
  };
  auto candidate = p;
  candidate.warp_depth = number(object["depth"], .0001, 1000);
  candidate.warp_edit = boolean(object["edit"]);
  candidate.warp_surface = boolean(object["surface"]);
  candidate.warp_handle_size = number(object["handle_size"], 2, 24);
  const auto color = object["color"].toArray();
  if (color.size() != 3)
    throw std::runtime_error("Invalid Warp color");
  for (int c = 0; c < 3; ++c)
    candidate.warp_color[c] = float(number(color[c], 0, 65504));
  candidate.warp = read_warp_grid(object["grid"]);
  candidate.warp_geometry = read_warp_geometry(object["geometry"]);
  candidate.warp_fit.reset();
  validate_fill_projection(candidate);
  p = std::move(candidate);
}
}  // namespace bts
