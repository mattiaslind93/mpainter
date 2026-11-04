#pragma once
#include "AssetCatalog.hh"
#include <cstdint>
namespace bts {
// Ten editable, code-only mask generators; no external mask archives.
const std::vector<StudioAsset> &builtin_smart_mask_assets();
// Procedural generators do not decode bundled image inputs.
std::uint64_t builtin_smart_mask_rcc_decodes();
}
