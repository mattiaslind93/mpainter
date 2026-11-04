#pragma once
#include "PreviewSourceMeshes.hh"
#include <QJsonObject>
#include <memory>
#include <map>
#include <string>
#include <set>
#include <span>
namespace blender {struct Main;struct Scene;struct Depsgraph;struct View3D;}
namespace bts {
class TextureDocument;struct SceneSnapshot;struct UsdTextureSetExport;
// Private staging database: no native pointer into the editable scene escapes.
class NativeMeshExport {
 public:
  static std::shared_ptr<NativeMeshExport> prepare(blender::Main &,blender::Scene &,blender::Depsgraph &,blender::View3D *,const SourceMeshMap &,const TextureDocument *document=nullptr,const SceneSnapshot *geometry=nullptr,const std::set<std::uint32_t> &active={},bool recompute_normals=true);
  ~NativeMeshExport();
  NativeMeshExport(const NativeMeshExport &)=delete;
  void write(const std::string &path,bool triangulate);
  void attach_usd_textures(std::span<const UsdTextureSetExport>);
  void attach_usd_material_libraries(const std::map<std::uint32_t,std::string> &);
  QJsonObject summary()const;
 private:
  void finalize_geometry();
  NativeMeshExport();struct Impl;std::unique_ptr<Impl> impl_;
};
}
