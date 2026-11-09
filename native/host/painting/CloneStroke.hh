// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SmudgeStroke.hh"
namespace bts {
// Immutable source pixels avoid feedback when the clone passes over its own ink.
// Relative sources advance with each gesture; Absolute reuses the sampled footprint.
class CloneStroke {
 public:
  CloneStroke(const TilePaintPixels &pixels,
              int resolution,
              PaintPixels::Stamp source,
              bool absolute,
              AdjustmentPaintSource adjustment = {})
      : pixels_(pixels),
        resolution_(resolution),
        source_(std::move(source)),
        absolute_(absolute),
        adjustment_(std::move(adjustment))
  {
  }
  PaintPixels::Stamp source_frame(const PaintPixels::Stamp &destination)
  {
    if (!first_)
      first_ = destination;
    auto frame = source_;
    frame.channels = destination.channels;
    frame.stencil.reset();
    frame.axis_x = destination.axis_x;
    frame.axis_y = destination.axis_y;
    if (!absolute_)
      for (int c = 0; c < 2; ++c)
        frame.center[c] += destination.center[c] - first_->center[c];
    if (source_.surface) {
      auto surface = std::make_shared<PaintPixels::SurfaceFootprint>(*source_.surface);
      surface->unwrapped = false;
      if (destination.surface)
        surface->radius = destination.surface->radius;
      if (!absolute_) {
        if (destination.surface && first_->surface)
          for (int c = 0; c < 3; ++c)
            surface->center[c] += destination.surface->center[c] - first_->surface->center[c];
        else {
          const double rx = std::max(1e-12, std::hypot(source_.axis_x[0], source_.axis_x[1])),
                       ry = std::max(1e-12, std::hypot(source_.axis_y[0], source_.axis_y[1]));
          for (int c = 0; c < 3; ++c)
            surface->center[c] +=
                source_.surface->radius *
                ((destination.center[0] - first_->center[0]) / rx * surface->tangent[c] +
                 (destination.center[1] - first_->center[1]) / ry * surface->bitangent[c]);
        }
      }
      frame.surface = std::move(surface);
    }
    last_ = frame;
    return frame;
  }
  PaintPixels::Stamp dab(const PaintPixels::Stamp &destination)
  {
    auto stamp = destination;
    const auto frame = source_frame(destination);
    stamp.smudge.reset();
    stamp.projected.reset();
    stamp.cloned = capture_brush_payload(pixels_, frame, resolution_, adjustment_);
    return stamp;
  }
  const std::optional<PaintPixels::Stamp> &last_source() const
  {
    return last_;
  }

 private:
  TilePaintPixels pixels_;
  int resolution_;
  PaintPixels::Stamp source_;
  bool absolute_;
  AdjustmentPaintSource adjustment_;
  std::optional<PaintPixels::Stamp> first_, last_;
};
}  // namespace bts
