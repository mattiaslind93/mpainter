#pragma once
#include "AssetCatalog.hh"

namespace bts {
// Immutable encoded originals, shared between catalogs. Decoding is deferred
// to the existing asynchronous texture/thumbnail loaders.
const std::vector<StudioAsset> &builtin_procedural_assets();
}
