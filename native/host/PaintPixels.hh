#pragma once
#include "MaterialValues.hh"
#include "TextureExtent.hh"
#include "BrushAlpha.hh"
#include "BrushProjection.hh"
#include "BrushStencil.hh"
#include "ProjectionMaterial.hh"
#include "SmudgePayload.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>
namespace bts {
struct QuickMaskField;
struct PaintRecipe;
struct PaintRasterSource;
// Authoring storage is independent of native material images. Copies share immutable pages
// until touched, so document/history snapshots preserve every channel in a single transaction.
class PaintPixels {
 public:
  using Pixel = std::array<float, 4>;  // Premultiplied RGB (scalar in R), authored coverage in A.
  static constexpr int page_side = sparse_texture_page_side;
  struct SurfaceTriangle {
    std::array<std::array<double, 2>, 3> uv;
    std::array<std::array<double, 3>, 3> position;
    std::array<std::array<double, 2>, 3> paint_coordinates{};
    std::array<double, 3> normal{0, 0, 1};
    bool bounded_depth = false;
    std::array<bool, 3>
        seam_edges{};  // Pad UV seams/boundaries, never internal triangulation edges.
  };
  struct SurfaceFootprint {
    std::array<double, 3> center;
    double radius;
    std::array<double, 3> tangent{1, 0, 0}, bitangent{0, 1, 0}, normal{0, 0, 1},
        view_direction{0, 0, -1};
    bool projected = false, unwrapped = false, planar_fade = false, cull = false;
    float cull_angle = 90;
    std::vector<SurfaceTriangle> triangles;
  };
  struct Stamp {
    std::array<double, 2> center;
    std::array<double, 2> axis_x, axis_y;  // Unit circle -> UV ellipse, including orientation.
    MaterialValues values;
    std::array<bool, 5> channels{true, true, true, true, true};
    float flow = 1;
    float hardness = 0;
    std::shared_ptr<const SurfaceFootprint> surface;
    BrushAlpha alpha;
    std::shared_ptr<const StencilMapping> stencil;
    std::shared_ptr<const ProjectionMapping> projected;
    std::shared_ptr<const SmudgePayload> smudge, cloned;
    std::shared_ptr<const QuickMaskField> coverage_mask;
    std::array<double, 2> stencil_uv_offset{};
  };
  struct Bounds {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const
    {
      return x0 >= x1 || y0 >= y1;
    }
  };
  // Single-tile authoring seam. UV outside 1001 is clipped, never wrapped into another tile.
  Bounds stamp(int resolution, const Stamp &);
  Pixel sample(int channel, int x, int y) const;
  // Evaluate retained authoring pixels at another size without replacing them.
  // Downsampling averages aligned source texels; upsampling is bilinear in
  // associated RGB/coverage space, extending boundary texels within each tile.
  Pixel sample_resampled(
      int channel, int x, int y, int output_resolution, int output_height = 0) const;
  // Compose one accumulated stroke over its immutable starting pixels, once at stroke opacity.
  // Re-evaluating a touched rectangle does not accumulate opacity a second time.
  static PaintPixels difference(const PaintPixels &before, const PaintPixels &after, bool erase);
  Bounds mix_stroke(const PaintPixels &before,
                    const PaintPixels &transported,
                    float opacity,
                    Bounds touched);
  Bounds over_stroke(const PaintPixels &before,
                     const PaintPixels &stroke,
                     float opacity,
                     Bounds touched,
                     bool erase = false,
                     bool replace = false,
                     bool complete_stroke = false);
  // Import our associated RGB/coverage representation transactionally; zero coverage stays sparse.
  void assign_channel(int resolution, int channel, std::span<const float> rgba);
  // Drop one authored channel without evaluating retained GPU stroke recipes.
  // Other channels keep their page identities and all Undo snapshots stay valid.
  void clear_channel(int channel);
  void visit_pages(const std::function<void(int, std::uint32_t, std::span<const Pixel>)> &) const;
  // O(page count) GPU cache keys; revisions never enter file checksums or undo metadata.
  void visit_versioned_pages(
      int channel,
      const std::function<void(std::uint32_t, std::uint64_t, std::span<const Pixel>)> &) const;
  // Metadata-only visitor, including deferred recipes. Never reads raster data.
  void visit_page_versions(int channel,
                           const std::function<void(std::uint32_t, std::uint64_t)> &) const;
  void assign_page(int resolution, int channel, std::uint32_t key, std::span<const Pixel>);
  std::size_t page_count(int channel) const;
  std::uint64_t hash(int channel) const;
  int resolution() const
  {
    return resolution_;
  }
  bool empty() const;
  static std::size_t resident_bytes();
  static std::uint64_t hash_bytes();
  using PageVersions = std::array<std::map<std::uint32_t, std::uint64_t>, 5>;
  // The supplied page keys conservatively bound all possible source output.
  // Versions are assigned here, independently of the producer's metadata.
  static PaintPixels from_source(int resolution,
                                 std::shared_ptr<const PaintRasterSource>,
                                 const PageVersions &declared_pages);
  PageVersions page_versions() const;
  std::uint64_t page_version(int channel, std::uint32_t key) const;
  std::span<const Pixel> page_data(int channel, std::uint32_t key) const;
  const std::shared_ptr<const PaintRecipe> &recipe() const
  {
    return recipe_;
  }
  PaintPixels materialized() const;
  static std::uint64_t recipe_materializations();

 private:
  friend struct PaintRecipe;
  std::shared_ptr<const PaintRecipe> recipe_;
  struct Page;
  using Pages = std::map<std::uint32_t, std::shared_ptr<Page>>;
  std::array<Pages, 5> channels_;
  int resolution_ = 0;
  mutable std::array<std::optional<std::uint64_t>, 5> hash_cache_;
  Pixel &writable(int channel, int x, int y);
  void prune_empty(Bounds);
  Bounds stamp_surface(int resolution, const Stamp &);
};
}  // namespace bts
