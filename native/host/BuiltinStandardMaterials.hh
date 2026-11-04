#pragma once
#include "AssetCatalog.hh"
#include "TextureDocument.hh"
#include <vector>

namespace bts {
const std::vector<StudioAsset> &builtin_standard_material_assets();
const std::vector<StudioAsset> &builtin_standard_microtexture_assets();
const std::vector<StudioAsset> &builtin_standard_smart_material_assets();
void resolve_user_material_textures(std::vector<StudioAsset> &assets);

struct FolderMaterialBundle {
  std::vector<StudioAsset> textures;
  StudioAsset material;
};
FolderMaterialBundle build_folder_material_bundle(const TextureSet &folder_set, const QString &name);
} // namespace bts
