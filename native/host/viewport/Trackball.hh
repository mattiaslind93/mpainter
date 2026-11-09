// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPointF>
#include <QRect>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace bts::trackball {
using Quaternion = std::array<double, 4>;  // Scalar-first, world-to-view rotation.
using Rows = std::array<std::array<double, 3>, 3>;
constexpr float radius = 1.1f, sensitivity = 1.5f;
inline Quaternion normalized(Quaternion q)
{
  double n = 0;
  for (double x : q)
    n += x * x;
  n = std::sqrt(n);
  if (!std::isfinite(n) || n == 0)
    return {1, 0, 0, 0};
  for (auto &x : q)
    x /= n;
  return q;
}
inline Quaternion product(const Quaternion &a, const Quaternion &b)
{
  return {a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
          a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
          a[0] * b[2] + a[2] * b[0] + a[3] * b[1] - a[1] * b[3],
          a[0] * b[3] + a[3] * b[0] + a[1] * b[2] - a[2] * b[1]};
}
inline Rows rows(const Quaternion &q)
{
  const auto [w, x, y, z] = q;
  return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
           {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
           {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
}
inline Quaternion from_rows(const Rows &m)
{
  Quaternion q;
  const double trace = m[0][0] + m[1][1] + m[2][2];
  if (trace > 0) {
    const double s = 2 * std::sqrt(trace + 1);
    q = {s / 4, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s};
  }
  else {
    int i = 0;
    if (m[1][1] > m[i][i])
      i = 1;
    if (m[2][2] > m[i][i])
      i = 2;
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    const double s = 2 * std::sqrt(1 + m[i][i] - m[j][j] - m[k][k]);
    q[0] = (m[k][j] - m[j][k]) / s;
    q[i + 1] = s / 4;
    q[j + 1] = (m[j][i] + m[i][j]) / s;
    q[k + 1] = (m[k][i] + m[i][k]) / s;
  }
  return normalized(q);
}
// Qt logical top-left coordinates -> physical pane-local Blender coordinates.
// Preserve integer region centers and allow the captured pointer outside the pane.
inline std::optional<QPoint> pointer(QPointF logical, QRect pane, double dpr)
{
  if (pane.width() <= 1 || pane.height() <= 1 || !std::isfinite(dpr) || dpr <= 0 ||
      !std::isfinite(logical.x()) || !std::isfinite(logical.y()))
    return {};
  const double x = std::floor(logical.x() * dpr) - pane.x(),
               y = double(pane.height()) - 1 - (std::floor(logical.y() * dpr) - pane.y());
  if (!std::isfinite(x) || !std::isfinite(y) ||
      std::max(std::abs(x), std::abs(y)) > std::numeric_limits<int>::max())
    return {};
  return QPoint(int(x), int(y));
}
inline std::array<float, 3> vector(QPoint p, QSize size)
{
  // A physical W-by-H Qt pane is Blender's inclusive rcti(0,W-1,0,H-1).
  // winx is BLI_rcti_size_x(winrct)+1, not the calctrackballvec extent.
  if (size.width() <= 1 || size.height() <= 1)
    return {};
  const int xmax = size.width() - 1, ymax = size.height() - 1;
  const float width = float(xmax), height = float(ymax), minimum = std::min(width, height);
  const float aspect_x = minimum / width, aspect_y = minimum / height;
  const float x = float(double(p.x()) - xmax / 2) / ((width * aspect_x) / 2),
              y = float(double(p.y()) - ymax / 2) / ((height * aspect_y) / 2),
              d = std::sqrt(x * x + y * y), t = radius / std::sqrt(2.f);
  return {x, y, d < t ? std::sqrt(radius * radius - d * d) : t * t / d};
}
// Pinned Blender calctrackballvec + viewrotate_apply: no vector normalization
// before the chord-length angle, fixed sensitivity, wrapped angle, LEFT product.
inline Quaternion rotation(QPoint press, QPoint current, QSize size, const Quaternion &initial)
{
  const auto a = vector(press, size), b = vector(current, size);
  std::array<float, 3> axis{
      a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
  float angle = std::sqrt(dx * dx + dy * dy + dz * dz) / (2 * radius) * std::numbers::pi_v<float> *
                sensitivity;
  const float tau = 2 * std::numbers::pi_v<float>;
  angle = (angle + std::numbers::pi_v<float>)-tau *
              std::floor((angle + std::numbers::pi_v<float>) / tau) -
          std::numbers::pi_v<float>;
  const float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
  Quaternion delta{1, 0, 0, 0};
  if (length > 0) {
    const double factor = std::sin(angle * .5) / length;
    delta = {std::cos(angle * .5), axis[0] * factor, axis[1] * factor, axis[2] * factor};
  }
  return normalized(product(delta, initial));
}
struct Press {
  QRect pane;
  double dpr;
  QPoint pointer;
  Quaternion orientation;
};
}  // namespace bts::trackball
