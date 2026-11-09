// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SkewEdges.hh"
#include "SkewField.hh"
#include <memory>
namespace bts {
class SkewGeometry {
 public:
  explicit SkewGeometry(const SceneSnapshot &);
  ~SkewGeometry();
  float weight(const SkewField &,
               int tile,
               double u,
               double v,
               const std::array<float, 3> &point,
               std::uint32_t object) const;
  double distance(const std::array<float, 3> &, std::uint32_t object) const;
  std::size_t edges() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
