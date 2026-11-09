#pragma once
#include "SurfaceBrushBounds.hh"
namespace bts {
using SmudgeUvBounds = BrushUvBounds;
inline std::vector<SmudgeUvBounds> smudge_source_bounds(const PaintPixels::Stamp &stamp)
{
  return brush_uv_bounds(stamp);
}
}  // namespace bts
