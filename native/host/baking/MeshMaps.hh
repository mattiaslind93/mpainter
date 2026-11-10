// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MeshMapStorage.hh"
#include "TextureExtent.hh"
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace bts {
enum class MeshMapRole {
  Normal,
  WorldNormal,
  Id,
  AmbientOcclusion,
  Curvature,
  Position,
  Thickness
};
inline constexpr std::array<const char *, 7> mesh_map_names{"Normal",
                                                            "World space normal",
                                                            "ID",
                                                            "Ambient occlusion",
                                                            "Curvature",
                                                            "Position",
                                                            "Thickness"};
class MeshMapPixels {
 public:
  const int resolution, height;
  const std::string hash;
  const MeshMapStorage rgba;
  MeshMapPixels(int size, std::vector<float> pixels, int rows = 0)
      : resolution(size),
        height(rows ? rows : size),
        hash(checksum(size, height, pixels)),
        rgba(std::move(pixels))
  {
  }
  MeshMapPixels(const MeshMapPixels &) = delete;

 private:
  static std::string checksum(int size, int rows, const std::vector<float> &pixels)
  {
    if (!valid_sparse_texture_extent(size, 32, true) ||
        !valid_sparse_texture_extent(rows, 32, true) ||
        pixels.size() != resource_product(texture_texel_count(size, rows), 4))
      throw std::invalid_argument("Invalid baked map dimensions");
    std::uint64_t hash = 1469598103934665603ULL;
    std::size_t component = 0;
    for (float value : pixels) {
      if (component++ % 4 == 3 && (value < 0 || value > 1))
        throw std::invalid_argument("Invalid baked map coverage");
      if (!std::isfinite(value) || std::abs(value) > 65504)
        throw std::invalid_argument("Invalid baked map data");
      hash ^= std::bit_cast<std::uint32_t>(value);
      hash *= 1099511628211ULL;
    }
    const char *digits = "0123456789abcdef";
    std::string text(16, '0');
    for (int i = 15; i >= 0; --i) {
      text[i] = digits[hash & 15];
      hash >>= 4;
    }
    return text;
  }
};
struct MeshMapPlane {
  int resolution = 512;
  std::string hash;
  std::shared_ptr<const MeshMapPixels> pixels;
  int height = 0;
  int rows() const
  {
    return height ? height : resolution;
  }
};
struct MeshMap {
  std::string name;
  std::map<int, MeshMapPlane> tiles;
};
using MeshMaps = std::array<std::optional<MeshMap>, 7>;
}  // namespace bts
