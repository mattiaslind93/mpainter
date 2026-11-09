// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SmartMaterialPreset.hh"
#include "AssetCatalog.hh"
#include "MaskPresetTextures.hh"
#include "SpsmAssetDispatch.hh"
#include <QCryptographicHash>
namespace bts {
inline const QString mask_source_tag = QStringLiteral("internal:smart-mask-textures");
// Applied masks own their effect stacks and paint already. Persistence needs
// only one copy of each embedded texture, not every complete preset archive.
// This runs on the detached project writer, never in an interactive edit.
inline std::vector<StudioAsset> canonical_mask_resource_banks(
    const std::vector<StudioAsset> &assets)
{
  std::vector<StudioAsset> result;
  std::map<std::string, std::shared_ptr<const FillTexture>> unique;
  for (const auto &asset : assets) {
    if (is_spsm_archive_asset(asset) || !asset.search_terms.contains(mask_source_tag)) {
      result.push_back(asset);
      continue;
    }
    LayerMask source;
    if (asset.smart_mask)
      source = *asset.smart_mask;
    else if (asset.material_preset) {
      for (const auto &layer : asset.material_preset->set.layers) {
        for (int channel = 0; channel < 15; ++channel)
          if (const auto &texture = fill_channel_texture(layer.fill, channel)) {
            MaskEffect effect;
            effect.mapping.projection.texture = texture;
            source.effects.push_back(std::move(effect));
          }
        for (const auto &content : layer.effects)
          if (content.bitmap_fill) {
            MaskEffect effect;
            effect.kind = MaskEffectKind::Fill;
            effect.mapping = *content.bitmap_fill;
            source.effects.push_back(std::move(effect));
          }
        if (layer.mask)
          source.effects.insert(
              source.effects.end(), layer.mask->effects.begin(), layer.mask->effects.end());
      }
    }
    else
      throw std::invalid_argument("Embedded preset source has not been hydrated");
    for (const auto &effect : mask_effects_recursive(source.effects))
      if (const auto &texture = effect.mapping.projection.texture) {
        // Same source/interpretation may have different per-use names and UUIDs.
        std::string key = texture->hash + ":" + std::to_string(texture->raw) + ":" +
                          texture->source_space + ":" + std::to_string(texture->width) + ":" +
                          std::to_string(texture->height);
        if (texture->tiles)
          for (const auto &[tile, member] : *texture->tiles)
            key += ":" + std::to_string(tile) + ":" + member->hash;
        unique.try_emplace(std::move(key), texture);
      }
  }
  const auto identity = [](const QByteArray &bytes) {
    return QUuid::fromRfc4122(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).left(16));
  };
  auto it = unique.begin();
  while (it != unique.end()) {
    LayerMask bank;
    bank.background = 0;
    QByteArray keys;
    for (int count = 0; count < 64 && it != unique.end(); ++count, ++it) {
      MaskEffect effect;
      effect.id = identity(QByteArray::fromStdString(it->first));
      effect.kind = MaskEffectKind::Fill;
      effect.mapping.projection.texture = it->second;
      bank.effects.push_back(std::move(effect));
      keys += QByteArray::fromStdString(it->first);
      keys += '\n';
    }
    const auto id = identity(keys);
    const auto retained = std::find_if(assets.begin(), assets.end(), [&](const auto &asset) {
      return asset.id == id && asset.search_terms.contains(mask_source_tag);
    });
    if (retained != assets.end()) {
      result.push_back(*retained);
      continue;
    }
    StudioAsset asset;
    asset.id = id;
    asset.name = "Smart-mask texture sources";
    asset.filename = "mask-textures.btsmask";
    asset.scope = AssetScope::Project;
    asset.usages = {"smartmask"};
    asset.search_terms = {mask_source_tag};
    asset.smart_mask = std::make_shared<const LayerMask>(std::move(bank));
    asset.bytes = write_mask_preset(*asset.smart_mask);
    result.push_back(std::move(asset));
  }
  return result;
}
}  // namespace bts
