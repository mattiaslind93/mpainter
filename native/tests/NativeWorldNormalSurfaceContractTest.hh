// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// CPU-only effective Painter surface admission and immutable source witnesses.
#include "NativeMaterialFragmentNormalFixture.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeWorldNormalPlan.hh"
#include "NativeWorldNormalSurface.hh"
#include "NativeMaterialSourcePolicy.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialResource.hh"
#include "NativeMaterialHeightResource.hh"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialSourceFiles.hh"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>
#include <array>
#include <future>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace bts::native_world_surface_contract_test {
using native_fragment_normal_test::require;
using native_fragment_normal_test::socket;
enum class Kind {Bump,MapOriginal,MapDisplaced,Grouped,MutedCombined,SharedHeight,Flat};
inline const char *name(Kind k){switch(k){case Kind::Bump:return "bump";case Kind::MapOriginal:return "normal-map-original";case Kind::MapDisplaced:return "normal-map-displaced";case Kind::Grouped:return "grouped-normal";case Kind::MutedCombined:return "muted-bump-bypass";case Kind::SharedHeight:return "shared-height";case Kind::Flat:return "flat-normal";}throw std::logic_error("Invalid surface fixture");}
inline auto work(auto function){auto result=std::async(std::launch::async,std::move(function));QElapsedTimer clock;clock.start();while(result.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){QCoreApplication::processEvents();QThread::msleep(1);require(clock.elapsed()<120000,"Surface contract worker timed out");}return result.get();}
inline QString hash(const QJsonObject &value){return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());}
inline QJsonObject source_state(const blender::Material &material){
 using namespace blender;auto result=native_fragment_normal_test::source_state(*material.nodetree);QJsonArray outputs;std::set<const bNodeTree *> seen;
 const auto visit=[&](auto &&self,const bNodeTree &tree)->void{if(!seen.insert(&tree).second)return;for(const auto &node:tree.nodes){for(const auto &out:node.outputs){QJsonArray value;if(out.default_value){if(out.type==SOCK_FLOAT)value.append(static_cast<const bNodeSocketValueFloat *>(out.default_value)->value);else if(out.type==SOCK_VECTOR)for(float v:static_cast<const bNodeSocketValueVector *>(out.default_value)->value)value.append(v);else if(out.type==SOCK_RGBA)for(float v:static_cast<const bNodeSocketValueRGBA *>(out.default_value)->value)value.append(v);}outputs.append(QJsonObject{{"tree",tree.id.name},{"node",node.name},{"socket",out.identifier},{"flag",int(out.flag)},{"value",value}});}if(STREQ(node.idname,"ShaderNodeGroup")&&node.id)self(self,*reinterpret_cast<const bNodeTree *>(node.id));}};
 visit(visit,*material.nodetree);result["output_defaults"]=outputs;result["material_displacement_method"]=int(material.displacement_method);return result;
}
inline DetachedNativeMaterial fixture(Kind kind){
 using namespace blender;DetachedNativeMaterial source;
 if(kind==Kind::Grouped)source=native_fragment_normal_test::fixture(native_fragment_normal_test::Kind::Grouped);
 else{source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Effective Painter surface fixture");auto &tree=*source.material->nodetree;auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));
  bNode *normal=nullptr;
  if(kind==Kind::MapOriginal||kind==Kind::MapDisplaced||kind==Kind::MutedCombined)normal=bke::node_add_node(nullptr,tree,UString("ShaderNodeNormalMap"));
  if(kind==Kind::Bump||kind==Kind::MutedCombined||kind==Kind::SharedHeight){auto *bump=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));auto *height=bke::node_add_node(nullptr,tree,UString("ShaderNodeValue"));STRNCPY(height->name,"Shared authored Height witness");static_cast<bNodeSocketValueFloat *>(socket(*height,"Value",false).default_value)->value=.37f;bke::node_add_link(tree,*height,socket(*height,"Value",false),*bump,socket(*bump,"Height",true));bump->custom1=1;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Strength",true).default_value)->value=.35f;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Distance",true).default_value)->value=-.4f;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Filter Width",true).default_value)->value=.75f;if(normal)bke::node_add_link(tree,*normal,socket(*normal,"Normal",false),*bump,socket(*bump,"Normal",true));if(kind==Kind::MutedCombined)bump->flag|=NODE_MUTED;normal=bump;}
  if(normal)bke::node_add_link(tree,*normal,socket(*normal,"Normal",false),*pbr,socket(*pbr,"Normal",true));
 }
 auto &tree=*source.material->nodetree;bNode *out=nullptr,*shared=nullptr;for(auto &node:tree.nodes){if(STREQ(node.idname,"ShaderNodeOutputMaterial"))out=&node;if(STREQ(node.name,"Shared authored Height witness"))shared=&node;}require(out,"Surface fixture lacks its Material Output");
 auto *height=kind==Kind::SharedHeight?shared:bke::node_add_node(nullptr,tree,UString("ShaderNodeValue"));require(height,"Surface fixture lacks its authored displacement Height");if(kind!=Kind::SharedHeight){STRNCPY(height->name,"Displacement-only Height witness");static_cast<bNodeSocketValueFloat *>(socket(*height,"Value",false).default_value)->value=.29f;}
 auto *disp=bke::node_add_node(nullptr,tree,UString("ShaderNodeDisplacement"));disp->custom1=SHD_SPACE_OBJECT;static_cast<bNodeSocketValueFloat *>(socket(*disp,"Midlevel",true).default_value)->value=.17f;static_cast<bNodeSocketValueFloat *>(socket(*disp,"Scale",true).default_value)->value=.83f;bke::node_add_link(tree,*height,socket(*height,"Value",false),*disp,socket(*disp,"Height",true));bke::node_add_link(tree,*disp,socket(*disp,"Displacement",false),*out,socket(*out,"Displacement",true));source.material->displacement_method=MA_DISPLACEMENT_BOTH;
 std::set<bNodeTree *> seen;const auto maps=[&](auto &&self,bNodeTree &t)->void{if(!seen.insert(&t).second)return;for(auto &node:t.nodes){if(STREQ(node.idname,"ShaderNodeNormalMap")){auto &settings=*static_cast<NodeShaderNormalMap *>(node.storage);settings.space=SHD_SPACE_TANGENT;settings.base=kind==Kind::MapOriginal||kind==Kind::MutedCombined?SHD_NORMAL_MAP_BASE_ORIGINAL:SHD_NORMAL_MAP_BASE_DISPLACED;settings.convention=SHD_NORMAL_MAP_CONVENTION_OPENGL;STRNCPY(settings.uv_map,"FixtureUV");static_cast<bNodeSocketValueFloat *>(socket(node,"Strength",true).default_value)->value=.65f;auto *rgba=static_cast<bNodeSocketValueRGBA *>(socket(node,"Color",true).default_value);rgba->value[0]=.2f;rgba->value[1]=.7f;rgba->value[2]=1;rgba->value[3]=1;}if(STREQ(node.idname,"ShaderNodeGroup")&&node.id)self(self,*reinterpret_cast<bNodeTree *>(node.id));}BKE_ntree_update_after_single_tree_change(*source.main,t);};maps(maps,tree);BKE_ntree_update_after_single_tree_change(*source.main,tree);return source;
}
inline std::set<const blender::bNode *> reachable(const blender::bNodeTree &tree,const blender::bNode &root){std::set<const blender::bNode *> result;std::vector<const blender::bNode *> pending{&root};while(!pending.empty()){const auto *node=pending.back();pending.pop_back();if(!result.insert(node).second)continue;for(const auto &edge:tree.links)if(edge.tonode==node)pending.push_back(edge.fromnode);}return result;}
}

namespace bts {
inline QJsonObject verify_native_world_normal_surface_contract(){
 using namespace native_world_surface_contract_test;using Contract=NativeWorldNormalSurfaceContract;
 static_assert(std::is_const_v<decltype(std::declval<NativeMaterialGraph>().world_normal_surface_contract)>);
 static_assert(std::is_const_v<decltype(std::declval<NativeMaterialGraph>().world_normal_source_displacement)>);
 require(QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread(),"Surface fixture requires initialized CPU owner");
 require(!blender::GPU_context_active_get(),"Surface CPU fixture entered with a GPU context");const auto compiles=blender::GPU_pass_global_compilation_count();std::atomic_bool cancel=false;QJsonArray cases;
 for(Kind kind:{Kind::Bump,Kind::MapOriginal,Kind::MapDisplaced,Kind::Grouped,Kind::MutedCombined,Kind::SharedHeight,Kind::Flat})cases.append(work([&]{
  using namespace blender;const bool flat=kind==Kind::Flat;auto source=fixture(kind);const auto original=source_state(*source.material);const auto source_hash=hash(original);
  // Omitting the contract must exercise the real conservative API default.
  auto authored=finish_native_material_source(std::move(source),cancel);const auto authored_description=native_material_description_json(authored->description);
  require(authored->world_normal_surface_contract==Contract::SourceAuthored&&authored->world_normal_source_displacement,"Default graph contract/provenance was silently upgraded");
  const int authored_materials=BLI_listbase_count(&authored->main->materials),authored_trees=BLI_listbase_count(&authored->main->nodetrees);
  prepare_optional_native_world_normal(authored,cancel);
  require(!authored->world_normal_tree&&authored->world_normal_perturbed==!flat,"Default authored contract used a Painter surface");
  if(flat)require(authored->world_normal_error.isEmpty()&&BLI_listbase_count(&authored->main->materials)==authored_materials&&BLI_listbase_count(&authored->main->nodetrees)==authored_trees,"Flat authored source allocated a private surface");
  else require(authored->world_normal_error.contains("authored Displacement"),"Default authored displacement rejection lost its precise reason");
  const auto authored_error=authored->world_normal_error;prepare_optional_native_world_normal(authored,cancel);
  require(!authored->world_normal_tree&&authored->world_normal_error==authored_error&&source_state(*authored->material)==original&&hash(source_state(*authored->material))==source_hash&&native_material_description_json(authored->description)==authored_description,"Default repeated preparation upgraded/mutated source policy or controls");

  auto painter_source=fixture(kind);const auto painter_original=source_state(*painter_source.material);const auto painter_hash=hash(painter_original);
  auto painter=finish_native_material_source(std::move(painter_source),cancel,nullptr,Contract::PainterUndisplaced);const auto painter_description=native_material_description_json(painter->description);
  require(painter->world_normal_surface_contract==Contract::PainterUndisplaced&&painter->world_normal_source_displacement,"Painter graph lost immutable contract/provenance");
  require(painter->description.warnings.contains(QString::fromUtf8(native_world_normal_surface_warning().data()))==!flat&&!authored->description.warnings.contains(QString::fromUtf8(native_world_normal_surface_warning().data())),"Painter contribution disclosure missing or attached to the wrong contract/flat fast path");
  const int materials=BLI_listbase_count(&painter->main->materials),trees=BLI_listbase_count(&painter->main->nodetrees);prepare_optional_native_world_normal(painter,cancel);
  int bumps=0,maps=0,authored_displacement_nodes=0,original_output_links=0;
  if(flat)require(!painter->world_normal_perturbed&&!painter->world_normal_tree&&painter->world_normal_error.isEmpty()&&BLI_listbase_count(&painter->main->materials)==materials&&BLI_listbase_count(&painter->main->nodetrees)==trees,"Painter contract defeated flat-Normal allocation-free path");
  else{
   require(painter->world_normal_perturbed&&painter->world_normal_tree&&painter->world_normal_error.isEmpty(),"Explicit undisplaced Painter surface failed admission");
   bNode *aov=nullptr;int roots=0;for(auto &node:painter->world_normal_tree->nodes){if(STREQ(node.idname,"ShaderNodeOutputAOV")){aov=&node;++roots;}if(STREQ(node.idname,"ShaderNodeOutputMaterial"))for(const auto &in:node.inputs){original_output_links+=in.link!=nullptr;for(const auto &edge:painter->world_normal_tree->links)require(edge.tosock!=&in,"Private Painter output retained an original MaterialOutput input edge");}}
   require(roots==1&&aov&&socket(*aov,"Color",true).link&&!socket(*aov,"Value",true).link&&original_output_links==0,"Private Painter AOV/pruned MaterialOutput roots differ");
   const auto nodes=reachable(*painter->world_normal_tree,*aov);
   for(const auto *node:nodes){
    if(STREQ(node->idname,"ShaderNodeDisplacement")||STREQ(node->idname,"ShaderNodeVectorDisplacement"))++authored_displacement_nodes;
    if(STREQ(node->idname,"ShaderNodeBump")){++bumps;require(socket(*const_cast<bNode *>(node),"Height",true).link,"Explicit Bump lost its reachable authored Height");}
    if(STREQ(node->idname,"ShaderNodeNormalMap")){++maps;const auto &settings=*static_cast<const NodeShaderNormalMap *>(node->storage);const int base=kind==Kind::MapOriginal||kind==Kind::MutedCombined?SHD_NORMAL_MAP_BASE_ORIGINAL:SHD_NORMAL_MAP_BASE_DISPLACED;require(settings.base==base&&settings.space==SHD_SPACE_TANGENT&&settings.convention==SHD_NORMAL_MAP_CONVENTION_OPENGL&&STREQ(settings.uv_map,"FixtureUV")&&node->custom1==0,"Painter admission rewrote NormalMap base/space/convention/UV/derivative mode");const auto &strength=socket(*const_cast<bNode *>(node),"Strength",true),&color=socket(*const_cast<bNode *>(node),"Color",true);require(!strength.link&&!color.link&&static_cast<const bNodeSocketValueFloat *>(strength.default_value)->value==.65f,"Painter NormalMap controls were rebound");const auto *rgba=static_cast<const bNodeSocketValueRGBA *>(color.default_value)->value;require(rgba[0]==.2f&&rgba[1]==.7f&&rgba[2]==1&&rgba[3]==1,"Painter NormalMap color/alpha was changed");}
   }
   const int expected_bumps=kind==Kind::Bump||kind==Kind::Grouped||kind==Kind::SharedHeight?1:0;const int expected_maps=kind==Kind::MapOriginal||kind==Kind::MapDisplaced||kind==Kind::Grouped||kind==Kind::MutedCombined?1:0;
   require(bumps==expected_bumps&&maps==expected_maps&&authored_displacement_nodes==0,"Private Painter AOV lost effective Normal reachability or retained the authored displacement branch");
   require(painter->world_normal_tree!=painter->material->nodetree&&BLI_findindex(&painter->main->nodetrees,&painter->world_normal_tree->id)>=0,"Painter AOV escaped private source ownership");
  }
  require(source_state(*painter->material)==painter_original&&hash(source_state(*painter->material))==painter_hash&&native_material_description_json(painter->description)==painter_description,"Painter admission changed original controls/edges/storage/material method/signature");
  for(const auto &node:painter->material->nodetree->nodes)if(STREQ(node.idname,"ShaderNodeOutputMaterial"))require(socket(*const_cast<bNode *>(&node),"Displacement",true).link,"Painter admission disconnected original authored displacement");
  return QJsonObject{{"case",name(kind)},{"default_source_authored_rejected",!flat},{"explicit_painter_admitted",!flat},{"flat_fast_path",flat},{"original_controls_edges_settings_signature_preserved",true},{"original_displacement_edge_preserved",true},{"private_original_output_links",original_output_links},{"reachable_authored_displacement_nodes",authored_displacement_nodes},{"effective_bumps",bumps},{"effective_normal_maps",maps},{"source_sha256",painter_hash},{"immutable_contract_and_provenance",true}};
 }));
 const auto cache=work([&]{
  const auto prepared=[&](Contract contract){auto graph=finish_native_material_source(fixture(Kind::Bump),cancel,nullptr,contract);prepare_optional_native_world_normal(graph,cancel);return graph;};
  auto authored=prepared(Contract::SourceAuthored),painter=prepared(Contract::PainterUndisplaced),alias=prepared(Contract::PainterUndisplaced);
  const auto original_authored=source_state(*authored->material),original_painter=source_state(*painter->material);NativeMaterialGpuLibrary library;
  library.install("authored","same-encoded-source-content",authored);library.install("painter","same-encoded-source-content",painter);library.install("painter-alias","same-encoded-source-content",alias);
  const auto stats=library.statistics();require(stats.resources==3&&stats.sources==2&&stats.programs==0&&stats.world_programs==0,"Shared GPU cache aliased differing surface contracts or allocated a GPU program");
  require(library.revision("authored")!=library.revision("painter")&&library.revision("painter")==library.revision("painter-alias"),"Surface cache contract separation or same-contract dedup differs");
  require(library.world_normal_context_state("authored")==NativeWorldNormalContextState::Unsupported&&library.world_normal_context_state("painter")==NativeWorldNormalContextState::Ready&&library.world_normal_context_state("painter-alias")==NativeWorldNormalContextState::Ready,"Surface cache reused the wrong contract's context admission");
  const auto a=library.signature("authored",0,library.description("authored")),p=library.signature("painter",0,library.description("painter")),same=library.signature("painter-alias",0,library.description("painter-alias"));require(a!=p&&p==same,"Actual program-cache signature aliases different surface contracts or splits identical ones");
  require(source_state(*authored->material)==original_authored&&source_state(*painter->material)==original_painter,"Cache sharing mutated authored source properties");
  return QJsonObject{{"resources",3},{"sources",2},{"different_contract_revisions",true},{"same_contract_deduplicated",true},{"program_cache_signatures_separate",true},{"source_preserved",true},{"programs",0},{"world_programs",0}};
 });
 const auto raw_height=work([&]{
  using namespace blender;
  const auto scalar_source=[&]{auto source=fixture(Kind::Bump);for(auto &node:source.material->nodetree->nodes)if(STREQ(node.idname,"ShaderNodeBump"))node.custom1=0;bke::node_add_node(nullptr,*source.material->nodetree,UString("ShaderNodeBevel"));bke::node_add_node(nullptr,*source.material->nodetree,UString("ShaderNodeAmbientOcclusion"));BKE_ntree_update_after_single_tree_change(*source.main,*source.material->nodetree);return source;};
  auto graph=finish_native_material_source(scalar_source(),cancel,nullptr,Contract::PainterUndisplaced);prepare_optional_native_world_normal(graph,cancel);const auto original=source_state(*graph->material),description=native_material_description_json(graph->description);
  auto raw=prepare_native_raw_height(scalar_source(),cancel,&graph->description,Contract::PainterUndisplaced);
  require(raw.tree&&raw.source->world_normal_surface_contract==Contract::PainterUndisplaced&&raw.source->world_normal_source_displacement&&native_material_description_json(raw.source->description)==description,"Separate Painter raw Height lost its exact immutable source interface/contract");
  require(raw.source->description.height_policy==NativeHeightPolicy::None&&std::find(raw.source->description.channels.begin(),raw.source->description.channels.end(),4)==raw.source->description.channels.end(),"Surface contract promoted scalar Height policy/capability");
  auto duplicate_source=scalar_source();auto inputs=describe_native_material_source(*duplicate_source.material,cancel);inputs.warnings.append("Retained third-party warning");inputs.warnings.append(QString::fromUtf8(native_material_node_context_warning("ShaderNodeBevel").data()));inputs.warnings.append("Ambient Occlusion uses the project baked AO map. Rebake to change ray distance/normal policy; Inside/Only Local modes are unsupported.");inputs.warnings.append(QString::fromUtf8(native_world_normal_surface_warning().data()));const auto known_warnings=inputs.warnings;
  auto duplicate=std::make_shared<NativeMaterialGraph>(std::move(duplicate_source.main),duplicate_source.material,std::move(inputs),cancel,Contract::PainterUndisplaced);require(duplicate->description.warnings==known_warnings,"Known app warnings duplicated or changed retained unknown warning/order");
  auto archive=archive_native_material(*graph,"/cpu-fixtures/painter-surface.blend",cancel,true);auto asset=native_material_asset("Painter surface Height fixture",archive.bytes,archive.description);asset.scope=AssetScope::Project;const auto metadata=asset_metadata(asset);const auto content=asset.hash();
  auto hydrated=hydrate_native_material_resource(asset,cancel,archive.textures);require(hydrated->world_normal_surface_contract==Contract::PainterUndisplaced&&native_material_description_json(hydrated->description)==description,"Actual resource hydration lost Painter contract or its disclosure");
  auto resource=prepare_native_material_height_resource(asset,archive.textures,cancel);
  require(resource.provenance().height_socket==raw.provenance.height_socket&&asset_metadata(asset)==metadata&&asset.hash()==content&&source_state(*graph->material)==original&&native_material_description_json(graph->description)==description,"Actual separate Height resource changed source/metadata/scalar provenance");
  auto plain=scalar_source();for(auto &node:plain.material->nodetree->nodes)if(STREQ(node.idname,"ShaderNodeOutputMaterial")){auto &input=socket(node,"Displacement",true);if(input.link)bke::node_remove_link(plain.material->nodetree,*input.link);}BKE_ntree_update_after_single_tree_change(*plain.main,*plain.material->nodetree);
  auto plain_graph=finish_native_material_source(std::move(plain),cancel,nullptr,Contract::PainterUndisplaced);require(!plain_graph->world_normal_source_displacement,"Height contract negative retained source displacement");auto plain_archive=archive_native_material(*plain_graph,"/cpu-fixtures/painter-surface-no-displacement.blend",cancel,true);auto plain_asset=native_material_asset("No displacement Height source",plain_archive.bytes,plain_archive.description);plain_asset.scope=AssetScope::Project;
  auto plain_painter=hydrate_native_material_resource(plain_asset,cancel,plain_archive.textures);auto plain_scene=plain_asset;plain_scene.bytes=read_native_material_bundle(plain_asset.bytes).blend;auto plain_source=read_native_material_source(materialize_native_material_archive(plain_scene,cancel),plain_asset.fill_preset->native_material->material,cancel);auto plain_raw=prepare_native_raw_height(std::move(plain_source),cancel,plain_asset.fill_preset->native_material.get());require(plain_raw.source->world_normal_surface_contract==Contract::SourceAuthored&&native_material_description_json(plain_raw.source->description)==native_material_description_json(plain_painter->description),"Height contract negative must have identical descriptions across differing contracts");
  auto wrong_contract=prepare_native_material_height_resource(plain_asset,plain_archive.textures,cancel,Contract::SourceAuthored);NativeMaterialGpuLibrary plain_library;plain_library.install("painter-height",plain_asset.hash().toStdString(),plain_painter);bool contract_rejected=false;try{auto candidate=plain_library.stage_height("painter-height",std::move(wrong_contract));}catch(const std::invalid_argument &error){contract_rejected=std::string_view(error.what()).find("surface contract")!=std::string_view::npos;}require(contract_rejected&&plain_library.statistics().programs==0&&plain_library.statistics().world_programs==0,"Mismatched Height surface contract reached GPU-owner staging or passed matching source descriptions");
  return QJsonObject{{"actual_archive_hydrate_height_resource",true},{"exact_source_interface_preserved",true},{"painter_contract_preserved",true},{"known_warnings_idempotent_unknown_order_preserved",true},{"mismatched_height_contract_rejected_before_gpu_owner",true},{"mismatched_height_descriptions_identical",true},{"default_height_policy_none",true},{"scalar_capability_not_promoted",true},{"source_preserved",true}};
 });
 TextureSet set;TextureLayer layer;layer.kind=LayerKind::Fill;auto description=std::make_shared<NativeMaterialDescription>();description->material="Painter surface metadata fixture";layer.fill.native_material=description;layer.fill.material_resource="painter-surface-fixture";set.layers={layer};set.selected_layer=layer.id;const std::array ids{layer.id};const auto defaults=native_material_description_json(*description);
 require(native_world_normal_plan(set,ids).eligible,"Undisplaced document unexpectedly rejected Painter surface metadata");set.displacement.allocated=true;set.displacement.channel=true;set.displacement.enabled=true;const auto active=native_world_normal_plan(set,ids);require(active.eligible,"Allocated document displacement disabled native tangent normals");set.displacement.enabled=false;require(native_world_normal_plan(set,ids).eligible,"Inactive document displacement rejected undisplaced Painter surface");set.displacement.enabled=true;require(native_world_normal_plan(set,ids).eligible&&native_material_description_json(*description)==defaults,"Document displacement restore changed source policy or disabled tangent normals");
 const auto authored_name=native_world_normal_surface_name(Contract::SourceAuthored),painter_name=native_world_normal_surface_name(Contract::PainterUndisplaced);require(!authored_name.empty()&&!painter_name.empty()&&authored_name!=painter_name,"Surface contract metadata identities alias");
 const auto warning=native_world_normal_surface_warning();const auto text=QString::fromUtf8(warning.data(),qsizetype(warning.size()));require(text.contains("displacement",Qt::CaseInsensitive)&&text.contains("not included",Qt::CaseInsensitive)&&text.contains("not reproduced",Qt::CaseInsensitive),"Effective Painter surface contract lost its authored-displacement disclosure");
 require(!blender::GPU_context_active_get()&&blender::GPU_pass_global_compilation_count()==compiles,"Surface contract CPU fixture created a GPU context or compiled a shader");
 return {{"complete",true},{"scope","CPU graph/admission/cache metadata only; explicit Painter undisplaced mesh frame, no original displaced-material parity"},{"cases",cases},{"default_source_authored_rejections",6},{"explicit_painter_admissions",6},{"normal_map_bases_preserved",true},{"flat_fast_paths",2},{"grouped_muted_shared_height_reachability",true},{"original_source_displacement_preserved",true},{"private_authored_displacement_dependency_absent",true},{"document_displacement_planner_cases",4},{"document_displacement_tangent_normal_admitted",true},{"cache",cache},{"raw_height_resource",raw_height},{"contract_warning",text},{"gpu_context_created",false},{"gpu_compilations",0}};
}
}
