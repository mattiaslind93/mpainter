// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>
namespace blender {
struct Main;
struct Material;
}  // namespace blender
namespace bts {
struct ExportRequest;
// Color, Metallic, Roughness, complete Occlusion. No normal map.
struct UsdTextureSetExport {
  std::uint32_t material_uid = 0;
  std::array<std::string, 4> files;
  std::set<int> tiles;
  bool scene_linear_color = false;
};
std::vector<UsdTextureSetExport> usd_texture_exports(const ExportRequest &);
void attach_native_usd_textures(blender::Main &, blender::Material &, const UsdTextureSetExport &);
}  // namespace bts
