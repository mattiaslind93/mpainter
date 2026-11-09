#pragma once
#include "TextureDocument.hh"
#include "CompositeChannels.hh"
#include <algorithm>
namespace bts {
inline CompositeChannels composite_dependencies(const TextureSet &set,
                                                bool ao,
                                                CompositeChannels needed)
{
  needed[15] = needed[15] && set.selected_mask && !set.selected_layer.isNull();
  for (int c = 7; c < 15; ++c)
    needed[c] = needed[c] && set.users[c - 7].present;
  needed[5] = needed[5] && ao;
  needed[16] = needed[16] && set.displacement.allocated;
  if (needed[3])
    needed[4] = true;
  if (needed[6])
    needed[3] = needed[4] = true;
  return needed;
}
inline std::uint64_t composite_peak_bytes(const TextureSet &set,
                                          bool ao,
                                          const CompositeChannels &changed)
{
  const auto needed = composite_dependencies(set, ao, changed);
  std::uint64_t largest = 0;
  for (int tile : set.tiles)
    largest = std::max(largest, std::uint64_t(set.width(tile)) * set.rows(tile));
  // Retained output planes, a padding ImBuf plus decoder/copy scratch, and
  // byte coverage masks. Existing source/document storage is already excluded
  // by the OS available-memory query.
  return set.evaluation_texels() * (16 * std::count(needed.begin(), needed.end(), true) + 1) +
         largest * 32;
}
struct CompositeBatch {
  CompositeChannels channels{}, remaining{};
};
inline CompositeBatch composite_batch(const TextureSet &set,
                                      bool ao,
                                      const CompositeChannels &requested,
                                      std::uint64_t budget)
{
  if (composite_peak_bytes(set, ao, requested) <= budget)
    return {requested, {}};
  CompositeBatch result;
  result.remaining = requested;
  for (int c = 0; c < 17; ++c)
    if (requested[c]) {
      auto candidate = result.channels;
      candidate[c] = true;
      if (composite_peak_bytes(set, ao, candidate) <= budget) {
        result.channels = candidate;
        result.remaining[c] = false;
      }
    }
  // An unserviceable single channel fails on its worker with a clear memory
  // error. Always consume one item so failures cannot become a retry loop.
  if (std::none_of(result.channels.begin(), result.channels.end(), [](bool b) { return b; }))
    for (int c = 0; c < 17; ++c)
      if (requested[c]) {
        result.channels[c] = true;
        result.remaining[c] = false;
        break;
      }
  return result;
}
}  // namespace bts
