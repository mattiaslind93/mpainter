// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialParameters.hh"
#include "NativeWorldNormalSurface.hh"
#include <atomic>
#include <memory>
#include <string>
namespace blender {struct Main;struct Material;struct bNodeTree;struct Image;}
namespace bts {
class NativeMaterialImageResource;
class NativeMaterialImageTileSet;
struct NativeMaterialChannelGraph {
 int channel=0;
 blender::bNodeTree *tree=nullptr; // Owned by the detached Main below.
 QString error;
 bool normal_perturbed=false; // Typed effective, inlined Principled Normal-path result.
};
// Built entirely in a worker. GPU compilation then belongs to one native owner
// context; source image IDs remain shared by all channel trees in this Main.
struct NativeMaterialGraph {
 using MainOwner=std::unique_ptr<blender::Main,void(*)(blender::Main *)>;
 MainOwner main;
 blender::Material *material;
 NativeMaterialDescription description;
 std::vector<std::array<float,4>> parameters;
 std::vector<NativeMaterialChannelGraph> channels;
 std::array<float,9> color_to_aces{1,0,0,0,1,0,0,0,1},color_from_aces=color_to_aces;
 std::vector<std::pair<blender::Image *,std::shared_ptr<NativeMaterialImageResource>>> image_resources;
 std::vector<std::pair<blender::Image *,std::shared_ptr<const NativeMaterialImageTileSet>>> tiled_image_resources;
 // Optional private production contribution. Flat sources keep this null and
 // never allocate a fragment program, geometry job or front/back texture pair.
 blender::bNodeTree *world_normal_tree=nullptr;
 QString world_normal_error;
 bool world_normal_perturbed=false;
 const NativeWorldNormalSurfaceContract world_normal_surface_contract;
 const bool world_normal_source_displacement; // Original active EEVEE output edge, not document displacement.
 bool original_inputs_preserved=false;
 std::string resource_content_hash; // Worker-normalized immutable graph/image identity, excluding preview PNG.
 bool generate_resource_preview=false; // Explicit import only, evaluated once before Shelf publication.
 NativeMaterialGraph(MainOwner,blender::Material *,NativeMaterialDescription,const std::atomic_bool &,
   NativeWorldNormalSurfaceContract=NativeWorldNormalSurfaceContract::SourceAuthored);
};
}
