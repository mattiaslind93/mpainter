// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SceneCamera.hh"

#include "MaterialValues.hh"
#include "PreviewSourceMeshes.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace blender {
struct bContext;
struct bNode;
struct Main;
struct Material;
struct Scene;
struct View3D;
}  // namespace blender

namespace bts {

struct MaterialSummary {
  // Native session identity only; never serialize this as a document UUID.
  std::uint32_t session_id = 0;
  std::string name;
  std::array<float, 4> display_base_color{};
  MaterialValues values;
  bool uniform_editable = false;
  float roughness = 0.5f;
  bool constant_base_color = false;
  bool constant_roughness = false;
  bool roughness_editable = false;
  std::string roughness_unavailable_reason;
};

struct UvFaceTriangle {
  std::array<std::array<float, 2>, 3> corners{};
  std::size_t material = 0;
  std::array<std::array<float, 3>, 3> positions{};
  std::array<std::array<float, 3>, 3> normals{};
  bool has_normals = false;
  std::array<std::uint64_t, 3> vertex_ids{};
  // Bit i marks a real polygon boundary from corner i to (i + 1) % 3.
  unsigned char boundary_edges = 0;
  std::uint64_t triangle_id = 0, polygon_id = 0;
};

struct SceneSnapshot {
  std::vector<SceneCamera> cameras;
  std::map<std::uint32_t, std::string> object_names;
  std::map<std::uint32_t, int> object_vertex_counts;
  std::vector<MaterialSummary> materials;
  std::vector<UvFaceTriangle> uv_triangles;
  std::array<float, 3> minimum{}, maximum{};
  std::size_t mesh_objects = 0;
  std::size_t meshes_without_uvs = 0;
  bool has_bounds = false;
  // Runtime preview density for displacement-aware baking, not source geometry.
  std::map<std::uint32_t, int> preview_subdivision_levels;
  bool evaluated_subdivision = false;
};

// Native owner-thread helpers share the same supported shader/input checks for read and write.
MaterialSummary read_material_summary(const blender::Main &main,
                                      const blender::Material *material,
                                      const blender::Scene &scene);
void write_material_roughness(blender::Main &main, blender::Material &material, float roughness);

blender::bNode *native_principled_surface(blender::Material &);
blender::bNode *native_material_output(blender::Material &);
void write_material_values(blender::Main &, blender::Material &, const MaterialValues &);

// Copy evaluated mesh-object data on the native owner thread. No native pointer escapes.
// Collection/geometry-node instances are not expanded by this initial mesh-object reader.
SceneSnapshot read_scene_snapshot(blender::bContext *context,
                                  const blender::View3D *view,
                                  bool include_unmapped = false,
                                  const SourceMeshMap *source_meshes = nullptr);

}  // namespace bts
