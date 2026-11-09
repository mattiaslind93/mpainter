// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialParameters.hh"
#include "BKE_node.hh"
#include "BKE_node_runtime.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_node_types.h"
namespace bts {
inline blender::bNodeSocket &native_mapping_socket(blender::bNode &node,const char *identifier,bool in){for(auto &socket:in?node.inputs:node.outputs)if(STREQ(socket.identifier,identifier))return socket;throw std::runtime_error("Native Mapping adapter socket missing");}
inline blender::bNodeLink &native_mapping_source_link(blender::bNode &top,const NativeMaterialParameter &parameter){
 using namespace blender;auto &target=native_mapping_socket(top,parameter.identifier.toUtf8().constData(),true);auto *link=target.link;
 if(target.type!=SOCK_VECTOR||!link||!link->fromnode||!link->fromsock||link->tonode!=&top||link->tosock!=&target||parameter.source_node!=link->fromnode->idname||parameter.source_socket!=link->fromsock->identifier||link->fromsock->type!=SOCK_VECTOR)throw std::invalid_argument("Native linked Mapping source identity changed");
 return *link;
}
inline bool native_mapping_edge_active(const blender::bNodeLink &link){return (link.flag&blender::NODE_LINK_VALID)&&link.is_used();}
// Admission is an explicit source operation policy, not an arbitrary linked-vector
// override. The original node remains encoded and executes before extra POINT.
inline bool native_mapping_vector_math_supported(const blender::bNode &node,const blender::bNodeSocket &output){
 using namespace blender;
 return STREQ(node.idname,"ShaderNodeVectorMath")&&output.type==SOCK_VECTOR&&STREQ(output.identifier,"Vector")&&
        (node.custom1==NODE_VECTOR_MATH_ADD||node.custom1==NODE_VECTOR_MATH_MULTIPLY);
}
inline void validate_native_mapping_source_node(const blender::bNodeLink &link){
 using namespace blender;const auto *from=link.fromnode;const auto *output=link.fromsock;
 if(STREQ(from->idname,"ShaderNodeMapping")){
  if(!STREQ(output->identifier,"Vector")||from->custom1<NODE_MAPPING_TYPE_POINT||from->custom1>NODE_MAPPING_TYPE_NORMAL)throw std::invalid_argument("Native linked Mapping has an unsupported original vector mode");
 }else if(STREQ(from->idname,"ShaderNodeVectorMath")){
  if(!native_mapping_vector_math_supported(*from,*output))throw std::invalid_argument("Native linked Vector Math transform requires the Vector output of Add or Multiply");
 }else if(!STREQ(from->idname,"ShaderNodeTexCoord"))throw std::invalid_argument("Native linked Mapping source node is unsupported");
}
// Runtime copy only: leave encoded defaults, inactive edges and all fanout intact.
inline void bind_native_material_mappings(blender::bNodeTree &tree,blender::bNode &top,blender::bNode &original_top,const NativeMaterialDescription &description){
 using namespace blender;const auto socket=native_mapping_socket;
 const auto add=[&](const char *type)->bNode &{auto *value=bke::node_add_node(nullptr,tree,UString(type));if(!value)throw std::runtime_error("Cannot add native Mapping runtime adapter");return *value;};
 for(std::size_t i=0;i<description.parameters.size();++i){const auto &parameter=description.parameters[i];
  if(!parameter.mapping_transform.identity())throw std::invalid_argument("Native source graph must retain identity Mapping; overrides belong to instances");
  if(!parameter.mapping_transform_supported())continue;
  auto &original_link=native_mapping_source_link(original_top,parameter);
  // Copying a tree can rebuild socket availability. Authority comes from the
  // untouched original: an ignored edge must never become a live adapter.
  if(!native_mapping_edge_active(original_link)){
   // Blender ignores this original edge. Detach it only in the runtime copy:
   // declaration/update must not accidentally reactivate it or its adapter.
   auto &target=socket(top,parameter.identifier.toUtf8().constData(),true);
   if(target.link)bke::node_remove_link(&tree,*target.link);
   continue;
  }
  validate_native_mapping_source_node(original_link);
  auto &link_value=native_mapping_source_link(top,parameter);auto *link=&link_value;
  if(!native_mapping_edge_active(*link))throw std::invalid_argument("Native linked Mapping edge became unavailable during preparation");
  validate_native_mapping_source_node(*link);
  auto &target=*link->tosock;auto *from=link->fromnode;auto *output=link->fromsock;
  auto &mapping=add("ShaderNodeMapping");mapping.custom1=NODE_MAPPING_TYPE_POINT;
  bke::node_add_link(tree,*from,*output,mapping,socket(mapping,"Vector",true));
  for(int row=0;row<3;++row){auto &attribute=add("ShaderNodeAttribute");const auto name=native_mapping_attribute(i,row);STRNCPY(static_cast<NodeShaderAttribute *>(attribute.storage)->name,name.c_str());bke::node_add_link(tree,attribute,socket(attribute,"Vector",false),mapping,socket(mapping,row==0?"Location":row==1?"Rotation":"Scale",true));}
  bke::node_remove_link(&tree,*link);bke::node_add_link(tree,mapping,socket(mapping,"Vector",false),top,target);
 }
}
}
