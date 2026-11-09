#include "TextureExtent.hh"
#pragma once
#include "UvTransfer.hh"
#include "TilePaintPixels.hh"
#include "TextureMemory.hh"
#include <unordered_map>
#include <vector>
#include <array>
#include <algorithm>
namespace bts {
// CPU reference/import seam. Index the transfer geometry once, then visit only
// target texels reached by occupied source pages, including bilinear neighbors.
// Intermediate storage contains sparse pages, never a full float RGBA image per
// channel/stroke. The native viewport compositor remains GPU-owned.
class SparseStrokeTransfer {
  using Pixel = PaintPixels::Pixel;
  struct Target {
    std::uint32_t grid = 0, pixel = 0;
    bool operator==(const Target &) const = default;
  };
  struct Grid {
    int tile;
    const std::vector<UvTransferSample> *samples;
  };
  const UvTransferMap &mapping_;
  const std::set<int> &allowed_;
  std::vector<Grid> grids_;
  std::unordered_map<std::uint64_t, std::vector<Target>> reverse_;
  static int divide(int value, int size)
  {
    return value >= 0 ? value / size : (value - size + 1) / size;
  }
  static std::uint64_t source_key(int tile, std::uint32_t page)
  {
    return std::uint64_t(tile) << 32 | page;
  }
  Pixel sample(const TilePaintPixels &raster, int channel, double u, double v) const
  {
    const int size = mapping_.resolution;
    const double x = u * size - .5, y = v * size - .5;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const auto at = [&](int x, int y) {
      const int tu = divide(x, size), tv = divide(y, size), tile = 1001 + tu + tv * 10;
      if (tu < 0 || tu > 9 || tv < 0 || !allowed_.contains(tile))
        return Pixel{};
      return raster.sample(channel, x - tu * size, y - tv * size, tile);
    };
    const auto a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    Pixel result;
    for (int k = 0; k < 4; ++k)
      result[k] = std::lerp(std::lerp(a[k], b[k], float(x - ix)),
                            std::lerp(c[k], d[k], float(x - ix)),
                            float(y - iy));
    return result;
  }

 public:
  SparseStrokeTransfer(const UvTransferMap &mapping, const std::set<int> &allowed)
      : mapping_(mapping), allowed_(allowed)
  {
    const int size = mapping.resolution;
    require_sparse_texture_extent(size, "Stroke reprojection", 128, true);
    std::uint64_t cells = 0;
    for (const auto &[tile, values] : mapping.tiles) {
      if (!allowed.contains(tile) || values.size() != std::size_t(size) * size)
        throw std::invalid_argument("Invalid stroke reprojection map");
      cells += values.size();
    }

    for (const auto &[tile, values] : mapping.tiles) {
      const auto grid = std::uint32_t(grids_.size());
      grids_.push_back({tile, &values});
      for (std::uint32_t index = 0; index < values.size(); ++index) {
        const auto &value = values[index];
        if (!value.valid)
          continue;
        const double u = value.source[0], v = value.source[1];
        if (!std::isfinite(u) || !std::isfinite(v) || u < -1 || u > 11 || v < -1 || v > 101)
          continue;
        const int x = int(std::floor(u * size - .5)), y = int(std::floor(v * size - .5));
        std::array<std::uint64_t, 4> keys{};
        int count = 0;
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) {
            const int px = x + dx, py = y + dy, tu = divide(px, size), tv = divide(py, size),
                      source_tile = 1001 + tu + tv * 10;
            if (tu < 0 || tu > 9 || tv < 0 || !allowed.contains(source_tile))
              continue;
            const auto page = std::uint32_t((py - tv * size) / 64) * 65536u +
                              std::uint32_t((px - tu * size) / 64);
            const auto key = source_key(source_tile, page);
            if (std::find(keys.begin(), keys.begin() + count, key) == keys.begin() + count)
              keys[count++] = key;
          }
        for (int k = 0; k < count; ++k)
          reverse_[keys[k]].push_back({grid, index});
      }
    }
  }
  StrokeRaster apply(const StrokeRaster &source) const
  {
    StrokeRaster result;
    const int size = mapping_.resolution;
    for (int field = 0; field < 2; ++field) {
      const auto &input = field ? source.ao : source.paint;
      auto &output = field ? result.ao : result.paint;
      for (int channel = 0; channel < 5; ++channel) {
        std::vector<Target> candidates;
        for (const auto &[tile, pixels] : input.tiles()) {
          const auto versions = pixels.page_versions();
          for (const auto &[key, version] : versions[channel]) {
            const auto found = reverse_.find(source_key(tile, key));
            if (found != reverse_.end())
              candidates.insert(candidates.end(), found->second.begin(), found->second.end());
          }
        }
        const auto order = [](Target a, Target b) {
          return a.grid < b.grid || (a.grid == b.grid && a.pixel < b.pixel);
        };
        std::sort(candidates.begin(), candidates.end(), order);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        using Page = std::array<Pixel, 64 * 64>;
        std::map<std::pair<int, std::uint32_t>, std::unique_ptr<Page>> pages;
        for (const auto &target : candidates) {
          const auto &grid = grids_.at(target.grid);
          const auto &entry = grid.samples->at(target.pixel);
          auto pixel = sample(input, channel, entry.source[0], entry.source[1]);
          if (pixel[3] <= 0)
            continue;
          if (!field && channel == 3) {
            const auto before = pixel;
            for (int c = 0; c < 3; ++c) {
              pixel[c] = 0;
              for (int axis = 0; axis < 3; ++axis)
                pixel[c] += entry.normal[c * 3 + axis] * before[axis];
            }
          }
          const int x = target.pixel % size, y = target.pixel / size;
          const auto key = std::make_pair(grid.tile,
                                          std::uint32_t(y / 64) * 65536u + std::uint32_t(x / 64));
          auto &page = pages[key];
          if (!page)
            page = std::make_unique<Page>();
          (*page)[(y % 64) * 64 + x % 64] = pixel;
        }
        for (const auto &[key, page] : pages)
          output.assign_page(size, channel, key.second, *page, key.first);
      }
    }
    return result;
  }
};
}  // namespace bts
