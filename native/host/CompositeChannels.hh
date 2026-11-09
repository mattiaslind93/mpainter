#pragma once
#include <array>
namespace bts {
using CompositeChannels = std::array<bool, 17>;
inline constexpr CompositeChannels complete_composite_channels = [] {
  CompositeChannels value{};
  value.fill(true);
  value[15] = false;
  return value;
}();
}  // namespace bts
