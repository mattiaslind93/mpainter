// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialRawHeight.hh"
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
#include "IMB_colormanagement.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <set>

namespace bts::native_raw_height_test {
inline void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
inline blender::bNodeSocket &socket(blender::bNode &node,const char *id,bool in){for(auto &value:in?node.inputs:node.outputs)if(STREQ(value.identifier,id))return value;throw std::runtime_error("Raw Height fixture socket missing");}
enum class Kind {Affine,Constant,NoHeight,CustomNormal,ColorOutput,DoubleBump,LinkedStrength,Invert,NormalInHeight,InactiveHeight,UnrelatedInactive,MutedBump};
inline DetachedNativeMaterial fixture(Kind kind=Kind::Affine,float constant=.5f){
 using namespace blender;DetachedNativeMaterial result;result.main={BKE_main_new(),BKE_main_free};auto &main=*result.main;IMB_colormanagement_working_space_init_default(&main);result.material=BKE_material_add(&main,"Native raw Height fixture");auto &tree=*result.material->nodetree;
 auto *group=bke::node_tree_add_tree(&main,"Raw Height input fixture","ShaderNodeTree");
 const auto bias=std::string(group->tree_interface.add_socket("Bias","","NodeSocketFloat",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);
 const auto gain=std::string(group->tree_interface.add_socket("Gain","","NodeSocketFloat",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);
 const auto coords=std::string(group->tree_interface.add_socket("Coordinates","","NodeSocketVector",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);
 const auto out=std::string(group->tree_interface.add_socket("Shader","","NodeSocketShader",NODE_INTERFACE_SOCKET_OUTPUT,nullptr)->identifier);
 auto *inputs=bke::node_add_node(nullptr,*group,UString("NodeGroupInput"));auto *output=bke::node_add_node(nullptr,*group,UString("NodeGroupOutput"));auto *pbr=bke::node_add_node(nullptr,*group,UString("ShaderNodeBsdfPrincipled"));auto *bump=bke::node_add_node(nullptr,*group,UString("ShaderNodeBump"));auto *separate=bke::node_add_node(nullptr,*group,UString("ShaderNodeSeparateXYZ"));
 const auto math=[&](int operation){auto *node=bke::node_add_node(nullptr,*group,UString("ShaderNodeMath"));node->custom1=operation;return node;};
 auto *x=math(NODE_MATH_MULTIPLY),*y=math(NODE_MATH_MULTIPLY),*z=math(NODE_MATH_MULTIPLY),*sum=math(NODE_MATH_ADD),*xyz=math(NODE_MATH_ADD),*scaled=math(NODE_MATH_MULTIPLY),*height=math(NODE_MATH_ADD);
 BKE_ntree_update_after_single_tree_change(main,*group);
 const auto link=[&](bNode &from,const char *a,bNode &to,const char *b){bke::node_add_link(*group,from,socket(from,a,false),to,socket(to,b,true));};
 const auto second=[](bNode &node)->bNodeSocket &{return *static_cast<bNodeSocket *>(node.inputs.first)->next;};
 link(*inputs,coords.c_str(),*separate,"Vector");link(*separate,"X",*x,"Value");link(*separate,"Y",*y,"Value");link(*separate,"Z",*z,"Value");static_cast<bNodeSocketValueFloat *>(second(*x).default_value)->value=.2f;static_cast<bNodeSocketValueFloat *>(second(*y).default_value)->value=-.3f;static_cast<bNodeSocketValueFloat *>(second(*z).default_value)->value=.4f;
 link(*x,"Value",*sum,"Value");bke::node_add_link(*group,*y,socket(*y,"Value",false),*sum,second(*sum));link(*sum,"Value",*xyz,"Value");bke::node_add_link(*group,*z,socket(*z,"Value",false),*xyz,second(*xyz));link(*xyz,"Value",*scaled,"Value");bke::node_add_link(*group,*inputs,socket(*inputs,gain.c_str(),false),*scaled,second(*scaled));link(*scaled,"Value",*height,"Value");bke::node_add_link(*group,*inputs,socket(*inputs,bias.c_str(),false),*height,second(*height));
 bNode *source=height;const char *source_socket="Value";
 if(kind==Kind::Constant){source=bke::node_add_node(nullptr,*group,UString("ShaderNodeValue"));static_cast<bNodeSocketValueFloat *>(socket(*source,"Value",false).default_value)->value=constant;}
 if(kind==Kind::ColorOutput){source=bke::node_add_node(nullptr,*group,UString("ShaderNodeRGB"));source_socket="Color";}
 if(kind==Kind::NormalInHeight){auto *normal=bke::node_add_node(nullptr,*group,UString("ShaderNodeNormalMap"));std::vector<bNodeLink *> old;for(auto &edge:group->links)if(edge.tosock==&socket(*separate,"Vector",true))old.push_back(&edge);for(auto *edge:old)bke::node_remove_link(group,*edge);link(*normal,"Normal",*separate,"Vector");}
 if(kind!=Kind::NoHeight)link(*source,source_socket,*bump,"Height");
 static_cast<bNodeSocketValueFloat *>(socket(*bump,"Strength",true).default_value)->value=.25f;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Distance",true).default_value)->value=1;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Filter Width",true).default_value)->value=1;
 if(kind==Kind::CustomNormal||kind==Kind::DoubleBump){auto *normal=bke::node_add_node(nullptr,*group,UString(kind==Kind::CustomNormal?"ShaderNodeNormalMap":"ShaderNodeBump"));if(kind==Kind::DoubleBump)link(*height,"Value",*normal,"Height");link(*normal,"Normal",*bump,"Normal");}
 if(kind==Kind::LinkedStrength)link(*height,"Value",*bump,"Strength");if(kind==Kind::Invert)bump->custom1=1;
 if(kind==Kind::MutedBump)bump->flag|=NODE_MUTED;
 bNode *unused=nullptr;if(kind==Kind::UnrelatedInactive){unused=math(NODE_MATH_ADD);link(*height,"Value",*unused,"Value");}
 link(*bump,"Normal",*pbr,"Normal");link(*pbr,"BSDF",*output,out.c_str());BKE_ntree_update_after_single_tree_change(main,*group);
 if(kind==Kind::InactiveHeight)socket(*bump,"Height",true).link->flag|=NODE_LINK_MUTED;
 if(unused)socket(*unused,"Value",true).link->flag|=NODE_LINK_MUTED;
 auto *top=bke::node_add_node(nullptr,tree,UString("ShaderNodeGroup"));top->id=&group->id;id_us_plus(top->id);auto *coordinate=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexCoord"));auto *surface=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(main,tree);
 static_cast<bNodeSocketValueFloat *>(socket(*top,bias.c_str(),true).default_value)->value=.5f;static_cast<bNodeSocketValueFloat *>(socket(*top,gain.c_str(),true).default_value)->value=1;
 bke::node_add_link(tree,*coordinate,socket(*coordinate,"Generated",false),*top,socket(*top,coords.c_str(),true));bke::node_add_link(tree,*top,socket(*top,out.c_str(),false),*surface,socket(*surface,"Surface",true));BKE_ntree_update_after_single_tree_change(main,tree);return result;
}
inline QJsonObject source_state(const blender::bNodeTree &root){
 using namespace blender;QJsonArray trees;std::set<const bNodeTree *> seen;
 const auto visit=[&](auto &&self,const bNodeTree &tree)->void{if(!seen.insert(&tree).second)return;QJsonArray nodes,links;for(const auto &node:tree.nodes){QJsonArray inputs;for(const auto &in:node.inputs){QJsonArray value;if(in.default_value){if(in.type==SOCK_FLOAT)value.append(static_cast<const bNodeSocketValueFloat *>(in.default_value)->value);else if(in.type==SOCK_VECTOR){for(float v:static_cast<const bNodeSocketValueVector *>(in.default_value)->value)value.append(v);}else if(in.type==SOCK_RGBA){for(float v:static_cast<const bNodeSocketValueRGBA *>(in.default_value)->value)value.append(v);}}inputs.append(QJsonObject{{"id",in.identifier},{"flag",int(in.flag)},{"value",value}});}QJsonObject state{{"name",node.name},{"type",node.idname},{"flag",int(node.flag)},{"mode",node.custom1},{"custom2",node.custom2},{"inputs",inputs}};if(STREQ(node.idname,"ShaderNodeTexNoise")&&node.storage){const auto &noise=*static_cast<const NodeTexNoise *>(node.storage);state["noise_storage"]=QJsonObject{{"dimensions",noise.dimensions},{"type",int(noise.type)},{"normalize",int(noise.normalize)}};}nodes.append(state);if(STREQ(node.idname,"ShaderNodeGroup")&&node.id)self(self,*reinterpret_cast<const bNodeTree *>(node.id));}for(const auto &edge:tree.links)links.append(QJsonArray{QString(edge.fromnode->name),QString(edge.fromsock->identifier),QString(edge.tonode->name),QString(edge.tosock->identifier),int(edge.flag)});trees.append(QJsonObject{{"name",tree.id.name},{"nodes",nodes},{"links",links}});};visit(visit,root);return {{"trees",trees}};
}
inline const NativeMaterialChannelGraph &bump(const NativeMaterialGraph &graph){for(const auto &channel:graph.channels)if(channel.channel==4)return channel;throw std::runtime_error("Original Bump channel record missing");}
}
