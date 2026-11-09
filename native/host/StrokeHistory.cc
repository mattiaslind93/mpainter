#include "StrokeHistory.hh"
#include "ResourceAvailability.hh"
#include "PaintRecipe.hh"
#include <limits>
#include <algorithm>
namespace bts {
PaintPixels::Bounds stroke_bounds(const StrokeRaster &raster)
{
  PaintPixels::Bounds total;
  for (const auto *tiles : {&raster.paint, &raster.ao})
    for (const auto &[tile, pixels] : tiles->tiles())
      for (const auto &channel : pixels.page_versions())
        for (const auto &[key, version] : channel) {
          PaintPixels::Bounds page{int(key % 65536) * 64,
                                   int(key / 65536) * 64,
                                   int(key % 65536) * 64 + 64,
                                   int(key / 65536) * 64 + 64};
          if (total.empty())
            total = page;
          else {
            total.x0 = std::min(total.x0, page.x0);
            total.y0 = std::min(total.y0, page.y0);
            total.x1 = std::max(total.x1, page.x1);
            total.y1 = std::max(total.y1, page.y1);
          }
        }
  return total;
}
std::shared_ptr<const StrokeHistory> append_surface_path(
    const std::shared_ptr<const StrokeHistory> &previous,
    const StrokeRaster &before,
    const StrokeRaster &ink,
    std::shared_ptr<const SurfacePath> path,
    int resolution)
{
  if (!path)
    throw std::invalid_argument("Surface path is missing");
  path->validate();
  if (previous)
    for (const auto &operation : previous->operations)
      if (operation->path && operation->path->id == path->id)
        throw std::invalid_argument("Duplicate surface path identity");
  auto history = std::make_shared<StrokeHistory>(*append_stroke(
      previous, before, before, ink, true, false, path->brush.stroke_opacity / 100));
  auto operation = std::make_shared<StrokeOperation>(*history->operations.back());
  operation->path = std::move(path);
  history->operations.back() = std::move(operation);
  history->declared_paths.clear();
  history->final_pixels = std::make_shared<StrokeRaster>(replay_strokes(*history, resolution));
  return history;
}
bool has_surface_paths(const std::shared_ptr<const StrokeHistory> &history)
{
  if (!history)
    return false;
  if (!history->declared_paths.empty())
    return true;
  return std::any_of(history->operations.begin(),
                     history->operations.end(),
                     [](const auto &operation) { return operation && bool(operation->path); });
}
std::shared_ptr<const StrokeHistory> edit_surface_path(
    const std::shared_ptr<const StrokeHistory> &source,
    const std::string &id,
    std::shared_ptr<const SurfacePath> path,
    std::shared_ptr<const StrokeRaster> ink,
    int resolution)
{
  if (!source || !source->loaded || !source->baseline)
    throw std::invalid_argument("Surface path history is unavailable");
  if (path) {
    path->validate();
    if (path->id != id || !ink)
      throw std::invalid_argument("Surface path edit identity mismatch");
  }
  auto history = std::make_shared<StrokeHistory>(*source);
  const auto found = std::find_if(history->operations.begin(),
                                  history->operations.end(),
                                  [&](const auto &op) { return op->path && op->path->id == id; });
  if (found == history->operations.end())
    throw std::invalid_argument("Surface path is missing");
  if (!path)
    history->operations.erase(found);
  else {
    auto operation = std::make_shared<StrokeOperation>(**found);
    operation->path = std::move(path);
    operation->ink = std::move(ink);
    operation->bounds = stroke_bounds(*operation->ink);
    operation->opacity = operation->path->brush.stroke_opacity / 100;
    *found = std::move(operation);
  }
  history->declared_paths.clear();
  const auto pixels = replay_strokes(*history, resolution);
  history->final_pixels = std::make_shared<StrokeRaster>(pixels);
  return history;
}
StrokeRaster replay_strokes(const StrokeHistory &history, int resolution)
{
  if (!history.loaded || !history.baseline ||
      history.operations.size() > std::size_t(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Stroke history is incomplete");
  for (const auto &operation : history.operations) {
    if (!operation || !operation->ink || !std::isfinite(operation->opacity) ||
        operation->opacity < 0 || operation->opacity > 1)
      throw std::invalid_argument("Invalid stroke operation");
    if (operation->path)
      operation->path->validate();
  }
  std::vector<PaintPixels> materialized;
  std::vector<PaintPixels> inputs;
  if (!record_gpu_paint) {
    for (const auto &operation : history.operations)
      if (operation && operation->ink && (!operation->path || operation->path->visible))
        for (const auto *tiles : {&operation->ink->paint, &operation->ink->ao})
          for (const auto &[number, pixels] : tiles->tiles())
            inputs.push_back(pixels);
    materialized = evaluate_paint_batch(inputs);
  }
  std::size_t input_index = 0;

  StrokeRaster result = *history.baseline;
  for (const auto &operation : history.operations) {
    if (!operation || !operation->ink || !std::isfinite(operation->opacity) ||
        operation->opacity < 0 || operation->opacity > 1)
      throw std::invalid_argument("Invalid stroke operation");
    if (operation->path) {
      operation->path->validate();
      if (!operation->path->visible)
        continue;
    }
    StrokeRaster ink = *operation->ink;
    if (!record_gpu_paint)
      for (auto *tiles : {&ink.paint, &ink.ao}) {
        auto source = *tiles;
        for (const auto &[number, pixels] : source.tiles())
          tiles->assign_tile(materialized.at(input_index++), number);
      }
    auto before = result;
    const auto bounds = operation->bounds;
    result.paint.over_stroke(
        before.paint, ink.paint, operation->opacity, bounds, operation->erase);
    result.ao.over_stroke(before.ao, ink.ao, operation->opacity, bounds, operation->erase);
  }
  return result;
}
std::shared_ptr<const StrokeHistory> append_stroke(
    const std::shared_ptr<const StrokeHistory> &previous,
    const StrokeRaster &before,
    const StrokeRaster &after,
    const StrokeRaster &ink,
    bool spatial,
    bool erase,
    float opacity)
{
  if (has_surface_paths(previous) && (!previous->loaded || !previous->matches(before)))
    throw std::length_error(
        "Editable path history does not match the current pixels; project paint retained");
  auto history = std::make_shared<StrokeHistory>();
  if (previous && previous->loaded && previous->matches(before))
    *history = *previous;
  else
    history->baseline = std::make_shared<StrokeRaster>(before);
  auto operation = std::make_shared<StrokeOperation>();
  operation->ink = std::make_shared<StrokeRaster>(ink);
  operation->spatial = spatial;
  operation->erase = erase;
  operation->opacity = opacity;
  operation->bounds = stroke_bounds(ink);
  history->operations.push_back(operation);
  history->final_pixels = std::make_shared<StrokeRaster>(after);
  return history;
}
}  // namespace bts
