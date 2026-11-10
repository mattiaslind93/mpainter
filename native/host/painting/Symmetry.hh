// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace bts {
enum class SymmetryMode { Mirror, Radial };
struct SymmetrySettings {
  bool enabled = false, manipulator = false, intersection = true;
  SymmetryMode mode = SymmetryMode::Mirror;
  int axis = 0, copies = 8;  // X for new sessions/projects; stored axes are read explicitly.
  double offset = 0;
  std::array<double, 3> center{};
  void validate() const
  {
    if (int(mode) < 0 || int(mode) > 1 || axis < 0 || axis > 2 || copies < 2 || copies > 32 ||
        !std::isfinite(offset) || std::abs(offset) > 1e6)
      throw std::invalid_argument("Invalid symmetry settings");
    for (double v : center)
      if (!std::isfinite(v) || std::abs(v) > 1e6)
        throw std::invalid_argument("Invalid symmetry center");
  }
};
struct SymmetryTransform {
  std::array<std::array<double, 3>, 3> matrix{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  std::array<double, 3> translation{};
  float orientation = 1;
  template<class P> std::array<double, 3> vector(const P &p) const
  {
    std::array<double, 3> result{};
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 3; ++col)
        result[row] += matrix[row][col] * p[col];
    return result;
  }
  template<class P> std::array<double, 3> point(const P &p) const
  {
    auto result = vector(p);
    for (int i = 0; i < 3; ++i)
      result[i] += translation[i];
    return result;
  }
};
inline std::vector<SymmetryTransform> symmetry_transforms(const SymmetrySettings &settings)
{
  settings.validate();
  std::vector<SymmetryTransform> result;
  if (!settings.enabled)
    return result;
  if (settings.mode == SymmetryMode::Mirror) {
    SymmetryTransform t;
    t.matrix[settings.axis][settings.axis] = -1;
    t.translation[settings.axis] = 2 * settings.offset;
    t.orientation = -1;
    result.push_back(t);
  }
  else
    for (int copy = 1; copy < settings.copies; ++copy) {
      SymmetryTransform t;
      const int i = (settings.axis + 1) % 3, j = (settings.axis + 2) % 3;
      const double angle = 2 * 3.14159265358979323846 * copy / settings.copies;
      t.matrix[i][i] = t.matrix[j][j] = std::cos(angle);
      t.matrix[i][j] = -std::sin(angle);
      t.matrix[j][i] = std::sin(angle);
      const auto center = t.vector(settings.center);
      for (int k = 0; k < 3; ++k)
        t.translation[k] = settings.center[k] - center[k];
      result.push_back(t);
    }
  return result;
}
}  // namespace bts
