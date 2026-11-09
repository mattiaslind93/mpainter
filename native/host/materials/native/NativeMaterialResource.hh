// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AssetCatalog.hh"
#include <atomic>
#include <memory>
namespace blender {
struct Main;
}
namespace bts {
struct NativeMaterialGraph;
struct PreparedNativeMaterialResource {
  StudioAsset asset;
  std::shared_ptr<NativeMaterialGraph> graph;
  QJsonObject diagnostics;
  std::vector<StudioAsset> textures;
};
// Worker-only. No live scene IDs or GPU resources cross this boundary.
PreparedNativeMaterialResource prepare_native_material_resource(const blender::Main &,
                                                                const QString &library,
                                                                const QString &material,
                                                                const std::atomic_bool &);
std::shared_ptr<NativeMaterialGraph> hydrate_native_material_resource(
    const blender::Main &,
    const StudioAsset &,
    const std::atomic_bool &,
    const std::vector<StudioAsset> &textures = {});
// Archive restoration uses its own recorded working space and owns every ID;
// it does not require a live or fabricated reference Main.
std::shared_ptr<NativeMaterialGraph> hydrate_native_material_resource(
    const StudioAsset &, const std::atomic_bool &, const std::vector<StudioAsset> &textures = {});
}  // namespace bts
