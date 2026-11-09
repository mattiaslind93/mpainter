// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HybridNormalToBump.hh"
#include <memory>
namespace bts {
struct ExportNormalContribution {
  std::shared_ptr<const std::vector<float>> pixels;   // encoded OpenGL, bottom-up RGBA
  std::shared_ptr<const std::vector<float>> weights;  // optional scalar coverage after integration
  float strength = 1;
  bool includes_scalar_bump = false;  // full baked surface chain: do not add Height twice
};
inline std::vector<float> export_normal_bump(std::span<const ExportNormalContribution> sources,
                                             int width,
                                             int height,
                                             const std::atomic_bool *cancel = nullptr)
{
  std::vector<float> result(std::size_t(width) * height, 0.f);
  for (const auto &source : sources) {
    if (!source.pixels || !std::isfinite(source.strength) ||
        (source.weights && source.weights->size() != result.size()))
      throw std::invalid_argument("Invalid export normal contribution");
    auto height_values = hybrid_normal_to_bump(*source.pixels, width, height, 4, false, cancel);
    for (std::size_t i = 0; i < result.size(); ++i) {
      const float weight = source.weights ? source.weights->at(i) : 1.f;
      if (!std::isfinite(weight) || weight < 0 || weight > 1)
        throw std::invalid_argument("Invalid export normal coverage");
      result[i] += (height_values[i] - .5f) * source.strength * weight;
    }
  }
  return result;
}
}  // namespace bts
