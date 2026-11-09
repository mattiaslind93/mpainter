#pragma once
#include "OpenPbrUsdImport.hh"
#include "AssetCatalog.hh"
#include <atomic>
#include <functional>
namespace bts {
// Worker-only encoded resource preparation. Regular files stay referenced on
// disk; package members are streamed once into the supplied private cache.
// No decode, GPU access, document mutation or aggregate UDIM byte bundle.
StudioAsset prepare_openpbr_usd_resource(
    const OpenPbrUsdImage &,
    const std::set<int> &required_tiles,
    const QString &cache_directory,
    const std::atomic_bool &cancel,
    const std::function<void(int, int, const QString &)> &progress = {});
}  // namespace bts
