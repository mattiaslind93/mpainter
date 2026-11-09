// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeJsonProperty.hh"
#include "DisplayPostEffects.hh"
#include <QJsonDocument>
#include <memory>
#include <algorithm>
#include "UI_resources.hh"
#include "IMB_colormanagement.hh"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "BKE_context.hh"
#include "BKE_colortools.hh"
#include "BLI_math_color.h"
#include "BKE_idprop.hh"
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_node_runtime.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_main.hh"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_listbase.h"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DNA_ID.h"
#include "DNA_node_types.h"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"
#include "DNA_layer_types.h"
#include "DNA_view3d_types.h"
namespace bts {using namespace blender;namespace {
constexpr const char *state_key="bts_display_post_effects",*tree_name="BTS Display Post Effects";
bNodeSocket &socket(bNode &node,const char *name,bool out=false){if(out){for(auto &s:node.outputs)if(STREQ(s.identifier,name))return s;}else{for(auto &s:node.inputs)if(STREQ(s.identifier,name))return s;}auto *result=out?bke::node_find_enabled_output_socket(node,name):bke::node_find_enabled_input_socket(node,name);if(!result)throw std::runtime_error(std::string("Display compositor socket missing: ")+name);return *result;}
bNodeSocket &input(bNode &node,int index){for(auto &s:node.inputs)if(index--==0)return s;throw std::runtime_error("Display compositor input missing");}
bNode &node(bNodeTree &tree,const char *type,const char *name,const bContext *context=nullptr){if(auto *existing=bke::node_find_node_by_name(tree,name))return *existing;auto *result=bke::node_add_node(context,tree,UString(type));if(!result)throw std::runtime_error("Cannot create display compositor node");STRNCPY(result->name,name);return *result;}
bool link(bNodeTree &tree,bNode &from,const char *output,bNode &to,bNodeSocket &input){auto &source=socket(from,output,true);for(const auto &existing:tree.links)if(existing.tosock==&input&&existing.fromnode==&from&&existing.fromsock==&source)return false;bke::node_remove_socket_links(tree,input);bke::node_add_link(tree,from,source,to,input);return true;}
bool number(bNodeTree &tree,bNodeSocket &s,float value){auto &stored=static_cast<bNodeSocketValueFloat *>(s.default_value)->value;if(stored==value)return false;stored=value;BKE_ntree_update_tag_socket_property(&tree,&s);return true;}
bool vector(bNodeTree &tree,bNodeSocket &s,float x,float y){auto *stored=static_cast<bNodeSocketValueVector *>(s.default_value)->value;if(stored[0]==x&&stored[1]==y)return false;stored[0]=x;stored[1]=y;BKE_ntree_update_tag_socket_property(&tree,&s);return true;}
bNodeTree &ensure_tree(Main &main,Scene &scene){
 if(scene.compositing_node_group){auto &tree=*scene.compositing_node_group;if(!bke::node_find_node_by_name(tree,"__BTS_DisplaySource"))throw std::invalid_argument("This scene already has a custom compositor; display effects cannot replace it");return tree;}
 auto *tree=bke::node_tree_add_tree(&main,tree_name,"CompositorNodeTree");scene.compositing_node_group=tree;
 tree->tree_interface.add_socket("Image","","NodeSocketColor",NODE_INTERFACE_SOCKET_INPUT,nullptr);tree->tree_interface.add_socket("Image","","NodeSocketColor",NODE_INTERFACE_SOCKET_OUTPUT,nullptr);
 std::unique_ptr<bContext,decltype(&CTX_free)> context(CTX_create(),CTX_free);CTX_data_main_set(context.get(),&main);CTX_data_scene_set(context.get(),&scene);
 auto &source=node(*tree,"CompositorNodeRLayers","__BTS_DisplaySource",context.get());auto &output=node(*tree,"NodeGroupOutput","__BTS_DisplayOutput");auto &mask=node(*tree,"CompositorNodeEllipseMask","__BTS_DisplayVignetteMask");auto &blur=node(*tree,"CompositorNodeBlur","__BTS_DisplayVignetteBlur");auto &multiply=node(*tree,"ShaderNodeMixRGB","__BTS_DisplayVignette");multiply.custom1=MA_RAMP_MULT;
 BKE_ntree_update_after_single_tree_change(main,*tree);vector(*tree,socket(mask,"Size"),1,1);link(*tree,mask,"Mask",blur,socket(blur,"Image"));link(*tree,source,"Image",multiply,input(multiply,1));link(*tree,blur,"Image",multiply,input(multiply,2));link(*tree,multiply,"Color",output,input(output,0));BKE_ntree_update_after_single_tree_change(main,*tree);DEG_relations_tag_update(&main);return *tree;
}
bool color(bNodeTree &,bNodeSocket &,const std::array<float,4> &);
bool background(Main &main,Scene &scene,View3D &view,bNodeTree &tree){
 auto *source=bke::node_find_node_by_name(tree,"__BTS_DisplaySource");const bool created=!bke::node_find_node_by_name(tree,"__BTS_DisplayBackground");auto &over=node(tree,"CompositorNodeAlphaOver","__BTS_DisplayBackground");bool changed=created;changed|=link(tree,*source,"Image",over,socket(over,"Foreground"));
 float high[4],low[4];ui::theme::bThemeState saved;ui::theme::theme_store(&saved);ui::theme::theme_set(SPACE_VIEW3D,RGN_TYPE_WINDOW);ui::theme::get_color_type_4fv(TH_BACK,SPACE_VIEW3D,high);ui::theme::get_color_type_4fv(TH_BACK_GRAD,SPACE_VIEW3D,low);const int type=ui::theme::get_value(TH_BACKGROUND_TYPE);ui::theme::theme_restore(&saved);high[3]=low[3]=1;
 // Native overlays linearize sRGB theme colors, then use gamma 2.2 when
 // merging them in the display shader. Preserve that displayed result.
 srgb_to_linearrgb_v4(high,high);srgb_to_linearrgb_v4(low,low);for(int c=0;c<3;++c){high[c]=std::pow(high[c],1/2.2f);low[c]=std::pow(low[c],1/2.2f);}
 ColorManagedViewSettings effective=scene.view_settings;if(!V3D_USES_SCENE_LIGHTS(&view)&&!V3D_USES_SCENE_WORLD(&view)){BKE_color_managed_view_settings_init(&effective,&scene.display_settings,nullptr);STRNCPY_UTF8(effective.view_transform,scene.view_settings.view_transform);STRNCPY_UTF8(effective.look,scene.view_settings.look);}
 auto processor=ColormanageProcessor::display_processor_new(&effective,&scene.display_settings,DISPLAY_SPACE_DRAW,true);processor.apply_v3(high);processor.apply_v3(low);
 if(type==TH_BACKGROUND_SINGLE_COLOR){bke::node_remove_socket_links(tree,socket(over,"Background"));changed|=color(tree,socket(over,"Background"),{high[0],high[1],high[2],1});return changed;}
 auto &coordinates=node(tree,"CompositorNodeImageCoordinates","__BTS_DisplayBackgroundCoordinates");changed|=link(tree,*source,"Image",coordinates,socket(coordinates,"Image"));auto &mix=node(tree,"ShaderNodeMixRGB","__BTS_DisplayBackgroundGradient");
 if(type==TH_BACKGROUND_GRADIENT_RADIAL){auto &offset=node(tree,"ShaderNodeVectorMath","__BTS_DisplayBackgroundCenter");offset.custom1=NODE_VECTOR_MATH_SUBTRACT;auto &length=node(tree,"ShaderNodeVectorMath","__BTS_DisplayBackgroundRadius");length.custom1=NODE_VECTOR_MATH_LENGTH;auto &scale=node(tree,"ShaderNodeMath","__BTS_DisplayBackgroundScale");scale.custom1=NODE_MATH_MULTIPLY;changed|=link(tree,coordinates,"Normalized",offset,input(offset,0));changed|=vector(tree,input(offset,1),.5,.5);changed|=link(tree,offset,"Vector",length,input(length,0));changed|=link(tree,length,"Value",scale,input(scale,0));changed|=number(tree,input(scale,1),std::sqrt(2.f));changed|=link(tree,scale,"Value",mix,input(mix,0));changed|=color(tree,input(mix,1),{high[0],high[1],high[2],1});changed|=color(tree,input(mix,2),{low[0],low[1],low[2],1});}
 else{auto &separate=node(tree,"ShaderNodeSeparateXYZ","__BTS_DisplayBackgroundY");changed|=link(tree,coordinates,"Normalized",separate,socket(separate,"Vector"));changed|=link(tree,separate,"Y",mix,input(mix,0));changed|=color(tree,input(mix,1),{low[0],low[1],low[2],1});changed|=color(tree,input(mix,2),{high[0],high[1],high[2],1});}
 changed|=link(tree,mix,"Color",over,socket(over,"Background"));if(created){BKE_ntree_update_after_single_tree_change(main,tree);DEG_relations_tag_update(&main);}return changed;
}
struct Stage {bNode *node;const char *output;};
bool menu(bNodeTree &tree,bNode &node,const char *name,int value){auto &s=socket(node,name);auto &stored=static_cast<bNodeSocketValueMenu *>(s.default_value)->value;if(stored==value)return false;stored=value;BKE_ntree_update_tag_socket_property(&tree,&s);return true;}
bool color(bNodeTree &tree,bNodeSocket &s,const std::array<float,4> &value){auto *stored=static_cast<bNodeSocketValueRGBA *>(s.default_value)->value;if(std::equal(value.begin(),value.end(),stored))return false;std::copy(value.begin(),value.end(),stored);BKE_ntree_update_tag_socket_property(&tree,&s);return true;}
Stage color_correction(bNodeTree &tree,Stage source,const DisplayPostSettings &settings,bool &changed){
 auto &hsv=node(tree,"CompositorNodeHueSat","__BTS_DisplaySaturation");auto &contrast=node(tree,"CompositorNodeBrightContrast","__BTS_DisplayContrast");auto &bias=node(tree,"ShaderNodeMixRGB","__BTS_DisplayBias");bias.custom1=MA_RAMP_ADD;auto &gray=node(tree,"CompositorNodeRGBToBW","__BTS_DisplaySepiaGray");auto &tint=node(tree,"ShaderNodeMixRGB","__BTS_DisplaySepiaTint");tint.custom1=MA_RAMP_MULT;auto &sepia=node(tree,"ShaderNodeMixRGB","__BTS_DisplaySepia");auto &white=node(tree,"CompositorNodeColorBalance","__BTS_DisplayWhiteBalance");
 changed|=link(tree,*source.node,source.output,hsv,socket(hsv,"Image"));changed|=link(tree,hsv,"Image",contrast,socket(contrast,"Image"));changed|=link(tree,contrast,"Image",bias,input(bias,1));changed|=link(tree,bias,"Color",gray,socket(gray,"Image"));changed|=link(tree,gray,"Val",tint,input(tint,1));changed|=link(tree,bias,"Color",sepia,input(sepia,1));changed|=link(tree,tint,"Color",sepia,input(sepia,2));changed|=link(tree,sepia,"Color",white,socket(white,"Image"));
 changed|=number(tree,socket(hsv,"Saturation"),settings.saturation);changed|=number(tree,socket(contrast,"Bright"),(settings.brightness-1)*100);changed|=number(tree,socket(contrast,"Contrast"),(settings.contrast-1)*100);changed|=number(tree,input(bias,0),1);changed|=color(tree,input(bias,2),{settings.bias,settings.bias,settings.bias,1});changed|=number(tree,input(tint,0),1);float encoded[3]={1,.88f,.68f},linear[3];IMB_colormanagement_srgb_to_scene_linear_v3(linear,encoded);changed|=color(tree,input(tint,2),{linear[0],linear[1],linear[2],1});changed|=number(tree,input(sepia,0),settings.sepia);changed|=menu(tree,white,"Type",CMP_NODE_COLOR_BALANCE_WHITEPOINT);changed|=number(tree,socket(white,"Input Temperature"),6500);changed|=number(tree,socket(white,"Output Temperature"),settings.temperature);return {&white,"Image"};
}
Stage tone_mapping(bNodeTree &tree,Stage source,const DisplayPostSettings &settings,bool &changed){
 auto &exposure=node(tree,"CompositorNodeExposure","__BTS_DisplayExposure");changed|=link(tree,*source.node,source.output,exposure,socket(exposure,"Image"));changed|=number(tree,socket(exposure,"Exposure"),std::log2(settings.exposure));Stage result{&exposure,"Image"};
 if(settings.tone_function){auto &tone=node(tree,"CompositorNodeTonemap","__BTS_DisplayToneMap");changed|=link(tree,*result.node,result.output,tone,socket(tone,"Image"));changed|=menu(tree,tone,"Type",settings.tone_function==1?CMP_NODE_TONE_MAP_SIMPLE:CMP_NODE_TONE_MAP_PHOTORECEPTOR);changed|=number(tree,socket(tone,"Balance"),settings.mapping_factor);changed|=number(tree,socket(tone,"Intensity"),std::log2(settings.mapping_factor/32));result={&tone,"Image"};}
 auto &gamma=node(tree,"ShaderNodeGamma","__BTS_DisplayGamma");changed|=link(tree,*result.node,result.output,gamma,socket(gamma,"Color"));changed|=number(tree,socket(gamma,"Gamma"),1/settings.gamma);return {&gamma,"Color"};
}
void depth_pass(Main &main,Scene &scene,bool wanted){
 auto *layer=static_cast<ViewLayer *>(scene.view_layers.first);if(!layer)return;auto *root=IDP_EnsureProperties(&scene.id);auto *owned=IDP_GetPropertyFromGroup(root,"bts_post_owned_depth");const bool ours=owned&&owned->type==IDP_BOOLEAN&&IDP_bool_get(owned);bool changed=false;
 if(wanted&&!(layer->passflag&SCE_PASS_DEPTH)){layer->passflag|=SCE_PASS_DEPTH;IDP_ReplaceInGroup(root,bke::idprop::create_bool("bts_post_owned_depth",true).release());changed=true;}
 if(!wanted&&ours){layer->passflag&=~SCE_PASS_DEPTH;IDP_ReplaceInGroup(root,bke::idprop::create_bool("bts_post_owned_depth",false).release());changed=true;}
 if(changed){BKE_ntree_update_tag_id_changed(&main,&scene.id);BKE_ntree_update(main);DEG_id_tag_update_ex(&main,&scene.id,ID_RECALC_SYNC_TO_EVAL);DEG_relations_tag_update(&main);}
}
Stage depth_of_field(bNodeTree &tree,Stage image,const DisplayPostSettings &settings,bool &changed){
 auto &source=*bke::node_find_node_by_name(tree,"__BTS_DisplaySource");auto &depth=node(tree,"ShaderNodeMath","__BTS_DisplayPositiveDepth");depth.custom1=NODE_MATH_MAXIMUM;auto &ratio=node(tree,"ShaderNodeMath","__BTS_DisplayFocusRatio");ratio.custom1=NODE_MATH_DIVIDE;auto &delta=node(tree,"ShaderNodeMath","__BTS_DisplayFocusDelta");delta.custom1=NODE_MATH_SUBTRACT;auto &absolute=node(tree,"ShaderNodeMath","__BTS_DisplayFocusAbsolute");absolute.custom1=NODE_MATH_ABSOLUTE;auto &scale=node(tree,"CompositorNodeRelativeToPixel","__BTS_DisplayAperturePixels");scale.custom1=CMP_NODE_RELATIVE_TO_PIXEL_DATA_TYPE_FLOAT;scale.custom2=CMP_NODE_RELATIVE_TO_PIXEL_REFERENCE_DIMENSION_GREATER;auto &radius=node(tree,"ShaderNodeMath","__BTS_DisplayDefocusRadius");radius.custom1=NODE_MATH_MULTIPLY;auto &blur=node(tree,"CompositorNodeDefocus","__BTS_DisplayDOF");auto *data=static_cast<NodeDefocus *>(blur.storage);if(data->no_zbuf!=1||data->maxblur!=16||data->scale!=1){data->no_zbuf=1;data->maxblur=16;data->scale=1;BKE_ntree_update_tag_node_property(&tree,&blur);changed=true;}
 changed|=link(tree,source,"Depth",depth,input(depth,0));changed|=number(tree,input(depth,1),.000001f);changed|=number(tree,input(ratio,0),settings.focus_distance);changed|=link(tree,depth,"Value",ratio,input(ratio,1));changed|=number(tree,input(delta,0),1);changed|=link(tree,ratio,"Value",delta,input(delta,1));changed|=link(tree,delta,"Value",absolute,input(absolute,0));changed|=link(tree,source,"Image",scale,socket(scale,"Image"));changed|=number(tree,socket(scale,"Float Value"),settings.aperture*.004f);changed|=link(tree,absolute,"Value",radius,input(radius,0));changed|=link(tree,scale,"Float Value",radius,input(radius,1));changed|=link(tree,*image.node,image.output,blur,socket(blur,"Image"));changed|=link(tree,radius,"Value",blur,socket(blur,"Z"));return {&blur,"Image"};
}
Stage glare(bNodeTree &tree,Stage image,const DisplayPostSettings &settings,bool &changed){
 auto &effect=node(tree,"CompositorNodeGlare","__BTS_DisplayGlare");auto &gain=node(tree,"ShaderNodeMixRGB","__BTS_DisplayGlareGain");gain.custom1=MA_RAMP_MULT;auto &add=node(tree,"ShaderNodeMixRGB","__BTS_DisplayGlareAdd");add.custom1=MA_RAMP_ADD;
 changed|=link(tree,*image.node,image.output,effect,socket(effect,"Image"));changed|=menu(tree,effect,"Type",settings.glare_shape==0?CMP_NODE_GLARE_STREAKS:settings.glare_shape==1?CMP_NODE_GLARE_FOG_GLOW:CMP_NODE_GLARE_GHOST);changed|=menu(tree,effect,"Quality",CMP_NODE_GLARE_QUALITY_MEDIUM);changed|=number(tree,socket(effect,"Highlights Threshold"),settings.glare_threshold);changed|=number(tree,socket(effect,"Color Modulation"),(settings.glare_remap-1)/7);changed|=link(tree,effect,"Glare",gain,input(gain,1));changed|=number(tree,input(gain,0),1);changed|=color(tree,input(gain,2),{settings.glare_luminance,settings.glare_luminance,settings.glare_luminance,1});changed|=link(tree,*image.node,image.output,add,input(add,1));changed|=link(tree,gain,"Color",add,input(add,2));changed|=number(tree,input(add,0),1);return {&add,"Color"};
}
Stage lens_distortion(bNodeTree &tree,Stage image,const DisplayPostSettings &settings,bool &changed){
 auto &lens=node(tree,"CompositorNodeLensdist","__BTS_DisplayLens");changed|=link(tree,*image.node,image.output,lens,socket(lens,"Image"));changed|=menu(tree,lens,"Type",CMP_NODE_LENS_DISTORTION_RADIAL);changed|=number(tree,socket(lens,"Distortion"),LensProjection::coefficient(settings.lens_power,settings.lens_fov));changed|=number(tree,socket(lens,"Dispersion"),0);
 // Rounded-rectangle signed distance in output UV space, evaluated on the GPU.
 auto &uv=node(tree,"CompositorNodeImageCoordinates","__BTS_DisplayLensCoordinates");changed|=link(tree,*image.node,image.output,uv,socket(uv,"Image"));
 const auto vec=[&](const char *name,int operation)->bNode &{auto &n=node(tree,"ShaderNodeVectorMath",name);n.custom1=operation;return n;};
 const auto math=[&](const char *name,int operation)->bNode &{auto &n=node(tree,"ShaderNodeMath",name);n.custom1=operation;return n;};
 auto &center=vec("__BTS_LensCenter",NODE_VECTOR_MATH_SUBTRACT);auto &absolute=vec("__BTS_LensAbsolute",NODE_VECTOR_MATH_ABSOLUTE);auto &q=vec("__BTS_LensBorder",NODE_VECTOR_MATH_SUBTRACT);auto &positive=vec("__BTS_LensPositive",NODE_VECTOR_MATH_MAXIMUM);auto &length=vec("__BTS_LensLength",NODE_VECTOR_MATH_LENGTH);auto &xy=node(tree,"ShaderNodeSeparateXYZ","__BTS_LensXY");auto &maximum=math("__BTS_LensMax",NODE_MATH_MAXIMUM);auto &inside=math("__BTS_LensInside",NODE_MATH_MINIMUM);auto &sum=math("__BTS_LensSum",NODE_MATH_ADD);auto &distance=math("__BTS_LensDistance",NODE_MATH_SUBTRACT);
 changed|=link(tree,uv,"Normalized",center,input(center,0));changed|=vector(tree,input(center,1),.5,.5);changed|=link(tree,center,"Vector",absolute,input(absolute,0));changed|=link(tree,absolute,"Vector",q,input(q,0));changed|=vector(tree,input(q,1),.5-settings.lens_roundness,.5-settings.lens_roundness);changed|=link(tree,q,"Vector",positive,input(positive,0));changed|=vector(tree,input(positive,1),0,0);changed|=link(tree,positive,"Vector",length,input(length,0));changed|=link(tree,q,"Vector",xy,socket(xy,"Vector"));changed|=link(tree,xy,"X",maximum,input(maximum,0));changed|=link(tree,xy,"Y",maximum,input(maximum,1));changed|=link(tree,maximum,"Value",inside,input(inside,0));changed|=number(tree,input(inside,1),0);changed|=link(tree,length,"Value",sum,input(sum,0));changed|=link(tree,inside,"Value",sum,input(sum,1));changed|=link(tree,sum,"Value",distance,input(distance,0));changed|=number(tree,input(distance,1),settings.lens_roundness);
 auto &feather=node(tree,"ShaderNodeMapRange","__BTS_LensFeather");auto *mapping=static_cast<NodeMapRange *>(feather.storage);if(mapping->interpolation_type!=NODE_MAP_RANGE_SMOOTHSTEP){mapping->interpolation_type=NODE_MAP_RANGE_SMOOTHSTEP;BKE_ntree_update_tag_node_property(&tree,&feather);changed=true;}changed|=link(tree,distance,"Value",feather,socket(feather,"Value"));changed|=number(tree,socket(feather,"From Min"),-std::max(.000001f,.005f*settings.lens_smoothness));changed|=number(tree,socket(feather,"From Max"),0);changed|=number(tree,socket(feather,"To Min"),1);changed|=number(tree,socket(feather,"To Max"),0);
 auto &alpha=node(tree,"CompositorNodeSetAlpha","__BTS_LensAlpha");changed|=menu(tree,alpha,"Type",CMP_NODE_SETALPHA_MODE_APPLY);changed|=link(tree,lens,"Image",alpha,socket(alpha,"Image"));changed|=link(tree,feather,"Result",alpha,socket(alpha,"Alpha"));auto &over=node(tree,"CompositorNodeAlphaOver","__BTS_LensOpaqueBorder");changed|=color(tree,socket(over,"Background"),{0,0,0,1});changed|=link(tree,alpha,"Image",over,socket(over,"Foreground"));return {&over,"Image"};
}
void tag(Main &main,Scene &scene,bNodeTree &tree){BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);DEG_id_tag_update_ex(&main,&scene.id,ID_RECALC_SYNC_TO_EVAL);}
}
DisplayPostSettings display_post_settings(const Scene &scene){auto *value=scene.id.properties?IDP_GetPropertyFromGroup(scene.id.properties,state_key):nullptr;if(!value)return {};const auto bytes=native_json_property_bytes(*value,"Display post effect JSON decoding");QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::invalid_argument("Invalid display post effect metadata");return DisplayPostSettings::from_json(doc.object());}
void set_display_post_settings(Main &main,Scene &scene,View3D &view,DisplayPostSettings value){value.validate();if(value.active())ensure_tree(main,scene);const auto bytes=QJsonDocument(value.to_json()).toJson(QJsonDocument::Compact);IDP_ReplaceInGroup(IDP_EnsureProperties(&scene.id),new_native_json_property(bytes,state_key,"Display post effect JSON storage"));prepare_display_post_effects(main,scene,view,1,1);}
void prepare_display_post_effects(Main &main,Scene &scene,View3D &view,int width,int height){
 const auto value=display_post_settings(scene);depth_pass(main,scene,value.enabled&&value.dof_active());view.shading.use_compositor=value.active()?V3D_SHADING_USE_COMPOSITOR_ALWAYS:V3D_SHADING_USE_COMPOSITOR_DISABLED;
 if(!value.active()&&(!scene.compositing_node_group||!bke::node_find_node_by_name(*scene.compositing_node_group,"__BTS_DisplaySource")))return;
 auto &tree=ensure_tree(main,scene);const int prior_count=tree.nodes.count();Stage result{bke::node_find_node_by_name(tree,"__BTS_DisplaySource"),"Image"};bool changed=false;
 if(value.active()){
  changed|=background(main,scene,view,tree);result={bke::node_find_node_by_name(tree,"__BTS_DisplayBackground"),"Image"};
  if(value.color_active())result=color_correction(tree,result,value,changed);
  if(value.dof_active())result=depth_of_field(tree,result,value,changed);
  if(value.tone_active())result=tone_mapping(tree,result,value,changed);
  if(value.glare_active())result=glare(tree,result,value,changed);
  if(value.vignette&&value.vignette_strength>0){auto &mix=*bke::node_find_node_by_name(tree,"__BTS_DisplayVignette");auto &blur=*bke::node_find_node_by_name(tree,"__BTS_DisplayVignetteBlur");changed|=link(tree,*result.node,result.output,mix,input(mix,1));changed|=number(tree,input(mix,0),value.vignette_strength);if(width>1&&height>1)changed|=vector(tree,socket(blur,"Size"),width*.18f,height*.18f);result={&mix,"Color"};}
  if(value.lens_active())result=lens_distortion(tree,result,value,changed);
 }
 auto &output=*bke::node_find_node_by_name(tree,"__BTS_DisplayOutput");changed|=link(tree,*result.node,result.output,output,input(output,0));if(prior_count!=tree.nodes.count())DEG_relations_tag_update(&main);if(changed)tag(main,scene,tree);
}
}
