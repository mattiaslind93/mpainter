#pragma once
#include "AssetCatalog.hh"
#include "TextureDocument.hh"
#include <QCryptographicHash>
#include <QJsonArray>
#include <set>
namespace bts::spmsk_shelf_proof {
inline QString digest(QByteArrayView bytes)
{
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
inline QJsonObject texture_witness(const FillTexture &texture)
{
  texture.validate();
  if (!texture.pixels)
    throw std::runtime_error("SPMSK packed bitmap has no pixels");
  const auto bytes = resource_product(texture.pixels->size(), sizeof(FillTexture::Pixel));
  if (bytes > std::uint64_t(std::numeric_limits<qsizetype>::max()))
    throw std::length_error("SPMSK pixel witness is too large");
  return {{"asset", QString::fromStdString(texture.asset)},
          {"hash", QString::fromStdString(texture.hash)},
          {"width", texture.width},
          {"height", texture.height},
          {"pixels",
           digest(QByteArrayView(reinterpret_cast<const char *>(texture.pixels->data()),
                                 qsizetype(bytes)))}};
}
// Explicit diagnostic only: existing authored parameters, packed source bytes
// and any already-resolved bitmap pixels. No composition, hydration or IO.
inline QJsonObject witness(const TextureDocument &document,
                           const std::vector<StudioAsset> &resources)
{
  QJsonArray targets, sources;
  std::set<QString> hashes;
  int fills = 0, folders = 0;
  for (const auto &set : document.texture_sets())
    for (const auto &layer : set.layers)
      if (layer.name.starts_with("BN SPMSK ") && layer.name != "BN SPMSK child") {
        if (!layer.mask || layer.mask->effects.empty())
          throw std::runtime_error("SPMSK target has no editable mask effects");
        if (layer.kind == LayerKind::Fill)
          ++fills;
        else if (layer.kind == LayerKind::Folder)
          ++folders;
        else
          throw std::runtime_error("SPMSK witness target has wrong kind");
        QJsonArray textures;
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          if (effect.mapping.projection.texture) {
            auto row = texture_witness(*effect.mapping.projection.texture);
            row["effect"] = effect.id.toString();
            textures.append(row);
          }
        targets.append(QJsonObject{{"set", set.id.toString()},
                                   {"layer", layer.id.toString()},
                                   {"name", QString::fromStdString(layer.name)},
                                   {"kind", layer.kind == LayerKind::Folder ? "folder" : "fill"},
                                   {"background", layer.mask->background},
                                   {"enabled", layer.mask->enabled},
                                   {"effects", mask_effects_json(layer.mask->effects)},
                                   {"textures", textures}});
      }
  for (const auto &asset : resources)
    if (asset.scope == AssetScope::Project &&
        asset.filename.endsWith(".spmsk", Qt::CaseInsensitive))
    {
      if (asset.bytes.isEmpty())
        throw std::runtime_error("SPMSK retained original source bytes missing");
      const auto hash = digest(asset.bytes);
      if (!hashes.insert(hash).second)
        throw std::runtime_error("SPMSK source retained more than once");
      sources.append(QJsonObject{{"id", asset.id.toString()},
                                 {"name", asset.name},
                                 {"filename", asset.filename},
                                 {"sha256", hash},
                                 {"bytes", qint64(asset.bytes.size())},
                                 {"policy", QJsonArray::fromStringList(asset.search_terms)}});
    }
  if (targets.size() != 4 || fills != 2 || folders != 2 || sources.size() != 2)
    throw std::runtime_error(
        "SPMSK witness requires two imported sources on both Fill and Folder");
  return {{"version", 1},
          {"targets", targets},
          {"sources", sources},
          {"fill_targets", fills},
          {"folder_targets", folders},
          {"source_appearance_parity_claimed", false}};
}
inline QJsonObject validate(const TextureDocument &document,
                            const std::vector<StudioAsset> &resources,
                            const QJsonObject &expected)
{
  if (expected["version"].toInt() != 1 || expected["targets"].toArray().size() != 4 ||
      expected["sources"].toArray().size() != 2)
    throw std::runtime_error("Invalid SPMSK persistence witness");
  const auto actual = witness(document, resources);
  if (actual != expected)
    throw std::runtime_error("Packed SPMSK parameters, source bytes or bitmap pixels changed");
  return {{"targets", 4},
          {"sources", 2},
          {"fill_targets", 2},
          {"folder_targets", 2},
          {"parameters_original_bytes_and_resolved_pixels_equal", true},
          {"no_hydration_or_cpu_composition", true}};
}
}  // namespace bts::spmsk_shelf_proof
