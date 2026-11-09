// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialGraph.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include <algorithm>
#include <limits>
#include <string>
namespace bts::native_mapping_test {
inline blender::bNodeSocket &socket(blender::bNode &node,const char *id,bool input){for(auto &value:input?node.inputs:node.outputs)if(STREQ(value.identifier,id))return value;throw std::runtime_error("Mapping fixture socket missing");}
struct Fixture {NativeMaterialGraph::MainOwner owner{blender::BKE_main_new(),blender::BKE_main_free};blender::Material *material=nullptr;NativeMaterialDescription description;};
inline Fixture fixture(const char *basis){
 using namespace blender;Fixture result;auto &main=*result.owner;result.material=BKE_material_add(&main,"Linked Mapping fixture");auto &tree=*result.material->nodetree;
 auto *group=bke::node_tree_add_tree(&main,"Independent linked coordinates","ShaderNodeTree");
 const auto first=std::string(group->tree_interface.add_socket("Mapping A","","NodeSocketVector",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);
 const auto second=std::string(group->tree_interface.add_socket("Mapping B","","NodeSocketVector",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);
 const auto shader_output=std::string(group->tree_interface.add_socket("Shader","","NodeSocketShader",NODE_INTERFACE_SOCKET_OUTPUT,nullptr)->identifier);
 auto *input=bke::node_add_node(nullptr,*group,UString("NodeGroupInput"));auto *output=bke::node_add_node(nullptr,*group,UString("NodeGroupOutput"));auto *shader=bke::node_add_node(nullptr,*group,UString("ShaderNodeBsdfPrincipled"));auto *separate=bke::node_add_node(nullptr,*group,UString("ShaderNodeSeparateXYZ"));BKE_ntree_update_after_single_tree_change(main,*group);
 bke::node_add_link(*group,*input,socket(*input,first.c_str(),false),*shader,socket(*shader,"Base Color",true));bke::node_add_link(*group,*input,socket(*input,second.c_str(),false),*separate,socket(*separate,"Vector",true));bke::node_add_link(*group,*separate,socket(*separate,"X",false),*shader,socket(*shader,"Roughness",true));bke::node_add_link(*group,*shader,socket(*shader,"BSDF",false),*output,socket(*output,shader_output.c_str(),true));BKE_ntree_update_after_single_tree_change(main,*group);
 auto *top=bke::node_add_node(nullptr,tree,UString("ShaderNodeGroup"));top->id=&group->id;id_us_plus(top->id);auto *coords=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexCoord"));auto *surface=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(main,tree);
 bke::node_add_link(tree,*coords,socket(*coords,basis,false),*top,socket(*top,first.c_str(),true));bke::node_add_link(tree,*coords,socket(*coords,basis,false),*top,socket(*top,second.c_str(),true));bke::node_add_link(tree,*top,socket(*top,shader_output.c_str(),false),*surface,socket(*surface,"Surface",true));BKE_ntree_update_after_single_tree_change(main,tree);
 result.description.material="Linked Mapping fixture";result.description.group="Independent linked coordinates";result.description.surface_type="ShaderNodeGroup";
 for(const auto &id:{first,second}){NativeMaterialParameter parameter;parameter.identifier=QString::fromStdString(id);parameter.name=id==first?"Mapping A":"Mapping B";parameter.type=NativeParameterType::Vector;parameter.mapping=true;parameter.source_node="ShaderNodeTexCoord";parameter.source_socket=basis;parameter.minimum=-double(std::numeric_limits<float>::max());parameter.maximum=double(std::numeric_limits<float>::max());const auto *value=static_cast<const bNodeSocketValueVector *>(socket(*top,id.c_str(),true).default_value);std::copy_n(value->value,3,parameter.value.begin());result.description.parameters.push_back(parameter);}
 return result;
}

// One retained original Mapping feeds both top-group edges. The runtime extra
// transform must never rewrite this node or its linked control expressions.
inline constexpr std::array<float,3> original_location{.17f,-.23f,.31f},original_rotation{.11f,-.19f,.29f},original_scale{1.7f,.6f,-1.2f};
inline constexpr std::array<float,3> linked_location{-.21f,.14f,.27f},linked_rotation{-.17f,.23f,-.31f},linked_scale{-.9f,1.3f,.7f};
inline Fixture upstream_fixture(const char *basis,int mode,bool linked_controls=false,bool chained=false,bool zero_scale=false){
 using namespace blender;auto result=fixture(basis);auto &tree=*result.material->nodetree;
 auto *mapping=bke::node_add_node(nullptr,tree,UString("ShaderNodeMapping"));mapping->custom1=mode;
 auto scale=original_scale,actual_linked_scale=linked_scale;if(zero_scale)scale[2]=actual_linked_scale[2]=0;
 for(auto [id,values]:{std::pair{"Location",original_location},{"Rotation",original_rotation},{"Scale",scale}}){auto &target=socket(*mapping,id,true);std::copy_n(values.data(),3,static_cast<bNodeSocketValueVector *>(target.default_value)->value);
  if(linked_controls){const auto linked=std::string_view(id)=="Location"?linked_location:std::string_view(id)=="Rotation"?linked_rotation:actual_linked_scale;auto *combine=bke::node_add_node(nullptr,tree,UString("ShaderNodeCombineXYZ"));for(int c=0;c<3;++c)static_cast<bNodeSocketValueFloat *>(socket(*combine,c==0?"X":c==1?"Y":"Z",true).default_value)->value=linked[c];bke::node_add_link(tree,*combine,socket(*combine,"Vector",false),*mapping,target);}}
 std::vector<bNodeLink *> targets;for(auto &link:tree.links)if(STREQ(link.fromnode->idname,"ShaderNodeTexCoord")&&STREQ(link.tonode->idname,"ShaderNodeGroup"))targets.push_back(&link);
 if(targets.size()!=2)throw std::runtime_error("Upstream Mapping fixture edges missing");auto *from=targets[0]->fromnode;auto *out=targets[0]->fromsock;
 if(chained){auto *previous=bke::node_add_node(nullptr,tree,UString("ShaderNodeMapping"));previous->custom1=NODE_MAPPING_TYPE_POINT;
  const std::array<std::array<float,3>,3> values{{{-.1f,.3f,-.2f},{-.07f,.13f,-.17f},{.8f,1.2f,.5f}}};for(int row=0;row<3;++row)std::copy_n(values[row].data(),3,static_cast<bNodeSocketValueVector *>(socket(*previous,row==0?"Location":row==1?"Rotation":"Scale",true).default_value)->value);
  bke::node_add_link(tree,*from,*out,*previous,socket(*previous,"Vector",true));from=previous;out=&socket(*previous,"Vector",false);}
 bke::node_add_link(tree,*from,*out,*mapping,socket(*mapping,"Vector",true));
 for(auto *link:targets){auto *target=link->tonode;auto *input=link->tosock;bke::node_remove_link(&tree,*link);bke::node_add_link(tree,*mapping,socket(*mapping,"Vector",false),*target,*input);}
 BKE_ntree_update_after_single_tree_change(*result.owner,tree);
 for(auto &p:result.description.parameters){p.source_node="ShaderNodeMapping";p.source_socket="Vector";}
 return result;
}
// A retained vector expression fans out to two independent top-group inputs.
// The source operation and its linked second operand must never be rewritten by
// per-instance POINT rows. Values are deliberately signed and non-uniform.
inline constexpr std::array<float,3> vector_math_addend{.17f,-.23f,.31f},vector_math_factor{.75f,-1.25f,2.f};
inline Fixture vector_math_fixture(const char *basis,int operation,bool linked_operand=true){
 using namespace blender;auto result=fixture(basis);auto &tree=*result.material->nodetree;
 auto *math=bke::node_add_node(nullptr,tree,UString("ShaderNodeVectorMath"));math->custom1=operation;
 const auto operand=operation==NODE_VECTOR_MATH_MULTIPLY?vector_math_factor:vector_math_addend;
 std::copy_n(operand.data(),3,static_cast<bNodeSocketValueVector *>(socket(*math,"Vector_001",true).default_value)->value);
 if(linked_operand){auto *combine=bke::node_add_node(nullptr,tree,UString("ShaderNodeCombineXYZ"));for(int c=0;c<3;++c)static_cast<bNodeSocketValueFloat *>(socket(*combine,c==0?"X":c==1?"Y":"Z",true).default_value)->value=operand[c];bke::node_add_link(tree,*combine,socket(*combine,"Vector",false),*math,socket(*math,"Vector_001",true));}
 std::vector<bNodeLink *> targets;for(auto &link:tree.links)if(STREQ(link.fromnode->idname,"ShaderNodeTexCoord")&&STREQ(link.tonode->idname,"ShaderNodeGroup"))targets.push_back(&link);
 if(targets.size()!=2)throw std::runtime_error("Vector Math fixture fanout missing");
 bke::node_add_link(tree,*targets[0]->fromnode,*targets[0]->fromsock,*math,socket(*math,"Vector",true));
 for(auto *link:targets){auto *top=link->tonode;auto *input=link->tosock;bke::node_remove_link(&tree,*link);bke::node_add_link(tree,*math,socket(*math,"Vector",false),*top,*input);}
 BKE_ntree_update_after_single_tree_change(*result.owner,tree);
 for(auto &parameter:result.description.parameters){parameter.source_node="ShaderNodeVectorMath";parameter.source_socket="Vector";}
 return result;
}

inline Fixture unconsumed_fixture(bool mapping,bool unsupported_normal){
 using namespace blender;auto result=mapping?upstream_fixture("UV",NODE_MAPPING_TYPE_POINT):fixture("UV");bNodeTree *group=nullptr;for(auto &node:result.material->nodetree->nodes)if(STREQ(node.idname,"ShaderNodeGroup"))group=reinterpret_cast<bNodeTree *>(node.id);if(!group)throw std::runtime_error("Unused Mapping group missing");
 bNode *input=nullptr,*shader=nullptr;for(auto &node:group->nodes){if(STREQ(node.idname,"NodeGroupInput"))input=&node;if(STREQ(node.idname,"ShaderNodeBsdfPrincipled"))shader=&node;}
 auto &source=socket(*input,result.description.parameters[0].identifier.toUtf8().constData(),false);std::vector<bNodeLink *> links;for(auto &link:group->links)if(link.fromsock==&source)links.push_back(&link);for(auto *link:links)bke::node_remove_link(group,*link);
 if(unsupported_normal)bke::node_add_link(*group,*input,source,*shader,socket(*shader,"Normal",true));BKE_ntree_update_after_single_tree_change(*result.owner,*group);BKE_ntree_update_after_single_tree_change(*result.owner,*result.material->nodetree);return result;
}

}
