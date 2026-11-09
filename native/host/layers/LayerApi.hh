// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "AssetCatalog.hh"
#include <functional>
namespace bts {
struct LayerApiResources {
  std::function<StudioAsset(const QUuid &, bool pack)> asset;
  std::function<std::shared_ptr<const FillTexture>(const StudioAsset &, bool raw)> texture;
};
QJsonObject layer_api_reference(const TextureDocument &,
                                const TextureSet &,
                                const TextureLayer &,
                                const QString &scope = "layer",
                                QUuid node = {});
QUuid layer_api_set_id(const TextureDocument &, const QJsonObject &parameters);
bool layer_api_read_only(const QString &operation);
QJsonValue layer_api_query(const TextureDocument &,
                           const QString &,
                           const QJsonObject &,
                           const LayerApiResources & = {});
QJsonValue layer_api_edit(TextureDocument &,
                          const QString &,
                          const QJsonObject &,
                          const LayerApiResources & = {});
}  // namespace bts
