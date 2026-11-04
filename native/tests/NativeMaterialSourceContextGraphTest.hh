#pragma once
#include "NativeMaterialFragmentNormalGraphTest.hh"
#include "NativeMaterialShaderMixGraphTest.hh"
#include "NativeFragmentGeometryTest.hh"
#include "NativeMaterialDependencies.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialGpuCompiler.hh"
#include "NativeMaterialFragmentContext.hh"
#include "NativeMaterialBakedAoContext.hh"
#include <initializer_list>

namespace bts {
// Real Blender graphs with no Texture Coordinate node. This proves worker
// preparation, dependency classification and source immutability, not rendering.
inline QJsonObject verify_native_material_source_context_graph(){
 using namespace blender;using namespace native_fragment_normal_test;
 using native_fragment_normal_test::detail::work;
 require(!GPU_context_active_get(),"Source-context CPU test acquired a GPU context");const auto compiles=GPU_pass_global_compilation_count();QJsonArray cases;const auto shader_mixtures=verify_native_shader_mix_graph();
 struct Case{const char *type,*output;bool supported;const char *attribute;bool surface,object,view,ao,uv;};
 for(const Case test:std::initializer_list<Case>{
  {"ShaderNodeNewGeometry","Position",true,"BTS_Geometry_Position",true,false,false,false,false},
  {"ShaderNodeNewGeometry","Normal",true,"BTS_Geometry_Normal",true,false,false,false,false},
  {"ShaderNodeNewGeometry","Pointiness",true,"",false,false,false,false,false},
  {"ShaderNodeNewGeometry","Incoming",true,"BTS_Geometry_Incoming",true,false,true,false,false},
  {"ShaderNodeNewGeometry","Backfacing",false,"physical surface facing",false,false,false,false,false},
  {"ShaderNodeNewGeometry","True Normal",false,"geometric-normal binding",false,false,false,false,false},
  {"ShaderNodeObjectInfo","Color",true,"BTS_Object_Color",true,true,false,false,false},
  {"ShaderNodeObjectInfo","Random",true,"BTS_Object_Random",true,true,false,false,false},
  {"ShaderNodeObjectInfo","Alpha",true,"BTS_Object_Alpha",true,true,false,false,false},
  {"ShaderNodeObjectInfo","Location",true,"BTS_Object_Location",true,true,false,false,false},
  {"ShaderNodeObjectInfo","Object Index",false,"object/material binding",false,false,false,false,false},
  {"ShaderNodeTexNoise","Fac",true,"",true,true,false,false,false},
  {"ShaderNodeAmbientOcclusion","AO",true,"BTS_Mesh_AO",false,false,false,true,false},
  {"ShaderNodeBevel","Normal",true,"",true,false,false,false,false}}){
  const auto proof=work([&]{
   DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"No Texture Coordinate source");auto &tree=*source.material->nodetree;
   auto *value=bke::node_add_node(nullptr,tree,UString(test.type));auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
   bke::node_add_link(tree,*value,socket(*value,test.output,false),*pbr,socket(*pbr,"Base Color",true));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);
   for(const auto &node:tree.nodes)require(!STREQ(node.idname,"ShaderNodeTexCoord"),"No-TexCoord fixture contains a coordinate node");
   const auto original=source_state(tree);std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);
   require(graph->original_inputs_preserved&&source_state(*graph->material->nodetree)==original,"Context preparation modified source nodes/defaults/links");
   const auto channel=std::find_if(graph->channels.begin(),graph->channels.end(),[](const auto &c){return c.channel==0;});require(channel!=graph->channels.end()&&bool(channel->tree)==test.supported,"Context channel admission differs");
   // A rejected Color dependency cannot disable independent literal PBR data.
   for(int id:{1,2}){const auto other=std::find_if(graph->channels.begin(),graph->channels.end(),[&](const auto &c){return c.channel==id;});require(other!=graph->channels.end()&&other->tree,"Unavailable context removed independent PBR channels");}
   QJsonObject result{{"node",test.type},{"output",test.output},{"supported",test.supported},{"source_preserved",true},{"texture_coordinate_nodes",0}};
   if(test.supported){
    const auto d=native_material_dependencies(*channel->tree,graph->parameters.size());require(d.surface==test.surface&&d.object==test.object&&d.view==test.view&&d.ao==test.ao&&d.uv==test.uv&&!d.facing,"Actual graph context dependency differs");
    if(STREQ(test.output,"Pointiness"))require(graph->description.warnings.contains("Geometry Pointiness uses Blender EEVEE's constant 0.5. Cycles mesh-curvature pointiness is not evaluated."),"EEVEE Pointiness lost its Cycles capability warning");
    if(test.attribute[0]){bool found=false;for(const auto &node:channel->tree->nodes)if(STREQ(node.idname,"ShaderNodeAttribute"))found|=STREQ(static_cast<const NodeShaderAttribute *>(node.storage)->name,test.attribute);require(found,"Context output binding missing");}
    result["dependencies"]=QJsonObject{{"surface",d.surface},{"object",d.object},{"view",d.view},{"ao",d.ao},{"uv",d.uv}};
    if(STREQ(test.type,"ShaderNodeBevel")){
     const auto warning=QString::fromUtf8(native_material_node_context_warning(test.type).data());require(graph->description.warnings.contains(warning),"EEVEE Bevel source lost its non-rounded capability warning");
     bool retained=false;for(auto &node:channel->tree->nodes)if(STREQ(node.idname,"ShaderNodeBevel")){retained=true;require(!socket(node,"Normal",true).link&&node.custom1==4,"Implicit Bevel Normal/samples changed instead of retaining Blender's node");}
     require(retained,"EEVEE Bevel was replaced with a fabricated normal");result["eevee_input_normal_passthrough"]=true;result["cycles_ray_rounded_bevel_evaluated"]=false;result["warning"]=warning;
    }
   }else{require(channel->error.contains(test.attribute),"Unavailable context has an unrelated diagnostic");result["reason"]=channel->error;}
   return result;
  });cases.append(proof);
 }
 // An unused context node must not request a surface atlas or a view update.
 QJsonArray unused_context_nodes;
 for(const char *type:{"ShaderNodeBevel","ShaderNodeFresnel","ShaderNodeLayerWeight"})unused_context_nodes.append(work([&]{auto source=fixture(Kind::Unlinked);auto &tree=*source.material->nodetree;bke::node_add_node(nullptr,tree,UString(type));BKE_ntree_update_after_single_tree_change(*source.main,tree);const auto original=source_state(tree);std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);for(int id:{0,1,2}){const auto c=std::find_if(graph->channels.begin(),graph->channels.end(),[&](const auto &value){return value.channel==id;});require(c!=graph->channels.end()&&c->tree,"Unused context node vetoed another channel");const auto d=native_material_dependencies(*c->tree,graph->parameters.size());require(!d.surface&&!d.view,"Unused context node requested a surface atlas or view context");}require(source_state(*graph->material->nodetree)==original,"Unused context classification changed source");return QJsonObject{{"node",type},{"surface_required",false},{"view_required",false},{"source_preserved",true}};}));
 const auto bevel_normal=work([&]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"EEVEE Bevel Normal source");auto &tree=*source.material->nodetree;
  auto *bevel=bke::node_add_node(nullptr,tree,UString("ShaderNodeBevel"));auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
  bke::node_add_link(tree,*bevel,socket(*bevel,"Normal",false),*pbr,socket(*pbr,"Normal",true));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);const auto original=source_state(tree);std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);prepare_optional_native_world_normal(graph,cancel);require(graph->world_normal_tree&&graph->world_normal_perturbed,"Bevel Normal source lost its private Normal contribution");
  const auto d=native_material_dependencies(*graph->world_normal_tree,graph->parameters.size());require(d.surface&&!d.view&&!d.uv,"Bevel Normal source lost its real surface dependency");NativeMaterialGpuLibrary library;library.install("eevee-bevel-normal","eevee-bevel-source",graph);require(library.world_normal_error("eevee-bevel-normal").isEmpty(),"Bevel Normal CPU context preflight rejected Blender EEVEE passthrough");const auto stats=library.statistics();require(stats.programs==0&&stats.world_programs==0&&source_state(*graph->material->nodetree)==original,"Bevel Normal CPU admission compiled GPU state or changed source");
  return QJsonObject{{"source_preserved",true},{"surface_required",true},{"implicit_normal_unlinked",true},{"world_context_preflight",true},{"programs",0},{"world_programs",0},{"cycles_ray_rounded_bevel_evaluated",false}};
 });
 QJsonArray resident_objects;
 for(const auto test:std::initializer_list<std::pair<const char *,const char *>>{{"ShaderNodeObjectInfo","Color"},{"ShaderNodeObjectInfo","Alpha"},{"ShaderNodeObjectInfo","Random"},{"ShaderNodeObjectInfo","Location"},{"ShaderNodeTexCoord","Object"},{"ShaderNodeTexCoord","Generated"}})resident_objects.append(work([&]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Captured resident owner Normal");auto &tree=*source.material->nodetree;
  auto *value=bke::node_add_node(nullptr,tree,UString(test.first));auto *bump=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
  bke::node_add_link(tree,*value,socket(*value,test.second,false),*bump,socket(*bump,"Height",true));bke::node_add_link(tree,*bump,socket(*bump,"Normal",false),*pbr,socket(*pbr,"Normal",true));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);const auto original=source_state(tree);
  int coordinates=0;for(const auto &node:tree.nodes)coordinates+=STREQ(node.idname,"ShaderNodeTexCoord");require(coordinates==(STREQ(test.first,"ShaderNodeTexCoord")?1:0),"ObjectInfo fixture acquired a Texture Coordinate dependency");
  std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);prepare_optional_native_world_normal(graph,cancel);require(graph->world_normal_tree,"Owner-dependent Normal extraction failed");
  const auto d=native_material_dependencies(*graph->world_normal_tree,graph->parameters.size());require(d.object&&d.surface&&!d.view&&!d.uv,"Resident object source dependency differs");
  NativeMaterialGpuLibrary library;library.install("resident-owner","resident-owner-source",graph);const bool generated=STREQ(test.second,"Generated");const auto error=library.world_normal_error("resident-owner");
  if(generated)require(error.contains("BTS_Coord_Generated")&&library.world_normal_context_state("resident-owner")==NativeWorldNormalContextState::PendingGenerated,"Generated was fabricated from position or lost its precise pending ORCO requirement");
  else{
   require(error.isEmpty()&&library.world_normal_uses_object("resident-owner"),"Real resident owner context was rejected");const PreparedNativeFragmentNormal prepared{graph,graph->world_normal_tree};const auto layout=native_world_normal_source_layout(prepared);const auto diagnostic=inspect_native_fragment_source_context(prepared,layout);require(layout.resident_object_context&&diagnostic.object_context&&!diagnostic.codegen_invoked,"CPU resident inspection lost actual owner dependency or compiled a shader");
   for(bool resident:{false,true}){auto unsupported=layout;unsupported.resident_object_context=resident;unsupported.resident_raster_origin=false;bool rejected=false;try{inspect_native_fragment_source_context(prepared,unsupported);}catch(const std::invalid_argument &failure){rejected=std::string_view(failure.what()).find("captured resident owner context")!=std::string_view::npos;}require(rejected,"Standalone fragment accepted missing resident owner payload");}
  }
  require(source_state(*graph->material->nodetree)==original&&library.statistics().programs==0,"Resident owner admission mutated source or allocated a GPU program");
  return QJsonObject{{"node",test.first},{"output",test.second},{"resident_supported",!generated},{"standalone_rejected",!generated},{"source_preserved",true},{"texture_coordinate_nodes",coordinates},{"reason",error},{"gpu_programs",0}};
 }));
 const auto remap=work([&]{
  auto source=fixture(Kind::Unlinked);auto *tree=bke::node_tree_add_tree(source.main.get(),"Logical parameter remap dependency","ShaderNodeTree");auto *parameter=bke::node_add_node(nullptr,*tree,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(parameter->storage)->name,"BTS_Param1");auto *aov=bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputAOV"));bke::node_add_link(*tree,*parameter,socket(*parameter,"Color",false),*aov,socket(*aov,"Color",true));BKE_ntree_update_after_single_tree_change(*source.main,*tree);
  NativeMaterialCompilation layout{tree,{},8};layout.parameter_slots={3,7};const auto d=native_material_dependencies(*tree,layout.logical_parameter_count());require(d.parameters==std::vector<std::size_t>{1}&&!d.surface&&!d.view,"Compiler dependency visitor confused logical IDs with physical storage");bool rejected=false;try{native_material_dependencies(*tree,1);}catch(const std::invalid_argument &){rejected=true;}require(rejected,"Logical dependency overflow was silently admitted");return QJsonObject{{"physical_parameters",8},{"logical_parameters",2},{"dependency",1},{"invalid_logical_count_rejected",true},{"surface_required",false}};
 });
 const auto mapping=work([&]{
  const auto prepare=[](bool unavailable_context){auto source=fixture(Kind::ActiveBump);auto &tree=*source.material->nodetree;
   for(auto &link:tree.links)if(STREQ(link.fromnode->idname,"ShaderNodeTexCoord")){link.fromsock=&socket(*link.fromnode,"UV",false);BKE_ntree_update_tag_link_changed(&tree);}
   if(unavailable_context){
    // Keep the Mapping edge genuinely editable (UV). A separate reachable
    // Camera input makes only its world Normal context unavailable.
    for(auto &top:tree.nodes)if(STREQ(top.idname,"ShaderNodeGroup")){auto &group=*reinterpret_cast<bNodeTree *>(top.id);auto *camera=bke::node_add_node(nullptr,group,UString("ShaderNodeTexCoord"));for(auto &node:group.nodes)if(STREQ(node.idname,"ShaderNodeBump"))bke::node_add_link(group,*camera,socket(*camera,"Camera",false),node,socket(node,"Normal",true));BKE_ntree_update_after_single_tree_change(*source.main,group);}
   }
   BKE_ntree_update_after_single_tree_change(*source.main,tree);std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);prepare_optional_native_world_normal(graph,cancel);return graph;
  };
  auto graph=prepare(false);require(graph->world_normal_tree,"World-only Mapping fixture has no private Normal tree");const auto original=source_state(*graph->material->nodetree),defaults=native_material_description_json(graph->description);
  auto edited=graph->description;const auto found=std::find_if(edited.parameters.begin(),edited.parameters.end(),[](const auto &p){return p.mapping_transform_supported();});require(found!=edited.parameters.end(),"World-only Mapping fixture parameter missing");const auto index=std::size_t(found-edited.parameters.begin());
  for(const auto &c:graph->channels)if(c.tree){const auto d=native_material_dependencies(*c.tree,graph->parameters.size());require(std::find(d.parameters.begin(),d.parameters.end(),index)==d.parameters.end(),"World-only Mapping fixture also affects an ordinary channel");}
  NativeMaterialGpuLibrary library;library.install("world-mapping","world-mapping-source",graph);require(library.world_normal_error("world-mapping").isEmpty(),"CPU world context preflight rejected a UV source");
  const auto before=library.world_normal_signature("world-mapping",edited);found->mapping_transform.offset[0]=.125;library.validate_mapping_edit("world-mapping",&graph->description,edited);
  require(before!=library.world_normal_signature("world-mapping",edited),"Installed world signature ignored a Normal-only Mapping parameter");
  const auto stats=library.statistics();require(stats.programs==0&&stats.world_programs==0,"CPU install or Mapping validation allocated a GPU program");
  require(source_state(*graph->material->nodetree)==original&&native_material_description_json(graph->description)==defaults,"CPU Mapping validation changed source topology/defaults");
  auto unsupported=prepare(true);const auto failed_source=source_state(*unsupported->material->nodetree);auto failed_edit=unsupported->description;auto control=std::find_if(failed_edit.parameters.begin(),failed_edit.parameters.end(),[](const auto &p){return p.mapping_transform_supported();});require(control!=failed_edit.parameters.end(),"Unsupported world Mapping fixture parameter missing");control->mapping_transform.offset[0]=.125;
  library.install("failed-world-mapping","failed-world-mapping-source",unsupported);const auto unavailable=library.world_normal_error("failed-world-mapping");require(unavailable.contains("BTS_Coord_Camera"),"Failed world context lost its exact missing binding");
  bool rejected=false;try{library.validate_mapping_edit("failed-world-mapping",&unsupported->description,failed_edit);}catch(const std::invalid_argument &error){rejected=std::string_view(error.what()).find("unused by supported native channels")!=std::string_view::npos;}
  require(rejected&&source_state(*unsupported->material->nodetree)==failed_source,"Unavailable world context admitted Mapping or changed its source");
  return QJsonObject{{"cpu_install_to_validate_mapping_edit",true},{"world_only_mapping_accepted",true},{"world_signature_changed",true},{"source_preserved",true},{"unavailable_context_rejected",true},{"unavailable_context_reason",unavailable},{"gpu_prepare_called",false},{"programs",qint64(stats.programs)},{"world_programs",qint64(stats.world_programs)}};
 });
 // Physical payload is captured on its actual native owner, then used only
 // after the Main and meshes are destroyed. No GPU preparation is called.
 std::shared_ptr<const NativeFragmentGeometrySource> generated_source,ordinary_source;
 {
  native_fragment_geometry_test::Fixture owners;NativeFragmentGeometryRequest request;request.material_uid=owners.material->id.session_uid;request.tiles={1001};request.uv_requests={{NativeFragmentUvKind::DefaultRender,{}}};request.generated_stream=true;request.allow_original_orco=true;std::atomic_bool cancel=false;
  generated_source=capture_native_fragment_geometry(owners.context,nullptr,{},request,cancel);request.generated_stream=false;ordinary_source=capture_native_fragment_geometry(owners.context,nullptr,{},request,cancel);
 }
 std::atomic_bool generated_cancel=false;
 const auto generated_geometry=work([&]{return prepare_native_fragment_geometry(generated_source,generated_cancel);});generated_source.reset();
 const auto ordinary_geometry=work([&]{return prepare_native_fragment_geometry(ordinary_source,generated_cancel);});ordinary_source.reset();
 QJsonArray generated_admission;
 for(bool implicit:{false,true})generated_admission.append(work([&]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Authoritative Generated Normal");auto &tree=*source.material->nodetree;
  auto *value=bke::node_add_node(nullptr,tree,UString(implicit?"ShaderNodeTexNoise":"ShaderNodeTexCoord"));auto *bump=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
  bke::node_add_link(tree,*value,socket(*value,implicit?"Fac":"Generated",false),*bump,socket(*bump,"Height",true));bke::node_add_link(tree,*bump,socket(*bump,"Normal",false),*pbr,socket(*pbr,"Normal",true));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);const auto original=source_state(tree);
  int coordinates=0;for(const auto &node:tree.nodes)coordinates+=STREQ(node.idname,"ShaderNodeTexCoord");require(coordinates==(implicit?0:1),"Implicit Generated gained a Texture Coordinate node");
  std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);prepare_optional_native_world_normal(graph,cancel);require(graph->world_normal_tree&&graph->world_normal_perturbed,"Generated Normal contribution was discarded");const PreparedNativeFragmentNormal prepared{graph,graph->world_normal_tree};
  const auto dependencies=native_material_dependencies(*graph->world_normal_tree,graph->parameters.size());require(dependencies.surface&&dependencies.object&&!dependencies.uv&&!dependencies.view,"Generated acquired an authored UV/view requirement");
  NativeMaterialGpuLibrary library;library.install("generated","generated-source",graph);require(library.world_normal_context_state("generated")==NativeWorldNormalContextState::PendingGenerated&&library.world_normal_requires_generated("generated")&&library.world_normal_error("generated").contains("authoritative ORCO"),"Source installation did not preserve pending Generated admission");
  const auto requested=native_world_normal_source_request(prepared);require(requested.generated_stream&&requested.resident_raster_origin,"Generated request lost optional physical payload requirement");
  library.prepare_world_normal("generated");require(library.world_normal_context_state("generated")==NativeWorldNormalContextState::PendingGenerated&&library.statistics().programs==0,"Generated source-only preparation did not defer its GPU program");
  const auto typed_reject=[&](auto operation){bool rejected=false;try{operation();}catch(const NativeFragmentContextError &error){rejected=error.code==NativeFragmentContextCode::GeneratedOrco;}require(rejected,"Missing physical Generated payload was not a typed pre-GPU rejection");};
  typed_reject([&]{native_world_normal_source_layout(prepared);});typed_reject([&]{library.validate_world_normal_geometry("generated",*ordinary_geometry);});
  auto standalone=requested;standalone.resident_raster_origin=false;typed_reject([&]{inspect_native_fragment_source_context(prepared,standalone);});
  library.validate_world_normal_geometry("generated",*generated_geometry);const auto layout=native_world_normal_source_layout(prepared,generated_geometry.get());const auto diagnostic=inspect_native_fragment_source_context(prepared,layout);require(layout.generated_stream&&diagnostic.generated_context&&!diagnostic.codegen_invoked,"Actual ORCO was rejected or CPU preflight invoked GPU codegen");
  require(library.world_normal_context_state("generated")==NativeWorldNormalContextState::PendingGenerated&&library.statistics().programs==0&&library.statistics().world_programs==0,"CPU payload validation compiled or published a GPU program");require(source_state(*graph->material->nodetree)==original,"Generated admission mutated source topology/defaults");
  return QJsonObject{{"input",implicit?"implicit-noise":"explicit-generated"},{"texture_coordinate_nodes",coordinates},{"pending_before_capture",true},{"source_only_prepare_deferred",true},{"actual_orco_geometry_admitted",true},{"missing_payload_typed_rejected",true},{"standalone_typed_rejected",true},{"source_main_destroyed_before_prepare",true},{"source_preserved",true},{"programs",0},{"world_programs",0},{"generated_stream",true}};
 }));
 QJsonArray view_admission;
 for(const auto test:std::initializer_list<std::pair<const char *,const char *>>{{"ShaderNodeNewGeometry","Incoming"},{"ShaderNodeFresnel","Fac"},{"ShaderNodeLayerWeight","Fresnel"},{"ShaderNodeLayerWeight","Facing"}})view_admission.append(work([&]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Real current-view Normal");auto &tree=*source.material->nodetree;
  auto *value=bke::node_add_node(nullptr,tree,UString(test.first));auto *bump=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
  bke::node_add_link(tree,*value,socket(*value,test.second,false),*bump,socket(*bump,"Height",true));bke::node_add_link(tree,*bump,socket(*bump,"Normal",false),*pbr,socket(*pbr,"Normal",true));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);const auto original=source_state(tree);for(const auto &node:tree.nodes)require(!STREQ(node.idname,"ShaderNodeTexCoord"),"View source acquired a Texture Coordinate requirement");
  std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);prepare_optional_native_world_normal(graph,cancel);require(graph->world_normal_tree,"View-dependent Normal extraction was discarded");const PreparedNativeFragmentNormal prepared{graph,graph->world_normal_tree};
  const auto dependencies=native_material_dependencies(*graph->world_normal_tree,graph->parameters.size());require(dependencies.surface&&dependencies.view&&!dependencies.object&&!dependencies.uv,"View dependency incorrectly requires UV/object metadata");NativeMaterialGpuLibrary library;library.install("view","view-source",graph);require(library.world_normal_context_state("view")==NativeWorldNormalContextState::PendingView&&library.world_normal_requires_view("view")&&library.world_normal_error("view").contains("real validated view"),"View source installation lost pending real camera contract");library.prepare_world_normal("view");
  const auto typed_reject=[&](auto operation){bool rejected=false;try{operation();}catch(const NativeFragmentContextError &error){rejected=error.code==NativeFragmentContextCode::View;}require(rejected,"Missing view was not rejected through typed pre-GPU admission");};
  typed_reject([&]{native_world_normal_source_layout(prepared);});typed_reject([&]{library.validate_world_normal_view("view",nullptr);});NativeMaterialGpuContext absent;typed_reject([&]{library.validate_world_normal_view("view",&absent.view);});auto standalone=native_world_normal_source_request(prepared);standalone.resident_raster_origin=false;typed_reject([&]{inspect_native_fragment_source_context(prepared,standalone);});
  const std::array<float,16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,-5,1},projection{1,0,0,0,0,1,0,0,0,0,-1,-1,0,0,-.2f,0};auto view=capture_native_material_view(matrix,projection);library.validate_world_normal_view("view",&view);const auto layout=native_world_normal_source_layout(prepared,nullptr,&view);const auto diagnostic=inspect_native_fragment_source_context(prepared,layout);require(layout.resident_view_context&&diagnostic.view_context&&!diagnostic.codegen_invoked,"Real camera admission lost its source context or compiled GPU code");
  const auto signature=library.world_normal_signature("view",graph->description,&view);const float original_x=view.position[0];view.position[0]=1;require(signature!=library.world_normal_signature("view",graph->description,&view),"Camera values did not change actual output signature");view.position[0]=original_x;require(signature==library.world_normal_signature("view",graph->description,&view),"Restored camera did not restore exact output signature");require(library.statistics().programs==0&&library.statistics().world_programs==0&&source_state(*graph->material->nodetree)==original,"View CPU admission compiled GPU state or changed source/defaults");
  return QJsonObject{{"node",test.first},{"output",test.second},{"texture_coordinate_nodes",0},{"real_view_admitted",true},{"pending_without_camera",true},{"default_camera_typed_rejected",true},{"standalone_typed_rejected",true},{"output_signature_changed_and_restored",true},{"source_preserved",true},{"programs",0},{"world_programs",0}};
 }));
 QJsonArray baked_ao_admission;
 for(int mode:{0,1,2,3})baked_ao_admission.append(work([&]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Retained project AO Normal");auto &tree=*source.material->nodetree;
  auto *ao=bke::node_add_node(nullptr,tree,UString("ShaderNodeAmbientOcclusion"));if(mode>=2)ao->custom2=mode==2?SHD_AO_INSIDE:SHD_AO_LOCAL;
  auto *bump=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
  bke::node_add_link(tree,*ao,socket(*ao,mode==1?"Color":"AO",false),*bump,socket(*bump,"Height",true));bke::node_add_link(tree,*bump,socket(*bump,"Normal",false),*pbr,socket(*pbr,"Normal",true));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);const auto original=source_state(tree);for(const auto &node:tree.nodes)require(!STREQ(node.idname,"ShaderNodeTexCoord"),"AO source acquired a Texture Coordinate requirement");
  std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);prepare_optional_native_world_normal(graph,cancel);
  const QString warning="Ambient Occlusion uses the project baked AO map. Rebake to change ray distance/normal policy; Inside/Only Local modes are unsupported.";require(graph->description.warnings.contains(warning),"AO source lost its visible project-bake adapter warning");
  for(int id:{0,1,2}){const auto channel=std::find_if(graph->channels.begin(),graph->channels.end(),[&](const auto &c){return c.channel==id;});require(channel!=graph->channels.end()&&channel->tree,"AO Normal limitation removed independent literal Color/PBR");}
  if(mode>=2){require(!graph->world_normal_tree&&graph->world_normal_error.contains("Inside/Only Local"),"Unsupported Inside/Only Local AO was replaced with an outside bake");require(source_state(*graph->material->nodetree)==original,"Rejected AO source changed originals");return QJsonObject{{"mode",mode==2?"inside":"only-local"},{"supported",false},{"reason",graph->world_normal_error},{"source_preserved",true},{"independent_PBR_preserved",true}};}
  require(graph->world_normal_tree&&graph->world_normal_perturbed,"Project AO Normal source was discarded");const PreparedNativeFragmentNormal prepared{graph,graph->world_normal_tree};NativeMaterialGpuLibrary library;library.install("project-AO","project-AO-source",graph);
  require(library.world_normal_requires_baked_ao("project-AO")&&library.world_normal_context_state("project-AO")==NativeWorldNormalContextState::PendingBakedAo&&library.world_normal_error("project-AO").contains("retained project AO"),"AO installation lost its pending actual-map contract");library.prepare_world_normal("project-AO");
  const auto typed_reject=[&](auto operation){bool rejected=false;try{operation();}catch(const NativeFragmentContextError &error){rejected=error.code==NativeFragmentContextCode::BakedAo;}require(rejected,"Missing project AO did not reject before GPU preparation");};
  typed_reject([&]{native_world_normal_source_layout(prepared);});typed_reject([&]{library.validate_world_normal_ao("project-AO",nullptr);});auto standalone=native_world_normal_source_request(prepared);standalone.resident_raster_origin=false;typed_reject([&]{inspect_native_fragment_source_context(prepared,standalone);});typed_reject([&]{NativeBakedAoInput{}.validate(1001);});
  MeshMaps maps;maps[3]=MeshMap{};for(int tile:{1001,1002}){auto pixels=std::make_shared<MeshMapPixels>(32,std::vector<float>(32*32*4,1),32);maps[3]->tiles.emplace(tile,MeshMapPlane{32,pixels->hash,std::move(pixels),32});}const std::array<int,2> tiles{1001,1002};const auto captured=NativeMaterialBakedAoContext::capture(maps,tiles);library.validate_world_normal_ao("project-AO",&captured);const auto layout=native_world_normal_source_layout(prepared,nullptr,nullptr,&captured);const auto diagnostic=inspect_native_fragment_source_context(prepared,layout);
  require(layout.resident_baked_ao_context&&diagnostic.baked_ao_context&&!diagnostic.codegen_invoked,"Actual AO snapshot admission lost raster binding or compiled a shader");const auto first=library.world_normal_ao_signature("project-AO",1001,&captured),second=library.world_normal_ao_signature("project-AO",1002,&captured);
  auto pixels=std::make_shared<MeshMapPixels>(64,std::vector<float>(64*32*4,.5f),32);maps[3]->tiles.at(1002)={64,pixels->hash,std::move(pixels),32};const auto rebaked=NativeMaterialBakedAoContext::capture(maps,tiles);require(first==library.world_normal_ao_signature("project-AO",1001,&rebaked)&&second!=library.world_normal_ao_signature("project-AO",1002,&rebaked),"AO replacement did not change only the matching tile output identity");
  require(source_state(*graph->material->nodetree)==original&&library.statistics().programs==0&&library.statistics().world_programs==0,"AO CPU admission changed originals or compiled GPU state");return QJsonObject{{"mode",mode==1?"color":"AO"},{"supported",true},{"pending_without_map",true},{"missing_and_standalone_typed_rejected",true},{"actual_snapshot_admitted",true},{"per_tile_output_replacement",true},{"source_preserved",true},{"texture_coordinate_nodes",0},{"programs",0},{"world_programs",0}};
 }));
 require(!GPU_context_active_get()&&GPU_pass_global_compilation_count()==compiles,"Source-context CPU test compiled GPU state");
 return {{"complete",true},{"scope","CPU graph/binding classification only; actual shader/render parity unverified"},{"cases",cases},{"shader_mixtures",shader_mixtures},{"unreachable_bevel_ignored",true},{"unreachable_context_nodes",unused_context_nodes},{"resident_object_context",resident_objects},{"generated_orco_admission",generated_admission},{"view_context_admission",view_admission},{"baked_ao_admission",baked_ao_admission},{"bevel_eevee_normal",bevel_normal},{"logical_parameter_remap",remap},{"world_only_mapping",mapping},{"gpu_context_created",false},{"gpu_compilations",0}};
}
}
