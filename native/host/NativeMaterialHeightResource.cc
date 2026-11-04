#include <QtCore>
#include "NativeMaterialHeightResource.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialImageDependencies.hh"
#include "NativeMaterialSource.hh"
#include "NativeMaterialSourceFiles.hh"
#include "NativeMaterialHeightContext.hh"
#include "NativeMaterialImages.hh"
#include "NativeHeightImageRetention.hh"
#include "IMB_colormanagement.hh"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "BLI_string.h"
#include "BLI_listbase.h"
#include <array>
#include "BKE_node_tree_update.hh"
#include "BKE_node_runtime.hh"
#include "DNA_material_types.h"
#include "DNA_ID.h"
#include <QCoreApplication>
#include <QThread>
#include <set>

namespace bts {
namespace {
// The native inliner can fold global-color functions/conversions into Value
// nodes. Inspect actual owned originals first. This intentionally over-rejects
// unused/ignored branches of this selected material and its referenced groups;
// it never scans unrelated Main materials or changes normal source behavior.
NativeHeightRequirements original_global_requirements(const blender::bNodeTree &root){
 using blender::ID_Type;
 NativeHeightRequirements result;const ResourceBudget budget;std::set<const blender::bNodeTree *> visited;
 std::vector<const blender::bNodeTree *> pending{&root};
 while(!pending.empty()){
  const auto *tree=pending.back();pending.pop_back();if(visited.contains(tree))continue;
  budget.require(resource_product(resource_sum(resource_sum(visited.size(),pending.capacity()),1),sizeof(void *)*8),0,"Original native Height color-context admission");visited.insert(tree);
  for(const auto &node:tree->nodes){
   result.global_blackbody|=STREQ(node.idname,"ShaderNodeBlackbody");
   result.global_wavelength|=STREQ(node.idname,"ShaderNodeWavelength");
   result.global_luminance|=STREQ(node.idname,"ShaderNodeRGBToBW");
   if(STREQ(node.idname,"ShaderNodeGroup")&&node.id){if(GS(node.id->name)!=blender::ID_NT)throw std::invalid_argument("Raw authored Height source group has an invalid tree ID");budget.require(resource_product(resource_sum(resource_sum(visited.size(),pending.capacity()),resource_sum(pending.size(),1)),sizeof(void *)*8),0,"Original native Height group traversal");pending.push_back(reinterpret_cast<const blender::bNodeTree *>(node.id));}
  }
  for(const auto &edge:tree->links)if(edge.fromsock&&edge.tosock&&edge.fromsock->type==blender::SOCK_RGBA&&edge.tosock->type==blender::SOCK_FLOAT)result.global_luminance=true;
 }
 return result;
}
// A positive image admission is structural, not a blanket image-policy flag.
// The original Bump provenance was already checked by raw_height_candidate().
bool scalar_image_height(const PreparedNativeRawHeight &prepared,const blender::Image **selected=nullptr){
 using namespace blender;
 if(prepared.provenance.bypass_nodes!=0)return false; // This image unit admits one Bump only.
 const auto linked=[](const bNodeSocket &socket)->const bNodeLink *{const auto *link=socket.link;return link&&link->fromnode&&link->fromsock&&link->tosock==&socket&&(link->flag&NODE_LINK_VALID)&&link->is_used()?link:nullptr;};
 const bNodeLink *root=nullptr;
 for(auto &node:prepared.tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV")){
  if(root)return false;const auto *value=bke::node_find_enabled_input_socket(node,"Value");if(!value||(root=linked(*value))==nullptr)return false;
 }
 if(!root||!STREQ(root->fromnode->idname,"ShaderNodeSeparateColor")||!STREQ(root->fromsock->identifier,"Red")||!root->fromnode->storage)return false;
 if(static_cast<const NodeCombSepColor *>(root->fromnode->storage)->mode!=NODE_COMBSEP_COLOR_RGB)return false;
 const auto *color=bke::node_find_enabled_input_socket(*root->fromnode,"Color");const auto *image_link=color?linked(*color):nullptr;
 if(!image_link||!STREQ(image_link->fromnode->idname,"ShaderNodeTexImage")||!STREQ(image_link->fromsock->identifier,"Color")||!image_link->fromnode->id||GS(image_link->fromnode->id->name)!=ID_IM||!image_link->fromnode->storage)return false;
 const auto &image=*reinterpret_cast<const Image *>(image_link->fromnode->id);const auto &settings=*static_cast<const NodeTexImage *>(image_link->fromnode->storage);
 if(image.source!=IMA_SRC_FILE||image.type!=IMA_TYPE_IMAGE||BLI_listbase_count(&image.tiles)!=1||!IMB_colormanagement_space_name_is_data(image.colorspace_settings.name))return false;
 if(settings.projection!=SHD_PROJ_FLAT||(settings.interpolation!=SHD_INTERP_LINEAR&&settings.interpolation!=SHD_INTERP_CLOSEST)||(settings.extension!=SHD_IMAGE_EXTENSION_REPEAT&&settings.extension!=SHD_IMAGE_EXTENSION_EXTEND))return false;
 const auto dependencies=native_material_dependencies(*prepared.tree,prepared.source->description.parameters.size());
 if(!dependencies.uv||dependencies.surface||dependencies.object||dependencies.view||dependencies.ao)return false;
 if(selected)*selected=&image;return true;
}
void require_original_global_context(const blender::bNodeTree &root){
 const auto requirements=original_global_requirements(root);
 if(requirements.global_blackbody||requirements.global_wavelength||requirements.global_luminance)throw std::invalid_argument("Raw authored Height excludes global-color functions and Color-to-Float conversions in the original selected material and referenced groups, including unused or ignored branches; the ordinary source is unchanged");
}
}
NativeHeightRequirements native_height_requirements(const PreparedNativeRawHeight &prepared){
 using blender::ID_Type; // GS uses this native enum in its macro expansion.
 if(!prepared.source||!prepared.tree)throw std::invalid_argument("Raw authored Height has no prepared scalar graph");
 std::set<const blender::bNode *> visited;auto requirements=original_global_requirements(*prepared.source->material->nodetree);
 const auto procedural=[&](auto &&self,const blender::bNode &node)->void{
  if(!visited.insert(&node).second)return;
  if(STREQ(node.idname,"ShaderNodeCameraData"))requirements.view=true;
  if(STREQ(node.idname,"ShaderNodeAmbientOcclusion"))requirements.ao=true;
  if(STREQ(node.idname,"ShaderNodeAttribute")&&node.storage){
   const std::string_view name(static_cast<const blender::NodeShaderAttribute *>(node.storage)->name);
   if(name=="BTS_Geometry_Incoming"||name=="BTS_Geometry_Backfacing"||name=="BTS_Coord_Camera"||name=="BTS_Coord_Window"||name=="BTS_Coord_Reflection")requirements.view=true;
  }
  if(STREQ(node.idname,"ShaderNodeTexImage")||STREQ(node.idname,"ShaderNodeTexEnvironment")||(node.id&&GS(node.id->name)==blender::ID_IM))requirements.images=true;
  // Its native callback bakes process-global luminance coefficients. Do not
  // silently treat those as this immutable graph's recorded color context.
  if(STREQ(node.idname,"ShaderNodeRGBToBW"))requirements.global_luminance=true;
  if(STREQ(node.idname,"ShaderNodeBlackbody"))requirements.global_blackbody=true;
  if(STREQ(node.idname,"ShaderNodeWavelength"))requirements.global_wavelength=true;
  for(const auto &socket:node.inputs)if(socket.link&&socket.link->fromnode){
   if(socket.type==blender::SOCK_FLOAT&&socket.link->fromsock&&socket.link->fromsock->type==blender::SOCK_RGBA)requirements.global_luminance=true;
   self(self,*socket.link->fromnode);
  }
 };
 for(const auto &node:prepared.tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV"))procedural(procedural,node);
 const auto dependencies=native_material_dependencies(*prepared.tree,prepared.source->description.parameters.size());
 requirements.view|=dependencies.view;requirements.ao|=dependencies.ao;
 if(requirements.images)requirements.scalar_image=scalar_image_height(prepared);
 return requirements;
}
NativeMaterialHeightResource prepare_native_material_height_resource(
 const StudioAsset &asset,const std::vector<StudioAsset> &resources,const std::atomic_bool &cancel,NativeWorldNormalSurfaceContract surface_contract){
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Native Height preparation requires a worker");
 if(cancel.load())throw std::runtime_error("Native Height preparation cancelled");
 if(!is_native_material_asset(asset)||asset.bytes.isEmpty()||!asset.fill_preset||!asset.fill_preset->native_material)throw std::invalid_argument("Native Height requires an immutable native source archive/interface");
 require_native_source_policy(*asset.fill_preset->native_material);
 asset_metadata_read(asset_metadata(asset));validate_native_material_dependencies(asset,resources);
 const auto bundle=read_native_material_bundle(asset.bytes);auto scene=asset;scene.bytes=bundle.blend;
 auto detached=read_native_material_source(materialize_native_material_archive(scene,cancel),asset.fill_preset->native_material->material,cancel);
 bind_native_material_source_images(*detached.main,bundle,resources,cancel);
 require_native_height_no_drivers(*detached.main);
 blender::BKE_ntree_update_after_single_tree_change(*detached.main,*detached.material->nodetree);
 require_original_global_context(*detached.material->nodetree);
 auto prepared=prepare_native_raw_height(std::move(detached),cancel,asset.fill_preset->native_material.get(),surface_contract);
 auto expected=native_material_description_json(*asset.fill_preset->native_material),actual=native_material_description_json(prepared.source->description);
 // Same source-interface validation as ordinary immutable resource hydration.
 // A persisted instance policy is never allowed to weaken this source check.
 for(const auto *field:{"warnings","channels","color_to_aces","color_from_aces"}){expected.remove(field);actual.remove(field);}
 if(expected!=actual)throw std::invalid_argument("Native Height source interface does not match its immutable metadata");
 const auto requirements=native_height_requirements(prepared);validate_native_height_requirements(requirements);
 // Height-only images are not ordinary source-channel dependencies. Decode
 // this immutable extra root on the worker without adding a source capability.
 if(requirements.images){
  const blender::Image *image=nullptr;if(!scalar_image_height(prepared,&image)||!image)throw std::logic_error("Admitted Height image closure changed before decode");
  const bool packed_bytes=native_height_has_packed_image_bytes(image->packedfiles);
  require_native_height_image_retention(QString::fromUtf8(image->id.name+2),packed_bytes,bundle.images,[&](const QString &hash){return std::any_of(resources.begin(),resources.end(),[&](const auto &source){return source.usages.contains("texture")&&source.hash()==hash;});});
  std::array<blender::bNodeTree *,1> roots{prepared.tree};prepare_native_material_images(*prepared.source,cancel,roots,false);
 }
 const auto dependencies=native_material_dependencies(*prepared.tree,prepared.source->description.parameters.size());
 if(cancel.load())throw std::runtime_error("Native Height preparation cancelled");
 return {asset.hash(),std::move(prepared),dependencies};
}
}
