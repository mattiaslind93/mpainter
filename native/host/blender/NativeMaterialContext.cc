// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialContext.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.h"
#include "BLI_hash.h"
#include "BLI_listbase.h"
#include "BLI_math_matrix.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_material_types.h"
#include "DEG_depsgraph_query.hh"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts {
void bind_native_material_object(blender::Main &main,
                                 blender::Depsgraph *depsgraph,
                                 std::uint32_t material,
                                 NativeMaterialGpuContext &context)
{
  using namespace blender;
  Object *match = nullptr;
  for (auto &object : main.objects) {
    if (object.type != OB_MESH || !object.data)
      continue;
    bool uses = false;
    for (int slot = 1; slot <= object.totcol; ++slot)
      if (auto *candidate = BKE_object_material_get(&object, slot);
          candidate && candidate->id.session_uid == material)
        uses = true;
    if (!uses)
      continue;
    if (match)
      throw std::runtime_error(
          "Native material on multiple objects requires per-texel object context (not installed yet)");
    match = &object;
  }
  if (!match)
    throw std::runtime_error("Native material has no project mesh object");
  auto *evaluated = depsgraph ? DEG_get_evaluated(depsgraph, match) : match;
  if (!evaluated)
    throw std::runtime_error("Native material object evaluation is unavailable");
  const auto &matrix = evaluated->object_to_world();
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      context.object_to_world[c * 4 + r] = matrix[c][r];
  if (!invert_m4_m4(reinterpret_cast<float (*)[4]>(context.world_to_object.data()),
                    reinterpret_cast<const float (*)[4]>(context.object_to_world.data())))
    throw std::runtime_error("Native material object transform is singular");
  float location[3], size[3];
  BKE_mesh_texspace_get(reinterpret_cast<Mesh *>(match->data), location, size);
  for (int i = 0; i < 3; ++i) {
    context.bounds_min[i] = location[i] - size[i];
    context.bounds_max[i] = location[i] + size[i];
  }
  std::copy_n(evaluated->color, 4, context.object_color.begin());
  // Same non-instance ObjectRef::random(0) seed as draw/intern/draw_handle.hh.
  context.object_random = BLI_hash_int_2d(BLI_hash_string(evaluated->id.name + 2), 0) *
                          (1.0f / float(0xffffffffu));
}
void bind_native_material_objects(blender::Main &main,
                                  blender::Depsgraph *depsgraph,
                                  std::uint32_t material,
                                  NativeMaterialGpuContext &context,
                                  bool include_texspace)
{
  using namespace blender;
  std::vector<NativeMaterialObject> objects;
  for (auto &object : main.objects) {
    if (object.type != OB_MESH || !object.data)
      continue;
    bool uses = false;
    for (int slot = 1; slot <= object.totcol; ++slot)
      if (auto *candidate = BKE_object_material_get(&object, slot);
          candidate && candidate->id.session_uid == material)
        uses = true;
    if (!uses)
      continue;
    auto *evaluated = depsgraph ? DEG_get_evaluated(depsgraph, &object) : &object;
    if (!evaluated)
      throw std::runtime_error("Native material object evaluation is unavailable");
    NativeMaterialObject entry;
    entry.owner = object.id.session_uid;
    const auto &matrix = evaluated->object_to_world();
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r)
        entry.object_to_world[c * 4 + r] = matrix[c][r];
    if (!invert_m4_m4(reinterpret_cast<float (*)[4]>(entry.world_to_object.data()),
                      reinterpret_cast<const float (*)[4]>(entry.object_to_world.data())))
      throw std::runtime_error("Native material object transform is singular");
    if (include_texspace) {
      float location[3], size[3];
      BKE_mesh_texspace_get(reinterpret_cast<Mesh *>(object.data), location, size);
      for (int i = 0; i < 3; ++i) {
        entry.bounds_min[i] = location[i] - size[i];
        entry.bounds_max[i] = location[i] + size[i];
      }
    }
    std::copy_n(evaluated->color, 4, entry.color.begin());
    entry.random = BLI_hash_int_2d(BLI_hash_string(evaluated->id.name + 2), 0) *
                   (1.0f / float(0xffffffffu));
    objects.push_back(entry);
  }
  if (objects.empty())
    throw std::runtime_error("Native material has no project mesh object");
  context.objects = std::make_shared<const NativeMaterialObjectTable>(std::move(objects));
}
bool native_material_geometry_updated(blender::Main &main, blender::Depsgraph *graph)
{
  using namespace blender;
  if (!graph)
    return false;
  if (DEG_id_type_updated(graph, ID_ME))
    return true;
  if (!DEG_id_type_updated(graph, ID_OB))
    return false;
  for (auto &object : main.objects)
    if (object.type == OB_MESH) {
      const auto *evaluated = DEG_get_evaluated(graph, &object);
      if ((object.id.recalc | (evaluated ? evaluated->id.recalc : 0)) & ID_RECALC_GEOMETRY)
        return true;
    }
  return false;
}
NativeMaterialViewContext capture_native_material_view(const std::array<float, 16> &view,
                                                       const std::array<float, 16> &projection)
{
  const auto reject = [](const char *why) {
    throw NativeFragmentContextError(
        NativeFragmentContextCode::View,
        std::string("Native fragment requires a real validated view: ") + why);
  };
  for (const auto *matrix : {&view, &projection})
    for (float value : *matrix)
      if (!std::isfinite(value))
        reject("matrix is nonfinite");
  if (view[3] != 0 || view[7] != 0 || view[11] != 0 || view[15] != 1)
    reject("view is not affine");
  if (projection[3] != 0 || projection[7] != 0 || (projection[15] != 0 && projection[15] != 1) ||
      (projection[15] == 0 ? projection[11] == 0 : projection[11] != 0))
    reject("projection is malformed");
  float inverse[4][4], inverse_projection[4][4];
  if (!blender::invert_m4_m4(inverse, reinterpret_cast<const float (*)[4]>(view.data())))
    reject("view matrix is singular");
  if (!blender::invert_m4_m4(inverse_projection,
                             reinterpret_cast<const float (*)[4]>(projection.data())))
    reject("projection matrix is singular");
  for (const auto *matrix : {inverse, inverse_projection})
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r)
        if (!std::isfinite(matrix[c][r]))
          reject("inverse matrix is nonfinite");
  NativeMaterialViewContext result;
  for (int i = 0; i < 3; ++i) {
    result.position[i] = inverse[3][i];
    result.forward[i] = inverse[2][i];
  }
  result.position[3] = projection[15] == 0 ? 1.f : 0.f;
  result.valid = true;
  result.validate();
  return result;
}
void bind_native_material_view(const std::array<float, 16> &view,
                               const std::array<float, 16> &projection,
                               NativeMaterialGpuContext &context)
{
  const auto candidate = capture_native_material_view(view, projection);
  // No destination field changes until all matrix/payload admission succeeds.
  for (int i = 0; i < 3; ++i) {
    context.camera_position[i] = candidate.position[i];
    context.camera_forward[i] = candidate.forward[i];
  }
  context.perspective = candidate.position[3] != 0;
  context.view = candidate;
}
}  // namespace bts
