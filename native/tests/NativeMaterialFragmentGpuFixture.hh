#pragma once
#include "NativeMaterialFragmentNormalFixture.hh"
namespace bts::native_fragment_gpu_test {
using native_raw_height_test::require;
using native_raw_height_test::socket;
// Original Principled material, not an AOV stencil. The worker API alone makes
// the private COLOR Normal root and existing parameter/Mapping/ramp adapters.
inline DetachedNativeMaterial source(float width=.5f,bool invert=false,bool geometry=false,const char *domain="uv",bool ramp=false,bool linked_width=false){
 using namespace blender;DetachedNativeMaterial result;result.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(result.main.get());result.material=BKE_material_add(result.main.get(),"Prepared fragment Normal fixture");auto &tree=*result.material->nodetree;
 auto *group=bke::node_tree_add_tree(result.main.get(),"Prepared UV quadratic Bump","ShaderNodeTree");
 const auto strength=std::string(group->tree_interface.add_socket("Strength","","NodeSocketFloat",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier),distance=std::string(group->tree_interface.add_socket("Distance","","NodeSocketFloat",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier),coordinates=std::string(group->tree_interface.add_socket("Coordinates","","NodeSocketVector",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier),shader=std::string(group->tree_interface.add_socket("Shader","","NodeSocketShader",NODE_INTERFACE_SOCKET_OUTPUT,nullptr)->identifier);
 auto *inputs=bke::node_add_node(nullptr,*group,UString("NodeGroupInput")),*end=bke::node_add_node(nullptr,*group,UString("NodeGroupOutput")),*pbr=bke::node_add_node(nullptr,*group,UString("ShaderNodeBsdfPrincipled")),*bump=bke::node_add_node(nullptr,*group,UString("ShaderNodeBump")),*separate=bke::node_add_node(nullptr,*group,UString("ShaderNodeSeparateXYZ")),*square=bke::node_add_node(nullptr,*group,UString("ShaderNodeMath"));square->custom1=NODE_MATH_MULTIPLY;bump->custom1=invert;
 const auto dynamic_width=linked_width?std::string(group->tree_interface.add_socket("Width","","NodeSocketFloat",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier):std::string{};
 static_cast<bNodeSocketValueFloat *>(socket(*bump,"Filter Width",true).default_value)->value=width;BKE_ntree_update_after_single_tree_change(*result.main,*group);
 const auto link=[&](bNode &a,const char *out,bNode &b,const char *in){bke::node_add_link(*group,a,socket(a,out,false),b,socket(b,in,true));};
 link(*inputs,coordinates.c_str(),*separate,"Vector");link(*separate,"X",*square,"Value");bke::node_add_link(*group,*separate,socket(*separate,"X",false),*square,*static_cast<bNodeSocket *>(square->inputs.first)->next);
 if(linked_width)link(*inputs,dynamic_width.c_str(),*bump,"Filter Width");
 link(*inputs,strength.c_str(),*bump,"Strength");link(*inputs,distance.c_str(),*bump,"Distance");
 if(std::string_view(domain)=="view"){auto *view=bke::node_add_node(nullptr,*group,UString("ShaderNodeFresnel"));link(*view,"Fac",*bump,"Height");}
 else if(std::string_view(domain)=="image"){auto *image=bke::node_add_node(nullptr,*group,UString("ShaderNodeTexImage"));link(*image,"Color",*bump,"Height");}
 else link(*square,"Value",*bump,"Height");
 if(!geometry)link(*bump,"Normal",*pbr,"Normal");link(*pbr,"BSDF",*end,shader.c_str());BKE_ntree_update_after_single_tree_change(*result.main,*group);
 auto *top=bke::node_add_node(nullptr,tree,UString("ShaderNodeGroup"));top->id=&group->id;id_us_plus(top->id);auto *output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*result.main,tree);
 static_cast<bNodeSocketValueFloat *>(socket(*top,strength.c_str(),true).default_value)->value=1;static_cast<bNodeSocketValueFloat *>(socket(*top,distance.c_str(),true).default_value)->value=.3f;
 if(linked_width)static_cast<bNodeSocketValueFloat *>(socket(*top,dynamic_width.c_str(),true).default_value)->value=width;
 if(std::string_view(domain)=="named"){auto *uv=bke::node_add_node(nullptr,tree,UString("ShaderNodeUVMap"));STRNCPY(static_cast<NodeShaderUVMap *>(uv->storage)->uv_map,"FixtureUV");bke::node_add_link(tree,*uv,socket(*uv,"UV",false),*top,socket(*top,coordinates.c_str(),true));}
 else {auto *uv=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexCoord"));bke::node_add_link(tree,*uv,socket(*uv,std::string_view(domain)=="object"?"Generated":"UV",false),*top,socket(*top,coordinates.c_str(),true));}
 if(ramp){auto *value=bke::node_add_node(nullptr,tree,UString("ShaderNodeValToRGB"));static_cast<bNodeSocketValueFloat *>(socket(*value,"Fac",true).default_value)->value=.5f;auto &band=*static_cast<ColorBand *>(value->storage);for(int i=0;i<band.tot;++i){band.data[i].r=band.data[i].g=band.data[i].b=.5f;band.data[i].a=1;}bke::node_add_link(tree,*value,socket(*value,"Color",false),*top,socket(*top,strength.c_str(),true));}
 bke::node_add_link(tree,*top,socket(*top,shader.c_str(),false),*output,socket(*output,"Surface",true));BKE_ntree_update_after_single_tree_change(*result.main,tree);return result;
}
}
