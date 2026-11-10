// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkewGeometry.hh"
#include "BLI_kdopbvh.hh"
#include "BLI_math_geom.h"
#include <limits>
#include <map>
namespace bts {
struct SkewGeometry::Impl {
  struct Tree {
    std::vector<SkewEdge> segments;
    blender::BVHTree *bvh = nullptr;
    ~Tree()
    {
      if (bvh)
        blender::BLI_bvhtree_free(bvh);
    }
  };
  std::map<std::uint32_t, std::unique_ptr<Tree>> objects;
  double extent = 1;
  std::size_t count = 0;
  static void nearest(void *data, int index, const float point[3], blender::BVHTreeNearest *hit)
  {
    const auto &edge = static_cast<Tree *>(data)->segments[index];
    float closest[3];
    blender::closest_to_line_segment_v3(closest, point, edge.a.data(), edge.b.data());
    float distance = 0;
    for (int k = 0; k < 3; ++k)
      distance += (closest[k] - point[k]) * (closest[k] - point[k]);
    if (distance < hit->dist_sq) {
      hit->dist_sq = distance;
      hit->index = index;
    }
  }
};
SkewGeometry::SkewGeometry(const SceneSnapshot &source) : impl_(std::make_unique<Impl>())
{
  auto &s = *impl_;
  s.extent = 0;
  for (int k = 0; k < 3; ++k)
    s.extent = std::max(s.extent, double(source.maximum[k] - source.minimum[k]));
  if (s.extent <= 0)
    s.extent = 1;
  for (const auto &edge : skew_hard_edges(source)) {
    auto &tree = s.objects[edge.object];
    if (!tree)
      tree = std::make_unique<Impl::Tree>();
    tree->segments.push_back(edge);
    ++s.count;
  }
  for (auto &[id, tree] : s.objects) {
    tree->bvh = blender::BLI_bvhtree_new(int(tree->segments.size()), 0, 4, 6);
    if (!tree->bvh)
      throw std::runtime_error("Cannot allocate skew hard-edge BVH");
    for (int i = 0; i < int(tree->segments.size()); ++i) {
      const std::array<std::array<float, 3>, 2> points{tree->segments[i].a, tree->segments[i].b};
      blender::BLI_bvhtree_insert(tree->bvh, i, points[0].data(), 2);
    }
    blender::BLI_bvhtree_balance(tree->bvh);
  }
}
SkewGeometry::~SkewGeometry() = default;
double SkewGeometry::distance(const std::array<float, 3> &point, std::uint32_t object) const
{
  const auto found = impl_->objects.find(object);
  if (found == impl_->objects.end())
    return std::numeric_limits<double>::infinity();
  blender::BVHTreeNearest hit{};
  hit.index = -1;
  hit.dist_sq = std::numeric_limits<float>::max();
  blender::BLI_bvhtree_find_nearest(
      found->second->bvh, point.data(), &hit, Impl::nearest, found->second.get());
  return hit.index < 0 ? std::numeric_limits<double>::infinity() : std::sqrt(hit.dist_sq);
}
float SkewGeometry::weight(const SkewField &field,
                           int tile,
                           double u,
                           double v,
                           const std::array<float, 3> &point,
                           std::uint32_t object) const
{
  const float value = field.sample(tile, u, v);
  if (value <= 0 || !field.settings.edge_protection)
    return value;
  return skew_edge_weight(value,
                          distance(point, object),
                          field.settings.edge_distance * impl_->extent,
                          field.settings.edge_contrast);
}
std::size_t SkewGeometry::edges() const
{
  return impl_->count;
}
}  // namespace bts
