// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialFragmentNormalGraphTest.hh"
#include "NativeMaterialDecodedImage.hh"
#include "BKE_image.hh"
#include "BKE_report.hh"
#include "IMB_imbuf_types.hh"
#include <QTemporaryDir>
#include <QImage>

namespace bts {
inline QJsonArray verify_native_shader_mix_graph(){
 using namespace blender;using namespace native_fragment_normal_test;
 using native_fragment_normal_test::detail::work;
 QJsonArray results;
 for(bool camera_branch:{false,true})results.append(work([&]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Shader mixture source");auto &tree=*source.material->nodetree;
  auto *first=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));
  auto *second=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));
  auto *mix=bke::node_add_node(nullptr,tree,UString("ShaderNodeMixShader"));
  auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
  static_cast<bNodeSocketValueRGBA *>(socket(*first,"Base Color",true).default_value)->value[0]=.125f;
  static_cast<bNodeSocketValueRGBA *>(socket(*second,"Base Color",true).default_value)->value[0]=.875f;
  static_cast<bNodeSocketValueFloat *>(socket(*first,"Roughness",true).default_value)->value=.25f;
  static_cast<bNodeSocketValueFloat *>(socket(*second,"Roughness",true).default_value)->value=.75f;
  if(camera_branch){
   auto *ray=bke::node_add_node(nullptr,tree,UString("ShaderNodeLightPath"));
   bke::node_add_link(tree,*ray,socket(*ray,"Is Camera Ray",false),*mix,socket(*mix,"Fac",true));
  }else{
   auto *noise=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexNoise"));
   bke::node_add_link(tree,*noise,socket(*noise,"Fac",false),*mix,socket(*mix,"Fac",true));
   bke::node_add_link(tree,*first,socket(*first,"BSDF",false),*mix,socket(*mix,"Shader",true));
  }
  bke::node_add_link(tree,*second,socket(*second,"BSDF",false),*mix,socket(*mix,"Shader_001",true));
  bke::node_add_link(tree,*mix,socket(*mix,"Shader",false),*out,socket(*out,"Surface",true));
  BKE_ntree_update_after_single_tree_change(*source.main,tree);const auto original=source_state(tree);std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);
  require(source_state(*graph->material->nodetree)==original,"Shader mixture preparation changed original source");
  require(graph->description.warnings.contains("Mix Shader is evaluated as blended material channels. Separate BSDF lobes are not reproduced by viewport shading.")!=camera_branch,"Shader mixture capability warning differs from the effective source");
  for(int id:{0,1,2}){
   const auto channel=std::find_if(graph->channels.begin(),graph->channels.end(),[&](const auto &value){return value.channel==id;});
   require(channel!=graph->channels.end()&&channel->tree,"Shader mixture lost a PBR channel");
   bNode *aov=nullptr;for(auto &node:channel->tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV"))aov=&node;
   require(aov,"Shader mixture has no output");auto &result=socket(*aov,id==0?"Color":"Value",true);
   if(camera_branch){
    require(!result.link,"Camera branch retained an inactive unconnected shader");
    if(id==0)require(static_cast<bNodeSocketValueRGBA *>(result.default_value)->value[0]==.875f,"Camera ray selected the wrong color branch");
    if(id==2)require(static_cast<bNodeSocketValueFloat *>(result.default_value)->value==.75f,"Camera ray selected the wrong roughness branch");
   }else{
    require(result.link&&STREQ(result.link->fromnode->idname,"ShaderNodeMix"),"Dynamic mixture discarded a shader branch");
    auto &adapter=*result.link->fromnode;require(socket(adapter,"Factor_Float",true).link,"Dynamic mixture discarded its factor graph");
    if(id==0){require(static_cast<bNodeSocketValueRGBA *>(socket(adapter,"A_Color",true).default_value)->value[0]==.125f,"Mixture lost first color");require(static_cast<bNodeSocketValueRGBA *>(socket(adapter,"B_Color",true).default_value)->value[0]==.875f,"Mixture lost second color");}
    if(id==2){require(static_cast<bNodeSocketValueFloat *>(socket(adapter,"A_Float",true).default_value)->value==.25f,"Mixture lost first roughness");require(static_cast<bNodeSocketValueFloat *>(socket(adapter,"B_Float",true).default_value)->value==.75f,"Mixture lost second roughness");}
   }
  }
  return QJsonObject{{"camera_branch",camera_branch},{"source_preserved",true},{"color_metallic_roughness_preserved",true}};
 }));
 results.append(work([]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Camera ray channel source");auto &tree=*source.material->nodetree;
  auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));auto *ray=bke::node_add_node(nullptr,tree,UString("ShaderNodeLightPath"));
  for(const char *name:{"Base Color","Metallic","Roughness"})bke::node_add_link(tree,*ray,socket(*ray,STREQ(name,"Metallic")?"Is Shadow Ray":"Is Camera Ray",false),*pbr,socket(*pbr,name,true));
  bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);
  for(int id:{0,1,2}){const auto channel=std::find_if(graph->channels.begin(),graph->channels.end(),[&](const auto &value){return value.channel==id;});require(channel!=graph->channels.end()&&channel->tree,"Camera ray channel failed");bNode *aov=nullptr;for(auto &node:channel->tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV"))aov=&node;require(aov,"Camera ray channel has no output");const auto &value=socket(*aov,id==0?"Color":"Value",true);require(value.link&&STREQ(value.link->fromnode->idname,"ShaderNodeValue"),"Ray channel was not resolved for the camera");require(static_cast<bNodeSocketValueFloat *>(value.link->fromsock->default_value)->value==(id==1?0.f:1.f),"Ray channel selected an incorrect camera value");}
  return QJsonObject{{"camera_ray_channels",true}};
 }));
 for(const char *control:{"", "Transmission Weight", "Coat Weight", "Sheen Weight", "Subsurface Weight", "Emission Color", "linked-coat"})results.append(work([&]{
  DetachedNativeMaterial source;source.main={BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(source.main.get());source.material=BKE_material_add(source.main.get(),"Principled capability source");auto &tree=*source.material->nodetree;
  auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
  if(STREQ(control,"linked-coat")){auto *value=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexNoise"));bke::node_add_link(tree,*value,socket(*value,"Fac",false),*pbr,socket(*pbr,"Coat Weight",true));}
  else if(STREQ(control,"Emission Color")){static_cast<bNodeSocketValueRGBA *>(socket(*pbr,control,true).default_value)->value[0]=1.f;static_cast<bNodeSocketValueFloat *>(socket(*pbr,"Emission Strength",true).default_value)->value=1.f;}
  else if(control[0])static_cast<bNodeSocketValueFloat *>(socket(*pbr,control,true).default_value)->value=.5f;
  bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));BKE_ntree_update_after_single_tree_change(*source.main,tree);std::atomic_bool cancel=false;auto graph=finish_native_material_source(std::move(source),cancel);
  const bool warned=std::any_of(graph->description.warnings.begin(),graph->description.warnings.end(),[](const QString &warning){return warning.contains("not evaluated by viewport material channels");});
  const bool coat=STREQ(control,"Coat Weight")||STREQ(control,"linked-coat");
  if(warned!=(bool(control[0])&&!coat))throw std::runtime_error(std::string("Principled capability warning differs for control: ")+control);
  if(coat){const auto &weight=socket(*pbr,"Coat Weight",true);require(STREQ(control,"linked-coat")?bool(weight.link):static_cast<const bNodeSocketValueFloat *>(weight.default_value)->value==.5f,"Supported Coat source was removed instead of retained for the live shader");}
  return QJsonObject{{"principled_control",control},{"omitted_lobe_warning",warned}};
 }));
 results.append(work([]{
  QTemporaryDir directory;require(directory.isValid(),"Legacy color space fixture directory missing");const auto path=directory.filePath("legacy-srgb.png");QImage pixels(3,2,QImage::Format_RGBA8888);pixels.fill(qRgba(128,64,192,255));require(pixels.save(path),"Legacy color space fixture image write failed");
  std::unique_ptr<Main,decltype(&BKE_main_free)> main{BKE_main_new(),BKE_main_free};IMB_colormanagement_working_space_init_default(main.get());auto *image=BKE_image_load(main.get(),path.toUtf8().constData());require(image,"Legacy color space fixture Image missing");std::atomic_bool cancel=false;
  STRNCPY(image->colorspace_settings.name,"sRGB");auto current=decode_native_material_image(*main,*image,cancel);
  STRNCPY(image->colorspace_settings.name,"sRGB OETF");auto legacy=decode_native_material_image(*main,*image,cancel);
  require(STREQ(image->colorspace_settings.name,"sRGB OETF"),"Legacy alias changed the source Image declaration");
  require(legacy.metadata["authored_space"].toString()=="sRGB OETF"&&legacy.metadata["resolved_source_space"].toString()=="sRGB"&&legacy.metadata["legacy_space_alias"].toBool(),"Legacy color-space alias has no explicit provenance");
  require(std::equal(current.buffer->float_data(),current.buffer->float_data()+3*2*4,legacy.buffer->float_data()),"Legacy Filmic sRGB pixels differ from canonical sRGB normalization");
  ReportList reports{};BKE_reports_init(&reports,RPT_STORE);BKE_image_packfiles(&reports,image,main->filepath);BKE_reports_free(&reports);require(!image->packedfiles.is_empty(),"Legacy image fixture was not packed");auto packed=decode_native_material_image(*main,*image,cancel);require(std::equal(current.buffer->float_data(),current.buffer->float_data()+3*2*4,packed.buffer->float_data()),"Packed legacy sRGB pixels differ from canonical normalization");
  STRNCPY(image->colorspace_settings.name,"Unknown native source color space");bool rejected=false;try{decode_native_material_image(*main,*image,cancel);}catch(const std::exception &error){rejected=std::string(error.what()).find("Unknown authored native image color space")!=std::string::npos;}require(rejected,"Unknown source color space silently used a default");
  return QJsonObject{{"legacy_filmic_srgb_alias",true},{"packed_and_file_pixels_preserved",true},{"source_declaration_preserved",true},{"unknown_color_space_rejected",true}};
 }));
 return results;
}
}
