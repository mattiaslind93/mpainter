#pragma once
#include "NativeMaterialParameters.hh"
#include "AssetCatalog.hh"
#include <QByteArray>
#include <atomic>
namespace bts {
struct NativeMaterialGraph;
struct NativeMaterialArchive {
  QByteArray bytes;
  NativeMaterialDescription description;
  QJsonObject diagnostics;
  std::vector<StudioAsset> textures;
};
// Worker only, before publishing the detached graph to the GPU owner. Archives
// only the original material and its dependencies, never derived channel trees.
NativeMaterialArchive archive_native_material(NativeMaterialGraph &,
                                              const QString &source_library,
                                              const std::atomic_bool &,
                                              bool external_images = false);
}  // namespace bts
