// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialGraph.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_map.hh"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "NOD_shader.h"
#include "NOD_shader_nodes_inline.hh"
#include <QScopeGuard>
#include <set>
#include <map>
#include <cstdio>

namespace bts {
inline bool apply_native_material_coat(blender::Main &main,blender::bNodeTree &target,
    blender::bNode &principled,const NativeMaterialGraph *graph,
    const NativeMaterialDescription *instance,float opacity){
 using namespace blender;
 constexpr const char *prefix="__MP_NativeCoat_";
 constexpr std::array sockets{"Coat Weight","Coat Roughness","Coat IOR","Coat Tint","Coat Normal"};
 std::vector<bNode *> retired;
 for(auto &node:target.nodes)if(std::string_view(node.name).starts_with(prefix))retired.push_back(&node);
 for(auto *node:retired)bke::node_remove_node(&main,target,*node,true);
 std::vector<Image *> orphaned;
 for(auto &image:main.images)if(std::string_view(image.id.name+2).starts_with(prefix)&&image.id.us==0)orphaned.push_back(&image);
 for(auto *image:orphaned)BKE_id_free(&main,&image->id);
 for(const auto *name:sockets)if(auto *socket=bke::node_find_enabled_input_socket(principled,name);socket&&socket->link)bke::node_remove_link(&target,*socket->link);
 if(!retired.empty())static_cast<bNodeSocketValueFloat *>(bke::node_find_enabled_input_socket(principled,"Coat Weight")->default_value)->value=0;
 if(!graph||!instance)return !retired.empty();
 auto *flat=bke::node_tree_add_tree(&main,"mPainter imported coat","ShaderNodeTree");
 const auto release=qScopeGuard([&]{BKE_id_free(&main,&flat->id);});
 nodes::InlineShaderNodeTreeParams options;options.target_engine_=SHD_OUTPUT_EEVEE;
 if(!nodes::inline_shader_node_tree(*graph->material->nodetree,*flat,options)||!options.r_error_messages.is_empty()){std::fprintf(stderr,"mPainter material warning: Coat shader is unsupported; retained the other material channels.\n");return !retired.empty();}
 BKE_ntree_update_without_main(*flat);
 auto *output=ntreeShaderOutputNode(flat,SHD_OUTPUT_EEVEE);
 auto *surface=output?bke::node_find_enabled_input_socket(*output,"Surface"):nullptr;
 auto *source=surface&&surface->link?surface->link->fromnode:nullptr;
 if(!source||!STREQ(source->idname,"ShaderNodeBsdfPrincipled"))return !retired.empty();
 for(const auto &parameter:instance->parameters){
  auto *socket=bke::node_find_enabled_input_socket(*source,parameter.identifier.toUtf8().constData());
  if(!socket||socket->link||!socket->default_value)continue;
  if(socket->type==SOCK_FLOAT)static_cast<bNodeSocketValueFloat *>(socket->default_value)->value=parameter.value[0];
  else if(socket->type==SOCK_RGBA)for(int c=0;c<4;++c)static_cast<bNodeSocketValueRGBA *>(socket->default_value)->value[c]=parameter.value[c];
  else if(socket->type==SOCK_VECTOR)for(int c=0;c<3;++c)static_cast<bNodeSocketValueVector *>(socket->default_value)->value[c]=parameter.value[c];
 }
 std::set<const bNode *> required;
 const auto visit=[&](auto &&self,const bNode &node)->void{if(!required.insert(&node).second)return;for(const auto &socket:node.inputs)if(socket.link&&socket.link->fromnode)self(self,*socket.link->fromnode);};
 for(const auto *name:sockets){const auto *socket=bke::node_find_enabled_input_socket(*source,name);if(socket&&socket->link)visit(visit,*socket->link->fromnode);}
 // Every copied ID must outlive the detached source Main. Images are deep
 // copied below; unsupported object/collection references never enter the live tree.
 for(const auto *node:required)if(!node->typeinfo||!node->typeinfo->gpu_fn||(node->id&&GS(node->id->name)!=ID_IM)){
  std::fprintf(stderr,"mPainter material warning: Coat node %s requires unsupported external data or evaluation; retained the other material channels.\n",node->idname);
  return !retired.empty();
 }
 Map<const bNodeSocket *,bNodeSocket *> socket_map;std::map<const bNode *,bNode *> node_map;std::map<ID *,ID *> images;
 for(const auto *node:required){
  const auto name=std::string(prefix)+std::to_string(node_map.size());
  auto *copy=bke::node_copy_with_mapping(&target,*node,0,StringRefNull(name),std::nullopt,socket_map);
  if(copy->id&&GS(copy->id->name)==ID_IM){
   auto [found,inserted]=images.try_emplace(copy->id,nullptr);
   if(inserted){found->second=BKE_id_copy_ex(&main,copy->id,nullptr,0);if(!found->second)throw std::bad_alloc();BKE_id_rename(main,*found->second,std::string(prefix)+std::to_string(images.size()));id_us_min(found->second);}
   id_us_min(copy->id);copy->id=found->second;id_us_plus(copy->id);
  }
  node_map.emplace(node,copy);
 }
 for(const auto &link:flat->links)if(node_map.contains(link.fromnode)&&node_map.contains(link.tonode))bke::node_add_link(target,*node_map.at(link.fromnode),*socket_map.lookup(link.fromsock),*node_map.at(link.tonode),*socket_map.lookup(link.tosock));
 for(const auto *name:sockets){
  const auto *from=bke::node_find_enabled_input_socket(*source,name);auto *to=bke::node_find_enabled_input_socket(principled,name);if(!from||!to)continue;
  if(from->link)bke::node_add_link(target,*node_map.at(from->link->fromnode),*socket_map.lookup(from->link->fromsock),principled,*to);
  else if(from->type==SOCK_FLOAT)static_cast<bNodeSocketValueFloat *>(to->default_value)->value=static_cast<const bNodeSocketValueFloat *>(from->default_value)->value;
  else if(from->type==SOCK_RGBA)std::copy_n(static_cast<const bNodeSocketValueRGBA *>(from->default_value)->value,4,static_cast<bNodeSocketValueRGBA *>(to->default_value)->value);
 }
 auto *weight_value=bke::node_add_node(nullptr,target,UString("ShaderNodeValue"));STRNCPY(weight_value->name,"__MP_NativeCoat_Owner");
 // Live image dependencies must tag the Main depsgraph before consuming update flags.
 BKE_ntree_update_after_single_tree_change(main,target);
 auto *weight=bke::node_find_enabled_input_socket(principled,"Coat Weight");
 if(weight&&opacity<1){
  if(weight->link){auto *multiply=bke::node_add_node(nullptr,target,UString("ShaderNodeMath"));STRNCPY(multiply->name,"__MP_NativeCoat_Opacity");multiply->custom1=NODE_MATH_MULTIPLY;auto *first=static_cast<bNodeSocket *>(multiply->inputs.first);bke::node_add_link(target,*weight->link->fromnode,*weight->link->fromsock,*multiply,*first);static_cast<bNodeSocketValueFloat *>(first->next->default_value)->value=opacity;bke::node_remove_socket_links(target,*weight);bke::node_add_link(target,*multiply,*static_cast<bNodeSocket *>(multiply->outputs.first),principled,*weight);}
  else static_cast<bNodeSocketValueFloat *>(weight->default_value)->value*=opacity;
 }
 return true;
}
}
