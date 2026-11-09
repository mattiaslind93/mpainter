#pragma once
#include <array>
#include <cmath>
#include <optional>
namespace bts {
class StrokeDirection {
 public:
  void reset()
  {
    uv_.reset();
    world_.reset();
  }
  std::optional<double> uv(std::array<double, 2> point)
  {
    const auto before = uv_;
    uv_ = point;
    if (!before)
      return {};
    const double x = point[0] - (*before)[0], y = point[1] - (*before)[1];
    if (std::hypot(x, y) < 1e-10)
      return {};
    return std::atan2(y, x);
  }
  std::optional<double> world(std::array<double, 3> point,
                              const std::array<double, 3> &tangent,
                              const std::array<double, 3> &bitangent)
  {
    const auto before = world_;
    world_ = point;
    if (!before)
      return {};
    double x = 0, y = 0;
    for (int i = 0; i < 3; ++i) {
      const auto delta = point[i] - (*before)[i];
      x += delta * tangent[i];
      y += delta * bitangent[i];
    }
    if (std::hypot(x, y) < 1e-10)
      return {};
    return std::atan2(y, x);
  }

 private:
  std::optional<std::array<double, 2>> uv_;
  std::optional<std::array<double, 3>> world_;
};
}  // namespace bts
