#pragma once
#include "PreviewSourceMeshes.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace blender {
struct Depsgraph;
struct GPUOffScreen;
struct GPUViewport;
struct Main;
struct Scene;
struct View3D;
}

namespace bts {

struct UvPreviewSummary {
  std::shared_ptr<const std::vector<std::array<float,4>>> contours,wireframe,triangulated_wireframe;
  std::size_t mesh_objects = 0;
  std::size_t uv_triangles = 0;
  std::size_t meshes_without_uvs = 0;
  std::size_t meshes_with_invalid_normal_transform = 0;
  std::size_t skipped_nonfinite_triangles = 0;
  std::size_t skipped_degenerate_triangles = 0;
  std::size_t skipped_invalid_normal_triangles = 0;
  std::array<float, 2> minimum_uv{};
  std::array<float, 2> maximum_uv{};

  explicit operator bool() const { return uv_triangles != 0; }
};

// Unlit texture tiles in UV space, with separate island contours. Material mode displays
// the composited Base Color; solo modes display their authored channel/coverage.
// Helper material nodes are private copies, source image IDs are borrowed. All access runs
// on the native owner thread with the Blender GPU context. Free viewport caches before
// rebuilding or clearing this helper and clear it before retiring source IDs.
class UvPreview {
 public:
  UvPreview();
  ~UvPreview();
  UvPreview(const UvPreview &) = delete;
  UvPreview &operator=(const UvPreview &) = delete;

  // Original material UID, evaluated selected faces only. Buffers are checked
  // against available memory. Failed rebuild leaves no partial view.
  UvPreviewSummary rebuild(blender::Main &main,
                           blender::Scene &scene,
                           blender::Depsgraph &depsgraph,
                           const blender::View3D &source_view,
                           std::uint32_t selected_original_material_session_id, bool inspection = false,
                           const SourceMeshMap *source_meshes=nullptr);
  void clear();
  // Keep the previous material pass alive until its replacement has drawn.
  void retire();
  void tag_images_updated();
  // Owner-only view metadata; no GPU work, graph edits or geometry rebuild here.
  void set_material_layering_view(std::uint32_t material_uid,bool enabled,int active_mask);
  bool shader_rebuild_needed()const;

  // Caller activates Blender's GPU context and binds the matching offscreen target.
  // Uses a dedicated caller-owned viewport, never the 3D viewport's engine cache.
  // View the helper XY plane from +Z to avoid backface normal flipping in EEVEE.
  // Returns false without drawing if rebuild found no usable UVs. No CPU pixel readback.
  bool draw(int width,
            int height,
            const float view[4][4],
            const float projection[4][4],
            blender::GPUOffScreen *offscreen,
            blender::GPUViewport *viewport,
            const blender::View3D &source_view, bool color_management = true);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bts
