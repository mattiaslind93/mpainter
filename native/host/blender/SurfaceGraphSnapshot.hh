// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Test diagnostic values only. No native identity is encoded as an address.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace blender {
struct Main;
struct Depsgraph;
struct ViewLayer;
struct View3D;
}  // namespace blender
namespace bts {
struct SurfaceGraphNode {
  int identifier = 0;
  std::string name, type;
  bool muted = false;
  bool operator==(const SurfaceGraphNode &) const = default;
};
struct SurfaceGraphLink {
  SurfaceGraphNode from, to;
  std::string output, input;
  bool valid = false, muted = false, available = false;
  bool operator==(const SurfaceGraphLink &) const = default;
};
struct SurfaceGraphSocket {
  std::string identifier;
  int type = 0, components = 0;
  std::array<double, 4> value{};
  bool available = false;
  std::vector<SurfaceGraphLink> incoming;
  bool operator==(const SurfaceGraphSocket &) const = default;
};
struct SurfaceGraphState {
  bool valid = false;
  SurfaceGraphNode output, principled;
  // Output Surface then each wrapper's Fac / first / second shader sockets.
  std::vector<SurfaceGraphSocket> route;
  // Base Color, Metallic, Roughness, Normal, in that exact order.
  std::vector<SurfaceGraphSocket> inputs;
  bool operator==(const SurfaceGraphState &) const = default;
};
struct SurfaceGraphSlot {
  std::uint32_t object_uid = 0, material_original_uid = 0;
  int slot = 0, faces = 0;
  bool evaluated_object = false, evaluated_material = false, selected_binding = false;
  bool operator==(const SurfaceGraphSlot &) const = default;
};
enum class SurfaceGraphStatus {
  Disabled,
  NoSceneDraw,
  DrawFailed,
  NoSelection,
  NoEvaluated,
  MaterialOverride,
  UnsupportedGraph,
  NoVisibleSelectedSlot,
  Captured,
  Failed
};
struct SurfaceGraphSnapshot {
  SurfaceGraphStatus status = SurfaceGraphStatus::Disabled;
  std::uint64_t host_draw = 0, target_generation = 0;
  std::uint32_t selected_material_uid = 0;
  bool distinct_evaluated = false, original_backlink_matches = false, material_override = false;
  bool distinct_evaluated_tree = false, tree_original_backlink_matches = false;
  SurfaceGraphState original, evaluated;
  std::vector<SurfaceGraphSlot> visible_slots;
};
namespace detail {
// Called only by the owner immediately after its actual EEVEE Scene draw.
// No ensure/update, native/GPU mutation or pointer retained beyond this call.
SurfaceGraphSnapshot capture_surface_graph(const blender::Main &,
                                           const blender::Depsgraph &,
                                           blender::ViewLayer &,
                                           const blender::View3D &,
                                           std::uint32_t) noexcept;
}  // namespace detail
}  // namespace bts
