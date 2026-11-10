// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LayerMask.hh"
#include <bit>
#include <map>
#include <span>
namespace bts {
// GPU readback/CPU-oracle output, processed only by the bake worker. Keep a
// common constant background so a small stroke does not become a dense 4K mask.
inline void assign_flattened_mask(
    LayerMask &mask, int canvas, int tile, std::span<float> rgba, bool &background_chosen)
{
  if (rgba.size() != std::size_t(canvas) * canvas * 4)
    throw std::invalid_argument("Invalid flattened mask canvas");
  if (!background_chosen) {
    std::map<std::uint32_t, int> counts;
    for (int y = canvas > 32 ? 32 : 0; y < canvas; y += 64)
      for (int x = canvas > 32 ? 32 : 0; x < canvas; x += 64)
        ++counts[std::bit_cast<std::uint32_t>(rgba[(std::size_t(y) * canvas + x) * 4])];
    const auto common = std::max_element(
        counts.begin(), counts.end(), [](const auto &a, const auto &b) {
          return a.second < b.second;
        });
    mask.background = common == counts.end() ? 0 : std::bit_cast<float>(common->first);
    background_chosen = true;
  }
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    const float value = rgba[i];
    if (!std::isfinite(value))
      throw std::invalid_argument("Invalid captured mask value");
    rgba[i] = value == mask.background ? 0 : value;
    rgba[i + 1] = rgba[i + 2] = 0;
    rgba[i + 3] = value == mask.background ? 0 : 1;
  }
  mask.pixels.assign_channel(canvas, 2, rgba, tile);
}
}  // namespace bts
