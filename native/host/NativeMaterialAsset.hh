#pragma once
#include "AssetCatalog.hh"
#include "NativeMaterialFill.hh"

namespace bts {
inline bool is_native_material_asset(const StudioAsset &asset)
{
  return asset.filename.endsWith(".btsnative.blend");
}

// The archive is one ordinary immutable catalog blob. Instances retain only
// its UUID and their own parameter values, never another copy of its images.
inline StudioAsset native_material_asset(QString name, QByteArray archive,
                                         NativeMaterialDescription description)
{
  if (archive.isEmpty()) {
    throw std::invalid_argument("Native material source archive is empty");
  }
  description.validate();
  require_native_source_policy(description);
  StudioAsset asset;
  asset.name = std::move(name);
  asset.filename = "material.btsnative.blend";
  asset.usages = {"material"};
  asset.search_terms = {"blender-native"};
  asset.bytes = std::move(archive);
  asset.material = MaterialValues{};
  FillProjection fill;
  fill.material_resource = asset.id.toString(QUuid::WithoutBraces).toStdString();
  fill.native_material = std::make_shared<const NativeMaterialDescription>(std::move(description));
  asset.fill_preset = std::move(fill);
  return asset;
}
}
