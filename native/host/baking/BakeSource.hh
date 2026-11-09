#pragma once
#include "SceneSnapshot.hh"
#include <QJsonObject>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
namespace bts {
inline constexpr const char *bake_mesh_filter =
    "Mesh files (*.obj *.abc *.usd *.usda *.usdc *.usdz);;Wavefront OBJ (*.obj);;Alembic (*.abc);;Universal Scene Description (*.usd *.usda *.usdc *.usdz)";
struct BakeSourceOptions {
  std::optional<double> frame;
  double scale = 1;
  void validate() const
  {
    if (!std::isfinite(scale) || scale <= 0 || scale > 1e6 ||
        (frame && (!std::isfinite(*frame) || std::abs(*frame) > 1e6)))
      throw std::invalid_argument("Invalid bake source frame or scale");
  }
  bool operator==(const BakeSourceOptions &) const = default;
};
inline BakeSourceOptions bake_source_options(const QJsonObject &recipe)
{
  for (const char *key : {"source_scale", "source_frame"})
    if (recipe.contains(key) && !recipe.value(key).isDouble())
      throw std::invalid_argument("Bake source frame/scale must be numeric");
  if (recipe.contains("source_frame_enabled") && !recipe.value("source_frame_enabled").isBool())
    throw std::invalid_argument("Bake source frame switch must be boolean");
  BakeSourceOptions options;
  options.scale = recipe.value("source_scale").toDouble(1);
  if (recipe.value("source_frame_enabled").toBool())
    options.frame = recipe.value("source_frame").toDouble(1);
  options.validate();
  return options;
}
inline void store_bake_source_options(QJsonObject &recipe, const BakeSourceOptions &options)
{
  options.validate();
  recipe["source_scale"] = options.scale;
  recipe["source_frame_enabled"] = options.frame.has_value();
  recipe["source_frame"] = options.frame.value_or(1);
}
inline void scale_bake_source(SceneSnapshot &scene, double factor)
{
  if (factor == 1)
    return;
  for (auto &triangle : scene.uv_triangles)
    for (auto &point : triangle.positions)
      for (auto &value : point)
        value = float(value * factor);
  if (scene.has_bounds)
    for (int axis = 0; axis < 3; ++axis) {
      scene.minimum[axis] *= factor;
      scene.maximum[axis] *= factor;
    }
  for (const auto &triangle : scene.uv_triangles)
    for (const auto &point : triangle.positions)
      for (float value : point)
        if (!std::isfinite(value))
          throw std::overflow_error("Bake source scale exceeds finite geometry range");
}
using BakeSourceLoader =
    std::function<SceneSnapshot(const std::string &, const BakeSourceOptions &)>;
}  // namespace bts
