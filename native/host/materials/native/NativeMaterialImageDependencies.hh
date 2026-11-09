#pragma once
#include "NativeMaterialAsset.hh"
#include "NativeMaterialBundle.hh"
#include <algorithm>
namespace bts {
// Strict original Reset source closure. Only one original is permitted: a
// variant cannot use another variant (or itself) as Reset. References keep the
// FIRST UUID+SHA rather than silently rebinding to an equivalent catalog alias.
inline const StudioAsset *native_material_original_source(
    const StudioAsset &asset, const std::vector<StudioAsset> &resources)
{
  if (!is_native_material_asset(asset))
    return nullptr;
  const auto lineage = native_material_bundle_provenance(asset.bytes);
  if (!lineage)
    return nullptr;
  const auto original = std::find_if(resources.begin(), resources.end(), [&](const auto &a) {
    return a.id == lineage->original_id;
  });
  if (original == resources.end() || original->id == asset.id ||
      !is_native_material_asset(*original) || original->hash() != lineage->original_sha256 ||
      native_material_bundle_provenance(original->bytes))
    throw std::invalid_argument(
        "Native image variant original Reset resource is missing, changed or chained");
  read_native_material_bundle(original->bytes);
  return &*original;
}
inline void validate_native_material_dependencies(const StudioAsset &asset,
                                                  const std::vector<StudioAsset> &resources)
{
  if (!is_native_material_asset(asset))
    return;
  const auto images = [&](const StudioAsset &source) {
    for (const auto &image : native_material_bundle_image_manifest(source.bytes)) {
      const auto found = std::find_if(resources.begin(), resources.end(), [&](const auto &a) {
        return a.usages.contains("texture") && a.hash() == image.sha256;
      });
      if (found == resources.end())
        throw std::invalid_argument(
            (image.image + ": native material image dependency is missing").toStdString());
      if (image.source == NativeMaterialImageSourceKind::Tiled) {
        auto metadata = found->texture_files;
        for (auto &tile : metadata)
          tile.path.clear();
        if (metadata != image.tiles)
          throw std::invalid_argument(
              (image.image + ": native tiled dependency manifest differs").toStdString());
      }
    }
  };
  images(asset);
  if (const auto *original = native_material_original_source(asset, resources))
    images(*original);
}
}  // namespace bts
