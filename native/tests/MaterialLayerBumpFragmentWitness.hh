// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Test-only EEVEE fragment witness. It NEVER evaluates Bump through compute.
// Retain the actual Surface and Principled first branch; temporarily replace
// only its already-owned inspection branch, then restore before history/Save.
#include "MaterialLayeringGpuTap.hh"
#include "DEG_depsgraph.hh"
#include "BKE_main.hh"
#include "DNA_scene_types.h"
namespace bts::pd149_bump_test {
class NormalEmission final {
 blender::Main &main_;blender::Material &material_;blender::bNodeTree &tree_;
 blender::bNodeLink *original_=nullptr,*previous_=nullptr,*surface_link_=nullptr;
 blender::bNode *principled_=nullptr,*view_switch_=nullptr,*diagnostic_=nullptr;blender::bNodeSocket *factor_=nullptr;float old_factor_=0;bool factor_changed_=false;
 std::vector<blender::bNode *> created_;QJsonObject before_;bool detached_=false,dither_changed_=false;blender::Scene *scene_=nullptr;float old_dither_=0;
 QJsonObject graph()const{const auto value=pd149_test::live_signature(material_);return {{"nodes",value["nodes"]},{"links",value["links"]}};}
 blender::bNode &node(const char *id,const char *name){
  using namespace blender;pd149_test::require(!bke::node_find_node_by_name(tree_,name),"Bump fragment witness node name collision");
  auto *n=bke::node_add_node(nullptr,tree_,UString(id));pd149_test::require(n,"Bump fragment witness node allocation failed");created_.push_back(n);STRNCPY(n->name,name);return *n;
 }
 void updated(){using namespace blender;BKE_ntree_update_after_single_tree_change(main_,tree_);DEG_id_tag_update_ex(&main_,&material_.id,ID_RECALC_SHADING);DEG_id_tag_update_ex(&main_,&tree_.id,ID_RECALC_NTREE_OUTPUT);}
 public:
 float distance=0,strength=0,filter_width=0;
 NormalEmission(blender::Main &main,blender::Material &material):main_(main),material_(material),tree_(*material.nodetree){
  using namespace blender;using namespace pd149_test;created_.reserve(3);before_=graph();
  auto *bump=bke::node_find_node_by_name(tree_,"__BTS_LayeredBump"),*surface=native_principled_surface(material);
  require(bump&&surface&&STREQ(bump->idname,"ShaderNodeBump")&&!bump->custom1&&!bump->custom2,"Bump fragment witness requires the actual noninverted native Bump");
  const auto &actual=incoming(tree_,input(*surface,"Normal"));require(actual.fromnode==bump&&actual.fromsock==&output(*bump,"Normal"),"Bump fragment witness is not the actual Surface Normal");
  const auto scalar=[&](const char *id){auto &s=input(*bump,id);require(s.type==SOCK_FLOAT&&s.default_value,"Bump settings missing");for(const auto &link:tree_.links)require(link.tosock!=&s,"Bump settings unexpectedly linked");return static_cast<const bNodeSocketValueFloat *>(s.default_value)->value;};
  distance=scalar("Distance");strength=scalar("Strength");filter_width=scalar("Filter Width");
  require(distance==.001f&&strength==1.f&&filter_width==.1f,"Bump witness requires pinned ordinary Bump defaults");
  bNode *destination=nullptr;for(auto &n:tree_.nodes)if(STREQ(n.idname,"ShaderNodeOutputMaterial")&&(n.flag&NODE_DO_OUTPUT)){require(!destination,"Bump witness needs exactly one active Material output");destination=&n;}
  require(destination,"Bump witness active output missing");surface_link_=const_cast<bNodeLink *>(&incoming(tree_,input(*destination,"Surface")));view_switch_=bke::node_find_node_by_name(tree_,"__BTS_ViewSwitch");principled_=surface;
  require(view_switch_&&STREQ(view_switch_->idname,"ShaderNodeMixShader")&&surface_link_->fromnode==view_switch_&&surface_link_->fromsock==&output(*view_switch_,"Shader"),"Bump witness needs its existing canonical inspection Surface wrapper");
  const auto &first=incoming(tree_,input(*view_switch_,"Shader"));require(first.fromnode==principled_&&first.fromsock==&output(*principled_,"BSDF"),"Bump witness must retain the exact Principled first branch");
  original_=const_cast<bNodeLink *>(&incoming(tree_,input(*view_switch_,"Shader_001")));previous_=original_->prev;require(STREQ(original_->fromnode->name,"__BTS_Inspection")&&STREQ(original_->fromnode->idname,"ShaderNodeEmission"),"Bump witness requires its actual owned inspection branch");
  factor_=&input(*view_switch_,"Fac");require(factor_->type==SOCK_FLOAT&&factor_->default_value,"Bump view factor missing");for(const auto &link:tree_.links)require(link.tosock!=factor_,"Bump view factor is unexpectedly linked");old_factor_=static_cast<const bNodeSocketValueFloat *>(factor_->default_value)->value;require(old_factor_==0,"Bump witness must begin from the actual Material first branch");
  try{
   // The actor owns a one-scene fixture. Disable only display dithering during
   // this diagnostic so its known UNORM8 budget contains no stochastic term.
   // The ordinary artist scene is restored before any history or Save action.
   require(main_.scenes.first&&main_.scenes.first==main_.scenes.last,"Bump witness requires its own one-scene fixture");scene_=static_cast<Scene *>(main_.scenes.first);old_dither_=scene_->r.dither_intensity;scene_->r.dither_intensity=0;dither_changed_=true;
   auto &scale=node("ShaderNodeVectorMath","__BTS_TestBumpNormalScale"),&offset=node("ShaderNodeVectorMath","__BTS_TestBumpNormalOffset"),&emission=node("ShaderNodeEmission","__BTS_TestBumpNormalEmission");scale.custom1=NODE_VECTOR_MATH_MULTIPLY;offset.custom1=NODE_VECTOR_MATH_ADD;auto &power=input(emission,"Strength");require(power.type==SOCK_FLOAT&&power.default_value&&static_cast<const bNodeSocketValueFloat *>(power.default_value)->value==1,"Bump witness emission must have unit Strength");
   const auto constant=[&](bNode &n){auto &s=input(n,"Vector_001");require(s.type==SOCK_VECTOR&&s.default_value,"Bump witness vector constant missing");auto *v=static_cast<bNodeSocketValueVector *>(s.default_value)->value;std::fill_n(v,3,.5f);};constant(scale);constant(offset);
   bke::node_add_link(tree_,*bump,output(*bump,"Normal"),scale,input(scale,"Vector"));bke::node_add_link(tree_,scale,output(scale,"Vector"),offset,input(offset,"Vector"));bke::node_add_link(tree_,offset,output(offset,"Vector"),emission,input(emission,"Color"));
   BLI_remlink(&tree_.links,original_);detached_=true;
   bke::node_add_link(tree_,emission,output(emission,"Emission"),*view_switch_,input(*view_switch_,"Shader_001"));diagnostic_=&emission;static_cast<bNodeSocketValueFloat *>(factor_->default_value)->value=1;factor_changed_=true;BKE_ntree_update_tag_node_property(&tree_,view_switch_);updated();verify();
  }catch(...){restore();throw;}
 }
 void verify()const{
  using namespace blender;using namespace pd149_test;require(native_principled_surface(material_)==principled_&&static_cast<const bNodeSocketValueFloat *>(factor_->default_value)->value==1,"Bump witness lost canonical Principled identity or selected diagnostic factor");
  bool retained=false;for(const auto &link:tree_.links){retained|=&link==surface_link_;require(link.tosock!=factor_,"Bump diagnostic factor unexpectedly linked");}require(retained&&surface_link_->fromnode==view_switch_,"Bump witness changed the actual Surface link");
  const auto &first=incoming(tree_,input(*view_switch_,"Shader")),&second=incoming(tree_,input(*view_switch_,"Shader_001"));require(first.fromnode==principled_&&first.fromsock==&output(*principled_,"BSDF")&&second.fromnode==diagnostic_&&second.fromsock==&output(*diagnostic_,"Emission"),"Bump witness changed canonical or diagnostic branch identity");
 }
 void restore(){
  using namespace blender;if(created_.empty()&&!detached_&&!dither_changed_&&!factor_changed_)return;
  for(auto i=created_.rbegin();i!=created_.rend();++i)bke::node_remove_node(nullptr,tree_,**i,false,false);created_.clear();
  if(detached_){if(previous_)BLI_insertlinkafter(&tree_.links,previous_,original_);else BLI_addhead(&tree_.links,original_);detached_=false;}
  if(factor_changed_){static_cast<bNodeSocketValueFloat *>(factor_->default_value)->value=old_factor_;factor_changed_=false;BKE_ntree_update_tag_node_property(&tree_,view_switch_);}
  if(dither_changed_){scene_->r.dither_intensity=old_dither_;dither_changed_=false;}
  updated();pd149_test::require(graph()==before_,"Bump fragment witness failed to restore the exact material graph");
 }
 ~NormalEmission(){try{restore();}catch(...){qFatal("Bump fragment witness escaped Surface restoration");}}
 NormalEmission(const NormalEmission&)=delete;NormalEmission &operator=(const NormalEmission&)=delete;
};
}
