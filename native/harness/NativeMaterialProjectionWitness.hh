// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "EvaluationScope.hh"
#include "FillTextureJSON.hh"
#include "NativeMaterialAsset.hh"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <map>
namespace bts::projection_ui_proof {
inline void require(bool okay, const char *message)
{
  if (!okay)
    throw std::runtime_error(message);
}
inline QJsonObject layer_json(const TextureDocument &document,
                              const QUuid &set_id,
                              const QUuid &layer_id)
{
  EditStateScope metadata;
  for (const auto &value : document.to_json()["texture_sets"].toArray()) {
    const auto set = value.toObject();
    if (QUuid(set["id"].toString()) != set_id)
      continue;
    for (const auto &entry : set["layers"].toArray())
      if (QUuid(entry.toObject()["id"].toString()) == layer_id)
        return entry.toObject();
  }
  throw std::runtime_error("BJ persistent set/layer UUID missing");
}
inline void texture_witness(const std::shared_ptr<const FillTexture> &texture,
                            QJsonArray &result,
                            int channel,
                            int tile = 0)
{
  if (!texture)
    return;
  if (texture->tiles) {
    for (const auto &[number, image] : *texture->tiles)
      texture_witness(image, result, channel, number);
    return;
  }
  texture->validate();
  require(bool(texture->pixels), "BJ packed bitmap pixels unresolved");
  const auto bytes = resource_product(texture->pixels->size(), sizeof(FillTexture::Pixel));
  require(bytes <= std::uint64_t(std::numeric_limits<qsizetype>::max()),
          "BJ texture witness size overflow");
  result.append(QJsonObject{
      {"channel", channel},
      {"tile", tile},
      {"descriptor", fill_texture_json(texture)},
      {"pixels_sha256",
       QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(
                                                                       texture->pixels->data()),
                                                                   qsizetype(bytes)),
                                                    QCryptographicHash::Sha256)
                               .toHex())}});
}
// Disk-backed imports intentionally retain immutable original members rather
// than a second aggregate QByteArray. Compare their actual bytes independently
// of the private cache path, just as ResourceIO validates each packed member.
inline QJsonObject resource_witness(const StudioAsset &asset)
{
  QJsonObject row{{"id", asset.id.toString()}, {"filename", asset.filename}};
  if (asset.texture_files.empty()) {
    require(!asset.bytes.isEmpty(), "BJ packed resource has no retained original bytes");
    row["bytes"] = qint64(asset.bytes.size());
    row["sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(asset.bytes, QCryptographicHash::Sha256).toHex());
  }
  else {
    validate_texture_sources(asset.texture_files);
    qint64 bytes = 0;
    for (const auto &file : asset.texture_files) {
      require(QFileInfo(file.path).size() == file.size, "BJ retained texture member size changed");
      require(hash_texture_file(file.path) == file.sha256,
              "BJ retained texture member bytes changed");
      require(QFileInfo(file.path).size() == file.size,
              "BJ retained texture member size changed during read");
      require(file.size <= std::numeric_limits<qint64>::max() - bytes,
              "BJ retained texture member total overflow");
      bytes += file.size;
    }
    row["bytes"] = bytes;
    row["sha256"] = texture_sources_hash(asset.texture_files);
    row["texture_files"] = texture_sources_json(asset.texture_files, false);
  }
  return row;
}
// Metadata, retained immutable bytes and already-resolved bitmap pixels only.
// No CPU material evaluation, cache hydration or stock asset fallback occurs.
template<class Assets>
QJsonObject witness(const TextureDocument &document,
                    const Assets &assets,
                    const QUuid &set_id,
                    const QUuid &layer_id,
                    const QJsonArray &required_tiles = QJsonArray{1001, 1002, 1011})
{
  const TextureSet *owner = nullptr;
  const TextureLayer *target = nullptr;
  for (const auto &set : document.texture_sets())
    if (set.id == set_id) {
      owner = &set;
      for (const auto &layer : set.layers)
        if (layer.id == layer_id)
          target = &layer;
    }
  require(owner && target && target->kind == LayerKind::Fill,
          "BJ witness needs actual Fill target");
  QJsonArray tiles;
  for (int tile : owner->tiles)
    tiles.append(tile);
  require(!required_tiles.isEmpty() && tiles == required_tiles,
          "Projection fixture actual tile membership differs");
  require(!target->channels[3], "BJ internal Normal became an artist channel");
  if (target->fill.native_material &&
      target->fill.native_material->height_policy == NativeHeightPolicy::RawAuthoredFloatV1)
    require(owner->bump_origin == .5f, "BJ raw H neutral origin differs");
  QJsonArray textures;
  texture_witness(target->fill.texture, textures, 0);
  for (int c = 1; c < 15; ++c)
    texture_witness(fill_channel_texture(target->fill, c), textures, c);
  // Sort by persistent resource UUID so archive iteration order is irrelevant.
  std::map<QString, QJsonObject> resources;
  for (const auto &asset : assets) {
    auto row = resource_witness(asset);
    if (is_native_material_asset(asset)) {
      require(asset.fill_preset && bool(asset.fill_preset->native_material),
              "BJ original source description absent");
      require(asset.fill_preset->native_material->height_policy == NativeHeightPolicy::None,
              "BJ source policy was mutated");
      row["source_description"] = native_material_description_json(
          *asset.fill_preset->native_material);
    }
    resources.emplace(asset.id.toString(), std::move(row));
  }
  require(!resources.empty(), "BJ project source resources absent");
  QJsonArray packed;
  for (const auto &[id, row] : resources)
    packed.append(row);
  const auto layer = layer_json(document, set_id, layer_id);
  return {
      {"version", 1},
      {"set", set_id.toString()},
      {"layer", layer_id.toString()},
      {"tiles", tiles},
      {"resolution", owner->texture_resolution},
      {"height", owner->rows()},
      {"bump_origin", owner->bump_origin},
      {"projection", layer["fill_projection"]},
      {"channels", layer["channels"]},
      {"textures", textures},
      {"resources", packed},
      {"scope",
       "Persistent IDs, authored mapping/native parameters, original packed bytes and resolved bitmap pixels; no appearance reference"}};
}
template<class Assets>
QJsonObject validate(const TextureDocument &document,
                     const Assets &assets,
                     const QJsonObject &expected)
{
  require(expected["version"].toInt() == 1, "BJ packed witness version differs");
  const auto actual = witness(document,
                              assets,
                              QUuid(expected["set"].toString()),
                              QUuid(expected["layer"].toString()),
                              expected["tiles"].toArray());
  require(actual == expected, "BJ packed authored mapping/source bytes/pixel closure changed");
  return {{"data_first", true},
          {"set", expected["set"]},
          {"layer", expected["layer"]},
          {"actual_tiles", actual["tiles"]},
          {"source_bytes_parameters_and_resolved_pixels_equal", true},
          {"resource_count", actual["resources"].toArray().size()}};
}
}  // namespace bts::projection_ui_proof
