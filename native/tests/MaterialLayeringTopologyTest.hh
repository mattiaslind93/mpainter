#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include "MaterialInspection.hh"
#include "NativeWorldNormalImages.hh"
#include "GpuLayerPreview.hh"
#include "TextureDocument.hh"
#include "BKE_global.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include <stdexcept>

namespace bts {
// CPU fixture only, called after ordinary host startup. The admission API does
// not allocate or evaluate pixels; allocate_only creates the fixture beforehand.
inline QJsonObject verify_material_layering_topology_cpu(){
 using namespace blender;
 const auto require=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
 require(G_MAIN&&!GPU_context_active_get(),"Layering topology fixture requires live Main and no GPU");
 auto &main=*G_MAIN;const auto programs=GPU_pass_global_compilation_count();
 auto *material=BKE_material_add(&main,"PD149 topology fixture");require(material&&material->nodetree,"Cannot create topology fixture material");
 const auto cleanup=qScopeGuard([&]{GpuLayerPreview::collect_retired();BKE_id_free(&main,material);});
 auto &tree=*material->nodetree;auto *surface=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled")),*output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
 const auto socket=[](bNode &node,const char *id,bool inputs)->bNodeSocket &{for(auto &s:inputs?node.inputs:node.outputs)if(STREQ(s.identifier,id))return s;throw std::runtime_error("Topology fixture socket missing");};
 BKE_ntree_update_after_single_tree_change(main,tree);bke::node_add_link(tree,*surface,socket(*surface,"BSDF",false),*output,socket(*output,"Surface",true));BKE_ntree_update_after_single_tree_change(main,tree);
 TextureSet set;set.material_session_uid=material->id.session_uid;set.texture_resolution=32;set.uniform_editable=true;
 int positives=0,negatives=0;
 {
  MaterialInspection inspection;
  const auto drain=qScopeGuard([&]{inspection.reset();GpuLayerPreview::collect_retired();});
  inspection.update_pixels(main,set,32,{},false,true);
  // CPU metadata setup must not install sampled world targets or attempt their
  // GPU placeholders. The ordinary legacy Normal chain remains the oracle.
  const auto world=inspection.native_world_normal_status(set.material_session_uid);
  require(world["private_images"].toInt()==0&&!world["enabled"].toBool()&&!world["full_targets"].toBool()&&!world["pending"].toBool()&&world["programs"].toInt()==0,"CPU bootstrap created private world runtime state");
  for(const auto &n:tree.nodes)require(!std::string_view(n.name).starts_with("__BTS_NativeWorld"),"CPU bootstrap installed a GPU-only world shader");
  require(!GPU_context_active_get()&&GPU_pass_global_compilation_count()==programs,"CPU bootstrap activated or compiled GPU state");
  Image unowned{};for(int operation=0;operation<3;++operation){bool denied=false;try{if(operation==0)native_world_normal_placeholder(unowned);else if(operation==1)native_world_normal_resize(unowned,set);else native_world_normal_full(unowned);}catch(const std::logic_error &){denied=true;}require(denied&&!unowned.runtime&&unowned.flag==0,"World image helper touched an Image without its GPU owner context");}
  // Retain actual pointers, socket/default storage, image ownership metadata and
  // queue state; never acquire an ImBuf, sample pixels or hash their contents.
  const auto state=[&]{
   const auto pointer=[](const void *p){return QString::number(quintptr(p),16);};QJsonArray nodes,links,images;
   for(const auto &n:tree.nodes){QJsonArray inputs;for(const auto &s:n.inputs){QJsonArray value;if(s.default_value){if(s.type==SOCK_FLOAT)value.append(static_cast<const bNodeSocketValueFloat *>(s.default_value)->value);else if(s.type==SOCK_RGBA)for(float v:static_cast<const bNodeSocketValueRGBA *>(s.default_value)->value)value.append(v);else if(s.type==SOCK_VECTOR)for(float v:static_cast<const bNodeSocketValueVector *>(s.default_value)->value)value.append(v);}inputs.append(QJsonObject{{"pointer",pointer(&s)},{"identifier",s.identifier},{"type",int(s.type)},{"flags",int(s.flag)},{"value",value}});}nodes.append(QJsonObject{{"pointer",pointer(&n)},{"name",n.name},{"type",n.idname},{"id",pointer(n.id)},{"flags",int(n.flag)},{"custom1",n.custom1},{"custom2",n.custom2},{"storage",pointer(n.storage)},{"inputs",inputs}});}
   for(const auto &link:tree.links)links.append(QJsonArray{pointer(&link),pointer(link.fromnode),pointer(link.fromsock),pointer(link.tonode),pointer(link.tosock),int(link.flag)});
   for(const auto &image:main.images)images.append(QJsonArray{pointer(&image),image.id.us,int(image.source),int(image.alpha_mode)});
   return QJsonObject{{"nodes",nodes},{"links",links},{"images",images},{"queues",inspection.diagnostic_native_source_queues()}};
  };
  const auto admit=[&](const TextureSet &candidate){const auto before=state();inspection.validate_material_layering(candidate);require(state()==before,"Topology admission changed graph/IDs/queues");++positives;};
  admit(set);const auto original=state();
  auto *mix=bke::node_find_node_by_name(tree,"__BTS_BaseMix0"),*pixels=bke::node_find_node_by_name(tree,"__BTS_Pixels0"),*height=bke::node_find_node_by_name(tree,"__BTS_PaintHeight");require(mix&&pixels&&height,"Topology fixture retained targets missing");
  const auto reject=[&](auto change,auto restore){change();const auto changed=state();bool denied=false;try{inspection.validate_material_layering(set);}catch(const std::runtime_error &){denied=true;}const bool unchanged=state()==changed;restore();require(denied&&unchanged,"Topology corruption accepted or admission mutated it");require(state()==original,"Topology fixture did not restore original state");++negatives;admit(set);};
  const auto operation=mix->custom1;reject([&]{mix->custom1=MA_RAMP_MULT;},[&]{mix->custom1=operation;});
  const auto flags=mix->flag;reject([&]{mix->flag|=NODE_MUTED;},[&]{mix->flag=flags;});
  auto &destination=socket(*mix,"Color2",true);bNodeLink *link=nullptr;for(auto &candidate:tree.links)if(candidate.tosock==&destination)link=&candidate;require(link,"Topology fixture link missing");
  const auto link_flags=link->flag;reject([&]{link->flag|=NODE_LINK_MUTED;},[&]{link->flag=link_flags;});
  reject([&]{link->flag&=~NODE_LINK_VALID;},[&]{link->flag=link_flags;});
  auto *from=link->fromsock;reject([&]{link->fromsock=&socket(*pixels,"Color",false);},[&]{link->fromsock=from;});
  const auto socket_type=destination.type;reject([&]{destination.type=SOCK_FLOAT;},[&]{destination.type=socket_type;});
  auto *id=pixels->id;reject([&]{pixels->id=nullptr;},[&]{pixels->id=id;});
  auto &strength=socket(*height,"Strength",true);auto &value=static_cast<bNodeSocketValueFloat *>(strength.default_value)->value;const auto old=value;reject([&]{value=1;},[&]{value=old;});
  // A foreign same-name retained node cannot substitute for the binding-owned
  // pointer, even if its Blender node type is identical. No node is freed.
  auto *previous=mix->prev;reject([&]{BLI_remlink(&tree.nodes,mix);},[&]{BLI_insertlinkafter(&tree.nodes,previous,mix);});
  auto *foreign=bke::node_add_node(nullptr,tree,UString("ShaderNodeMixRGB"));require(foreign,"Cannot create foreign node fixture");STRNCPY(foreign->name,"__BTS_BaseMix0");const auto duplicate=state();bool denied=false;try{inspection.validate_material_layering(set);}catch(const std::runtime_error &){denied=true;}require(denied&&state()==duplicate,"Duplicate foreign retained identity accepted");bke::node_remove_node(&main,tree,*foreign,true);++negatives;
  admit(set);
  auto *reserved=bke::node_add_node(nullptr,tree,UString("ShaderNodeValue"));require(reserved,"Cannot create reserved collision fixture");STRNCPY(reserved->name,"__BTS_LayeringEnabled");const auto collision=state();denied=false;try{inspection.validate_material_layering(set);}catch(const std::runtime_error &){denied=true;}require(denied&&state()==collision,"First Enable adopted a foreign reserved Layer node");bke::node_remove_node(&main,tree,*reserved,true);++negatives;admit(set);
  // This API is intentionally topology-only. Disable of legacy metadata is not
  // retroactively rejected; the separate model gate owns enabled eligibility.
  auto legacy=set;legacy.material_layering.materials[0].values.normal={1,0,0};admit(legacy);
  const auto queues=inspection.diagnostic_native_source_queues();auto missing=set;missing.material_session_uid=0;bool absent=false;try{inspection.validate_material_layering(missing);}catch(const std::runtime_error &){absent=true;}require(absent&&queues==inspection.diagnostic_native_source_queues(),"Missing binding changed queue state");++negatives;
 }
 GpuLayerPreview::collect_retired();require(!GPU_context_active_get()&&GPU_pass_global_compilation_count()==programs,"Topology CPU fixture created GPU state");
 return {{"cpu_bootstrap_world_gpu_state_absent",true},{"gpu_owner_context_guard_rejections",3},{"world_private_images",0},{"world_placeholder_gpu_allocations",0},{"topology_positive_cases",positives},{"topology_negative_cases",negatives},{"pixel_api_evidence","source audit: admission calls no pixel API; setup is separate"},{"gpu_programs_created",0},{"metadata_eligibility_separate",true},{"historical_unretained_node_identity_not_claimed",true}};
}
}
