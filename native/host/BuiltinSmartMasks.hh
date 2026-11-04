#pragma once
#include "AssetCatalog.hh"
#include <cstdint>
namespace bts {
// No asset-based mask presets ship with the release.
const std::vector<StudioAsset> &builtin_smart_mask_assets();
// Compatibility diagnostic: the empty library performs no bundled PNG decodes.
std::uint64_t builtin_smart_mask_rcc_decodes();
}
