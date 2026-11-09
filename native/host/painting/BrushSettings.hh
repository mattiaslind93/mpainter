#pragma once
#include "MaterialValues.hh"
#include "BrushAlpha.hh"
#include "BrushProjection.hh"
#include "BrushJitter.hh"
#include "BrushStencil.hh"
#include "ProjectionMaterial.hh"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts {
struct BrushSettings {
  std::array<bool, 8> users{};
  std::array<float, 8> user_values{1, 1, 1, 1, 1, 1, 1, 1};
  MaterialValues values = [] {
    MaterialValues value;
    value.base_color = {0, 0, 0, 1};
    return value;
  }();
  bool erase = false, ao = false, smudge = false, clone = false, clone_absolute = false;
  bool displacement = true;
  float displacement_value = 1;
  BrushAlpha alpha;
  BrushProjection projection;
  BrushJitter jitter;
  BrushStencil stencil;
  std::optional<ProjectionMaterial> projected;
  float angle = 0;
  bool follow_path = false;
  float hardness = 0;
  float mask_value = 1;
  std::array<bool, 5> channels{true, true, true, true, true};
  // Product mapping: percentage of selected object's largest world-space dimension in 3D;
  // percentage of one UV tile in 2D. It remains independent of camera zoom.
  float diameter = 10;
  bool pressure_size = true, pressure_flow = false;
  float minimum_size = 5, minimum_flow = 5;
  static float pressure_factor(float pressure, float minimum)
  {
    if (!std::isfinite(pressure) || !std::isfinite(minimum) || minimum < 0 || minimum > 100)
      throw std::invalid_argument("Invalid pen pressure or minimum");
    return minimum / 100 + (1 - minimum / 100) * std::clamp(pressure, 0.f, 1.f);
  }
  float diameter_at(float pressure) const
  {
    return diameter * (pressure_size ? pressure_factor(pressure, minimum_size) : 1);
  }
  float flow_at(float pressure) const
  {
    return flow * (pressure_flow ? pressure_factor(pressure, minimum_flow) : 1);
  }
  bool lazy_mouse = false;
  float lazy_distance = 8;  // Percent of the input pane height; independent of camera/UV zoom.
  float spacing = 20;       // Percent of projected brush diameter between successive stamps.
  float stroke_opacity = 100;
  float flow = 100;  // Percent strength of each stamp, independent of stroke opacity.
};
inline BrushSettings default_smudge_brush()
{
  BrushSettings brush;
  brush.smudge = true;
  brush.flow = 50;
  brush.spacing = 3;
  return brush;
}
}  // namespace bts
