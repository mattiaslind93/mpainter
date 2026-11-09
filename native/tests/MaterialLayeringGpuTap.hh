// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Diagnostic only. Compile copies of actual connected artist outputs, never a
// second weight graph. All construction/use/destruction is on Host's GPU owner.
#include "NativeMaterialGpuProgram.hh"
#include "NativeGpuImages.hh"
#include "SceneSnapshot.hh"
#include "BKE_anim_data.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_node_runtime.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "GPU_context.hh"
#include "GPU_state.hh"
#include "MEM_guardedalloc.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <cstring>
#include <cstdio>
#include <set>
#include <thread>
namespace bts::pd149_test {
using namespace blender;
inline void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
inline bNodeSocket &input(bNode &node,const char *identifier){auto *socket=bke::node_find_socket(node,SOCK_IN,UString(identifier));if(!socket||!socket->is_available())throw std::runtime_error("PD149 diagnostic input missing: "+std::string(node.name)+"/"+identifier);return *socket;}
inline bNodeSocket &output(bNode &node,const char *name){for(auto &socket:node.outputs)if(STREQ(socket.identifier,name))return socket;throw std::runtime_error("PD149 diagnostic output missing");}
inline const bNodeLink &incoming(bNodeTree &tree,bNodeSocket &socket){const bNodeLink *found=nullptr;for(const auto &link:tree.links)if(link.tosock==&socket){require(!found,"PD149 ambiguous diagnostic source");found=&link;}require(found&&found->fromnode&&found->fromsock&&(found->flag&NODE_LINK_VALID)&&!(found->flag&NODE_LINK_MUTED),"PD149 diagnostic source is not an active unique link");return *found;}
inline QJsonArray pointer(const void *value){return {QString::number(reinterpret_cast<std::uintptr_t>(value),16)};}
inline QJsonArray defaults(const bNodeSocket &socket){
 if(!socket.default_value)return {};const auto *p=socket.default_value;
 switch(socket.type){case SOCK_FLOAT:return {static_cast<const bNodeSocketValueFloat *>(p)->value};case SOCK_INT:return {static_cast<const bNodeSocketValueInt *>(p)->value};case SOCK_BOOLEAN:return {static_cast<const bNodeSocketValueBoolean *>(p)->value};case SOCK_VECTOR:{auto *v=static_cast<const bNodeSocketValueVector *>(p)->value;return {v[0],v[1],v[2]};}case SOCK_RGBA:{auto *v=static_cast<const bNodeSocketValueRGBA *>(p)->value;return {v[0],v[1],v[2],v[3]};}default:return {};}
}
inline QJsonObject live_signature(Material &material){
 require(material.nodetree,"PD149 diagnostic material has no tree");auto &tree=*material.nodetree;QJsonArray nodes,links;
 for(const auto &node:tree.nodes){QJsonArray inputs,outputs;for(const auto &s:node.inputs)inputs.append(QJsonObject{{"pointer",pointer(&s)},{"id",s.identifier},{"type",s.type},{"flags",s.flag},{"default",defaults(s)}});for(const auto &s:node.outputs)outputs.append(QJsonObject{{"pointer",pointer(&s)},{"id",s.identifier},{"type",s.type},{"flags",s.flag},{"default",defaults(s)}});nodes.append(QJsonObject{{"pointer",pointer(&node)},{"identifier",node.identifier},{"name",node.name},{"type",node.idname},{"flags",node.flag},{"custom1",node.custom1},{"custom2",node.custom2},{"id",pointer(node.id)},{"inputs",inputs},{"outputs",outputs}});}
 for(const auto &link:tree.links)links.append(QJsonArray{pointer(&link),pointer(link.fromnode),pointer(link.fromsock),pointer(link.tonode),pointer(link.tosock),link.fromnode->identifier,link.fromsock->identifier,link.tonode->identifier,link.tosock->identifier,link.flag});
 return {{"material",pointer(&material)},{"material_recalc",qint64(material.id.recalc)},{"material_flag",material.id.flag},{"material_tag",int(material.id.tag)},{"tree",pointer(&tree)},{"tree_recalc",qint64(tree.id.recalc)},{"tree_flag",tree.id.flag},{"tree_tag",int(tree.id.tag)},{"nodes",nodes},{"links",links}};
}
enum class Output {Color,Metallic,Roughness,AO,Mask0,Mask1,Mask2};
struct Endpoint {bNode *node;std::string socket;bool scalar;};
inline Endpoint endpoint(Material &material,Output which){
 auto &tree=*material.nodetree;auto *surface=native_principled_surface(material);require(surface,"PD149 diagnostic requires actual Principled Surface");
 if(which==Output::Color||which==Output::Metallic||which==Output::Roughness){const auto &link=incoming(tree,input(*surface,which==Output::Color?"Base Color":which==Output::Metallic?"Metallic":"Roughness"));return {link.fromnode,link.fromsock->identifier,which!=Output::Color};}
 if(which==Output::AO){const auto &color=incoming(tree,input(*surface,"Base Color"));require(STREQ(color.fromnode->idname,"ShaderNodeMixRGB")&&color.fromnode->custom1==MA_RAMP_MULT,"PD149 final Color is not its actual AO product");const auto &ao=incoming(tree,input(*color.fromnode,"Color2"));return {ao.fromnode,ao.fromsock->identifier,true};}
 const int slot=int(which)-int(Output::Mask0);const auto name="__BTS_LayerMix0_"+std::to_string(slot);auto *mix=bke::node_find_node_by_name(tree,name.c_str());require(mix&&STREQ(mix->idname,"ShaderNodeMixRGB")&&mix->custom1==MA_RAMP_BLEND,"PD149 actual Color weight mix missing");const auto &factor=incoming(tree,input(*mix,"Fac"));require(STREQ(factor.fromnode->idname,"ShaderNodeClamp")&&factor.fromsock->type==SOCK_FLOAT,"PD149 actual Color factor is not a scalar clamp");return {factor.fromnode,factor.fromsock->identifier,true};
}
class GpuTap final {
 struct TreeDeleter {void operator()(bNodeTree *tree)const{if(tree)BKE_id_free_ex(nullptr,&tree->id,LIB_ID_FREE_NO_MAIN|LIB_ID_FREE_NO_USER_REFCOUNT|LIB_ID_FREE_NO_DEG_TAG,false);}};
 std::thread::id owner_=std::this_thread::get_id();GPUContext *context_=GPU_context_active_get();Material *source_;QJsonObject original_;std::unique_ptr<bNodeTree,TreeDeleter> tree_;std::unique_ptr<NativeMaterialGpuProgram> program_;std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)> target_{nullptr,GPU_texture_free};std::vector<Image *> images_;int width_,height_;
 QJsonArray handles()const{
  QJsonArray result;for(auto *image:images_){
   const bool cpu=BKE_image_has_loaded_ibuf(image),tiled=image->source==IMA_SRC_TILED;
   const auto cached=image->runtime?BKE_image_gpu_override_texture(image):ImageGPUTextures{};auto *texture=cached.texture?*cached.texture:nullptr;
   QJsonObject info{{"uid",qint64(image->id.session_uid)},{"name",image->id.name},{"source",image->source},{"cpu_cache_present",cpu},{"native_generated",native_gpu_image(image)},{"texture",pointer(texture)},{"format",texture?GPU_texture_format_name(GPU_texture_format(texture)):"none"},{"width",texture?GPU_texture_width(texture):0},{"height",texture?GPU_texture_height(texture):0},{"layers",texture?GPU_texture_layer_count(texture):0}};
   const auto check=[&](bool okay,const char *reason){if(!okay)throw std::runtime_error(std::string("PD149 resident Image rejected: ")+reason+" "+QJsonDocument(info).toJson(QJsonDocument::Compact).constData());};
   check(texture&&GPU_texture_dimensions(texture)==2&&GPU_texture_is_array(texture)==tiled&&GPU_texture_width(texture)>0&&GPU_texture_height(texture)>0&&!GPU_texture_has_integer_format(texture)&&!GPU_texture_has_depth_format(texture)&&!GPU_texture_has_stencil_format(texture),"sampleable storage missing/invalid");
   if(!cpu){check(native_gpu_image(image)&&(image->source==IMA_SRC_GENERATED||tiled),"CPU-cache-absent input is not a native generated source");check(GPU_texture_format(texture)==(image->flag&IMA_HIGH_BITDEPTH?gpu::TextureFormat::SFLOAT_32_32_32_32:gpu::TextureFormat::SFLOAT_16_16_16_16),"native generated precision mismatch");}
   check(!tiled||(cached.tile_mapping&&*cached.tile_mapping),"UDIM mapping missing");
   QJsonArray regions;std::set<int> membership;for(const auto &tile:image->tiles){membership.insert(tile.tile_number);ImageGpuTileRegion region;check(BKE_image_gpu_override_tile_region(image,tile.tile_number,region),"tile not resident");check(region.width>0&&region.height>0&&region.x>=0&&region.y>=0&&region.x<=GPU_texture_width(texture)-region.width&&region.y<=GPU_texture_height(texture)-region.height&&region.layer>=0&&region.layer<GPU_texture_layer_count(texture),"tile outside allocation");check(region.width==tile.gen_x&&region.height==tile.gen_y,"resident tile differs from generated dimensions");regions.append(QJsonObject{{"tile",tile.tile_number},{"x",region.x},{"y",region.y},{"width",region.width},{"height",region.height},{"layer",region.layer}});}
   check(membership==(tiled?std::set<int>{1001,1002}:std::set<int>{1001}),"unexpected synthetic tile membership");
   QJsonArray allocations;for(int type=0;type<TEXTARGET_COUNT;++type)for(int eye=0;eye<2;++eye)allocations.append(pointer(image->runtime->gputexture[type][eye]));
   info.insert("id_flags",image->id.flag);info.insert("id_tag",int(image->id.tag));info.insert("id_recalc",qint64(image->id.recalc));info.insert("id_users",image->id.us);info.insert("alpha_mode",image->alpha_mode);info.insert("flags",image->flag);info.insert("space",image->colorspace_settings.name);info.insert("allocations",allocations);info.insert("regions",regions);result.append(info);
  }return result;
 }
 QJsonArray handles_;
 void owner()const{require(owner_==std::this_thread::get_id()&&context_&&GPU_context_active_get()==context_,"PD149 tap requires its native owner context");}
 public:
 GpuTap(Material &material,Output which,int width,int height):source_(&material),original_(live_signature(material)),width_(width),height_(height){
  owner();require(width>0&&height>0,"PD149 tap grid invalid");auto &source=*material.nodetree;require(!BKE_animdata_from_id(&source.id),"PD149 canonical diagnostic excludes animated trees");for(const auto &node:source.nodes)require(!node.id||GS(node.id->name)!=ID_NT,"PD149 canonical diagnostic excludes node groups");const auto selected=endpoint(material,which);
  std::vector<bNode *> pending{selected.node};std::set<bNode *> visited;std::set<Image *> seen;
  while(!pending.empty()){auto *node=pending.back();pending.pop_back();if(!visited.insert(node).second)continue;if(STREQ(node->idname,"ShaderNodeTexImage")){require(node->id&&GS(node->id->name)==ID_IM,"PD149 linked diagnostic Image missing");seen.insert(reinterpret_cast<Image *>(node->id));}for(const auto &link:source.links)if(link.tonode==node){require(link.fromnode&&(link.flag&NODE_LINK_VALID)&&!(link.flag&NODE_LINK_MUTED),"PD149 diagnostic dependency is inactive");pending.push_back(link.fromnode);}}
  images_.assign(seen.begin(),seen.end());handles_=handles();
  tree_.reset(reinterpret_cast<bNodeTree *>(BKE_id_copy_in_lib(nullptr,std::optional<Library *>{nullptr},&source.id,std::optional<const ID *>{nullptr},nullptr,LIB_ID_COPY_LOCALIZE|LIB_ID_COPY_NO_ANIMDATA)));
  require(tree_&&!BKE_id_owner_get(&tree_->id,false),"PD149 copy must be outside Main without the artist owner");tree_->flag|=NTREE_IS_GPU_SHADER_INTERNAL;
  bNode *from=nullptr;std::vector<bNode *> remove;for(auto &node:tree_->nodes){if(node.identifier==selected.node->identifier)from=&node;if(STREQ(node.idname,"ShaderNodeOutputMaterial")||STREQ(node.idname,"ShaderNodeOutputAOV"))remove.push_back(&node);}require(from,"PD149 copied endpoint missing");for(auto *node:remove)bke::node_remove_node(nullptr,*tree_,*node,false,false);
  auto *aov=bke::node_add_node(nullptr,*tree_,UString("ShaderNodeOutputAOV"));require(aov,"PD149 AOV allocation failed");STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name,"BTS_Channel");bke::node_add_link(*tree_,*from,output(*from,selected.socket.c_str()),*aov,input(*aov,selected.scalar?"Value":"Color"));BKE_ntree_update_without_main(*tree_);
  const auto bytes=resource_product(resource_product(width,height),16);
  program_=std::make_unique<NativeMaterialGpuProgram>(*tree_,0,selected.scalar?1:0);target_.reset(GPU_texture_create_2d("PD149 actual material output",width,height,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr));require(bool(target_),"PD149 diagnostic output allocation failed");
  require(live_signature(material)==original_&&handles()==handles_,"PD149 tap construction mutated the artist source or resident images");
 }
 ~GpuTap(){if(!tree_&&!program_&&!target_)return;if(owner_!=std::this_thread::get_id()||!context_||GPU_context_active_get()!=context_)qFatal("PD149 diagnostic GPU lifetime escaped owner cleanup");target_.reset();program_.reset();tree_.reset();}
 std::vector<std::array<float,4>> sample(int tile){owner();require(live_signature(*source_)==original_&&handles()==handles_,"PD149 tap source changed; discard before discrete material edits");NativeMaterialGpuContext context;context.tile=tile;context.color_to_aces={1,0,0,0,1,0,0,0,1};program_->dispatch(target_.get(),{},context,{0,0,width_,height_});GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);const auto readback_bytes=resource_product(resource_product(width_,height_),16);auto *raw=static_cast<float *>(GPU_texture_read(target_.get(),GPU_DATA_FLOAT,0));require(raw,"PD149 diagnostic float readback missing");const auto release=qScopeGuard([&]{MEM_delete(raw);});std::vector<std::array<float,4>> result(std::size_t(width_)*height_);static_assert(sizeof(result[0])==4*sizeof(float));std::memcpy(result.data(),raw,result.size()*sizeof(result[0]));require(live_signature(*source_)==original_&&handles()==handles_,"PD149 diagnostic read changed artist source or GPU handles");return result;}
 void verify_missing_undecoded_rejected(){
  owner();require(!images_.empty(),"PD149 missing-source negative needs an actual image binding");
  std::unique_ptr<bNodeTree,TreeDeleter> copy(reinterpret_cast<bNodeTree *>(BKE_id_copy_in_lib(nullptr,std::optional<Library *>{nullptr},&tree_->id,std::optional<const ID *>{nullptr},nullptr,LIB_ID_COPY_LOCALIZE|LIB_ID_COPY_NO_ANIMDATA)));
  auto *missing=reinterpret_cast<Image *>(BKE_id_new_nomain(ID_IM,"PD149 missing undecoded file"));require(missing,"PD149 missing Image allocation failed");
  const auto release=qScopeGuard([&]{copy.reset();BKE_id_free_ex(nullptr,&missing->id,LIB_ID_FREE_NO_MAIN|LIB_ID_FREE_NO_USER_REFCOUNT|LIB_ID_FREE_NO_DEG_TAG,false);});
  missing->source=IMA_SRC_FILE;missing->type=IMA_TYPE_IMAGE;STRNCPY(missing->filepath,"/PD149-never-load-missing-source.exr");
  for(auto &node:copy->nodes)if(STREQ(node.idname,"ShaderNodeTexImage"))node.id=&missing->id;
  BKE_ntree_update_without_main(*copy);NativeMaterialGpuProgram negative(*copy,0,0);NativeMaterialGpuContext context;context.tile=1001;
  bool rejected=false;try{negative.dispatch(target_.get(),{},context,{0,0,width_,height_});}catch(const std::runtime_error &error){rejected=std::string_view(error.what()).find("not decoded")!=std::string_view::npos;}
  require(rejected&&!BKE_image_has_loaded_ibuf(missing)&&!missing->runtime->gputexture[TEXTARGET_2D][0],"PD149 missing undecoded source reached loading/dispatch");
  // A previously cached Blender UNORM error texture is not ownership evidence.
  auto *error_texture=GPU_texture_create_error(2,false);require(error_texture,"PD149 private error-texture allocation failed");missing->runtime->gputexture[TEXTARGET_2D][0]=error_texture;
  rejected=false;try{negative.dispatch(target_.get(),{},context,{0,0,width_,height_});}catch(const std::runtime_error &error){rejected=std::string_view(error.what()).find("not decoded")!=std::string_view::npos;}
  require(rejected&&!BKE_image_has_loaded_ibuf(missing),"PD149 unmarked cached file/error storage was admitted");
  missing->source=IMA_SRC_GENERATED;missing->type=IMA_TYPE_UV_TEST;missing->runtime->gpuflag|=native_generated_gpu_bit;missing->flag|=IMA_HIGH_BITDEPTH;for(auto &tile:missing->tiles){tile.gen_x=2;tile.gen_y=2;tile.gen_flag|=IMA_GEN_FLOAT;}
  rejected=false;try{negative.dispatch(target_.get(),{},context,{0,0,width_,height_});}catch(const std::runtime_error &error){rejected=std::string_view(error.what()).find("precision mismatch")!=std::string_view::npos;}
  require(rejected,"PD149 marked native source admitted UNORM error format");
  missing->runtime->gputexture[TEXTARGET_2D][0]=nullptr;GPU_texture_free(error_texture);
  const std::array<float,16> constant{.25f,.25f,.25f,1,.25f,.25f,.25f,1,.25f,.25f,.25f,1,.25f,.25f,.25f,1};
  auto *resident=GPU_texture_create_2d("PD149 private resident source",2,2,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,constant.data());require(resident,"PD149 private resident source allocation failed");missing->runtime->gputexture[TEXTARGET_2D][0]=resident;
  context.tile=1002;negative.dispatch(target_.get(),{},context,{0,0,width_,height_});
  require(!BKE_image_has_loaded_ibuf(missing)&&missing->runtime->gputexture[TEXTARGET_2D][0]==resident,"PD149 resident source path decoded or replaced its GPU allocation");
  std::puts("PD149_GPU_SOURCE_ADMISSION PASS missing-undecoded-and-cached-UNORM-rejected native-resident-no-CPU-cache-2D-context1002");
  require(live_signature(*source_)==original_&&handles()==handles_,"PD149 missing-source test mutated live image/source state");
 }
 float stored_scalar(float value)const{
  owner();require(images_.size()==1,"PD149 scalar storage expectation needs one actual Image");const auto cached=BKE_image_gpu_override_texture(images_.front());require(cached.texture&&*cached.texture,"PD149 scalar storage allocation missing");const auto format=GPU_texture_format(*cached.texture);if(format==gpu::TextureFormat::SFLOAT_16_16_16_16)return float(qfloat16(value));if(format==gpu::TextureFormat::SFLOAT_32_32_32_32)return value;throw std::runtime_error("PD149 scalar storage expectation has unsupported actual format");
 }
 QJsonArray image_handles()const{return handles_;}
};
}
