// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SurfaceGraphSnapshot.hh"
#include "DNA_node_types.h"
#include <cmath>
namespace bts::pd149_surface_test {
struct SurfaceGraphExpected {
  std::uint32_t material_uid = 0;
  std::uint64_t target_generation = 0;
  SurfaceGraphState original, evaluated;
  std::vector<SurfaceGraphSlot> visible_slots;
};
struct SurfaceGraphPaint {
  int phase = 0, phase_before = 0, phase_after = 0;
  std::uint64_t revision = 0, revision_before = 0, revision_after = 0;
  std::uint64_t phase_draw_floor = 0, draw_before = 0, frames_before = 0, frames_after = 0;
  bool actual_eevee_route = false;
};
inline bool valid_surface_graph(const SurfaceGraphState &s)
{
  using blender::SOCK_FLOAT;
  using blender::SOCK_RGBA;
  using blender::SOCK_VECTOR;
  using blender::SOCK_SHADER;
  if (!s.valid || s.output.type != "ShaderNodeOutputMaterial" ||
      s.principled.type != "ShaderNodeBsdfPrincipled" || s.output.muted || s.principled.muted ||
      s.route.empty() || s.inputs.size() != 4)
    return false;
  const auto valid_socket = [](const SurfaceGraphSocket &v) {
    if (!v.available || v.identifier.empty() || v.components < 0 || v.components > 4 ||
        v.incoming.size() > 1)
      return false;
    const int components = v.type == SOCK_FLOAT  ? 1 :
                           v.type == SOCK_RGBA   ? 4 :
                           v.type == SOCK_VECTOR ? 3 :
                           v.type == SOCK_SHADER ? 0 :
                                                   -1;
    if (components < 0 || v.components != components)
      return false;
    for (double x : v.value)
      if (!std::isfinite(x))
        return false;
    for (const auto &l : v.incoming)
      if (!l.valid || l.muted || !l.available || l.from.muted || l.to.muted ||
          l.from.type.empty() || l.to.type.empty() || l.input != v.identifier || l.output.empty())
        return false;
    return true;
  };
  if (s.route.front().identifier != "Surface" || s.route.front().incoming.size() != 1)
    return false;
  for (const auto &v : s.route)
    if (!valid_socket(v) || v.type != (v.identifier == "Fac" ? SOCK_FLOAT : SOCK_SHADER))
      return false;
  const char *names[]{"Base Color", "Metallic", "Roughness", "Normal"};
  const int components[]{4, 1, 1, 3}, types[]{SOCK_RGBA, SOCK_FLOAT, SOCK_FLOAT, SOCK_VECTOR};
  for (int i = 0; i < 4; ++i)
    if (s.inputs[i].identifier != names[i] || s.inputs[i].components != components[i] ||
        s.inputs[i].type != types[i] || !valid_socket(s.inputs[i]))
      return false;
  return true;
}
// Expected values are supplied independently for each actor/reference phase.
// This is graph+actual Paint identity only: caller must separately require
// witnessed sampling progression/terminal and retained framebuffer evidence.
inline bool surface_graph_matches(const SurfaceGraphSnapshot &s,
                                  const SurfaceGraphExpected &e,
                                  const SurfaceGraphPaint &p)
{
  if (s.status != SurfaceGraphStatus::Captured || !e.material_uid ||
      s.selected_material_uid != e.material_uid || !s.distinct_evaluated ||
      !s.original_backlink_matches || s.material_override || !s.distinct_evaluated_tree ||
      !s.tree_original_backlink_matches || !e.target_generation ||
      s.target_generation != e.target_generation)
    return false;
  if (p.phase < 1 || p.phase_before != p.phase || p.phase_after != p.phase || !p.revision ||
      p.revision_before != p.revision || p.revision_after != p.revision || !p.actual_eevee_route ||
      p.frames_after <= p.frames_before || !s.host_draw || s.host_draw <= p.phase_draw_floor ||
      s.host_draw <= p.draw_before)
    return false;
  if (!valid_surface_graph(s.original) || !valid_surface_graph(s.evaluated) ||
      s.original != e.original || s.evaluated != e.evaluated || s.visible_slots != e.visible_slots)
    return false;
  bool selected = false;
  for (const auto &v : s.visible_slots) {
    if (!v.object_uid || v.slot < 1 || v.faces < 1 || !v.evaluated_object)
      return false;
    if (v.selected_binding) {
      if (!v.evaluated_material || v.material_original_uid != e.material_uid)
        return false;
      selected = true;
    }
  }
  return selected;
}
}  // namespace bts::pd149_surface_test
