#pragma once
#include "UsdScene.hh"
#include "BlendLibrary.hh"
#include "PhysicalScale.hh"
#include <string>
#include <vector>
namespace bts {
enum class DocumentResolution : int {
  Pixels128 = 128,
  Pixels256 = 256,
  Pixels512 = 512,
  Pixels1024 = 1024,
  Pixels2048 = 2048,
  Pixels4096 = 4096,
  Pixels8192 = 8192,
};

enum class NormalMapFormat { OpenGL, DirectX };
enum class UvWorkflow { SingleTile, PreserveTiles };

enum class UvDataPolicy { Missing, Recompute };
struct AutoUnwrapOptions {
  bool enabled=false;
  UvDataPolicy seams=UvDataPolicy::Missing;
  UvDataPolicy islands=UvDataPolicy::Missing;
  UvDataPolicy packing=UvDataPolicy::Missing;
  int margin=1; // None, Small (0.2%), Medium (0.5%), Large (1%).
  bool hard_surface=false; // Native orthographic projection + axis-aligned packing.
  bool operator==(const AutoUnwrapOptions &) const = default;
};

struct NewProjectOptions {
  std::string absolute_mesh_path;
  DocumentResolution resolution = DocumentResolution::Pixels1024;
  NormalMapFormat normal_map_format = NormalMapFormat::OpenGL;
  UvWorkflow uv_workflow = UvWorkflow::SingleTile;
  std::vector<std::string> utility_maps;
  std::string template_path;
  bool tangent_per_fragment=false;
  bool import_cameras=true;
  AutoUnwrapOptions auto_unwrap;
  UsdImportOptions usd;
  BlendMeshSelection blend;
  UnitSettings units;
  SourceUnits source_units;
};

}
