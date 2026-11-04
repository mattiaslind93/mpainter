#pragma once
// Permanent synthetic native CPU regression. Caller supplies the initialized
// application/host on its owner thread; this helper constructs neither.
#include "NativeMaterialFragmentNormalFixture.hh"
#include "NativeMaterialRamp.hh"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>
#include <future>
#include <algorithm>
#include <set>
#include <vector>
#include <string_view>

namespace bts::native_fragment_normal_test::detail {
inline auto work(auto function){auto result=std::async(std::launch::async,std::move(function));QElapsedTimer clock;clock.start();while(result.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){QCoreApplication::processEvents();QThread::msleep(1);require(clock.elapsed()<120000,"Fragment Normal worker timed out");}return result.get();}
inline QString hash(const QJsonObject &value){return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());}
inline std::set<const blender::bNode *> reachable(const blender::bNodeTree &tree,const blender::bNode &root){
 std::set<const blender::bNode *> found;std::vector<const blender::bNode *> pending{&root};while(!pending.empty()){const auto *node=pending.back();pending.pop_back();if(!found.insert(node).second)continue;for(const auto &edge:tree.links)if(edge.tonode==node)pending.push_back(edge.fromnode);}return found;
}
}
namespace bts {
inline QJsonObject verify_native_fragment_normal_graph(){
 using namespace native_fragment_normal_test;
 using native_fragment_normal_test::detail::work;
 using native_fragment_normal_test::detail::hash;
 using native_fragment_normal_test::detail::reachable;
 require(QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread(),"Normal graph regression requires the initialized application owner");
  require(!blender::GPU_context_active_get(),"CPU-only test acquired a GPU context");const auto compiles=blender::GPU_pass_global_compilation_count();std::atomic_bool cancel=false;QJsonArray cases;
  for(Kind kind:{Kind::Unlinked,Kind::ActiveBump,Kind::NormalMap,Kind::Combined,Kind::Grouped,Kind::Ramp,Kind::Emission,Kind::DiffuseSurface,Kind::UnsupportedContext}){
   // Snapshot the exact owned source before extraction. A separately prepared
   // ordinary graph is the independent default capabilities/parameters oracle.
   auto detached=work([&]{return fixture(kind);});const auto before=source_state(*detached.material->nodetree);const auto before_hash=hash(before);
   auto ordinary=work([&]{return finish_native_material_source(fixture(kind),cancel);});const auto expected=native_material_description_json(ordinary->description);const auto values=ordinary->parameters;const auto ordinary_source=source_state(*ordinary->material->nodetree);
   require(std::find(ordinary->description.channels.begin(),ordinary->description.channels.end(),3)==ordinary->description.channels.end(),"Ordinary source advertised Normal3");
   const bool eligible=kind!=Kind::Emission&&kind!=Kind::UnsupportedContext;bool rejected=false,api_returned=false;QString reason;QJsonObject result;
   try {
    auto prepared=work([&]{return prepare_native_fragment_normal(std::move(detached),cancel);});api_returned=true;require(eligible,"Unsupported fragment Normal source admitted");auto &graph=*prepared.source;
    require(graph.main&&graph.material&&prepared.tree&&prepared.tree!=graph.material->nodetree,"Private owned AOV tree missing");require(blender::BLI_findindex(&graph.main->nodetrees,&prepared.tree->id)>=0,"Normal AOV tree is not owned by the detached Main");
    require(graph.original_inputs_preserved&&source_state(*graph.material->nodetree)==before&&hash(source_state(*graph.material->nodetree))==before_hash,"Normal extraction changed original defaults/links/storage/hash");
    require(native_material_description_json(graph.description)==expected&&graph.parameters==values,"Normal extraction changed source parameters/policy/default capabilities");
    require(graph.channels.size()==6&&graph.image_resources.empty()&&graph.description.height_policy==NativeHeightPolicy::None,"Normal extraction activated policy, image decoding or default channels");
    for(const auto &channel:graph.channels)require(channel.channel!=3,"Private Normal tree leaked into default channel list");
    blender::bNode *aov=nullptr;int roots=0;for(auto &node:prepared.tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV")){aov=&node;++roots;}
    require(roots==1&&aov&&STREQ(static_cast<blender::NodeShaderOutputAOV *>(aov->storage)->name,"BTS_Channel"),"Private COLOR AOV root identity differs");
    auto &color=socket(*aov,"Color",true);auto &scalar=socket(*aov,"Value",true);require(color.link&&!scalar.link&&color.link->fromsock->type==blender::SOCK_VECTOR,"Normal was lowered as scalar, encoded or disconnected");
    const auto nodes=reachable(*prepared.tree,*aov);int bumps=0,maps=0,geometry=0,parameters=0,mapping_rows=0,ramps=0;
    for(const auto *node:nodes){if(STREQ(node->idname,"ShaderNodeBump")){++bumps;require(socket(*const_cast<blender::bNode *>(node),"Height",true).link,"Active Bump lost its authored Height");}if(STREQ(node->idname,"ShaderNodeNormalMap"))++maps;if(STREQ(node->idname,"ShaderNodeValToRGB")){++ramps;require(node->custom2==blender::GPU_HOSTED_RAMP_TAG,"Existing native editable-ramp adapter was lost");}if(STREQ(node->idname,"ShaderNodeAttribute")){const auto *name=static_cast<const blender::NodeShaderAttribute *>(node->storage)->name;geometry+=STREQ(name,"BTS_Geometry_Normal");parameters+=std::string_view(name).starts_with("BTS_Param");mapping_rows+=std::string_view(name).starts_with(native_mapping_attribute_prefix);}}
    const int expected_bumps=(kind==Kind::ActiveBump||kind==Kind::Combined||kind==Kind::Grouped||kind==Kind::Ramp)?1:0,expected_maps=(kind==Kind::NormalMap||kind==Kind::Combined||kind==Kind::Grouped)?1:0;
    require(bumps==expected_bumps&&maps==expected_maps,"Actual Bump/Normal Map chain was omitted or duplicated");
    if(kind==Kind::Unlinked||kind==Kind::DiffuseSurface)require(geometry==1&&STREQ(color.link->fromnode->idname,"ShaderNodeAttribute"),"Unlinked Normal used socket default instead of geometry");
    else require(STREQ(color.link->fromsock->identifier,"Normal"),"World Normal was encoded or inverted into a scalar surrogate");
    if(kind==Kind::ActiveBump||kind==Kind::Combined){require(parameters>=2&&mapping_rows>=3,"Normal path lost existing parameter/Mapping adapters");for(const auto *node:nodes)if(STREQ(node->idname,"ShaderNodeBump")){require(node->custom1==1&&static_cast<const blender::bNodeSocketValueFloat *>(socket(*const_cast<blender::bNode *>(node),"Distance",true).default_value)->value==-.7f&&static_cast<const blender::bNodeSocketValueFloat *>(socket(*const_cast<blender::bNode *>(node),"Filter Width",true).default_value)->value==2,"Bump controls were interpreted as raw H or changed");}}
    if(kind==Kind::Ramp)require(ramps==1&&parameters>=1&&mapping_rows>=3,"Normal path lost existing ramp/parameter/Mapping bindings");
    result={{"source_sha256",before_hash},{"bumps",bumps},{"normal_maps",maps},{"editable_ramps",ramps},{"geometry_normal",geometry},{"parameter_attributes",parameters},{"mapping_attributes",mapping_rows},{"color_aov_world_vector",true},{"original_source_and_default_capabilities_unchanged",true}};
    // Source2 extension: the private inlined Normal Map must preserve the
    // actual UV identity and controls, not only keep a node of the same type.
    if(kind==Kind::NormalMap){
     const blender::bNode *normal=nullptr;for(const auto *node:nodes)if(STREQ(node->idname,"ShaderNodeNormalMap"))normal=node;
     require(normal&&normal->storage,"Private Normal Map storage missing");const auto &storage=*static_cast<const blender::NodeShaderNormalMap *>(normal->storage);
     require(STREQ(storage.uv_map,"FixtureUV")&&storage.space==blender::SHD_SPACE_TANGENT&&storage.convention==blender::SHD_NORMAL_MAP_CONVENTION_OPENGL&&storage.base==blender::SHD_NORMAL_MAP_BASE_DISPLACED,"Private Normal Map UV/space/convention/base changed");
     const auto &strength=socket(*const_cast<blender::bNode *>(normal),"Strength",true),&input_color=socket(*const_cast<blender::bNode *>(normal),"Color",true);
     require(!strength.link&&!input_color.link&&strength.default_value&&input_color.default_value,"Private Normal Map literal controls were replaced");
     const auto value=static_cast<const blender::bNodeSocketValueFloat *>(strength.default_value)->value;const auto *rgba=static_cast<const blender::bNodeSocketValueRGBA *>(input_color.default_value)->value;
     require(value==.65f&&rgba[0]==.2f&&rgba[1]==.7f&&rgba[2]==1.f&&rgba[3]==1.f,"Private Normal Map Strength/Color/alpha changed");
     result["normal_map_controls"]=QJsonObject{{"uv_map",storage.uv_map},{"space",storage.space},{"convention",int(storage.convention)},{"base",int(storage.base)},{"strength",value},{"color",QJsonArray{rgba[0],rgba[1],rgba[2],rgba[3]}}};
    }
   }catch(const std::exception &error){if(eligible||api_returned)throw;rejected=true;reason=QString::fromUtf8(error.what());}
   // Production worker preparation is optional, typed, and reuses this exact
   // exclusively owned Main. Flat inputs do not allocate another runtime copy.
   const auto bump=std::find_if(ordinary->channels.begin(),ordinary->channels.end(),[](const auto &c){return c.channel==4;});
   const bool perturbation=kind==Kind::ActiveBump||kind==Kind::NormalMap||kind==Kind::Combined||kind==Kind::Grouped||kind==Kind::Ramp||kind==Kind::UnsupportedContext;
   require(bump!=ordinary->channels.end()&&bump->normal_perturbed==perturbation,"Typed effective source Normal classification differs");
   const auto materials_before=blender::BLI_listbase_count(&ordinary->main->materials),trees_before=blender::BLI_listbase_count(&ordinary->main->nodetrees);const auto *owner_before=ordinary->main.get();
   work([&]{prepare_optional_native_world_normal(ordinary,cancel);return true;});
   require(ordinary->main.get()==owner_before&&ordinary->world_normal_perturbed==perturbation,"Optional world preparation replaced source owner/classification");
   if(!perturbation)require(!ordinary->world_normal_tree&&ordinary->world_normal_error.isEmpty()&&blender::BLI_listbase_count(&ordinary->main->materials)==materials_before&&blender::BLI_listbase_count(&ordinary->main->nodetrees)==trees_before,"Flat native source allocated a world Normal tree/Main copy");
   else if(eligible)require(ordinary->world_normal_tree&&ordinary->world_normal_error.isEmpty(),"Supported private world Normal tree not retained");
   else require(!ordinary->world_normal_tree&&!ordinary->world_normal_error.isEmpty(),"Unsupported optional world Normal lacks a specific diagnostic");
   require(native_material_description_json(ordinary->description)==expected&&source_state(*ordinary->material->nodetree)==ordinary_source,"Separate default graph changed during private extraction");
   require(rejected!=eligible,"Fragment Normal fixture admission differs");cases.append(QJsonObject{{"kind",int(kind)},{"eligible",eligible},{"reason",reason},{"result",result}});
  }
  bool cancelled=false;try{work([&]{std::atomic_bool stop=true;return prepare_native_fragment_normal(fixture(Kind::Combined),stop);});}catch(const std::runtime_error &){cancelled=true;}require(cancelled,"Cancelled Normal preparation admitted");
  auto owner_input=work([&]{return fixture(Kind::Unlinked);});bool owner_rejected=false;try{prepare_native_fragment_normal(std::move(owner_input),cancel);}catch(const std::logic_error &){owner_rejected=true;}require(owner_rejected,"Normal preparation accepted QApplication thread");
  bool missing=false;try{work([&]{return prepare_native_fragment_normal({},cancel);});}catch(const std::invalid_argument &){missing=true;}require(missing,"Missing detached source admitted");
  // The import worker may already have prepared channels/images. Reuse its
  // exact Main and immutable defaults; never import or decode the source twice.
  const auto reused=work([&]{
   auto graph=finish_native_material_source(fixture(Kind::Combined),cancel);
   const auto *main=graph->main.get();const auto *material=graph->material;
   const auto defaults=native_material_description_json(graph->description);
   const auto original=source_state(*graph->material->nodetree);
   auto prepared=prepare_owned_native_fragment_normal(graph,cancel);
   require(prepared.source==graph&&prepared.source->main.get()==main&&prepared.source->material==material,"Owned Normal preparation replaced its source Main/material");
   require(native_material_description_json(prepared.source->description)==defaults&&source_state(*prepared.source->material->nodetree)==original,"Owned Normal preparation changed source defaults");
   const auto before=source_state(*prepared.source->material->nodetree);
   auto alias=prepared.source;bool shared_rejected=false;
   try{prepare_owned_native_fragment_normal(prepared.source,cancel);}catch(const std::invalid_argument &error){shared_rejected=std::string_view(error.what()).find("exclusive unpublished")!=std::string_view::npos;}
   require(shared_rejected&&source_state(*alias->material->nodetree)==before,"Shared Normal source was mutated or admitted");
   auto emission=finish_native_material_source(fixture(Kind::Emission),cancel);
   const auto emission_state=source_state(*emission->material->nodetree);
   const auto emission_defaults=native_material_description_json(emission->description);
   bool unsupported=false;try{prepare_owned_native_fragment_normal(emission,cancel);}catch(const std::exception &){unsupported=true;}
   require(unsupported&&emission&&emission->main&&source_state(*emission->material->nodetree)==emission_state&&native_material_description_json(emission->description)==emission_defaults,"Optional Normal failure consumed or changed an otherwise usable source");
   return true;
  });
  const bool world_displacement_rejected=work([&]{
   using namespace blender;auto source=fixture(Kind::Combined);auto &tree=*source.material->nodetree;
   bNode *end=nullptr;for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeOutputMaterial"))end=&node;
   require(end,"World displacement fixture output missing");auto *value=bke::node_add_node(nullptr,tree,UString("ShaderNodeValue"));
   static_cast<bNodeSocketValueFloat *>(socket(*value,"Value",false).default_value)->value=.2f;
   bke::node_add_link(tree,*value,socket(*value,"Value",false),*end,socket(*end,"Displacement",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);
   auto graph=finish_native_material_source(std::move(source),cancel);const auto original=source_state(*graph->material->nodetree);const auto defaults=native_material_description_json(graph->description);
   prepare_optional_native_world_normal(graph,cancel);
   require(graph->world_normal_perturbed&&!graph->world_normal_tree&&graph->world_normal_error.contains("authored Displacement"),"Authored displacement silently used an undisplaced world frame");
   require(source_state(*graph->material->nodetree)==original&&native_material_description_json(graph->description)==defaults,"World displacement rejection changed ordinary source/default capabilities");return true;
  });
  require(!blender::GPU_context_active_get()&&blender::GPU_pass_global_compilation_count()==compiles,"CPU extraction compiled GPU state");
  QJsonObject proof{{"complete",true},{"scope","Worker graph extraction only; no GPU compilation/readback/image decode or production admission"},{"cases",cases},{"cancel_owner_missing_rejected",true},{"default_channels",QJsonArray{0,1,2,4,5,14}},{"artist_normal_enabled",false},{"active_bump_product_enabled",false},{"exclusive_source_reuse_and_shared_rejection",reused},{"optional_world_worker_typed_and_flat_fast_path",true},{"world_displacement_rejected",world_displacement_rejected}};
  return proof;
}
}
