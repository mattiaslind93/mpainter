#pragma once
#include "GpuStackPlan.hh"
#include "GpuMarginPlan.hh"
#include <array>
#include <map>
#include <set>
#include <string>
namespace bts {
// CPU tracks revisions and rectangles only; all pixel evaluation and global
// coverage decisions remain on GPU.
struct GpuDerivedDirty {
  using Chunks = std::map<int, std::set<int>>;
  using Sizes = std::map<int, std::array<int, 2>>;
  std::string parameters;
  std::map<std::pair<int, int>, std::uint64_t> seen;
  static void full(std::set<int> &dirty, int width, int height)
  {
    for (int i = 0; i < ((width + 255) / 256) * ((height + 255) / 256); ++i)
      dirty.insert(i);
  }
  static void add(std::set<int> &dirty, int width, int height, int x0, int y0, int x1, int y1)
  {
    x0 = std::clamp(x0, 0, width);
    x1 = std::clamp(x1, 0, width);
    y0 = std::clamp(y0, 0, height);
    y1 = std::clamp(y1, 0, height);
    if (x0 >= x1 || y0 >= y1)
      return;
    const int cols = (width + 255) / 256;
    for (int y = y0 / 256; y <= (y1 - 1) / 256; ++y)
      for (int x = x0 / 256; x <= (x1 - 1) / 256; ++x)
        dirty.insert(y * cols + x);
  }
  static std::set<int> padded(std::set<int> dirty, const GpuMarginPlan *plan)
  {
    if (plan) {
      const auto sources = dirty;
      for (int source : sources)
        for (auto destination : plan->affected_chunks.at(source))
          dirty.insert(int(destination));
    }
    return dirty;
  }
  static void normal_halo(Chunks &out, int tile, const std::set<int> &dirty, const Sizes &sizes)
  {
    const auto size = sizes.at(tile);
    const int cols = (size[0] + 255) / 256, tu = (tile - 1001) % 10, tv = (tile - 1001) / 10;
    for (auto chunk : dirty) {
      const int x = (chunk % cols) * 256, y = (chunk / cols) * 256,
                right = std::min(x + 256, size[0]), top = std::min(y + 256, size[1]);
      // Project source chunk to each adjacent tile in UV space. Two destination
      // texels cover bilinear interpolation plus Sobel's one-texel derivative.
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          if (tu + dx < 0 || tu + dx >= 10 || tv + dy < 0 || tv + dy >= 100)
            continue;
          const int other = tile + dx + 10 * dy;
          const auto found = sizes.find(other);
          if (found == sizes.end())
            continue;
          const auto n = found->second;
          const int left = int(std::floor((double(x) / size[0] - dx) * n[0])) - 2;
          const int bottom = int(std::floor((double(y) / size[1] - dy) * n[1])) - 2;
          const int last = int(std::ceil((double(right) / size[0] - dx) * n[0])) + 2;
          const int end = int(std::ceil((double(top) / size[1] - dy) * n[1])) + 2;
          add(out[other], n[0], n[1], left, bottom, last, end);
        }
    }
  }
  std::set<int> changed(int channel,
                        int tile,
                        std::uint64_t revision,
                        const std::set<int> &last,
                        int width,
                        int height,
                        bool force)
  {
    const auto key = std::pair{channel, tile};
    const auto found = seen.find(key);
    std::set<int> result;
    if (force || found == seen.end() || revision > found->second + 1)
      full(result, width, height);
    else if (revision != found->second)
      result = last;
    seen[key] = revision;
    return result;
  }
};
}  // namespace bts
