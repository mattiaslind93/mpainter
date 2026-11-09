#pragma once
#include "AssetCatalog.hh"
#include "NativeMaterialParameters.hh"
#include "NativeWorldNormalSurface.hh"
#include <QImage>
#include <stop_token>
#include <atomic>
namespace blender {
struct Main;
struct Material;
}  // namespace blender
namespace bts {
struct NativeMaterialGraph;
std::shared_ptr<NativeMaterialGraph> prepare_native_material_graph(
    const blender::Main &,
    const QString &,
    const QString &,
    const std::atomic_bool &,
    NativeWorldNormalSurfaceContract = NativeWorldNormalSurfaceContract::SourceAuthored);
// Worker-only hydration keeps the owned archive's encoded working space.
std::shared_ptr<NativeMaterialGraph> restore_native_material_graph(
    const QString &,
    const QString &,
    const std::atomic_bool &,
    NativeWorldNormalSurfaceContract = NativeWorldNormalSurfaceContract::SourceAuthored);
struct BlendMaterialEntry {
  QString name;
  bool marked_asset = false;
  QImage preview;
  QString catalog_id, catalog_name, description;
  QStringList tags;
};
// Explicit source only. Metadata reader may run on a file-browser worker.
std::vector<BlendMaterialEntry> inspect_blend_materials(const QString &, std::stop_token = {});
struct PreparedBlendMaterial {
  StudioAsset material;
  std::vector<StudioAsset> textures;
  bool preview_only = false;
  std::shared_ptr<NativeMaterialGraph> native_graph;
};
// Worker-only append into a private Main; reference contains a colour-space snapshot.
PreparedBlendMaterial import_blend_material(const blender::Main &,
                                            const QString &,
                                            const QString &,
                                            const std::atomic_bool &,
                                            bool preview_only = false);
NativeMaterialDescription inspect_blend_material_graph(const blender::Main &,
                                                       const QString &,
                                                       const QString &,
                                                       const std::atomic_bool &);
QImage blend_material_preview(const StudioAsset &);
QImage blend_material_datablock_preview(const blender::Material &);
QImage blend_material_source_preview(const QString &file, const QString &material);
void set_native_material_preview(StudioAsset &, const QImage &);
}  // namespace bts
