#pragma once
#include "NativeMaterialGpuProgram.hh"
#include <cstdint>
namespace blender {struct Main;struct Depsgraph;}
namespace bts {
// Native owner only. A shared-material/multi-object atlas needs per-texel object
// metadata; until installed it is rejected instead of using the first object.
void bind_native_material_object(blender::Main &,blender::Depsgraph *,std::uint32_t material,NativeMaterialGpuContext &);
// Per-texel owner table for a material shared by multiple project objects.
void bind_native_material_objects(blender::Main &,blender::Depsgraph *,std::uint32_t material,NativeMaterialGpuContext &,bool include_texspace=true);
// Original ID recalc flags, sampled before BKE clears evaluation notifications.
// Reads IDs only, never mesh attributes or geometry snapshots.
bool native_material_geometry_updated(blender::Main &,blender::Depsgraph *);
NativeMaterialViewContext capture_native_material_view(const std::array<float,16> &view,const std::array<float,16> &projection);
void bind_native_material_view(const std::array<float,16> &view,const std::array<float,16> &projection,NativeMaterialGpuContext &);
}
