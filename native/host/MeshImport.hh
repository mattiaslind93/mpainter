#pragma once

#include "UsdScene.hh"
#include "BlendLibrary.hh"
#include "PhysicalScale.hh"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <map>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <set>
#include <vector>

namespace blender {
struct Main;
struct Scene;
}

namespace bts {

struct SceneSnapshot;
struct AutoUnwrapOptions;
enum class ImportedUvState {
  None,
  SomePolygonMeshes,
  AllPolygonMeshes,
};

struct ImportedMaterial {
  // Native session identity, preserved by merge. Never serialize as a document UUID.
  std::uint32_t session_uid = 0;
  std::string name;
  std::set<int> tiles;
  bool udim_valid=true;
};

struct ImportSummary {
  SourceUnits source_units;
  std::string source_path;
  std::string authored_color_source =
      "Linear Rec.709: raw MTL RGB assumed; OBJ vertex RGB decoded from sRGB by Blender";
  std::size_t objects = 0;
  std::size_t cameras = 0;
  std::map<std::uint32_t,std::string> camera_names;
  std::size_t polygon_meshes = 0;
  std::size_t vertices = 0;
  std::size_t polygons = 0;
  std::size_t triangles = 0;
  std::size_t nondegenerate_triangles = 0;
  std::size_t meshes_with_uvs = 0;
  ImportedUvState uv_state = ImportedUvState::None;
  std::vector<ImportedMaterial> materials;
  std::vector<std::string> warnings;
};

class MeshImportCancelled : public std::runtime_error {
 public:
  MeshImportCancelled();
};

// Owns a separate native Main, Scene and context until a validated import is consumed.
// All methods and destruction run on the native owner thread. Destroy before live Main
// replacement or Blender shutdown. No context, scene, GPU or UI switching happens here.
class PendingMeshProject {
 public:
  enum class State { Ready, Consumed };

  // Requires an absolute UTF-8 path and initialized Blender. Uses native OBJ defaults
  // (-Z forward, Y up, unit scale), preserves native meshes/UVs/material slots, and does
  // not request Blender-relative image paths. Sources must remain unchanged during this call.
  // Cancellation is checked during preflight and validation and around OBJ_import;
  // Blender's synchronous parser itself cannot be interrupted. Failure discards staging.
  //
  // Source, geometry and snapshot estimates use current available RAM/VRAM.
  // Only real native index/path representations and invalid data are rejected.
  // Image pixels and rendering are not loaded during staging.
  // Native parser repairs are retained and count changes reported, not silently filtered.
  // Some native parser/MTL warnings only reach Blender's CLG log, not ReportList.
  //
  // Raw MTL RGB is assumed linear Rec.709; Blender decodes OBJ vertex RGB from sRGB.
  // Native authored-color conversion brings both into live Main's working space.
  // It preserves imported Image color tags/pixels and Normal Map/Bump data defaults.
  // This guarantee covers native OBJ-created trees, not arbitrary group/data graphs.
  // The application's New rendering/working-space defaults are applied separately.
  static std::unique_ptr<PendingMeshProject> from_scene(blender::Main &,const std::string &,bool import_cameras=true,std::stop_token cancellation={},const UsdImportOptions &usd={},const BlendMeshSelection &blend={});
  static std::unique_ptr<PendingMeshProject> from_blend(blender::Main &,const std::string &,bool,const BlendMeshSelection &,std::stop_token);
  static std::unique_ptr<PendingMeshProject> from_fbx(blender::Main &,const std::string &,bool,std::stop_token);
  static std::unique_ptr<PendingMeshProject> from_alembic(blender::Main &,const std::string &,bool,std::stop_token,std::optional<double> frame={});
  static std::unique_ptr<PendingMeshProject> from_usd(blender::Main &,const std::string &,bool,const UsdImportOptions &,std::stop_token);
  static std::unique_ptr<PendingMeshProject> from_obj(
      blender::Main &live,
      const std::string &absolute_source_path,
      std::stop_token cancellation = {});

  ~PendingMeshProject();
  PendingMeshProject(const PendingMeshProject &) = delete;
  PendingMeshProject &operator=(const PendingMeshProject &) = delete;

  State state() const;
  // Immutable after Ready, including after consumption. UV state means layer presence
  // per polygon mesh; native OBJ zero-fills missing UV corners in partially mapped faces.
  const ImportSummary &summary() const;
  SceneSnapshot snapshot() const;
  void prepare_uvs(const AutoUnwrapOptions &);

  // One-way ownership transfer into the SAME live Main and unchanged working space.
  // Forces every staged top-level ID through BKE_main_merge, so existing same-name IDs
  // are not reused. Blender may make names unique. Frees staging graph hashes first.
  // Returns the imported Scene now owned by live Main; state becomes Consumed.
  // Caller owns active-window switching, old-project cleanup and display/paint defaults.
  // This native merge has no rollback once begun; all validation precedes it.
  blender::Scene &merge_into(blender::Main &live);

 private:
  struct Impl;
  explicit PendingMeshProject(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace bts
