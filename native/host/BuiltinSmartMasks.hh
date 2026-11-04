#pragma once
#include "AssetCatalog.hh"
#include <cstdint>
namespace bts {
// Ten editable native mask recipes with CC0 grunge inputs; no Adobe archives.
const std::vector<StudioAsset> &builtin_smart_mask_assets();
// Number of shared CC0 inputs decoded during initial catalog construction.
std::uint64_t builtin_smart_mask_rcc_decodes();
}
