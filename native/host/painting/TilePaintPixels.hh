// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PaintPixels.hh"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace bts {
// A layer's sparse authoring pages partitioned by UDIM, sharing one stroke/history entry.
class TilePaintPixels {
 public:
  using Bounds = PaintPixels::Bounds;
  using Pixel = PaintPixels::Pixel;
  using Stamp = PaintPixels::Stamp;
  const std::map<int, PaintPixels> &tiles() const
  {
    return tiles_;
  }
  void clear_channel(int channel)
  {
    for (auto it = tiles_.begin(); it != tiles_.end();) {
      it->second.clear_channel(channel);
      if (!it->second.resolution())
        it = tiles_.erase(it);
      else
        ++it;
    }
  }
  const PaintPixels &tile(int number) const
  {
    const auto found = tiles_.find(number);
    static const PaintPixels empty;
    return found == tiles_.end() ? empty : found->second;
  }
  Pixel sample(int channel, int x, int y, int number = 1001) const
  {
    return tile(number).sample(channel, x, y);
  }
  Pixel sample_resampled(
      int channel, int x, int y, int resolution, int number = 1001, int height = 0) const
  {
    return tile(number).sample_resampled(channel, x, y, resolution, height);
  }
  bool empty() const
  {
    for (const auto &[number, pixels] : tiles_)
      if (!pixels.empty())
        return false;
    return true;
  }
  int resolution() const
  {
    return tiles_.empty() ? 0 : tiles_.begin()->second.resolution();
  }
  std::size_t page_count(int channel) const
  {
    std::size_t count = 0;
    for (const auto &[number, pixels] : tiles_)
      count += pixels.page_count(channel);
    return count;
  }
  std::uint64_t hash(int channel) const
  {
    bool extra = false;
    for (const auto &[number, pixels] : tiles_)
      extra |= number != 1001 && pixels.page_count(channel) > 0;
    if (!extra)
      return tile(1001).hash(channel);
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto &[number, pixels] : tiles_)
      if (pixels.page_count(channel) > 0) {
        hash ^= number;
        hash *= 1099511628211ULL;
        hash ^= pixels.hash(channel);
        hash *= 1099511628211ULL;
      }
    return hash;
  }
  void assign_channel(int resolution, int channel, std::span<const float> rgba, int number = 1001)
  {
    validate_tile(number);
    auto candidate = tile(number);
    candidate.assign_channel(resolution, channel, rgba);
    if (!candidate.empty())
      tiles_[number] = std::move(candidate);
    else
      tiles_.erase(number);
  }
  void assign_tile(PaintPixels pixels, int number)
  {
    validate_tile(number);
    if (pixels.resolution())
      tiles_[number] = std::move(pixels);
    else
      tiles_.erase(number);
  }
  void assign_page(
      int resolution, int channel, std::uint32_t key, std::span<const Pixel> pixels, int number)
  {
    validate_tile(number);
    auto candidate = tile(number);
    candidate.assign_page(resolution, channel, key, pixels);
    tiles_[number] = std::move(candidate);
  }
  Bounds stamp(int resolution, const Stamp &stamp, const std::set<int> &numbers = {1001})
  {
    auto candidate = *this;
    const auto bounds = candidate.stamp_working(resolution, stamp, numbers);
    *this = std::move(candidate);
    return bounds;
  }
  // A frame owns its working copy and discards it on failure. Mutate that copy
  // directly so overlapping dabs clone a COW page once per frame/prefix, rather
  // than cloning the same page again for every individual stamp transaction.
  Bounds stamp_working(int resolution, const Stamp &stamp, const std::set<int> &numbers = {1001})
  {
    Bounds total;
    for (int number : numbers) {
      validate_tile(number);
      auto shifted = stamp;
      const int u = (number - 1001) % 10, v = (number - 1001) / 10;
      shifted.center[0] -= u;
      shifted.center[1] -= v;
      shifted.stencil_uv_offset[0] += u;
      shifted.stencil_uv_offset[1] += v;
      if (stamp.surface) {
        auto surface = std::make_shared<PaintPixels::SurfaceFootprint>(*stamp.surface);
        for (auto &triangle : surface->triangles)
          for (auto &uv : triangle.uv) {
            uv[0] -= u;
            uv[1] -= v;
          }
        shifted.surface = std::move(surface);
      }
      auto &pixels = tiles_[number];
      const auto bounds = pixels.stamp(resolution, shifted);
      if (!bounds.empty())
        include(total, bounds);
      else if (pixels.empty())
        tiles_.erase(number);
    }
    return total;
  }
  static TilePaintPixels difference(const TilePaintPixels &before,
                                    const TilePaintPixels &after,
                                    bool erase)
  {
    TilePaintPixels result;
    std::set<int> numbers;
    for (const auto *source : {&before, &after})
      for (const auto &[number, pixels] : source->tiles_)
        numbers.insert(number);
    for (int number : numbers) {
      auto pixels = PaintPixels::difference(before.tile(number), after.tile(number), erase);
      if (!pixels.empty())
        result.tiles_[number] = std::move(pixels);
    }
    return result;
  }
  Bounds mix_stroke(const TilePaintPixels &before,
                    const TilePaintPixels &transported,
                    float opacity,
                    Bounds touched)
  {
    auto candidate = *this;
    std::set<int> numbers;
    const TilePaintPixels *current = this;
    for (const auto *source : {current, &before, &transported})
      for (const auto &[number, pixels] : source->tiles_)
        numbers.insert(number);
    Bounds total;
    for (int number : numbers) {
      auto pixels = candidate.tile(number);
      const auto bounds = pixels.mix_stroke(
          before.tile(number), transported.tile(number), opacity, touched);
      if (!bounds.empty()) {
        if (pixels.empty())
          candidate.tiles_.erase(number);
        else
          candidate.tiles_[number] = std::move(pixels);
        include(total, bounds);
      }
    }
    *this = std::move(candidate);
    return total;
  }
  Bounds over_stroke(const TilePaintPixels &before,
                     const TilePaintPixels &stroke,
                     float opacity,
                     Bounds touched,
                     bool erase = false,
                     bool replace = false,
                     bool complete_stroke = false)
  {
    auto candidate = *this;
    Bounds total;
    std::set<int> numbers;
    for (const auto &[number, pixels] : stroke.tiles_)
      numbers.insert(number);
    if (replace) {
      for (const auto &[number, pixels] : tiles_)
        numbers.insert(number);
      for (const auto &[number, pixels] : before.tiles_)
        numbers.insert(number);
    }
    for (int number : numbers) {
      const auto &pixels = stroke.tile(number);
      auto destination = candidate.tile(number);
      const auto bounds = destination.over_stroke(
          before.tile(number), pixels, opacity, touched, erase, replace, complete_stroke);
      if (!bounds.empty()) {
        if (destination.empty())
          candidate.tiles_.erase(number);
        else
          candidate.tiles_[number] = std::move(destination);
        include(total, bounds);
      }
    }
    *this = std::move(candidate);
    return total;
  }

 private:
  static void validate_tile(int tile)
  {
    if (tile < 1001 || tile > 2000)
      throw std::invalid_argument("Unsupported UDIM number");
  }
  static void include(Bounds &total, Bounds value)
  {
    if (total.empty()) {
      total = value;
      return;
    }
    total.x0 = std::min(total.x0, value.x0);
    total.y0 = std::min(total.y0, value.y0);
    total.x1 = std::max(total.x1, value.x1);
    total.y1 = std::max(total.y1, value.y1);
  }
  std::map<int, PaintPixels> tiles_;
};
}  // namespace bts
