// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SceneSnapshot.hh"
#include <QJsonObject>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
namespace bts {
struct ResidencyView {
  std::array<float, 16> view, projection;
  int width = 0, height = 0;
  bool uv = false;
};
// Owner-context software fallback: budgeted image/UDIM-array mip regions.
// Full-resolution CPU composites and document/export pixels are not modified.
class ViewportResidency {
 public:
  ViewportResidency();
  ~ViewportResidency();
  void reset();
  void update(blender::Main &,
              std::span<const ResidencyView>,
              std::optional<std::uint32_t>,
              const std::function<SceneSnapshot()> &,
              bool editing);
  void finish_frame();
  QJsonObject snapshot() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
