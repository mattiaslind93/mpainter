// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeReproject.hh"
#include "SparseStrokeTransfer.hh"
#include "PaintRecipe.hh"
#include <algorithm>
#include <cmath>
namespace bts {
bool spatial_history(const std::shared_ptr<const StrokeHistory> &history, const StrokeRaster &flat)
{
  return history && history->loaded && history->matches(flat) &&
         std::any_of(history->operations.begin(), history->operations.end(), [](const auto &op) {
           return op->spatial;
         });
}
std::pair<StrokeRaster, std::shared_ptr<const StrokeHistory>> transfer_stroke_history(
    const std::shared_ptr<const StrokeHistory> &source,
    const StrokeRaster &flat,
    const UvTransferMap &map,
    const std::set<int> &tiles)
{
  if (!spatial_history(source, flat))
    return {flat, source};
  SparseStrokeTransfer transfer(map, tiles);
  std::vector<PaintPixels> requests;
  for (const auto &op : source->operations)
    if (op->spatial)
      for (const auto *plane : {&op->ink->paint, &op->ink->ao})
        for (const auto &[tile, pixels] : plane->tiles())
          requests.push_back(pixels);
  const auto raw = evaluate_paint_batch(requests);
  std::size_t index = 0;
  auto history = std::make_shared<StrokeHistory>(*source);
  history->operations.clear();
  for (const auto &op : source->operations) {
    if (!op->spatial) {
      history->operations.push_back(op);
      continue;
    }
    auto source_ink = *op->ink;
    for (auto *plane : {&source_ink.paint, &source_ink.ao}) {
      auto before = *plane;
      for (const auto &[tile, pixels] : before.tiles())
        plane->assign_tile(raw.at(index++), tile);
    }
    auto next = std::make_shared<StrokeOperation>(*op);
    auto ink = transfer.apply(source_ink);
    if (ink.paint.empty() && ink.ao.empty() &&
        (!source_ink.paint.empty() || !source_ink.ao.empty()))
      throw std::runtime_error(
          "A spatial stroke has no matching replacement surface; project retained");
    next->ink = std::make_shared<StrokeRaster>(std::move(ink));
    next->bounds = stroke_bounds(*next->ink);
    history->operations.push_back(next);
  }
  auto result = replay_strokes(*history, map.resolution);
  history->final_pixels = std::make_shared<StrokeRaster>(result);
  return {std::move(result), history};
}
StrokeRaster transfer_stroke_raster(const StrokeRaster &source,
                                    const UvTransferMap &mapping,
                                    const std::set<int> &tiles)
{
  return SparseStrokeTransfer(mapping, tiles).apply(source);
}
}  // namespace bts
