#pragma once
#include "NativeMaterialRawHeightFixture.hh"
#include "NativeMaterialFragmentNormal.hh"
#include "DNA_colorband_types.h"
#include <cstring>

namespace bts::native_fragment_normal_test {
using native_raw_height_test::require;
using native_raw_height_test::socket;
enum class Kind {Unlinked,ActiveBump,NormalMap,Combined,Grouped,Ramp,Emission,DiffuseSurface,UnsupportedContext};
inline DetachedNativeMaterial fixture(Kind kind){
 using namespace blender;using Raw=native_raw_height_test::Kind;
 auto result=native_raw_height_test::fixture(kind==Kind::ActiveBump?Raw::LinkedStrength:(kind==Kind::Combined?Raw::CustomNormal:Raw::Affine));
 auto &tree=*result.material->nodetree;bNodeTree *group=nullptr;
 for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeGroup")){group=reinterpret_cast<bNodeTree *>(node.id);break;}
 require(group,"Fragment Normal fixture group absent");bNode *pbr=nullptr,*bump=nullptr,*out=nullptr;
 for(auto &node:group->nodes){if(STREQ(node.idname,"ShaderNodeBsdfPrincipled"))pbr=&node;if(STREQ(node.idname,"ShaderNodeBump"))bump=&node;if(STREQ(node.idname,"NodeGroupOutput"))out=&node;}
 require(pbr&&bump&&out,"Fragment Normal fixture nodes absent");
 const auto unlink=[&](bNodeSocket &value){if(value.link)bke::node_remove_link(group,*value.link);};
 if(kind==Kind::Unlinked||kind==Kind::NormalMap||kind==Kind::Grouped)unlink(socket(*pbr,"Normal",true));
 if(kind==Kind::ActiveBump||kind==Kind::Combined){
  bump->custom1=1;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Distance",true).default_value)->value=-.7f;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Filter Width",true).default_value)->value=2;
 }
 if(kind==Kind::NormalMap){
  auto *normal=bke::node_add_node(nullptr,*group,UString("ShaderNodeNormalMap"));
  auto &storage=*static_cast<NodeShaderNormalMap *>(normal->storage);STRNCPY(storage.uv_map,"FixtureUV");
  static_cast<bNodeSocketValueFloat *>(socket(*normal,"Strength",true).default_value)->value=.65f;
  auto *color=static_cast<bNodeSocketValueRGBA *>(socket(*normal,"Color",true).default_value);color->value[0]=.2f;color->value[1]=.7f;color->value[2]=1;
  bke::node_add_link(*group,*normal,socket(*normal,"Normal",false),*pbr,socket(*pbr,"Normal",true));
 }
 if(kind==Kind::Grouped){
  auto *inner=bke::node_tree_add_tree(result.main.get(),"Nested actual Normal chain","ShaderNodeTree");
  const auto output=std::string(inner->tree_interface.add_socket("Normal","","NodeSocketVector",NODE_INTERFACE_SOCKET_OUTPUT,nullptr)->identifier);
  auto *end=bke::node_add_node(nullptr,*inner,UString("NodeGroupOutput"));auto *normal=bke::node_add_node(nullptr,*inner,UString("ShaderNodeNormalMap"));auto *nested=bke::node_add_node(nullptr,*inner,UString("ShaderNodeBump"));auto *height=bke::node_add_node(nullptr,*inner,UString("ShaderNodeValue"));
  static_cast<bNodeSocketValueFloat *>(socket(*height,"Value",false).default_value)->value=-.4f;
  nested->custom1=1;static_cast<bNodeSocketValueFloat *>(socket(*nested,"Strength",true).default_value)->value=.6f;
  BKE_ntree_update_after_single_tree_change(*result.main,*inner);
  bke::node_add_link(*inner,*height,socket(*height,"Value",false),*nested,socket(*nested,"Height",true));bke::node_add_link(*inner,*normal,socket(*normal,"Normal",false),*nested,socket(*nested,"Normal",true));bke::node_add_link(*inner,*nested,socket(*nested,"Normal",false),*end,socket(*end,output.c_str(),true));
  BKE_ntree_update_after_single_tree_change(*result.main,*inner);
  auto *instance=bke::node_add_node(nullptr,*group,UString("ShaderNodeGroup"));instance->id=&inner->id;id_us_plus(instance->id);BKE_ntree_update_after_single_tree_change(*result.main,*group);
  bke::node_add_link(*group,*instance,socket(*instance,output.c_str(),false),*pbr,socket(*pbr,"Normal",true));
 }
 if(kind==Kind::Ramp){
  bNode *top=nullptr;for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeGroup")){top=&node;break;}
  require(top,"Ramp fixture original top group absent");bNodeSocket *bias=nullptr;for(auto &in:top->inputs)if(STREQ(in.name,"Bias")){bias=&in;break;}require(bias,"Ramp fixture Bias input absent");
  auto *ramp=bke::node_add_node(nullptr,tree,UString("ShaderNodeValToRGB"));static_cast<bNodeSocketValueFloat *>(socket(*ramp,"Fac",true).default_value)->value=.3f;
  auto &band=*static_cast<ColorBand *>(ramp->storage);band.data[0].r=.1f;band.data[0].g=.2f;band.data[0].b=.3f;band.data[1].r=.9f;band.data[1].g=.7f;band.data[1].b=.5f;
  bke::node_add_link(tree,*ramp,socket(*ramp,"Color",false),*top,*bias);
 }
 if(kind==Kind::Emission||kind==Kind::DiffuseSurface){
  auto &surface=*static_cast<bNodeSocket *>(out->inputs.first);unlink(surface);
  auto *shader=bke::node_add_node(nullptr,*group,UString(kind==Kind::Emission?"ShaderNodeEmission":"ShaderNodeBsdfDiffuse"));
  bke::node_add_link(*group,*shader,socket(*shader,kind==Kind::Emission?"Emission":"BSDF",false),*out,surface);
 }
 if(kind==Kind::UnsupportedContext)for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeTexCoord"))node.custom1|=1;
 BKE_ntree_update_after_single_tree_change(*result.main,*group);BKE_ntree_update_after_single_tree_change(*result.main,tree);return result;
}
// Hashable original links/defaults plus Normal Map/ramp storage, including nested
// groups. This is a source-preservation witness, not shader evaluation.
inline QJsonObject source_state(const blender::bNodeTree &tree){
 using namespace blender;auto result=native_raw_height_test::source_state(tree);QJsonArray storage;std::set<const bNodeTree *> seen;
 const auto visit=[&](auto &&self,const bNodeTree &t)->void{if(!seen.insert(&t).second)return;for(const auto &node:t.nodes){if(STREQ(node.idname,"ShaderNodeGroup")&&node.id)self(self,*reinterpret_cast<const bNodeTree *>(node.id));if(STREQ(node.idname,"ShaderNodeNormalMap")&&node.storage){const auto &s=*static_cast<const NodeShaderNormalMap *>(node.storage);storage.append(QJsonObject{{"tree",t.id.name},{"node",node.name},{"space",s.space},{"uv_map",s.uv_map},{"convention",int(s.convention)},{"base",int(s.base)}});}if(STREQ(node.idname,"ShaderNodeValToRGB")&&node.storage){const auto &s=*static_cast<const ColorBand *>(node.storage);QJsonArray stops;for(int i=0;i<s.tot;++i)stops.append(QJsonArray{s.data[i].pos,s.data[i].r,s.data[i].g,s.data[i].b,s.data[i].a});storage.append(QJsonObject{{"tree",t.id.name},{"node",node.name},{"stops",stops},{"mode",int(s.color_mode)},{"interpolation",int(s.ipotype)},{"hue",int(s.ipotype_hue)}});}}};visit(visit,tree);result["normal_ramp_storage"]=storage;return result;
}
}
