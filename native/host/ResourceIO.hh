#pragma once
#include "AssetCatalog.hh"
#include <map>
#include <string>
namespace blender {struct Main;struct Scene;struct Image;namespace bke::blendfile {class PartialWriteContext;}}
namespace bts {
// Associations originate in the same pass that validates/packs resource bytes.
// Pointers belong exclusively to the supplied pending/snapshot Main.
struct ProjectResourceImages {std::map<std::string,blender::Image *> by_key;std::map<std::string,QString> sha256;std::vector<std::uint32_t> retired_uids;};
void pack_project_assets(blender::Main &,blender::Scene &,const std::vector<StudioAsset> &,ProjectResourceImages *images=nullptr,blender::bke::blendfile::PartialWriteContext *snapshot=nullptr);
std::vector<StudioAsset> read_project_assets(const blender::Scene &,ProjectResourceImages *images=nullptr);
}
