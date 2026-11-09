#pragma once
#include "BlendMaterial.hh"
#include <QMap>
#include <functional>
namespace bts {
struct BlendLibraryMaterial {
  QString file, relative_file, catalog_path;
  BlendMaterialEntry material;
};
struct BlendAssetLibrary {
  QString root;
  QMap<QString, QString> catalogs;
  std::vector<BlendLibraryMaterial> materials;
  QStringList errors;
  int files = 0;
};
BlendAssetLibrary inspect_blend_asset_library(const QString &,
                                              std::stop_token,
                                              std::function<void(const QString &)> progress = {});
inline bool blend_library_matches(const BlendLibraryMaterial &entry,
                                  const QString &query,
                                  const QString &catalog,
                                  bool assets_only)
{
  if (assets_only && !entry.material.marked_asset)
    return false;
  if (catalog == "@unassigned") {
    if (!entry.catalog_path.isEmpty())
      return false;
  }
  else if (!catalog.isEmpty() && entry.catalog_path != catalog &&
           !entry.catalog_path.startsWith(catalog + "/"))
    return false;
  const auto text = entry.material.name + " " + entry.relative_file + " " + entry.catalog_path +
                    " " + entry.material.tags.join(' ');
  for (const auto &word : query.split(' ', Qt::SkipEmptyParts))
    if (!text.contains(word, Qt::CaseInsensitive))
      return false;
  return true;
}
}  // namespace bts
