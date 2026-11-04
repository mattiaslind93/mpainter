#pragma once
#include "MeshMaps.hh"
#include "TextureDocument.hh"
#include <charconv>
#include <filesystem>
namespace bts {
inline constexpr std::array<const char *,7> mesh_map_suffixes{"normal_base","world_space_normals","id","ambient_occlusion","curvature","position","thickness"};
struct MeshMapImportName {std::string set_name,resource_name;int role=0,tile=1001;};
inline std::optional<MeshMapImportName> mesh_map_import_name(const std::string &path){
  auto stem=std::filesystem::path(path).stem().string();int tile=1001;
  if(stem.size()>5&&(stem[stem.size()-5]=='.'||stem[stem.size()-5]=='_')){int number=0;const auto first=stem.data()+stem.size()-4,last=stem.data()+stem.size();const auto parsed=std::from_chars(first,last,number);if(parsed.ec==std::errc{}&&parsed.ptr==last&&number>=1001&&number<=2000){tile=number;stem.resize(stem.size()-5);}}
  for(int role=0;role<7;++role){const auto suffix="_"+std::string(mesh_map_suffixes[role]);if(stem.size()>suffix.size()&&stem.ends_with(suffix))return MeshMapImportName{stem.substr(0,stem.size()-suffix.size()),stem,role,tile};}return {};
}
struct ImportedMeshMap {MeshMapImportName name;MeshMapPlane plane;};
struct MeshMapImports {std::vector<ImportedMeshMap> maps;std::vector<std::string> ignored;};
// Native data decoding occurs before replacing a project. No Main IDs are created here.
MeshMapPlane load_mesh_map_image(const std::string &path);
MeshMapImports load_utility_maps(const std::vector<std::string> &paths);
std::vector<std::string> assign_utility_maps(TextureDocument &,const MeshMapImports &);
}
