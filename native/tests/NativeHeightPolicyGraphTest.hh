// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialRawHeightFixture.hh"
#include "NativeMaterialHeightResource.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialAsset.hh"
#include "BlendMaterial.hh"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include <future>
#include <QFile>
#include <QJsonDocument>

namespace bts {
// CPU diagnostic only. The ordinary native inspection CPU suite calls this
// once; no interactive layer evaluation or CPU native composite is introduced.
inline QJsonObject verify_native_height_policy_graph(){
 using namespace blender;
 using namespace native_raw_height_test;
 require(!GPU_context_active_get(),"Height policy graph regressions require no GPU context");
 const auto compiled=GPU_pass_global_compilation_count();
 auto result=std::async(std::launch::async,[]{
  QJsonArray cases;std::atomic_bool cancel=false;
  for(const auto &kind:{QString("scalar"),QString("image"),QString("view"),QString("incoming"),QString("camera-coordinates"),QString("reflection"),QString("ao"),QString("rgb-luma"),QString("implicit-luma"),QString("blackbody"),QString("unrelated-blackbody"),QString("unrelated-main-blackbody"),QString("folded-luma"),QString("wavelength")}){
   auto source=fixture(Kind::Constant,.5f);auto *group=static_cast<bNodeTree *>(nullptr);auto *bump_node=static_cast<bNode *>(nullptr);
   for(auto &tree:source.main->nodetrees)for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeBump")){require(!bump_node,"Synthetic Height fixture unexpectedly has multiple Bumps");group=&tree;bump_node=&node;}
   require(group&&bump_node,"Synthetic Height fixture has no Bump");
   if(kind=="unrelated-blackbody"){
    bke::node_add_node(nullptr,*group,UString("ShaderNodeBlackbody"));
    BKE_ntree_update_after_single_tree_change(*source.main,*group);BKE_ntree_update_after_single_tree_change(*source.main,*source.material->nodetree);
   }
   if(kind=="unrelated-main-blackbody"){
    auto *other=BKE_material_add(source.main.get(),"Unrelated Main material");bke::node_add_node(nullptr,*other->nodetree,UString("ShaderNodeBlackbody"));
    BKE_ntree_update_after_single_tree_change(*source.main,*other->nodetree);
   }
   if(kind!="scalar"&&kind!="unrelated-blackbody"&&kind!="unrelated-main-blackbody"){
    const auto add=[&](const char *type){return bke::node_add_node(nullptr,*group,UString(type));};
    bNode *tail=nullptr;const char *output=nullptr;
    if(kind=="image"){tail=add("ShaderNodeTexImage");output="Alpha";}
    else if(kind=="view"){tail=add("ShaderNodeCameraData");output="View Distance";}
    else if(kind=="incoming"||kind=="camera-coordinates"||kind=="reflection"){
     auto *coordinates=add(kind=="incoming"?"ShaderNodeNewGeometry":"ShaderNodeTexCoord");tail=add("ShaderNodeSeparateXYZ");
     bke::node_add_link(*group,*coordinates,socket(*coordinates,kind=="incoming"?"Incoming":kind=="reflection"?"Reflection":"Camera",false),*tail,socket(*tail,"Vector",true));output="Z";
    }
    else if(kind=="ao"){tail=add("ShaderNodeAmbientOcclusion");output="AO";}
    else if(kind=="rgb-luma"||kind=="implicit-luma"){
     auto *rgb=add("ShaderNodeRGB");tail=add(kind=="rgb-luma"?"ShaderNodeRGBToBW":"ShaderNodeMath");
     bke::node_add_link(*group,*rgb,socket(*rgb,"Color",false),*tail,socket(*tail,kind=="rgb-luma"?"Color":"Value",true));output=kind=="rgb-luma"?"Val":"Value";
    }else if(kind=="folded-luma"){
     auto *color=add("ShaderNodeCombineColor");tail=add("ShaderNodeMath");
     bke::node_add_link(*group,*color,socket(*color,"Color",false),*tail,socket(*tail,"Value",true));output="Value";
    }else{
     auto *rgb=add(kind=="blackbody"?"ShaderNodeBlackbody":"ShaderNodeWavelength");tail=add("ShaderNodeSeparateXYZ");
     bke::node_add_link(*group,*rgb,socket(*rgb,"Color",false),*tail,socket(*tail,"Vector",true));output="X";
    }
    auto &height=socket(*bump_node,"Height",true);if(height.link)bke::node_remove_link(group,*height.link);
    bke::node_add_link(*group,*tail,socket(*tail,output,false),*bump_node,height);
    BKE_ntree_update_after_single_tree_change(*source.main,*group);BKE_ntree_update_after_single_tree_change(*source.main,*source.material->nodetree);
   }
   const auto before=source_state(*source.material->nodetree);
   auto prepared=prepare_native_raw_height(std::move(source),cancel);
   require(source_state(*prepared.source->material->nodetree)==before,"Height policy graph admission changed original source links/values");
   const auto description=native_material_description_json(prepared.source->description);
   require(prepared.source->description.height_policy==NativeHeightPolicy::None,"Height proof activated a source policy");
   require(!description.contains("height_policy"),"Height proof persisted source interpretation");
   const auto &channels=prepared.source->description.channels;require(std::find(channels.begin(),channels.end(),4)==channels.end(),"Active Height silently changed default Bump capabilities");
   const auto requirements=native_height_requirements(prepared);
   if(kind=="image")require(requirements.images,"Actual image closure was not identified");
   if(kind=="view"||kind=="incoming"||kind=="camera-coordinates"||kind=="reflection")require(requirements.view,"Actual Camera closure was not identified");
   if(kind=="ao")require(requirements.ao,"Actual AO closure was not identified");
   if(kind=="rgb-luma"||kind=="implicit-luma"||kind=="folded-luma")require(requirements.global_luminance,"Actual native luminance conversion was not identified");
   if(kind=="blackbody"&&!requirements.global_blackbody&&!qEnvironmentVariableIsEmpty("BTS_NATIVE_HEIGHT_DIAGNOSTIC")){
    QFile diagnostic(qEnvironmentVariable("BTS_NATIVE_HEIGHT_DIAGNOSTIC"));require(diagnostic.open(QIODevice::WriteOnly),"Cannot save raw Height eligibility diagnostic");
    diagnostic.write(QJsonDocument(QJsonObject{{"case",kind},{"original_before",before},{"original_after",source_state(*prepared.source->material->nodetree)},{"prepared_raw",source_state(*prepared.tree)},{"raw_height_node",QString::fromStdString(prepared.provenance.height_node)},{"global_blackbody",requirements.global_blackbody}}).toJson());
   }
   if(kind=="blackbody"||kind=="unrelated-blackbody")require(requirements.global_blackbody,"Actual Blackbody closure was not identified");
   if(kind=="wavelength")require(requirements.global_wavelength,"Actual Wavelength closure was not identified");
   bool rejected=false;try{validate_native_height_requirements(requirements);}catch(const std::invalid_argument &){rejected=true;}
   require(rejected==(kind!="scalar"&&kind!="unrelated-main-blackbody"),"Actual Height eligibility did not match its explicit procedural-only contract");
   require(source_state(*prepared.source->material->nodetree)==before,"Read-only Height requirements changed source state");
   cases.append(QJsonObject{{"case",kind},{"rejected",rejected},{"source_unchanged",true},{"default_policy_none",true},{"default_bump_unavailable",true}});
  }
  QJsonObject actual;
  if(!qEnvironmentVariableIsEmpty("BTS_NATIVE_HEIGHT_CONFETTI")){
   const auto path=qEnvironmentVariable("BTS_NATIVE_HEIGHT_CONFETTI");
   std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(reference.get());
   auto graph=prepare_native_material_graph(*reference,path,"Particle-Confetti",cancel);
   const auto before=source_state(*graph->material->nodetree),description=native_material_description_json(graph->description);
   auto archive=archive_native_material(*graph,path,cancel,true);auto asset=native_material_asset("Particle-Confetti",archive.bytes,archive.description);asset.scope=AssetScope::Project;
   const auto metadata=asset_metadata(asset);const auto hash=asset.hash();
   auto resource=prepare_native_material_height_resource(asset,archive.textures,cancel);
   require(source_state(*graph->material->nodetree)==before&&native_material_description_json(graph->description)==description,"Actual raw worker preparation changed its original source/defaults");
   require(asset_metadata(asset)==metadata&&asset.hash()==hash&&graph->description.height_policy==NativeHeightPolicy::None,"Actual raw worker preparation changed its immutable archive/default None");
   require(std::find(graph->description.channels.begin(),graph->description.channels.end(),4)==graph->description.channels.end(),"Actual worker silently enabled source Bump");
   actual={{"material","Particle-Confetti"},{"worker_resource_ready",true},{"default_none_and_caps_unchanged",true},{"source_unchanged",true},{"source_hash",hash},{"height_socket",QString::fromStdString(resource.provenance().height_socket)}};
  }
  return QJsonObject{{"cases",cases},{"actual_confetti",actual},{"cpu_native_graph_only",true}};
 }).get();
 require(!GPU_context_active_get()&&GPU_pass_global_compilation_count()==compiled,"Height CPU admission compiled or activated GPU programs");
 return result;
}
}
