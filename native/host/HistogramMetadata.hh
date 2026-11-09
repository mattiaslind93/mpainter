#pragma once
#include "LayerEffect.hh"
#include <array>
#include <cstdint>
#include <optional>
namespace bts {
// Small asynchronous UI metadata; never contains authored or composite pixels.
struct HistogramRequest {
  QUuid layer, effect;
  bool mask = false, compare = false;
  int channel = 0, component = 0;
  LevelCurve curve;
};
struct HistogramMetadata {
  std::array<std::array<unsigned, 64>, 2> bins{};
  float minimum = 0, maximum = 1;
  std::uint64_t ticket = 0;
  bool valid = true;
};
}  // namespace bts
