#pragma once
#include "MeshImport.hh"
#include "SceneSnapshot.hh"
#include <map>
namespace bts {
class TextureDocument;
struct MeshReplacement {
  std::unique_ptr<PendingMeshProject> pending;
  ImportSummary summary;
  std::unique_ptr<TextureDocument> authored;
  SceneSnapshot source;
  std::map<std::uint32_t,std::uint32_t> materials;
};
MeshReplacement prepare_mesh_replacement(blender::Main &,const TextureDocument &,const std::string &,bool import_cameras,const AutoUnwrapOptions &,const UsdImportOptions &,const UnitSettings &,const BlendMeshSelection & = {});
void adopt_mesh_replacement(blender::Main &,blender::Scene &,MeshReplacement &);
}
