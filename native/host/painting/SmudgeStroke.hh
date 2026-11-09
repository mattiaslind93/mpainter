#pragma once
#include "StrokeHistory.hh"
#include "AdjustmentPaintSource.hh"
namespace bts {
std::shared_ptr<SmudgePayload> capture_brush_payload(const TilePaintPixels &,
                                                     const PaintPixels::Stamp &,
                                                     int resolution,
                                                     AdjustmentPaintSource = {});
class SmudgeStroke {
 public:
  SmudgeStroke(const TilePaintPixels &before, int resolution, AdjustmentPaintSource = {});
  PaintPixels::Bounds dab(const PaintPixels::Stamp &, const std::set<int> &tiles, float pickup);
  PaintPixels::Bounds publish(float opacity);
  const TilePaintPixels &pixels() const
  {
    return published_;
  }
  PaintPixels::Bounds touched() const
  {
    return touched_;
  }

 private:
  TilePaintPixels before_, working_, published_;
  int resolution_;
  AdjustmentPaintSource adjustment_;
  std::shared_ptr<const SmudgePayload> payload_;
  PaintPixels::Bounds touched_{};
};
std::shared_ptr<const StrokeHistory> append_smudge(const std::shared_ptr<const StrokeHistory> &,
                                                   const StrokeRaster &before,
                                                   const StrokeRaster &after,
                                                   bool spatial);
}  // namespace bts
