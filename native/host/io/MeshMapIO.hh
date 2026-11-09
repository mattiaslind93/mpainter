// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace blender {
struct Main;
struct Scene;
}  // namespace blender
namespace bts {
class TextureDocument;
void pack_mesh_maps(blender::Main &, blender::Scene &, const TextureDocument &);
void read_mesh_maps(const blender::Scene &, TextureDocument &);
}  // namespace bts
