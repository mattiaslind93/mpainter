// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>
#include <utility>
namespace bts {
using WarpVector = std::array<double, 3>;
inline WarpVector warp_add(WarpVector a, const WarpVector &b, double factor = 1)
{
  for (int c = 0; c < 3; ++c)
    a[c] += b[c] * factor;
  return a;
}
inline WarpVector warp_scale(WarpVector a, double factor)
{
  for (double &v : a)
    v *= factor;
  return a;
}
inline double warp_dot(const WarpVector &a, const WarpVector &b)
{
  double value = 0;
  for (int c = 0; c < 3; ++c)
    value += a[c] * b[c];
  return value;
}
inline WarpVector warp_cross(const WarpVector &a, const WarpVector &b)
{
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline WarpVector warp_normal(WarpVector a)
{
  const double length = std::sqrt(warp_dot(a, a));
  if (length < 1e-12)
    return {};
  return warp_scale(a, 1 / length);
}
struct WarpNode {
  WarpVector position{}, du{1, 0, 0}, dv{0, 0, 1}, duv{};
};
struct WarpSurface {
  WarpVector position{}, du{}, dv{}, duv{}, duu{}, dvv{}, duuv{}, duvv{};
  WarpVector normal() const
  {
    return warp_normal(warp_cross(du, dv));
  }
};
struct WarpCoordinate {
  double u = .5, v = .5, depth = 0, error = 0;
  WarpVector normal{0, -1, 0}, du{1, 0, 0}, dv{0, 0, 1};
};
// Tensor Hermite form of a bicubic Bezier patch. Tangents use global normalized
// UV units, so inserting a knot preserves both the surface and its derivatives.
// Blender's BKE_curve_forward_diff_bezier and the host SurfacePath Bernstein
// evaluator are the curve references; this adds the two-dimensional tensor form.
class WarpGrid {
  struct Basis {
    std::array<double, 2> value, tangent, derivative, tangent_derivative, second, tangent_second;
  };
  static Basis basis(double t, double span)
  {
    const double t2 = t * t, t3 = t2 * t;
    return {{2 * t3 - 3 * t2 + 1, -2 * t3 + 3 * t2},
            {(t3 - 2 * t2 + t) * span, (t3 - t2) * span},
            {(6 * t2 - 6 * t) / span, (-6 * t2 + 6 * t) / span},
            {3 * t2 - 4 * t + 1, 3 * t2 - 2 * t},
            {(12 * t - 6) / (span * span), (-12 * t + 6) / (span * span)},
            {(6 * t - 4) / span, (6 * t - 2) / span}};
  }
  static int interval(const std::vector<double> &knots, double value)
  {
    return std::clamp(int(std::upper_bound(knots.begin(), knots.end(), value) - knots.begin()) - 1,
                      0,
                      int(knots.size()) - 2);
  }
  WarpSurface inside(double u, double v) const
  {
    const int column = interval(columns, u), row = interval(rows, v);
    const double su = columns[column + 1] - columns[column], sv = rows[row + 1] - rows[row];
    const auto a = basis((u - columns[column]) / su, su), b = basis((v - rows[row]) / sv, sv);
    WarpSurface output;
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        const auto &node = at(column + i, row + j);
        for (int dv = 0; dv < 2; ++dv)
          for (int du = 0; du < 2; ++du) {
            const auto &value = du ? (dv ? node.duv : node.du) : (dv ? node.dv : node.position);
            const double au = du ? a.tangent[i] : a.value[i],
                         bu = du ? a.tangent_derivative[i] : a.derivative[i],
                         av = dv ? b.tangent[j] : b.value[j],
                         bv = dv ? b.tangent_derivative[j] : b.derivative[j];
            output.position = warp_add(output.position, value, au * av);
            output.du = warp_add(output.du, value, bu * av);
            output.dv = warp_add(output.dv, value, au * bv);
            output.duv = warp_add(output.duv, value, bu * bv);
            const double cu = du ? a.tangent_second[i] : a.second[i],
                         cv = dv ? b.tangent_second[j] : b.second[j];
            output.duu = warp_add(output.duu, value, cu * av);
            output.dvv = warp_add(output.dvv, value, au * cv);
            output.duuv = warp_add(output.duuv, value, cu * bv);
            output.duvv = warp_add(output.duvv, value, bu * cv);
          }
      }
    return output;
  }

 public:
  std::vector<double> columns, rows;
  std::vector<WarpNode> nodes;
  bool automatic_tangents = true;
  WarpGrid(int width = 3, int height = 3)
  {
    reset(width, height);
  }
  int width() const
  {
    return int(columns.size());
  }
  int height() const
  {
    return int(rows.size());
  }
  WarpNode &at(int column, int row)
  {
    return nodes.at(std::size_t(row) * width() + column);
  }
  const WarpNode &at(int column, int row) const
  {
    return nodes.at(std::size_t(row) * width() + column);
  }
  static WarpNode initial(double u, double v)
  {
    return {{u - .5, -.5, v - .5}, {1, 0, 0}, {0, 0, 1}, {}};
  }
  void reset(int width, int height)
  {
    if (width < 2 || height < 2 || width > 9 || height > 9)
      throw std::invalid_argument("Warp grid requires 2–9 points per axis");
    columns.resize(width);
    rows.resize(height);
    for (int i = 0; i < width; ++i)
      columns[i] = double(i) / (width - 1);
    for (int j = 0; j < height; ++j)
      rows[j] = double(j) / (height - 1);
    nodes.resize(width * height);
    reset_vertices();
  }
  void reset_vertices()
  {
    for (int j = 0; j < height(); ++j)
      for (int i = 0; i < width(); ++i)
        at(i, j) = initial(columns[i], rows[j]);
  }
  void validate() const
  {
    if (width() < 2 || width() > 9 || height() < 2 || height() > 9 ||
        nodes.size() != columns.size() * rows.size())
      throw std::invalid_argument("Invalid Warp grid dimensions");
    for (const auto *knots : {&columns, &rows}) {
      if (knots->front() != 0 || knots->back() != 1)
        throw std::invalid_argument("Warp knots must span 0–1");
      double previous = -1;
      for (double value : *knots) {
        if (!std::isfinite(value) || value < 0 || value > 1 || value - previous < 1e-5)
          throw std::invalid_argument("Invalid Warp grid knots");
        previous = value;
      }
    }
    for (const auto &node : nodes)
      for (const auto &vector : {node.position, node.du, node.dv, node.duv})
        for (double value : vector)
          if (!std::isfinite(value) || std::abs(value) > 10000)
            throw std::invalid_argument("Nonfinite or unbounded Warp vertex/tangent");
  }
  bool undeformed() const
  {
    for (int j = 0; j < height(); ++j)
      for (int i = 0; i < width(); ++i) {
        const auto &node = at(i, j);
        const auto expected = initial(columns[i], rows[j]);
        for (const auto pair : {std::pair{node.position, expected.position},
                                std::pair{node.du, expected.du},
                                std::pair{node.dv, expected.dv},
                                std::pair{node.duv, expected.duv}})
          for (int c = 0; c < 3; ++c)
            if (std::abs(pair.first[c] - pair.second[c]) > 1e-9)
              return false;
      }
    return true;
  }
  WarpSurface evaluate(double u, double v) const
  {
    const double cu = std::clamp(u, 0., 1.), cv = std::clamp(v, 0., 1.), eu = u - cu, ev = v - cv;
    auto value = inside(cu, cv);
    // Outside the editable grid, extend its boundary tangents rather than an
    // unbounded cubic polynomial. Shape Crop can disable this continuation.
    value.position = warp_add(
        warp_add(warp_add(value.position, value.du, eu), value.dv, ev), value.duv, eu * ev);
    value.du = warp_add(value.du, value.duv, ev);
    value.dv = warp_add(value.dv, value.duv, eu);
    value.duu = eu != 0 ? WarpVector{} : warp_add(value.duu, value.duuv, ev);
    value.dvv = ev != 0 ? WarpVector{} : warp_add(value.dvv, value.duvv, eu);
    return value;
  }
  void align_tangents()
  {
    for (int j = 0; j < height(); ++j)
      for (int i = 0; i < width(); ++i) {
        const int left = std::max(0, i - 1), right = std::min(width() - 1, i + 1),
                  below = std::max(0, j - 1), above = std::min(height() - 1, j + 1);
        auto &node = at(i, j);
        node.du = warp_scale(warp_add(at(right, j).position, at(left, j).position, -1),
                             1 / (columns[right] - columns[left]));
        node.dv = warp_scale(warp_add(at(i, above).position, at(i, below).position, -1),
                             1 / (rows[above] - rows[below]));
      }
    for (int j = 0; j < height(); ++j)
      for (int i = 0; i < width(); ++i) {
        const int below = std::max(0, j - 1), above = std::min(height() - 1, j + 1);
        at(i, j).duv = warp_scale(warp_add(at(i, above).du, at(i, below).du, -1),
                                  1 / (rows[above] - rows[below]));
      }
  }
  void move(int index, WarpVector position)
  {
    auto candidate = *this;
    candidate.nodes.at(index).position = position;
    if (candidate.automatic_tangents)
      candidate.align_tangents();
    candidate.validate();
    *this = std::move(candidate);
  }
  void split(bool vertical, double coordinate)
  {
    if (!std::isfinite(coordinate) || coordinate <= 1e-5 || coordinate >= 1 - 1e-5)
      throw std::invalid_argument("Warp split must lie inside the grid");
    auto candidate = *this;
    auto &knots = vertical ? candidate.columns : candidate.rows;
    if (knots.size() >= 9)
      throw std::length_error("Warp grid has reached 9 points per axis");
    for (double knot : knots)
      if (std::abs(knot - coordinate) < 1e-5)
        throw std::invalid_argument("Warp split is too close to an existing division");
    knots.insert(std::upper_bound(knots.begin(), knots.end(), coordinate), coordinate);
    candidate.nodes.resize(candidate.width() * candidate.height());
    for (int j = 0; j < candidate.height(); ++j)
      for (int i = 0; i < candidate.width(); ++i) {
        const auto sample = evaluate(candidate.columns[i], candidate.rows[j]);
        candidate.at(i, j) = {sample.position, sample.du, sample.dv, sample.duv};
      }
    candidate.validate();
    *this = std::move(candidate);
  }
  std::optional<WarpCoordinate> project(const WarpVector &point,
                                        const WarpVector &metric = {1, 1, 1}) const
  {
    WarpVector stretch;
    for (int c = 0; c < 3; ++c) {
      if (!std::isfinite(metric[c]) || metric[c] < 1e-12 || metric[c] > 1e12)
        return {};
      stretch[c] = std::sqrt(metric[c]);
    }
    const auto dot = [&](const WarpVector &a, const WarpVector &b) {
      double value = 0;
      for (int c = 0; c < 3; ++c)
        value += a[c] * b[c] * metric[c];
      return value;
    };
    const auto scaled = [&](WarpVector value) {
      for (int c = 0; c < 3; ++c)
        value[c] *= stretch[c];
      return value;
    };
    for (double value : point)
      if (!std::isfinite(value) || std::abs(value) > 1e7)
        return {};
    double u = point[0] + .5, v = point[2] + .5;
    // Use the true distance Hessian where positive definite. A tangent-plane
    // Gauss-Newton step can oscillate for dozens of iterations when the
    // projected point lies far above a curved patch. Fall back to that descent
    // direction only when curvature makes the true Hessian indefinite.
    for (int iteration = 0; iteration < 64; ++iteration) {
      const auto value = evaluate(u, v);
      const auto residual = warp_add(point, value.position, -1);
      double a = dot(value.du, value.du), b = dot(value.du, value.dv), c = dot(value.dv, value.dv),
             det = a * c - b * b;
      if (det < 1e-16)
        return {};
      const double ha = a - dot(residual, value.duu), hb = b - dot(residual, value.duv),
                   hc = c - dot(residual, value.dvv), hd = ha * hc - hb * hb;
      if (ha > 1e-12 && hc > 1e-12 && hd > 1e-16) {
        a = ha;
        b = hb;
        c = hc;
        det = hd;
      }
      const double x = dot(residual, value.du), y = dot(residual, value.dv);
      double du = (c * x - b * y) / det, dv = (a * y - b * x) / det;
      const double limit = std::max({1., std::abs(du) * 2, std::abs(dv) * 2});
      du /= limit;
      dv /= limit;
      if (std::hypot(du, dv) < 1e-9)
        break;
      const double old_distance = dot(residual, residual);
      bool accepted = false;
      for (int step = 0; step < 10; ++step) {
        const double factor = std::ldexp(1., -step),
                     next_u = std::clamp(u + du * factor, -32., 33.),
                     next_v = std::clamp(v + dv * factor, -32., 33.);
        const auto error = warp_add(point, evaluate(next_u, next_v).position, -1);
        if (dot(error, error) <= old_distance + 1e-14) {
          u = next_u;
          v = next_v;
          accepted = true;
          break;
        }
      }
      if (!accepted)
        break;
    }
    const auto value = evaluate(u, v);
    const auto normal = warp_normal(warp_cross(scaled(value.du), scaled(value.dv)));
    if (warp_dot(normal, normal) < .5)
      return {};
    const auto residual = scaled(warp_add(point, value.position, -1));
    const double depth = warp_dot(residual, normal);
    const auto tangential = warp_add(residual, normal, -depth);
    const double error = std::sqrt(warp_dot(tangential, tangential));
    if (error > 1e-5)
      return {};
    return WarpCoordinate{u, v, depth, error, normal, value.du, value.dv};
  }
};
}  // namespace bts
