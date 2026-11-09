// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <QCryptographicHash>
#include <QJsonArray>
#include <map>
#include <set>
namespace bts::smart_mask_proof {
inline QString pixel_sha256(const FillTexture &texture)
{
  texture.validate();
  if (!texture.pixels)
    throw std::runtime_error("Packed smart-mask texture is unresolved");
  const auto bytes = resource_product(texture.pixels->size(), sizeof(FillTexture::Pixel));
  if (bytes > std::uint64_t(std::numeric_limits<qsizetype>::max()))
    throw std::length_error("Smart-mask pixel witness exceeds addressable view");
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QByteArrayView(reinterpret_cast<const char *>(texture.pixels->data()), qsizetype(bytes)),
          QCryptographicHash::Sha256)
          .toHex());
}
// Diagnostic metadata only. Call after real UI apply/edit/Save; this never
// supplies source pixels or recreates a missing packed dependency.
inline QJsonObject witness(const TextureDocument &document, const std::vector<QUuid> &targets)
{
  QJsonArray layers;
  std::set<std::string> sources;
  std::set<QUuid> remaining(targets.begin(), targets.end());
  int fills = 0, folders = 0;
  for (const auto &set : document.texture_sets())
    for (const auto &layer : set.layers)
      if (remaining.erase(layer.id)) {
        if (!layer.mask)
          throw std::runtime_error("Smart-mask witness target has no mask");
        if (layer.kind == LayerKind::Fill)
          ++fills;
        else if (layer.kind == LayerKind::Folder)
          ++folders;
        else
          throw std::runtime_error("Smart-mask witness target is neither Fill nor Folder");
        QJsonArray textures;
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          if (effect.kind == MaskEffectKind::Fill && effect.mapping.projection.texture) {
            const auto &texture = *effect.mapping.projection.texture;
            if (effect.mapping.projection.mode != 1)
              throw std::runtime_error("Smart-mask witness requires true triplanar Fill");
            sources.insert(texture.hash);
            textures.append(QJsonObject{{"effect", effect.id.toString()},
                                        {"asset", QString::fromStdString(texture.asset)},
                                        {"source_hash", QString::fromStdString(texture.hash)},
                                        {"pixel_sha256", pixel_sha256(texture)},
                                        {"width", texture.width},
                                        {"height", texture.height}});
          }
        if (textures.isEmpty())
          throw std::runtime_error("Smart-mask witness has no bitmap dependency");
        layers.append(QJsonObject{{"set", set.id.toString()},
                                  {"layer", layer.id.toString()},
                                  {"name", QString::fromStdString(layer.name)},
                                  {"kind", layer.kind == LayerKind::Folder ? "folder" : "fill"},
                                  {"background", layer.mask->background},
                                  {"enabled", layer.mask->enabled},
                                  {"mask_effects", mask_effects_json(layer.mask->effects)},
                                  {"textures", textures}});
      }
  if (!remaining.empty() || targets.size() != 16 || layers.size() != 16 || fills != 8 ||
      folders != 8 || sources.size() != 8)
    throw std::runtime_error(
        "Smart-mask project witness needs all eight presets on Fill and Folder");
  return {{"version", 1},
          {"layers", layers},
          {"source_textures", 8},
          {"pixel_hash_domain", "bytewise native float32 RGBA"}};
}
// Must run before any GPU check, without a stock catalog or original PNG load.
inline QJsonObject validate(const TextureDocument &document, const QJsonObject &expected)
{
  const auto entries = expected["layers"].toArray();
  if (expected["version"].toInt() != 1 || entries.size() != 16)
    throw std::runtime_error("Invalid smart-mask project witness");
  std::map<std::string, const std::vector<FillTexture::Pixel> *> shared;
  std::set<QUuid> ids;
  int fill_count = 0, folder_count = 0, bindings = 0;
  QJsonArray records;
  for (const auto value : entries) {
    const auto entry = value.toObject();
    const QUuid id(entry["layer"].toString());
    if (id.isNull() || !ids.insert(id).second)
      throw std::runtime_error("Duplicate/invalid smart-mask layer witness");
    const TextureSet *owner = nullptr;
    const TextureLayer *target = nullptr;
    for (const auto &set : document.texture_sets())
      if (set.id == QUuid(entry["set"].toString()))
        for (const auto &layer : set.layers)
          if (layer.id == id) {
            owner = &set;
            target = &layer;
          }
    if (!owner || !target || !target->mask)
      throw std::runtime_error("Packed smart-mask target missing");
    const bool folder = entry["kind"].toString() == "folder";
    if (target->kind != (folder ? LayerKind::Folder : LayerKind::Fill))
      throw std::runtime_error("Packed smart-mask target kind changed");
    folder ? ++folder_count : ++fill_count;
    if (target->mask->background != float(entry["background"].toDouble()) ||
        target->mask->enabled != entry["enabled"].toBool() ||
        mask_effects_json(target->mask->effects) != entry["mask_effects"].toArray())
      throw std::runtime_error("Packed smart-mask parameters changed");
    for (const auto item : entry["textures"].toArray()) {
      const auto binding = item.toObject();
      const auto *effect = find_mask_effect(target->mask->effects,
                                            QUuid(binding["effect"].toString()));
      if (!effect || effect->kind != MaskEffectKind::Fill ||
          effect->mapping.projection.mode != 1 || !effect->mapping.projection.texture)
        throw std::runtime_error("Packed triplanar bitmap reference missing");
      const auto &texture = *effect->mapping.projection.texture;
      if (QString::fromStdString(texture.asset) != binding["asset"].toString() ||
          QString::fromStdString(texture.hash) != binding["source_hash"].toString() ||
          texture.width != binding["width"].toInt() ||
          texture.height != binding["height"].toInt() ||
          pixel_sha256(texture) != binding["pixel_sha256"].toString())
        throw std::runtime_error("Packed smart-mask pixel/reference hash changed");
      const auto key = texture.asset + ":" + texture.hash;
      const auto [found, inserted] = shared.emplace(key, texture.pixels.get());
      if (!inserted && found->second != texture.pixels.get())
        throw std::runtime_error("Packed mask generators lost shared image identity");
      ++bindings;
    }
    records.append(QJsonObject{{"layer", id.toString()}, {"packed_refs_pixels_parameters", true}});
  }
  if (fill_count != 8 || folder_count != 8 || shared.size() != 8 || bindings != 16)
    throw std::runtime_error("Packed smart-mask resource closure is incomplete");
  return {{"layers", records},
          {"fill_targets", fill_count},
          {"folder_targets", folder_count},
          {"bindings", bindings},
          {"shared_image_storages", int(shared.size())},
          {"packed_refs_pixels_parameters", true}};
}
}  // namespace bts::smart_mask_proof
