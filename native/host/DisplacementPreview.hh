#pragma once
#include "Displacement.hh"
#include "PreviewSourceMeshes.hh"
#include <QJsonObject>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include <functional>
namespace blender {struct Main;struct Scene;struct ViewLayer;struct Depsgraph;struct Object;struct Mesh;struct bNodeTree;struct ModifierData;}
namespace bts {
struct TextureSet;
// Owns private pre-preview evaluated meshes. Original Blender Mesh IDs are never edited.
class DisplacementPreview final {
 public:
  DisplacementPreview()=default;
  ~DisplacementPreview();
  bool apply(blender::Main &,blender::Depsgraph &,const TextureSet &,bool content);
  bool sync_contents(blender::Main &,blender::Depsgraph &,const std::function<bool(std::uint32_t)> &);
  void reset(blender::Main &);
  const SourceMeshMap &source_meshes()const{return source_meshes_;}
  const std::map<std::uint32_t,int> &material_levels()const{return material_levels_;}
  QJsonObject state(blender::Main &,blender::Depsgraph &)const;
  class SaveScope {
   public:
    explicit SaveScope(blender::Main &);
    ~SaveScope();
    SaveScope(const SaveScope &)=delete;
   private:
    std::vector<std::pair<blender::Object *,blender::ModifierData *>> detached_;
  };
 private:
  struct Request {DisplacementSettings settings;bool content=false,disabled=false;};
  std::map<std::uint32_t,Request> requests_;
  SourceMeshMap source_meshes_;
  std::map<std::uint32_t,int> levels_,material_levels_;
  std::map<std::uint32_t,double> source_edge_lengths_;
  std::uint64_t edge_scans_=0;
  blender::bNodeTree *smooth_group_=nullptr; // Main owns the node tree.
  void free_sources();
  bool refresh(blender::Main &,blender::Depsgraph &);
};
}
