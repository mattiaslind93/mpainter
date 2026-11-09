#pragma once
#include "Symmetry.hh"
#include <memory>
#include <numbers>
namespace bts {
struct FillSymmetry {
  SymmetrySettings constraint = [] {
    SymmetrySettings value;
    value.axis = 0;
    return value;
  }();
  bool flip_u = false, flip_v = false;
  void validate() const
  {
    constraint.validate();
  }
};
// Spatial constraints use scene units. A whole projection is folded into its
// reference half-space/sector, never copied as individual paint dabs.
// The reference follows the projection origin; when it lies on the constraint,
// +axis (mirror) or angle zero (radial) is the deterministic native convention.
template<class Surface> struct FoldedFillSurface {
  Surface surface;
  int copy = 0;
};
template<class Surface>
FoldedFillSurface<Surface> fold_fill_surface(const FillSymmetry *options,
                                             const Surface &surface,
                                             const std::array<double, 3> &projection_origin)
{
  FoldedFillSurface<Surface> result{surface, 0};
  if (!options || !options->constraint.enabled || !surface.valid)
    return result;
  const auto &s = options->constraint;
  const int axis = s.axis;
  if (s.mode == SymmetryMode::Mirror) {
    const double side = projection_origin[axis] >= s.offset ? 1 : -1;
    if ((surface.position[axis] - s.offset) * side < 0) {
      result.surface.position[axis] = float(2 * s.offset - surface.position[axis]);
      result.surface.normal[axis] = -surface.normal[axis];
      result.copy = 1;
    }
  }
  else {
    const int i = (axis + 1) % 3, j = (axis + 2) % 3;
    const double x = surface.position[i] - s.center[i], y = surface.position[j] - s.center[j];
    const double ox = projection_origin[i] - s.center[i], oy = projection_origin[j] - s.center[j];
    const double reference = std::hypot(ox, oy) < 1e-12 ? 0 : std::atan2(oy, ox);
    const double sector = 2 * std::numbers::pi / s.copies;
    // remainder keeps both sides of the angular seam in the same sector.
    const double relative = std::remainder(std::atan2(y, x) - reference, 2 * std::numbers::pi);
    const int copy = int(std::floor(relative / sector + .5));
    const double angle = -copy * sector, c = std::cos(angle), sn = std::sin(angle);
    result.surface.position[i] = float(s.center[i] + c * x - sn * y);
    result.surface.position[j] = float(s.center[j] + sn * x + c * y);
    result.surface.normal[i] = float(c * surface.normal[i] - sn * surface.normal[j]);
    result.surface.normal[j] = float(sn * surface.normal[i] + c * surface.normal[j]);
    result.copy = (copy % s.copies + s.copies) % s.copies;
  }
  return result;
}
}  // namespace bts
