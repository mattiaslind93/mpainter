// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BrushAlpha.hh"
#include "TextureResource.hh"
#include "BrushSettings.hh"
#include "MaterialValues.hh"
#include "FillProjection.hh"
#include "MaskPreset.hh"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <optional>
#include <vector>
namespace bts {
struct SmartMaterialPreset;
enum class AssetScope { Builtin, Session, Project, User };
enum class MaterialShelf { None, Materials, Presets };
struct StudioAsset {
  QUuid id = QUuid::createUuid();
  QString name, filename, path;
  QStringList usages;
  QStringList search_terms;  // Discovery tags, optionally persisted in resource metadata.
  AssetScope scope = AssetScope::Session;
  std::uint64_t revision = 0;  // Runtime thumbnail identity; imported resources are immutable.
  QByteArray bytes;
  std::vector<TextureSourceFile>
      texture_files;  // Immutable disk-backed originals; no aggregate UDIM blob.
  std::optional<BrushAlpha> alpha;
  std::optional<MaterialValues> material;
  std::optional<BrushSettings> brush_preset;
  std::optional<std::array<double, 3>> physical_size_cm;
  std::optional<FillProjection> fill_preset;
  bool projection_weave = false;  // Original built-in resource, available to Projection.
  bool smart_material = false;
  std::shared_ptr<const SmartMaterialPreset> material_preset;
  std::shared_ptr<const LayerMask> smart_mask;
  QString hash() const;

 private:
  // Keeping a COW reference makes in-place writes detach: cached digests are
  // never reused after byte mutation, even when QByteArray had one owner.
  friend void hydrate_asset_presets(StudioAsset &);
  friend void bind_verified_spsm_source(StudioAsset &, const QByteArray &, const QString &);
  friend MaterialShelf material_shelf(const StudioAsset &);
  friend void cache_material_shelf(StudioAsset &, MaterialShelf);
  QByteArray classified_bytes_;
  QStringList classified_policies_;
  MaterialShelf classified_shelf_ = MaterialShelf::None;
  QString hydrated_hash_;
  const void *hydrated_mask_ = nullptr, *hydrated_material_ = nullptr;
  mutable QByteArray hashed_bytes_;
  mutable QString cached_hash_;
};
class PreparedUserAsset {
  friend class AssetCatalog;
  StudioAsset asset_;
  QString directory_;
  PreparedUserAsset(StudioAsset asset, QString directory)
      : asset_(std::move(asset)), directory_(std::move(directory))
  {
  }

 public:
  PreparedUserAsset(PreparedUserAsset &&) = default;
  PreparedUserAsset &operator=(PreparedUserAsset &&) = default;
};
void hydrate_asset_presets(StudioAsset &);
MaterialShelf material_shelf(const StudioAsset &);
void cache_material_shelf(StudioAsset &, MaterialShelf);
FillProjection asset_fill_projection(const StudioAsset &, bool automatic_physical);
QString asset_scope_name(AssetScope);
QJsonObject asset_metadata(const StudioAsset &);
StudioAsset asset_metadata_read(const QJsonObject &);
class AssetCatalog {
 public:
  explicit AssetCatalog(QString library_directory);
  const std::vector<StudioAsset> &assets() const
  {
    return assets_;
  }
  const QString &directory() const
  {
    return directory_;
  }
  void add(StudioAsset);
  void rename(const QUuid &, const QString &);
  // Worker validates/decodes and writes the immutable blob. UI publishes only
  // metadata; the opaque token cannot be fabricated from unchecked bytes.
  static PreparedUserAsset prepare_user(StudioAsset, QString directory);
  void add_prepared(PreparedUserAsset);
  bool replace_prepared(PreparedUserAsset,
                        const QString &expected_hash,
                        std::uint64_t expected_revision);
  bool remove_user(const QUuid &, const QString &expected_hash, std::uint64_t expected_revision);
  QUuid retain_project(
      const StudioAsset &);  // Verified immutable bytes shared with a project reference.

  // Compare-and-replace: a completed disk scan cannot overwrite a newer import.
  bool replace(StudioAsset, const QString &expected_hash);
  // Publish only cached runtime preset data, without rewriting source/library
  // files. A changed resource cannot accept an obsolete worker completion.
  std::optional<StudioAsset> hydrate_preset(const QUuid &, const QString &expected_hash);
  void load_user_library();
  void replace_project(std::vector<StudioAsset>);
  std::vector<StudioAsset> project_assets() const;
  QString materialize(const StudioAsset &) const;

 private:
  QString directory_;
  std::vector<StudioAsset> assets_;
  void save_user_library() const;
};
}  // namespace bts
