// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SceneSnapshot.hh"
#include "NormalVector.hh"
#include "FragmentTangents.hh"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <numbers>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>
namespace bts {
// Weld UV duplicates within each object. Independent objects never share normals.
struct SurfaceCurvature {
  using Vec = std::array<float, 3>;
  struct Vertex {
    Vec p{}, n{};
    float area = 0, curvature = 0;
    std::uint32_t object = 0;
    int component = -1;
    std::vector<int> neighbors;
  };
  std::vector<Vertex> vertices;
  std::vector<std::array<int, 3>> corners;
  static Vec sub(const Vec &a, const Vec &b)
  {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  }
  static float dot(const Vec &a, const Vec &b)
  {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  }
  static Vec cross(const Vec &a, const Vec &b)
  {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  }
  explicit SurfaceCurvature(SceneSnapshot &scene, bool recompute_normals)
  {
    double extent = 0;
    for (int a = 0; a < 3; ++a)
      extent = std::max(extent, double(scene.maximum[a] - scene.minimum[a]));
    const double epsilon = std::max(extent * 1e-7, 1e-12);
    using Key = std::tuple<std::uint32_t, long long, long long, long long>;
    std::map<Key, int> weld;
    for (auto &tri : scene.uv_triangles) {
      std::array<int, 3> ids;
      const auto geometric = cross(sub(tri.positions[1], tri.positions[0]),
                                   sub(tri.positions[2], tri.positions[0]));
      const float area = std::sqrt(dot(geometric, geometric)) * .5f;
      const auto face = normalized_normal(geometric);
      for (int k = 0; k < 3; ++k) {
        const auto &p = tri.positions[k];
        const auto object = std::uint32_t(tri.vertex_ids[k] >> 32);
        Key key{object,
                std::llround((double(p[0]) - scene.minimum[0]) / epsilon),
                std::llround((double(p[1]) - scene.minimum[1]) / epsilon),
                std::llround((double(p[2]) - scene.minimum[2]) / epsilon)};
        auto [it, inserted] = weld.emplace(key, int(vertices.size()));
        if (inserted) {
          Vertex v;
          v.p = p;
          v.object = object;
          vertices.push_back(v);
        }
        ids[k] = it->second;
        auto &v = vertices[ids[k]];
        const auto a = normalized_normal(sub(tri.positions[(k + 1) % 3], p)),
                   b = normalized_normal(sub(tri.positions[(k + 2) % 3], p));
        const float angle = std::acos(std::clamp(dot(a, b), -1.f, 1.f));
        const auto n = !recompute_normals && tri.has_normals ? normalized_normal(tri.normals[k]) :
                                                               face;
        for (int c = 0; c < 3; ++c)
          v.n[c] += n[c] * angle;
        v.area += area / 3;
      }
      corners.push_back(ids);
      const bool polygon_edges = tri.polygon_id != 0 || tri.boundary_edges != 0;
      for (int k = 0; k < 3; ++k)
        if (!polygon_edges || (tri.boundary_edges & (1 << k))) {
          const int a = ids[k], b = ids[(k + 1) % 3];
          if (a == b)
            continue;
          vertices[a].neighbors.push_back(b);
          vertices[b].neighbors.push_back(a);
        }
    }
    for (auto &v : vertices) {
      v.n = normalized_normal(v.n);
      auto &n = v.neighbors;
      std::sort(n.begin(), n.end());
      n.erase(std::unique(n.begin(), n.end()), n.end());
    }
    for (int i = 0; i < int(vertices.size()); ++i)
      if (vertices[i].component < 0) {
        std::vector<int> queue{i};
        vertices[i].component = i;
        for (std::size_t j = 0; j < queue.size(); ++j)
          for (int next : vertices[queue[j]].neighbors)
            if (vertices[next].component < 0) {
              vertices[next].component = i;
              queue.push_back(next);
            }
      }
    // Assign unique welded IDs after subdivision; reusing the cage IDs collapses
    // new vertices when Blender reconstructs the rasterization mesh.
    for (std::size_t i = 0; i < corners.size(); ++i) {
      auto &tri = scene.uv_triangles[i];
      tri.has_normals = true;
      for (int k = 0; k < 3; ++k) {
        const auto id = corners[i][k];
        tri.normals[k] = vertices[id].n;
        tri.vertex_ids[k] = (std::uint64_t(vertices[id].object) << 32) | std::uint32_t(id);
      }
    }
  }
  // Balanced spatial tree: radius queries use bounded stack space and do not
  // allocate a texture-sized neighborhood table.
  struct Node {
    int vertex = -1, left = -1, right = -1, axis = 0;
  };
  std::vector<Node> tree;
  int build(std::vector<int> &order, int begin, int end, int depth)
  {
    if (begin == end)
      return -1;
    const int axis = depth % 3, middle = (begin + end) / 2;
    std::nth_element(order.begin() + begin,
                     order.begin() + middle,
                     order.begin() + end,
                     [&](int a, int b) { return vertices[a].p[axis] < vertices[b].p[axis]; });
    const int node = int(tree.size());
    tree.push_back({order[middle], -1, -1, axis});
    const int left = build(order, begin, middle, depth + 1),
              right = build(order, middle + 1, end, depth + 1);
    tree[node].left = left;
    tree[node].right = right;
    return node;
  }
  template<class F> void query(int node, const Vec &p, float radius, F &visit) const
  {
    if (node < 0)
      return;
    const auto &q = tree[node];
    const auto &v = vertices[q.vertex];
    const auto d = sub(v.p, p);
    const float r2 = radius * radius;
    if (dot(d, d) < r2)
      visit(q.vertex, dot(d, d));
    if (p[q.axis] - radius <= v.p[q.axis])
      query(q.left, p, radius, visit);
    if (p[q.axis] + radius >= v.p[q.axis])
      query(q.right, p, radius, visit);
  }
  void calculate_pointiness(float radius = 0)
  {
    if (!std::isfinite(radius) || radius < 0)
      throw std::invalid_argument("Pointiness smoothing radius must be nonnegative and finite");
    std::vector<float> raw(vertices.size(), .5f);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
      const auto &v = vertices[i];
      Vec direction{};
      for (int j : v.neighbors) {
        const auto edge = sub(vertices[j].p, v.p);
        const float length = std::sqrt(dot(edge, edge));
        if (length > 0)
          for (int a = 0; a < 3; ++a)
            direction[a] += edge[a] / length;
      }
      if (!v.neighbors.empty())
        raw[i] = std::acos(
                     std::clamp(dot(v.n, direction) / float(v.neighbors.size()), -1.f, 1.f)) /
                 std::numbers::pi_v<float>;
    }
    // Cycles Geometry Pointiness averages unit edge directions, then blurs
    // the resulting angle once over the same welded polygon-edge neighbors.
    for (std::size_t i = 0; i < vertices.size(); ++i) {
      auto &v = vertices[i];
      float sum = raw[i];
      for (int j : v.neighbors)
        sum += raw[j];
      v.curvature = sum / float(v.neighbors.size() + 1);
    }
    if (radius == 0)
      return;
    tree.clear();
    tree.reserve(vertices.size());
    std::vector<int> order(vertices.size());
    std::iota(order.begin(), order.end(), 0);
    build(order, 0, int(order.size()), 0);
    std::vector<float> smoothed(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
      const auto &v = vertices[i];
      double sum = 0, total = 0;
      auto smooth = [&](int j, float d2) {
        const auto &other = vertices[j];
        if (other.component != v.component)
          return;
        const double falloff = 1 - d2 / (radius * radius),
                     weight = falloff * falloff * std::max(other.area, 1e-20f);
        sum += other.curvature * weight;
        total += weight;
      };
      query(0, v.p, radius, smooth);
      smoothed[i] = total > 0 ? float(sum / total) : v.curvature;
    }
    for (std::size_t i = 0; i < vertices.size(); ++i)
      vertices[i].curvature = smoothed[i];
  }
  void calculate(float radius)
  {
    if (!std::isfinite(radius) || radius <= 0)
      throw std::invalid_argument("Curvature radius must be positive and finite");
    tree.clear();
    tree.reserve(vertices.size());
    std::vector<int> order(vertices.size());
    std::iota(order.begin(), order.end(), 0);
    build(order, 0, int(order.size()), 0);
    for (int i = 0; i < int(vertices.size()); ++i) {
      auto &v = vertices[i];
      const auto &normal = v.n;
      const Vec helper = std::abs(normal[2]) > .9f ? Vec{1, 0, 0} : Vec{0, 0, 1};
      const auto t = normalized_normal(cross(helper, normal)), b = cross(normal, t);
      double numerator = 0, denominator = 0;
      int count = 0;
      const auto add = [&](int j, float distance2, double weight) {
        if (j == i || distance2 < 1e-20f)
          return;
        const auto &other = vertices[j];
        if (other.component != v.component || dot(normal, other.n) < .25f)
          return;
        const auto dp = sub(other.p, v.p), dn = sub(other.n, normal);
        const double x = dot(dp, t), y = dot(dp, b), length = x * x + y * y;
        if (length < distance2 * .1)
          return;
        // Mean directional normal derivative, fitted without dividing individual
        // observations by tiny edge lengths. An isotropic world-space disk
        // estimates trace(S)/2; spheres remain exact at any tessellation/scale.
        numerator += weight * (dot(dn, t) * x + dot(dn, b) * y);
        denominator += weight * length;
        ++count;
      };
      auto visit = [&](int j, float d2) {
        const double w = 1 - d2 / (radius * radius);
        add(j, d2, w * w * std::max(vertices[j].area, 1e-20f));
      };
      query(0, v.p, radius, visit);
      if (count < 5) {
        numerator = denominator = 0;
        for (int j : v.neighbors) {
          const auto d = sub(vertices[j].p, v.p);
          add(j, dot(d, d), 1);
        }
      }
      v.curvature = denominator > 0 ? float(numerator / denominator) : 0;
    }
    // Area-weighted scale-space curvature. This suppresses tiny-bevel spikes
    // without changing constant curvature (a sphere) or normalizing each UV tile.
    std::vector<float> smoothed(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
      const auto &v = vertices[i];
      double sum = 0, total = 0;
      auto smooth = [&](int j, float d2) {
        const auto &other = vertices[j];
        if (other.component != v.component)
          return;
        const double agreement = std::max(0.f, dot(v.n, other.n)),
                     falloff = 1 - d2 / (radius * radius);
        const double weight = falloff * falloff * agreement * agreement *
                              std::max(other.area, 1e-20f);
        sum += other.curvature * weight;
        total += weight;
      };
      query(0, v.p, radius, smooth);
      smoothed[i] = total > 0 ? float(sum / total) : v.curvature;
    }
    for (std::size_t i = 0; i < vertices.size(); ++i)
      vertices[i].curvature = smoothed[i];
  }
};
}  // namespace bts
