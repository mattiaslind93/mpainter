#pragma once
#include "PaintPixels.hh"
#include "PreviewSourceMeshes.hh"
#include <optional>
namespace blender {
struct Main;
struct Scene;
struct Depsgraph;
struct View3D;
}  // namespace blender
namespace bts {
class SurfaceBrushCache;
std::shared_ptr<SurfaceBrushCache> make_surface_brush_cache();
void reset_surface_brush_cache(SurfaceBrushCache &);
struct SurfaceBrushSample {
  PaintPixels::Stamp footprint;
  std::array<float, 3> position;
  float radius;
  std::uint64_t triangle_id = 0;
};
// Reuses pre-preview source mesh BVHs for initial contact when a source override exists. Coverage
// follows edge-connected faces of the selected set; disabling culling also admits nearby
// components within the bounded footprint.
std::optional<SurfaceBrushSample> surface_brush_stamp(blender::Main &,
                                                      blender::Scene &,
                                                      blender::Depsgraph &,
                                                      const blender::View3D &,
                                                      std::uint32_t selected_material,
                                                      const std::array<float, 3> &origin,
                                                      const std::array<float, 3> &direction,
                                                      float diameter_percent,
                                                      bool coverage = false,
                                                      bool square = false,
                                                      const BrushProjection &projection = {},
                                                      const SourceMeshMap *source_meshes = nullptr,
                                                      SurfaceBrushCache *cache = nullptr);
}  // namespace bts
