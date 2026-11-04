#include "NativeMaterialGraph.hh"
#include "NativeMaterialBump.hh"
#include "NativeMaterialAmbient.hh"
#include "NativeMaterialRamp.hh"
#include "NativeMaterialMappingNodes.hh"
#include "NativeMaterialMappingValidation.hh"
#include "NativeMaterialRawHeight.hh"
#include "NativeMaterialFragmentNormal.hh"
#include "NativeMaterialSourcePolicy.hh"
#include <QCoreApplication>
#include <QThread>
#include <QScopeGuard>
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "NOD_shader.h"
#include "NOD_shader_nodes_inline.hh"
#include <algorithm>
#include <set>
#include <map>
#include <optional>
#include <cmath>
namespace bts {namespace {
using namespace blender;
bNodeSocket &input(bNode &node,const char *name){for(auto &socket:node.inputs)if(socket.is_available()&&STREQ(socket.identifier,name))return socket;auto *socket=bke::node_find_enabled_input_socket(node,name);if(!socket)throw std::runtime_error(std::string("Native material input missing: ")+name);return *socket;}
bNodeSocket &output(bNode &node,const char *name){for(auto &socket:node.outputs)if(STREQ(socket.identifier,name))return socket;throw std::runtime_error(std::string("Native material output missing: ")+name);}
bNode &add(bNodeTree &tree,const char *type){auto *node=bke::node_add_node(nullptr,tree,UString(type));if(!node)throw std::runtime_error("Cannot create native channel node");return *node;}
void copy_input(bNodeTree &tree,const bNodeSocket &from,bNode &to,bNodeSocket &destination){
 if(from.link){bke::node_add_link(tree,*from.link->fromnode,*from.link->fromsock,to,destination);return;}
 if(from.type==SOCK_FLOAT&&destination.type==SOCK_FLOAT)static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=static_cast<const bNodeSocketValueFloat *>(from.default_value)->value;
 else if(from.type==SOCK_RGBA&&destination.type==SOCK_RGBA)std::copy_n(static_cast<const bNodeSocketValueRGBA *>(from.default_value)->value,4,static_cast<bNodeSocketValueRGBA *>(destination.default_value)->value);
 else throw std::runtime_error("Unsupported native channel socket conversion");
}
bNode *linked_shader(bNodeSocket &surface){
 std::set<bNode *> visited;auto *node=surface.link?surface.link->fromnode:nullptr;
 while(node&&STREQ(node->idname,"NodeReroute")){if(!visited.insert(node).second)throw std::runtime_error("Cyclic native shader reroute");auto *socket=static_cast<bNodeSocket *>(node->inputs.first);node=socket&&socket->link?socket->link->fromnode:nullptr;}
 return node;
}
struct RawHeightCandidate {
 NativeRawHeightCandidate result;
 const bNodeSocket *height=nullptr;
};
RawHeightCandidate raw_height_candidate(const bNodeSocket &normal){
 RawHeightCandidate candidate;auto &result=candidate.result;const bNodeSocket *socket=&normal;std::set<const bNode *> visited;
 const auto fail=[&](const char *reason){result.reason=reason;return candidate;};
 const auto active=[](const bNodeSocket &value){return value.link&&value.link->tonode&&value.link->tosock==&value&&value.link->fromnode&&value.link->fromsock&&(value.link->flag&NODE_LINK_VALID)&&value.link->is_used();};
 while(socket->link){
  if(!active(*socket))return fail("Raw authored Height requires an active, valid Normal path");
  auto *node=socket->link->fromnode;if(!visited.insert(node).second)return fail("Raw authored Height cannot use a cyclic Normal path");
  if(node->flag&NODE_MUTED)return fail("Raw authored Height must be inspected after native muted-node bypass");
  if(STREQ(node->idname,"NodeReroute")){socket=static_cast<const bNodeSocket *>(node->inputs.first);if(!socket||!socket->link)return fail("Raw authored Height has an unconnected Normal reroute");continue;}
  if(!STREQ(node->idname,"ShaderNodeBump"))return fail("Raw authored Height cannot discard a custom Normal or Normal Map chain");
  if(!STREQ(socket->link->fromsock->identifier,"Normal"))return fail("Raw authored Height source is not the Bump Normal output");
  const auto *height=bke::node_find_enabled_input_socket(*node,"Height");const auto *input_normal=bke::node_find_enabled_input_socket(*node,"Normal");
  if(!height||!input_normal)return fail("Raw authored Height source is missing Height or Normal");
  if(height->link&&!active(*height))return fail("Raw authored Height does not select through an inactive Height edge");
  if(!height->link){++result.provenance.bypass_nodes;socket=input_normal;continue;}
  if(input_normal->link)return fail("Raw authored Height cannot discard a custom Normal or another Bump stage");
  if(height->type!=SOCK_FLOAT||height->link->fromsock->type!=SOCK_FLOAT)return fail("Raw authored Height requires a direct authored FLOAT output; no color/vector conversion");
  const auto literal=[&](const char *name,float &out){const auto *value=bke::node_find_enabled_input_socket(*node,name);if(!value||value->type!=SOCK_FLOAT||value->link||!value->default_value)return false;out=static_cast<const bNodeSocketValueFloat *>(value->default_value)->value;return std::isfinite(out);};
  if(!literal("Strength",result.provenance.strength)||!literal("Distance",result.provenance.distance)||!literal("Filter Width",result.provenance.filter_width))return fail("Raw authored Height does not consume linked or invalid Strength/Distance/Filter Width controls");
  result.provenance.invert=node->custom1!=0;if(result.provenance.invert)return fail("Raw authored Height phase-one proof does not adopt source Bump invert");
  std::set<const bNode *> height_visited,height_visiting;bool invalid=false,cyclic=false,inactive=false;
  const auto visit=[&](auto &&self,const bNode &value)->void{if(height_visiting.contains(&value)){cyclic=true;return;}if(!height_visited.insert(&value).second)return;height_visiting.insert(&value);const std::string_view type(value.idname);if(type=="ShaderNodeBump"||type=="ShaderNodeNormalMap"||type=="ShaderNodeBevel"||type=="ShaderNodeShaderToRGB"){invalid=true;height_visiting.erase(&value);return;}for(const auto &in:value.inputs)if(in.link){if(!active(in)){inactive=true;continue;}self(self,*in.link->fromnode);}height_visiting.erase(&value);};
  visit(visit,*height->link->fromnode);if(cyclic)return fail("Raw authored Height has a cyclic Height dependency");if(inactive)return fail("Raw authored Height has an inactive Height dependency edge");if(invalid)return fail("Raw authored Height cannot derive H from Bump/Normal Map or derivative/closure nodes");
  result.provenance.bump_node=node->name;result.provenance.height_node=height->link->fromnode->name;result.provenance.height_socket=height->link->fromsock->identifier;result.eligible=true;candidate.height=height;return candidate;
 }
 return fail("Native source has no unique active authored Height function");
}
void bind_parameters(bNodeTree &tree,bNode &top,const NativeMaterialDescription &description){
 for(std::size_t i=0;i<description.parameters.size();++i){const auto &parameter=description.parameters[i];if(!parameter.source_node.isEmpty())continue;
  bNodeSocket *target=nullptr;for(auto &socket:top.inputs)if(parameter.identifier==socket.identifier){target=&socket;break;}if(!target)throw std::runtime_error("Native group input identity changed during preparation");if(target->flag&SOCK_HIDE_VALUE)continue;
  auto &attribute=add(tree,"ShaderNodeAttribute");const auto name="BTS_Param"+std::to_string(i);STRNCPY(static_cast<NodeShaderAttribute *>(attribute.storage)->name,name.c_str());
  bke::node_add_link(tree,attribute,output(attribute,parameter.type==NativeParameterType::Scalar?"Fac":parameter.type==NativeParameterType::Vector?"Vector":"Color"),top,*target);
 }
}
void bind_context(bNodeTree &tree,bool defer_compute_context_validation,bool fragment_normal,bool *pointiness_as_eevee=nullptr){
 bind_native_material_ambient(tree);
 // Only a channel's reachable graph requires its source context. An unsupported
 // coordinate node on another channel must not prevent this channel loading.
 std::set<const bNode *> required;std::map<const bNode *,std::vector<const bNode *>> incoming;
 // Newly added AOV/adapter links do not populate socket.link until tree update.
 for(const auto &link:tree.links)incoming[link.tonode].push_back(link.fromnode);
 const auto visit=[&](auto &&self,const bNode &node)->void{if(!required.insert(&node).second)return;if(const auto found=incoming.find(&node);found!=incoming.end())for(const auto *source:found->second)self(self,*source);};
 for(const auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV"))visit(visit,node);
 if(!defer_compute_context_validation)for(const auto *node:required){
  const std::string_view type(node->idname);
  if(!node->typeinfo||!node->typeinfo->gpu_fn)throw std::invalid_argument("Unsupported material channel node: "+std::string(type));
  for(const auto &socket:node->inputs)if(socket.type==SOCK_SHADER)throw std::invalid_argument("Material channels cannot evaluate shader-closure input on "+std::string(type));
  if(type=="ShaderNodeScript"||type=="ShaderNodeParticleInfo"||type=="ShaderNodeHairInfo"||type=="ShaderNodeVolumeInfo"||type=="ShaderNodeUVAlongStroke"||type=="ShaderNodeCamera"||type=="ShaderNodeTangent"||type=="ShaderNodeBump"||type=="ShaderNodeNormalMap")throw std::invalid_argument("Unsupported material channel context: "+std::string(type));
 }
 struct Replacement{bNodeLink *link;std::string name;std::optional<float> literal;};std::vector<Replacement> replacements;
 for(auto &link:tree.links){const std::string_view type(link.fromnode->idname);
  if(!required.contains(link.tonode))continue;
  if(type=="ShaderNodeTexCoord"){
   if(link.fromnode->custom1&1)throw std::runtime_error("Native Texture Coordinate From Instancer context is not supported");
   if(link.fromnode->id&&STREQ(link.fromsock->identifier,"Object"))throw std::runtime_error("Native Texture Coordinate reference-object context is not supported");
  }
  if(type=="ShaderNodeLightPath"){
   replacements.push_back({&link,{},STREQ(link.fromsock->identifier,"Is Camera Ray")?1.f:0.f});continue;
  }
  if(type=="ShaderNodeNewGeometry"&&STREQ(link.fromsock->identifier,"Pointiness")){
   replacements.push_back({&link,{},.5f});if(pointiness_as_eevee)*pointiness_as_eevee=true;continue;
  }
  const char *prefix=type=="ShaderNodeTexCoord"?"BTS_Coord_":type=="ShaderNodeNewGeometry"?"BTS_Geometry_":type=="ShaderNodeObjectInfo"?"BTS_Object_":nullptr;if(prefix){
   const auto name=std::string(prefix)+link.fromsock->identifier;
   // Ordinary channels can only advertise context that their actual compute
   // backend binds. Fragment and Raw Height extraction retain context for
   // their own typed admission checks before any GPU preparation.
   if(!defer_compute_context_validation)native_material_compute_context_value(name,false);
   replacements.push_back({&link,name,{}});
  }}
 for(const auto &replacement:replacements){auto &link=*replacement.link;
  if(replacement.literal){auto &value=add(tree,"ShaderNodeValue");auto &socket=output(value,"Value");static_cast<bNodeSocketValueFloat *>(socket.default_value)->value=*replacement.literal;bke::node_add_link(tree,value,socket,*link.tonode,*link.tosock);bke::node_remove_link(&tree,link);continue;}
  auto &attribute=add(tree,"ShaderNodeAttribute");STRNCPY(static_cast<NodeShaderAttribute *>(attribute.storage)->name,replacement.name.c_str());const auto type=link.fromsock->type;
  bNode *value=&attribute;
  if(fragment_normal&&native_material_fragment_object_matrices_attribute(replacement.name)){
   // node_shader_tex_coord.cc56-69: differentiate first, then normalize.
   // Attribute's GPU implementation differentiates its raw value, so preserve
   // that order with an explicit private Normalize node. Compute stays intact.
   auto &normalize=add(tree,"ShaderNodeVectorMath");normalize.custom1=NODE_VECTOR_MATH_NORMALIZE;bke::node_add_link(tree,attribute,output(attribute,"Vector"),normalize,input(normalize,"Vector"));value=&normalize;
  }
  bke::node_add_link(tree,*value,output(*value,type==SOCK_FLOAT?"Fac":type==SOCK_RGBA?"Color":"Vector"),*link.tonode,*link.tosock);bke::node_remove_link(&tree,link);}
}
bNodeTree *fallback_channel(Main &main,bNode &shader,int channel){
 auto *tree=bke::node_tree_add_tree(&main,"mPainter retained channel default","ShaderNodeTree");tree->flag|=NTREE_IS_GPU_SHADER_INTERNAL;
 auto &aov=add(*tree,"ShaderNodeOutputAOV");STRNCPY(static_cast<NodeShaderOutputAOV *>(aov.storage)->name,"BTS_Channel");
 auto &destination=input(aov,channel==0?"Color":"Value");
 const auto *source=bke::node_find_enabled_input_socket(shader,channel==0?(STREQ(shader.idname,"ShaderNodeBsdfPrincipled")?"Base Color":"Color"):channel==1?"Metallic":"Roughness");
 if(channel==0){auto *value=static_cast<bNodeSocketValueRGBA *>(destination.default_value);value->value[0]=value->value[1]=value->value[2]=.5f;value->value[3]=1;if(source&&source->type==SOCK_RGBA&&source->default_value)std::copy_n(static_cast<const bNodeSocketValueRGBA *>(source->default_value)->value,4,value->value);}
 else static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=source&&source->type==SOCK_FLOAT&&source->default_value?static_cast<const bNodeSocketValueFloat *>(source->default_value)->value:channel==1?0.f:.5f;
 BKE_ntree_update_without_main(*tree);return tree;
}
struct NativeChannelSurfaceAdaptation {
 bool mixed_closures=false,emission_as_color=false,pointiness_as_eevee=false;
 std::set<std::string> omitted_principled_lobes;
 const NativeMaterialDescription *parameters=nullptr;
};
void connect_shader_channel(bNodeTree &tree,bNode *shader,int channel,bNode &aov,bNodeSocket &destination,
                            NativeRawHeightProvenance *raw_height,bool *normal_perturbed,
                            std::set<const bNode *> &visiting,NativeChannelSurfaceAdaptation *adaptation){
 if(!shader)throw std::runtime_error("Native material has an unconnected shader branch");
 if(!visiting.insert(shader).second)throw std::runtime_error("Cyclic native shader mixture");
 const auto release=qScopeGuard([&]{visiting.erase(shader);});
 if(STREQ(shader->idname,"ShaderNodeBsdfPrincipled")){
  if(channel==0&&adaptation){
   const auto active=[&](const bNodeSocket &socket){
    if(socket.link){
     const auto *node=socket.link->fromnode;
     if(adaptation->parameters&&node&&STREQ(node->idname,"ShaderNodeAttribute")){
      const auto name=QString::fromUtf8(static_cast<const NodeShaderAttribute *>(node->storage)->name);
      if(name.startsWith("BTS_Param")){bool valid=false;const auto index=name.mid(9).toUInt(&valid);if(valid&&index<adaptation->parameters->parameters.size()){
       const auto &parameter=adaptation->parameters->parameters[index];
       return parameter.type==NativeParameterType::Scalar?parameter.value[0]!=0.:parameter.value[0]!=0.||parameter.value[1]!=0.||parameter.value[2]!=0.;
      }}
     }
     return true;
    }
    if(socket.type==SOCK_FLOAT)return static_cast<const bNodeSocketValueFloat *>(socket.default_value)->value!=0.f;
    if(socket.type==SOCK_RGBA){const auto *color=static_cast<const bNodeSocketValueRGBA *>(socket.default_value)->value;return color[0]!=0.f||color[1]!=0.f||color[2]!=0.f;}
    return false;
   };
   for(const auto &[socket,label]:{std::pair{"Transmission Weight","Transmission"},std::pair{"Sheen Weight","Sheen"},std::pair{"Subsurface Weight","Subsurface"}})
    if(active(input(*shader,socket)))adaptation->omitted_principled_lobes.insert(label);
   if(active(input(*shader,"Emission Color"))&&active(input(*shader,"Emission Strength")))adaptation->omitted_principled_lobes.insert("Emission");
  }
  if(channel<=2)copy_input(tree,input(*shader,channel==0?"Base Color":channel==1?"Metallic":"Roughness"),aov,destination);
  else if(channel==3){
   // Explicit fragment-Normal worker API only; the ordinary constructor never
   // requests channel3. Keep Bump/Normal Map/vector chains intact as COLOR.
   const auto &normal=input(*shader,"Normal");
   if(normal.link)bke::node_add_link(tree,*normal.link->fromnode,*normal.link->fromsock,aov,destination);
   else {auto &geometry=add(tree,"ShaderNodeNewGeometry");bke::node_add_link(tree,geometry,output(geometry,"Normal"),aov,destination);}
  }
  else if(channel==4){
   if(raw_height){const auto candidate=raw_height_candidate(input(*shader,"Normal"));if(!candidate.result.eligible)throw std::invalid_argument(candidate.result.reason);*raw_height=candidate.result.provenance;copy_input(tree,*candidate.height,aov,destination);}
   else {const auto bump=native_bump_capability(input(*shader,"Normal"));if(normal_perturbed)*normal_perturbed|=!bump.neutral;if(!bump.neutral)throw std::runtime_error("Native bump/normal extraction unavailable: "+bump.reason);static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=.5f;}
  }
  else if(channel==5){static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=1;}
  else throw std::runtime_error("Native displacement/normal extraction is not implemented yet");
 }else if(STREQ(shader->idname,"ShaderNodeBsdfDiffuse")||STREQ(shader->idname,"ShaderNodeBsdfGlossy")||STREQ(shader->idname,"ShaderNodeBsdfAnisotropic")||STREQ(shader->idname,"ShaderNodeBsdfGlass")||STREQ(shader->idname,"ShaderNodeBsdfRefraction")||STREQ(shader->idname,"ShaderNodeBsdfVelvet")||STREQ(shader->idname,"ShaderNodeBsdfToon")){
  if(channel==0)copy_input(tree,input(*shader,"Color"),aov,destination);
  else if(channel==2){if(auto *rough=bke::node_find_enabled_input_socket(*shader,"Roughness"))copy_input(tree,*rough,aov,destination);else static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=.5f;}
  else if(channel==3){const auto &normal=input(*shader,"Normal");if(normal.link)bke::node_add_link(tree,*normal.link->fromnode,*normal.link->fromsock,aov,destination);else{auto &geometry=add(tree,"ShaderNodeNewGeometry");bke::node_add_link(tree,geometry,output(geometry,"Normal"),aov,destination);}}
  else if(channel==4){const auto &normal=input(*shader,"Normal");if(raw_height){const auto candidate=raw_height_candidate(normal);if(!candidate.result.eligible)throw std::invalid_argument(candidate.result.reason);*raw_height=candidate.result.provenance;copy_input(tree,*candidate.height,aov,destination);}else{const auto bump=native_bump_capability(normal);if(normal_perturbed)*normal_perturbed|=!bump.neutral;if(!bump.neutral)throw std::runtime_error("Native bump/normal requires surface derivatives: "+bump.reason);static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=.5f;}}
  else static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=channel==5?1:0;
 }else if(STREQ(shader->idname,"ShaderNodeEmission")){
  if(raw_height)throw std::invalid_argument("Raw authored Height requires a supported Principled Normal path");
  if(channel==3)throw std::invalid_argument("Fragment Normal requires a supported Principled Normal path; Emission has no Normal input");
  if(channel==0){if(adaptation)adaptation->emission_as_color=true;auto &scale=add(tree,"ShaderNodeVectorMath");scale.custom1=NODE_VECTOR_MATH_SCALE;
   const auto &color=input(*shader,"Color");auto &vector=input(scale,"Vector");if(color.link)bke::node_add_link(tree,*color.link->fromnode,*color.link->fromsock,scale,vector);else std::copy_n(static_cast<const bNodeSocketValueRGBA *>(color.default_value)->value,3,static_cast<bNodeSocketValueVector *>(vector.default_value)->value);
   copy_input(tree,input(*shader,"Strength"),scale,input(scale,"Scale"));bke::node_add_link(tree,scale,output(scale,"Vector"),aov,destination);
  }
  else static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=channel==2?.5f:channel==4?.5f:channel==5?1:0;
 }else if(STREQ(shader->idname,"ShaderNodeMixShader")){
  auto &factor=input(*shader,"Fac");auto &first=input(*shader,"Shader"),&second=input(*shader,"Shader_001");
  std::optional<float> literal;
  if(!factor.link)literal=static_cast<const bNodeSocketValueFloat *>(factor.default_value)->value;
  else if(const auto *source=linked_shader(factor);source&&STREQ(source->idname,"ShaderNodeLightPath")){
   const bNodeSocket *edge=&factor;while(edge->link&&STREQ(edge->link->fromnode->idname,"NodeReroute"))edge=static_cast<const bNodeSocket *>(edge->link->fromnode->inputs.first);
   literal=STREQ(edge->link->fromsock->identifier,"Is Camera Ray")?1.f:0.f;
  }
  if(literal&&(*literal<=0.f||*literal>=1.f)){
   connect_shader_channel(tree,linked_shader(*literal<=0.f?first:second),channel,aov,destination,raw_height,normal_perturbed,visiting,adaptation);return;
  }
  if(raw_height)throw std::invalid_argument("Raw authored Height cannot discard a dynamic shader mixture");
  if(adaptation)adaptation->mixed_closures=true;
  auto &mix=add(tree,"ShaderNodeMix");auto &settings=*static_cast<NodeShaderMix *>(mix.storage);
  const bool color=channel==0||channel==3;settings.data_type=color?SOCK_RGBA:SOCK_FLOAT;settings.clamp_factor=true;settings.clamp_result=false;
  BKE_ntree_update_without_main(tree);
  copy_input(tree,factor,mix,input(mix,"Factor_Float"));
  connect_shader_channel(tree,linked_shader(first),channel,mix,input(mix,color?"A_Color":"A_Float"),nullptr,normal_perturbed,visiting,adaptation);
  connect_shader_channel(tree,linked_shader(second),channel,mix,input(mix,color?"B_Color":"B_Float"),nullptr,normal_perturbed,visiting,adaptation);
  bke::node_add_link(tree,mix,output(mix,color?"Result_Color":"Result_Float"),aov,destination);
 }else throw std::runtime_error(std::string("Unsupported channel surface: ")+shader->idname);
}
bNodeTree *channel_tree(Main &main,bNodeTree &source,int channel,NativeRawHeightProvenance *raw_height=nullptr,bool *normal_perturbed=nullptr,NativeChannelSurfaceAdaptation *adaptation=nullptr){
 auto *tree=bke::node_tree_add_tree(&main,("mPainter channel "+std::to_string(channel)).c_str(),"ShaderNodeTree");tree->flag|=NTREE_IS_GPU_SHADER_INTERNAL;bool accepted=false;const auto release_failed=qScopeGuard([&]{if(!accepted)BKE_id_free(&main,&tree->id);});
 nodes::InlineShaderNodeTreeParams options;options.target_engine_=SHD_OUTPUT_EEVEE;if(!nodes::inline_shader_node_tree(source,*tree,options)||!options.r_error_messages.is_empty())throw std::runtime_error("Blender could not inline this shader graph");BKE_ntree_update_without_main(*tree);
 auto *surface=ntreeShaderOutputNode(tree,SHD_OUTPUT_EEVEE);if(!surface)throw std::runtime_error("Native material output missing after inlining");auto *shader=linked_shader(input(*surface,"Surface"));if(!shader)throw std::runtime_error("Native material surface is not connected");
 auto &aov=add(*tree,"ShaderNodeOutputAOV");STRNCPY(static_cast<NodeShaderOutputAOV *>(aov.storage)->name,"BTS_Channel");auto &destination=input(aov,(channel==0||channel==3)?"Color":"Value");
 if(channel==14){
  auto &displacement=input(*surface,"Displacement");auto *source=linked_shader(displacement);
  if(!source)static_cast<bNodeSocketValueFloat *>(destination.default_value)->value=0;
  else if(STREQ(source->idname,"ShaderNodeDisplacement")){
   if(input(*source,"Normal").link)throw std::runtime_error("A custom displacement normal cannot be represented by the scalar channel");
   auto &subtract=add(*tree,"ShaderNodeMath");subtract.custom1=NODE_MATH_SUBTRACT;auto *left=static_cast<bNodeSocket *>(subtract.inputs.first);copy_input(*tree,input(*source,"Height"),subtract,*left);copy_input(*tree,input(*source,"Midlevel"),subtract,*left->next);
   auto &scale=add(*tree,"ShaderNodeMath");scale.custom1=NODE_MATH_MULTIPLY;auto *factor=static_cast<bNodeSocket *>(scale.inputs.first);bke::node_add_link(*tree,subtract,output(subtract,"Value"),scale,*factor);copy_input(*tree,input(*source,"Scale"),scale,*factor->next);
   bNode *result=&scale;
   if(source->custom1==SHD_SPACE_OBJECT){auto &metric=add(*tree,"ShaderNodeAttribute");STRNCPY(static_cast<NodeShaderAttribute *>(metric.storage)->name,"BTS_Object_DisplacementScale");auto &world=add(*tree,"ShaderNodeMath");world.custom1=NODE_MATH_MULTIPLY;auto *value=static_cast<bNodeSocket *>(world.inputs.first);bke::node_add_link(*tree,scale,output(scale,"Value"),world,*value);bke::node_add_link(*tree,metric,output(metric,"Fac"),world,*value->next);result=&world;}
   bke::node_add_link(*tree,*result,output(*result,"Value"),aov,destination);
  }else throw std::runtime_error(std::string("Unsupported displacement graph: ")+source->idname);
 }else {
  std::set<const bNode *> visiting;connect_shader_channel(*tree,shader,channel,aov,destination,raw_height,normal_perturbed,visiting,adaptation);
 }
 // Native AOV roots retain their dependencies; unused BSDF closures are pruned.
 for(auto &socket:surface->inputs)bke::node_remove_socket_links(*tree,socket);
 bind_context(*tree,channel==3||raw_height!=nullptr,channel==3,adaptation?&adaptation->pointiness_as_eevee:nullptr);BKE_ntree_update_without_main(*tree);accepted=true;return tree;
}
}
NativeRawHeightCandidate classify_native_raw_height(const blender::bNodeSocket &normal){return raw_height_candidate(normal).result;}
PreparedNativeRawHeight prepare_native_raw_height(DetachedNativeMaterial detached,const std::atomic_bool &cancel,const NativeMaterialDescription *retained_source,NativeWorldNormalSurfaceContract surface_contract){
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Raw authored Height preparation requires a worker");
 if(cancel.load())throw std::runtime_error("Raw authored Height preparation cancelled");
 if(!detached.main||!detached.material||!detached.material->nodetree)throw std::invalid_argument("Raw authored Height detached source is missing");
 // Initialize owned topology before interpreting flags. A flag which simply
 // has not been updated yet is not evidence of an inactive/corrupt edge.
 blender::BKE_ntree_update_after_single_tree_change(*detached.main,*detached.material->nodetree);
 auto graph=finish_native_material_source(std::move(detached),cancel,retained_source,surface_contract);const auto original_description=native_material_description_json(graph->description);auto &original=*graph->material->nodetree;
 // Inlining can legitimately erase ignored edges. Phase one deliberately
 // rejects even unrelated inactive source branches before making another copy;
 // this conservative proof-only restriction never runs on normal imports.
 std::set<const blender::bNodeTree *> checked;const auto active_source=[&](auto &&self,const blender::bNodeTree &tree)->void{if(!checked.insert(&tree).second)return;for(const auto &link:tree.links)if(!link.fromnode||!link.fromsock||!link.tonode||!link.tosock||!(link.flag&blender::NODE_LINK_VALID)||!link.is_used())throw std::invalid_argument("Phase-one raw Height proof excludes inactive source links, including unrelated branches");for(const auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeGroup")&&node.id)self(self,*reinterpret_cast<const blender::bNodeTree *>(node.id));};active_source(active_source,original);
 const auto original_nodes=blender::BLI_listbase_count(&original.nodes);auto *original_surface=blender::ntreeShaderOutputNode(&original,blender::SHD_OUTPUT_EEVEE);auto *original_top=original_surface?linked_shader(input(*original_surface,"Surface")):nullptr;if(!original_top)throw std::invalid_argument("Raw authored Height original source has no shader");std::vector<std::pair<blender::bNodeSocket *,blender::bNodeLink *>> original_links;for(auto &socket:original_top->inputs)original_links.emplace_back(&socket,socket.link);
 auto *runtime=reinterpret_cast<blender::Material *>(BKE_id_copy_ex(graph->main.get(),&graph->material->id,nullptr,0));if(!runtime||!runtime->nodetree)throw std::runtime_error("Cannot copy raw authored Height runtime source");auto &tree=*runtime->nodetree;tree.flag|=blender::NTREE_IS_GPU_SHADER_INTERNAL;auto *surface=blender::ntreeShaderOutputNode(&tree,blender::SHD_OUTPUT_EEVEE);auto *top=surface?linked_shader(input(*surface,"Surface")):nullptr;if(!top)throw std::invalid_argument("Raw authored Height runtime source has no shader");
 bind_native_material_ramps(tree,*top,graph->description);bind_parameters(tree,*top,graph->description);bind_native_material_mappings(tree,*top,*original_top,graph->description);blender::BKE_ntree_update_without_main(tree);
 PreparedNativeRawHeight result;result.source=std::move(graph);result.tree=channel_tree(*result.source->main,tree,4,&result.provenance);
 if(cancel.load())throw std::runtime_error("Raw authored Height preparation cancelled");
 if(original_description!=native_material_description_json(result.source->description))throw std::logic_error("Raw authored Height changed default source capabilities");
 if(blender::BLI_listbase_count(&original.nodes)!=original_nodes||!std::all_of(original_links.begin(),original_links.end(),[](const auto &edge){return edge.first->link==edge.second;}))throw std::logic_error("Raw authored Height modified its original source graph");
 return result;
}
namespace {
bool active_source_displacement(blender::Material &material){
 if(!material.nodetree)return false;auto *surface=blender::ntreeShaderOutputNode(material.nodetree,blender::SHD_OUTPUT_EEVEE);if(!surface)return false;
 const auto *displacement=blender::bke::node_find_enabled_input_socket(*surface,"Displacement");return displacement&&displacement->link&&(displacement->link->flag&blender::NODE_LINK_VALID)&&displacement->link->is_used();
}
void require_world_normal_surface(const NativeMaterialGraph &graph){
 if(graph.world_normal_source_displacement&&graph.world_normal_surface_contract!=NativeWorldNormalSurfaceContract::PainterUndisplaced)throw std::invalid_argument("Native world Normal requires an undisplaced source; authored Displacement needs an actual displaced surface binding or the explicit effective Painter-undisplaced contract");
}
}
PreparedNativeFragmentNormal prepare_native_fragment_normal(DetachedNativeMaterial detached,const std::atomic_bool &cancel,const NativeMaterialDescription *retained_source){
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Fragment Normal preparation requires a worker");
 if(cancel.load())throw std::runtime_error("Fragment Normal preparation cancelled");
 if(!detached.main||!detached.material||!detached.material->nodetree)throw std::invalid_argument("Fragment Normal detached source is missing");
 blender::BKE_ntree_update_after_single_tree_change(*detached.main,*detached.material->nodetree);
 auto graph=finish_native_material_source(std::move(detached),cancel,retained_source);
 return prepare_owned_native_fragment_normal(graph,cancel);
}
PreparedNativeFragmentNormal prepare_owned_native_fragment_normal(const std::shared_ptr<NativeMaterialGraph> &graph,const std::atomic_bool &cancel){
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Fragment Normal preparation requires a worker");
 if(cancel.load())throw std::runtime_error("Fragment Normal preparation cancelled");
 if(!graph||!graph->main||!graph->material||!graph->material->nodetree)throw std::invalid_argument("Fragment Normal prepared source is missing");
 if(graph.use_count()!=1)throw std::invalid_argument("Fragment Normal preparation requires the exclusive unpublished source owner");
 require_world_normal_surface(*graph);
 const auto original_description=native_material_description_json(graph->description);auto &original=*graph->material->nodetree;
 const auto original_nodes=blender::BLI_listbase_count(&original.nodes);auto *original_surface=blender::ntreeShaderOutputNode(&original,blender::SHD_OUTPUT_EEVEE);auto *original_top=original_surface?linked_shader(input(*original_surface,"Surface")):nullptr;
 if(!original_top)throw std::invalid_argument("Fragment Normal original source has no shader");
 std::vector<std::pair<blender::bNodeSocket *,blender::bNodeLink *>> original_links;for(auto &socket:original_top->inputs)original_links.emplace_back(&socket,socket.link);
 auto *runtime=reinterpret_cast<blender::Material *>(BKE_id_copy_ex(graph->main.get(),&graph->material->id,nullptr,0));
 if(!runtime||!runtime->nodetree)throw std::runtime_error("Cannot copy fragment Normal runtime source");
 auto &tree=*runtime->nodetree;tree.flag|=blender::NTREE_IS_GPU_SHADER_INTERNAL;auto *surface=blender::ntreeShaderOutputNode(&tree,blender::SHD_OUTPUT_EEVEE);auto *top=surface?linked_shader(input(*surface,"Surface")):nullptr;
 if(!top)throw std::invalid_argument("Fragment Normal runtime source has no shader");
 bind_native_material_ramps(tree,*top,graph->description);bind_parameters(tree,*top,graph->description);bind_native_material_mappings(tree,*top,*original_top,graph->description);blender::BKE_ntree_update_without_main(tree);
 PreparedNativeFragmentNormal result;result.source=graph;result.tree=channel_tree(*result.source->main,tree,3);
 if(cancel.load())throw std::runtime_error("Fragment Normal preparation cancelled");
 if(original_description!=native_material_description_json(result.source->description))throw std::logic_error("Fragment Normal changed default source capabilities");
 if(blender::BLI_listbase_count(&original.nodes)!=original_nodes||!std::all_of(original_links.begin(),original_links.end(),[](const auto &edge){return edge.first->link==edge.second;}))throw std::logic_error("Fragment Normal modified its original source graph");
 return result;
}
void prepare_optional_native_world_normal(const std::shared_ptr<NativeMaterialGraph> &graph,const std::atomic_bool &cancel){
 if(!graph)throw std::invalid_argument("Native world Normal source is missing");
 if(cancel.load())throw std::runtime_error("Native world Normal preparation cancelled");
 if(graph->world_normal_tree||graph->world_normal_perturbed)return;
 // Typed classification is produced after Blender group/muted-node inlining
 // and effective Principled Normal-path inspection. Diagnostic wording is not
 // an admission signal; flat/bypass-only sources allocate no private tree.
 const auto bump=std::find_if(graph->channels.begin(),graph->channels.end(),[](const auto &c){return c.channel==4;});
 if(bump==graph->channels.end()||!bump->normal_perturbed)return;
 graph->world_normal_perturbed=true;
 try{
  // Source-authored displacement is not silently reinterpreted. Only actual
  // Painter resource consumers choose the immutable undisplaced contract;
  // the separate world planner still rejects active document displacement.
  require_world_normal_surface(*graph);
  auto prepared=prepare_owned_native_fragment_normal(graph,cancel);graph->world_normal_tree=prepared.tree;
 }
 catch(const std::exception &error){if(cancel.load())throw;graph->world_normal_error=QString::fromUtf8(error.what());}
}
void validate_native_mapping_override(const NativeMaterialGraph &graph,const NativeMaterialParameter &parameter,bool require_active){
 using namespace blender;auto *surface=ntreeShaderOutputNode(graph.material->nodetree,SHD_OUTPUT_EEVEE);auto *top=surface?linked_shader(input(*surface,"Surface")):nullptr;
 if(!top)throw std::invalid_argument("Native Mapping original source is unavailable");
 if(!parameter.mapping_transform_supported())throw std::invalid_argument("Native Mapping override source is unsupported");
 const auto &link=native_mapping_source_link(*top,parameter);
 if(!native_mapping_edge_active(link)){
  if(require_active)throw std::invalid_argument("Extra Mapping transform unavailable: original link is muted, invalid or its socket is unavailable");
  return; // Historical authored rows remain stored, but no adapter uses them.
 }
 validate_native_mapping_source_node(link);
}
void validate_native_mapping_instance(const NativeMaterialGraph &graph,const NativeMaterialDescription &instance){
 for(const auto &parameter:instance.parameters)if(!parameter.mapping_transform.identity())validate_native_mapping_override(graph,parameter,false);
}
NativeMaterialGraph::NativeMaterialGraph(MainOwner owner,blender::Material *source,NativeMaterialDescription inputs,const std::atomic_bool &cancel,NativeWorldNormalSurfaceContract surface_contract):main(std::move(owner)),material(source),description(std::move(inputs)),world_normal_surface_contract(surface_contract),world_normal_source_displacement(active_source_displacement(*source)){
 native_world_normal_surface_name(surface_contract);
 using namespace blender;const auto warning=[&](const QString &text){if(!description.warnings.contains(text))description.warnings.append(text);};if(description.node_types.contains("ShaderNodeBevel"))warning(QString::fromUtf8(native_material_node_context_warning("ShaderNodeBevel").data()));if(description.node_types.contains("ShaderNodeAmbientOcclusion"))warning("Ambient Occlusion uses the project baked AO map. Rebake to change ray distance/normal policy; Inside/Only Local modes are unsupported.");auto &original=*material->nodetree;const auto original_nodes=BLI_listbase_count(&original.nodes);std::vector<std::pair<bNodeSocket *,bNodeLink *>> original_links;auto *original_surface=ntreeShaderOutputNode(&original,SHD_OUTPUT_EEVEE);auto *original_top=original_surface?linked_shader(input(*original_surface,"Surface")):nullptr;if(!original_top)throw std::runtime_error("Native material has no shader");for(auto &socket:original_top->inputs)original_links.emplace_back(&socket,socket.link);
 // The encoded/source graph must retain its own defaults and links. Parameter
 // attributes belong only to a runtime copy used for native channel inlining.
 auto *runtime_material=reinterpret_cast<Material *>(BKE_id_copy_ex(main.get(),&material->id,nullptr,0));if(!runtime_material||!runtime_material->nodetree)throw std::runtime_error("Cannot copy native parameter material");auto &tree=*runtime_material->nodetree;tree.flag|=NTREE_IS_GPU_SHADER_INTERNAL;auto *surface=ntreeShaderOutputNode(&tree,SHD_OUTPUT_EEVEE);auto *top=surface?linked_shader(input(*surface,"Surface")):nullptr;if(!top)throw std::runtime_error("Native parameter tree has no shader");bind_native_material_ramps(tree,*top,description);bind_parameters(tree,*top,description);bind_native_material_mappings(tree,*top,*original_top,description);BKE_ntree_update_without_main(tree);
 for(const auto &parameter:description.parameters){std::array<float,4> value{0,0,0,1};if(parameter.type==NativeParameterType::Scalar)value={float(parameter.value[0]),float(parameter.value[0]),float(parameter.value[0]),1};else for(int i=0;i<parameter.components();++i)value[i]=float(parameter.value[i]);parameters.push_back(value);}
 NativeChannelSurfaceAdaptation adaptation;adaptation.parameters=&description;
 for(int channel:{0,1,2,4,5,14}){if(cancel.load())throw std::runtime_error("Native material preparation cancelled");NativeMaterialChannelGraph result;result.channel=channel;try{result.tree=channel_tree(*main,tree,channel,nullptr,&result.normal_perturbed,&adaptation);}catch(const std::exception &error){result.error=QString::fromUtf8(error.what());if(channel<=2){result.tree=fallback_channel(*main,*original_top,channel);description.warnings.removeIf([&](const QString &text){return text.contains("not replaced by a constant")&&(text.startsWith(channel==0?"Base Color:":channel==1?"Metallic:":"Roughness:")||(channel==0&&text.startsWith("Color:")));});warning(QString("%1 input is unsupported; retained its default and imported the other channels: %2").arg(channel==0?"Color":channel==1?"Metallic":"Roughness").arg(result.error));}}channels.push_back(std::move(result));}
 if(!adaptation.omitted_principled_lobes.empty()){QStringList omitted;for(const auto &lobe:adaptation.omitted_principled_lobes)omitted.append(QString::fromStdString(lobe));warning("Principled "+omitted.join(", ")+" inputs are preserved in the source but are not evaluated by viewport material channels.");}
 if(adaptation.pointiness_as_eevee)warning("Geometry Pointiness uses Blender EEVEE's constant 0.5. Cycles mesh-curvature pointiness is not evaluated.");
 if(adaptation.mixed_closures)warning("Mix Shader is evaluated as blended material channels. Separate BSDF lobes are not reproduced by viewport shading.");
 if(adaptation.emission_as_color)warning("Emission is evaluated as color. Self-illumination is not reproduced by viewport shading.");
 if(world_normal_source_displacement&&world_normal_surface_contract==NativeWorldNormalSurfaceContract::PainterUndisplaced&&std::any_of(channels.begin(),channels.end(),[](const auto &channel){return channel.normal_perturbed;}))warning(QString::fromUtf8(native_world_normal_surface_warning().data()));
 description.channels.clear();for(const auto &channel:channels)if(channel.tree)description.channels.push_back(channel.channel);
 original_inputs_preserved=BLI_listbase_count(&original.nodes)==original_nodes&&std::all_of(original_links.begin(),original_links.end(),[](const auto &entry){return entry.first->link==entry.second;});if(!original_inputs_preserved)throw std::runtime_error("Native preparation modified its source graph");
}
}
