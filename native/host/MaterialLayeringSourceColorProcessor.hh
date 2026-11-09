#pragma once
// Native-only, narrowly admitted processor family; no image/GPU/global context.
#include "MaterialLayeringSourceSnapshots.hh"
#include "BLI_math_matrix.hh"
#include <OpenColorIO/OpenColorIO.h>
#include <cmath>
#include <stdexcept>
namespace bts {
inline MaterialLayerSourceColorTransform material_layer_processor_transform(
    const OCIO_NAMESPACE::ConstGroupTransformRcPtr &group, const std::string &cache, bool srgb)
{
  using namespace blender;
  if (!group || group->getDirection() != OCIO_NAMESPACE::TRANSFORM_DIR_FORWARD)
    throw std::invalid_argument("Reusable source OCIO group direction is unsupported");
  double3x3 matrix = double3x3::identity();
  MaterialLayerSourceColorTransform result;
  result.processor = cache;
  result.srgb = srgb;
  bool curve = false;
  for (int i = 0; i < group->getNumTransforms(); ++i) {
    const auto op = group->getTransform(i);
    if (const auto *gamma = dynamic_cast<const OCIO_NAMESPACE::ExponentWithLinearTransform *>(
            op.get()))
    {
      if (!srgb || i != 0 || curve ||
          gamma->getDirection() != OCIO_NAMESPACE::TRANSFORM_DIR_FORWARD ||
          gamma->getNegativeStyle() != OCIO_NAMESPACE::NEGATIVE_LINEAR)
        throw std::invalid_argument(
            "Reusable sRGB processor must start with the supported forward negative-linear moncurve");
      double g[4], o[4];
      gamma->getGamma(g);
      gamma->getOffset(o);
      if (g[0] != 2.4 || g[1] != g[0] || g[2] != g[0] || g[3] != 1 || o[0] != .055 ||
          o[1] != o[0] || o[2] != o[0] || o[3] != 0)
        throw std::invalid_argument("Reusable sRGB processor curve or independent alpha changed");
      result.gamma = g[0];
      result.offset = o[0];
      result.breakpoint = o[0] / (g[0] - 1);
      result.slope = std::pow((result.breakpoint + o[0]) / (1 + o[0]), g[0]) / result.breakpoint;
      curve = true;
      continue;
    }
    const auto *linear = dynamic_cast<const OCIO_NAMESPACE::MatrixTransform *>(op.get());
    if (!linear || linear->getDirection() != OCIO_NAMESPACE::TRANSFORM_DIR_FORWARD ||
        (srgb && !curve))
      throw std::invalid_argument(
          "Reusable source OCIO processor has an unsupported operation or order");
    double c[16], offset[4];
    linear->getMatrix(c);
    linear->getOffset(offset);
    for (double v : c)
      if (!std::isfinite(v))
        throw std::invalid_argument("Reusable source color matrix is nonfinite");
    for (double v : offset)
      if (v != 0)
        throw std::invalid_argument("Reusable source color processor has an offset");
    if (c[3] != 0 || c[7] != 0 || c[11] != 0 || c[12] != 0 || c[13] != 0 || c[14] != 0 ||
        c[15] != 1)
      throw std::invalid_argument("Reusable source color processor changes independent alpha");
    double3x3 next;
    for (int column = 0; column < 3; ++column)
      for (int row = 0; row < 3; ++row)
        next[column][row] = c[row * 4 + column];
    matrix = next * matrix;
  }
  if (srgb && !curve)
    throw std::invalid_argument("Reusable sRGB processor is missing its curve");
  bool invertible = false;
  const auto inverse = math::invert(matrix, invertible);
  (void)inverse;
  if (!invertible)
    throw std::invalid_argument("Reusable source color matrix is singular");
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r) {
      const float value = float(matrix[c][r]);
      if (!std::isfinite(value))
        throw std::invalid_argument("Reusable source color matrix exceeds float precision");
      result.to_aces[c * 3 + r] = value;
    }
  return result;
}
// Reference/equivalence helper only; no interactive image loop uses it.
inline double material_layer_moncurve(double x, const MaterialLayerSourceColorTransform &t)
{
  return x <= t.breakpoint ? x * t.slope : std::pow((x + t.offset) / (1 + t.offset), t.gamma);
}
}  // namespace bts
