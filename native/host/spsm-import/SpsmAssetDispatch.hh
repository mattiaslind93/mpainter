// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AssetCatalog.hh"
#include <functional>

namespace bts {
inline bool is_spsm_asset(const StudioAsset &asset)
{
  return asset.filename.endsWith(".spsm", Qt::CaseInsensitive) &&
         asset.usages.contains("smartmaterial");
}
inline bool is_spmsk_asset(const StudioAsset &asset)
{
  return asset.filename.endsWith(".spmsk", Qt::CaseInsensitive) &&
         asset.usages.contains("smartmask");
}
inline bool is_spsm_archive_asset(const StudioAsset &asset)
{
  return is_spsm_asset(asset) || is_spmsk_asset(asset);
}
inline QStringList spsm_policy_tags(const StudioAsset &asset)
{
  QStringList result;
  for (const auto &tag : asset.search_terms)
    if (tag.startsWith("spsm:policy:"))
      result.append(tag);
  result.sort();
  return result;
}
// One availability rule for Shelf buttons, menus and drag/drop. Cold original
// SPSM sources use worker preparation, never the built-in Worn Metal fallback.
inline bool smart_material_target_ready(const StudioAsset &asset,
                                        bool project,
                                        bool mesh_maps_ready)
{
  return asset.smart_material && project &&
         (asset.material_preset || is_spsm_asset(asset) || mesh_maps_ready);
}
// Host-only reader/color management stays outside the pure document library.
// A GUI cache miss classifies metadata and defers hydration; workers validate or throw. A true
// result must provide an immutable native preset retaining the source report.
// Internal host boundary: canonical bytes and digest must already have been
// verified on a worker. Atomically rebind the COW bytes and cached digest so a
// GUI cache hit cannot accidentally rescan a large original archive.
void bind_verified_spsm_source(StudioAsset &, const QByteArray &, const QString &sha256);
using SpsmAssetHydrator = std::function<bool(StudioAsset &)>;
void set_spsm_asset_hydrator(SpsmAssetHydrator);
bool hydrate_spsm_asset(StudioAsset &);
}  // namespace bts
