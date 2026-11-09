#pragma once
#include "SceneSnapshot.hh"
#include <QVector3D>
#include <span>
namespace bts {
struct SymmetrySegment {
  QVector3D a, b, origin, normal;
};
// Mesh-space intersections do not depend on the camera. Keep only segments,
// their original face normal, and origin for cheap per-frame facing tests.
class SymmetryIntersectionCache {
  const UvFaceTriangle *source_ = nullptr;
  std::size_t size_ = 0;
  int axis_ = -1;
  double offset_ = 0;
  bool valid_ = false;
  std::vector<SymmetrySegment> lines_;
  std::uint64_t builds_ = 0;

 public:
  void invalidate()
  {
    valid_ = false;
  }
  std::uint64_t builds() const
  {
    return builds_;
  }
  const std::vector<SymmetrySegment> &get(std::span<const UvFaceTriangle> triangles,
                                          int axis,
                                          double offset)
  {
    if (valid_ && source_ == triangles.data() && size_ == triangles.size() && axis_ == axis &&
        offset_ == offset)
      return lines_;
    if (axis < 0 || axis > 2 || !std::isfinite(offset))
      throw std::invalid_argument("Invalid symmetry intersection plane");
    source_ = triangles.data();
    size_ = triangles.size();
    axis_ = axis;
    offset_ = offset;
    valid_ = true;
    ++builds_;
    lines_.clear();
    for (const auto &triangle : triangles) {
      const auto &p = triangle.positions;
      const QVector3D a(p[0][0], p[0][1], p[0][2]), b(p[1][0], p[1][1], p[1][2]),
          c(p[2][0], p[2][1], p[2][2]);
      std::array<QVector3D, 6> points;
      int count = 0;
      for (int i = 0; i < 3; ++i) {
        const int next = (i + 1) % 3;
        const double d = p[i][axis] - offset, e = p[next][axis] - offset;
        const QVector3D start(p[i][0], p[i][1], p[i][2]), end(p[next][0], p[next][1], p[next][2]);
        if (std::abs(d) < 1e-7)
          points[count++] = start;
        if (d * e < 0)
          points[count++] = start + (end - start) * float(d / (d - e));
      }
      if (count < 2)
        continue;
      const auto first = points[0];
      auto last = points[1];
      for (int i = 2; i < count; ++i)
        if ((points[i] - first).lengthSquared() > (last - first).lengthSquared())
          last = points[i];
      lines_.push_back({first, last, a, QVector3D::crossProduct(b - a, c - a)});
    }
    return lines_;
  }
};
}  // namespace bts
