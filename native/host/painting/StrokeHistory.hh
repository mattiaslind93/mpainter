#pragma once
#include "TilePaintPixels.hh"
#include "SurfacePath.hh"
namespace bts {
struct StrokeRaster {
  TilePaintPixels paint, ao;
};
using StrokeHashes = std::array<std::uint64_t, 6>;
inline StrokeHashes stroke_hashes(const StrokeRaster &raster)
{
  StrokeHashes hash{};
  for (int c = 0; c < 5; ++c)
    hash[c] = raster.paint.hash(c);
  hash[5] = raster.ao.hash(2);
  return hash;
}
inline bool same_stroke_storage(const StrokeRaster &a, const StrokeRaster &b)
{
  const auto same = [](const TilePaintPixels &a, const TilePaintPixels &b) {
    if (a.tiles().size() != b.tiles().size())
      return false;
    for (const auto &[tile, pixels] : a.tiles()) {
      const auto found = b.tiles().find(tile);
      if (found == b.tiles().end() || pixels.resolution() != found->second.resolution() ||
          pixels.page_versions() != found->second.page_versions())
        return false;
    }
    return true;
  };
  return same(a.paint, b.paint) && same(a.ao, b.ao);
}
struct StrokeOperation {
  std::shared_ptr<const SurfacePath> path;
  std::shared_ptr<const StrokeRaster> ink;
  bool spatial = false, erase = false;
  float opacity = 1;
  PaintPixels::Bounds bounds;
};
struct StrokeHistory {
  std::shared_ptr<const StrokeRaster> baseline;
  std::vector<std::shared_ptr<const StrokeOperation>> operations;
  StrokeHashes final_hash{};
  std::shared_ptr<const StrokeRaster> final_pixels;
  bool loaded = true;
  int declared_operations = 0, declared_spatial = 0;
  std::map<int, std::shared_ptr<const SurfacePath>> declared_paths;
  StrokeHashes final_hashes() const
  {
    return final_pixels ? stroke_hashes(*final_pixels) : final_hash;
  }
  bool matches(const StrokeRaster &raster) const
  {
    return final_pixels && same_stroke_storage(*final_pixels, raster) ?
               true :
               final_hashes() == stroke_hashes(raster);
  }
};
std::shared_ptr<const StrokeHistory> append_surface_path(
    const std::shared_ptr<const StrokeHistory> &,
    const StrokeRaster &before,
    const StrokeRaster &ink,
    std::shared_ptr<const SurfacePath>,
    int resolution);
bool has_surface_paths(const std::shared_ptr<const StrokeHistory> &);
std::shared_ptr<const StrokeHistory> edit_surface_path(
    const std::shared_ptr<const StrokeHistory> &,
    const std::string &id,
    std::shared_ptr<const SurfacePath>,
    std::shared_ptr<const StrokeRaster> ink,
    int resolution);
std::shared_ptr<const StrokeHistory> append_stroke(
    const std::shared_ptr<const StrokeHistory> &history,
    const StrokeRaster &before,
    const StrokeRaster &after,
    const StrokeRaster &ink,
    bool spatial,
    bool erase,
    float opacity);
PaintPixels::Bounds stroke_bounds(const StrokeRaster &);
StrokeRaster replay_strokes(const StrokeHistory &, int resolution);
}  // namespace bts
