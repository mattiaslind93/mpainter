#include "ResourceAvailability.hh"
#include "UvPreview.hh"
#include "Performance.hh"
#include "SceneSnapshot.hh"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include "BKE_attribute.hh"
#include "BKE_collection.hh"
#include "BKE_colortools.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_object.hh"
#include "BKE_scene.hh"
#include "BKE_screen.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "BLI_math_vector.hh"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "ED_view3d_offscreen.hh"
#include "GPU_context.hh"
#include "GPU_framebuffer.hh"
namespace bts {
namespace {
using namespace blender;
struct PreviewScene {
  Main *main=BKE_main_new();
  Scene *scene=nullptr;
  Depsgraph *depsgraph=nullptr;
  ~PreviewScene() { if (depsgraph) DEG_graph_free(depsgraph); BKE_main_free(main); }
};
void require_gpu_context() {
  if (!GPU_context_active_get()) throw std::logic_error("UV preview requires the caller's active Blender GPU context");
}
bNodeSocket &input(bNode &node,const char *name) {
  auto *socket=bke::node_find_enabled_input_socket(node,name);
  if (!socket) throw std::runtime_error("Flat UV shader input is missing");
  return *socket;
}
bNodeSocket &output(bNode &node,const char *name) {
  for (auto &socket:node.outputs) if (STREQ(socket.identifier,name)) return socket;
  throw std::runtime_error("Flat UV shader output is missing");
}
bool layering_node(const bNode &node){return STRPREFIX(node.name,"__BTS_Layer");}
bool scalar_trace_enabled(){const char *value=std::getenv("BTS_PD149_UV_SCALAR_TRACE");return value&&STREQ(value,"1");}
void trace_emission(const char *phase,Material &material){
 if(!scalar_trace_enabled())return;
 auto *tree=material.nodetree;auto *emission=tree?bke::node_find_node_by_name(*tree,"__BTS_UvFlatEmission"):nullptr;
 auto *color=emission?bke::node_find_enabled_input_socket(*emission,"Color"):nullptr;int count=0;
 if(tree&&color)for(const auto &link:tree->links)if(link.tosock==color){++count;std::printf("BTS_PD149_UV_EMISSION phase=%s material=%u tree=%p from=%s socket=%s valid=%d muted=%d\n",phase,material.id.session_uid,static_cast<void *>(tree),link.fromnode?link.fromnode->name:"none",link.fromsock?link.fromsock->identifier:"none",bool(link.flag&NODE_LINK_VALID),bool(link.flag&NODE_LINK_MUTED));}
 std::printf("BTS_PD149_UV_EMISSION phase=%s material=%u tree=%p emission=%d links=%d\n",phase,material.id.session_uid,static_cast<void *>(tree),bool(emission),count);std::fflush(stdout);
}
bNodeSocket *socket_by_identifier(auto &sockets,const char *id){for(auto &socket:sockets)if(STREQ(socket.identifier,id))return &socket;return nullptr;}
bool sync_layering_shader(const bNodeTree &source,bNodeTree &target,bool &changed){
 for(const bNode &node:source.nodes)if(layering_node(node)){auto *copy=bke::node_find_node_by_name(target,node.name);if(!copy||!STREQ(node.idname,copy->idname))return false;}
 for(const bNode &node:source.nodes)if(layering_node(node)){auto &copy=*bke::node_find_node_by_name(target,node.name);if(copy.custom1!=node.custom1||copy.custom2!=node.custom2){changed=true;copy.custom1=node.custom1;copy.custom2=node.custom2;BKE_ntree_update_tag_node_property(&target,&copy);}if(STREQ(node.idname,"ShaderNodeTexImage")&&copy.id!=node.id){changed=true;copy.id=node.id;BKE_ntree_update_tag_node_property(&target,&copy);}
  const auto defaults=[&](const auto &from,auto &to){for(const auto &s:from){auto *d=socket_by_identifier(to,s.identifier);if(!d||s.type!=d->type||!s.default_value||!d->default_value)continue;const float *src=nullptr;float *dst=nullptr;std::size_t count=0;
   if(s.type==SOCK_FLOAT){src=&static_cast<const bNodeSocketValueFloat *>(s.default_value)->value;dst=&static_cast<bNodeSocketValueFloat *>(d->default_value)->value;count=1;}
   if(s.type==SOCK_VECTOR){src=static_cast<const bNodeSocketValueVector *>(s.default_value)->value;dst=static_cast<bNodeSocketValueVector *>(d->default_value)->value;count=3;}
   if(s.type==SOCK_RGBA){src=static_cast<const bNodeSocketValueRGBA *>(s.default_value)->value;dst=static_cast<bNodeSocketValueRGBA *>(d->default_value)->value;count=4;}
   if(count&&std::memcmp(src,dst,count*sizeof(float))){changed=true;std::copy_n(src,count,dst);BKE_ntree_update_tag_socket_property(&target,d);}}};defaults(node.inputs,copy.inputs);defaults(node.outputs,copy.outputs);
  for(const auto &s:node.inputs){auto *destination=socket_by_identifier(copy.inputs,s.identifier);if(!destination)return false;const bNodeLink *wanted=nullptr;for(const auto &link:source.links)if(link.tosock==&s){wanted=&link;break;}bNode *from=wanted?bke::node_find_node_by_name(target,wanted->fromnode->name):nullptr;bNodeSocket *output=from?socket_by_identifier(from->outputs,wanted->fromsock->identifier):nullptr;if(wanted&&(!from||!output))return false;bool matched=false,linked=false;for(const auto &link:target.links)if(link.tosock==destination){linked=true;matched=link.fromsock==output;}if(matched||(!wanted&&!linked))continue;changed=true;bke::node_remove_socket_links(target,*destination);if(wanted)bke::node_add_link(target,*from,*output,copy,*destination);}
 }
 return true;
}
Material *flat_material(PreviewScene &preview,Material &source,bool inspection) {
  // Private shader copy: never switch or relink the live 3D material. Images remain shared IDs.
  auto *material=reinterpret_cast<Material *>(BKE_id_copy_ex(preview.main,&source.id,nullptr,LIB_ID_CREATE_NO_USER_REFCOUNT));
  if (!material || !material->nodetree) throw std::runtime_error("Cannot copy UV material");
  auto *principled=native_principled_surface(*material);
  auto *surface=native_material_output(*material);
  if (!principled || !surface) throw std::runtime_error("Flat UV material requires a supported Principled surface");
  auto &tree=*material->nodetree;
  auto *emission=bke::node_add_node(nullptr,tree,UString("ShaderNodeEmission"));
  STRNCPY(emission->name,"__BTS_UvFlatEmission");
  auto &color=input(*emission,"Color");
  if (inspection) {
    auto *channel=bke::node_find_node_by_name(tree,"__BTS_InspectCoverage");
    if (!channel) throw std::runtime_error("UV inspection channel is missing");
    bke::node_add_link(tree,*channel,output(*channel,"Color"),*emission,color);
  }
  else {
    auto &base=input(*principled,"Base Color");
    bool linked=false;
    if(auto *enabled=bke::node_find_node_by_name(tree,"__BTS_LayeringEnabled");enabled&&static_cast<bNodeSocketValueFloat *>(output(*enabled,"Value").default_value)->value>.5f){if(auto *layered=bke::node_find_node_by_name(tree,"__BTS_LayeredUvColor")){bke::node_add_link(tree,*layered,output(*layered,"Color"),*emission,color);linked=true;}}
    auto *own=bke::node_find_node_by_name(tree,"__BTS_AOMaterial");
    // The unlit texture view retains authored AO at its original unit factor;
    // shader-instance lighting intensity belongs exclusively to the 3D view.
    if(own)static_cast<bNodeSocketValueFloat *>(input(*own,"Factor").default_value)->value=1;
    if(!own)own=bke::node_find_node_by_name(tree,"__BTS_BaseMix0");
    if(own&&!linked){bke::node_add_link(tree,*own,output(*own,"Color"),*emission,color);linked=true;}
    for (bNodeLink &link:tree.links) {
      if (!linked && link.tosock==&base && (link.flag & NODE_LINK_VALID) && !(link.flag & NODE_LINK_MUTED)) {
        bke::node_add_link(tree,*link.fromnode,*link.fromsock,*emission,color); linked=true; break;
      }
    }
    if (!linked) std::copy_n(static_cast<bNodeSocketValueRGBA *>(base.default_value)->value,4,
                            static_cast<bNodeSocketValueRGBA *>(color.default_value)->value);
  }
  // UV presentation must never inherit vertex displacement from the 3D material.
  bke::node_remove_socket_links(tree,input(*surface,"Displacement"));material->displacement_method=MA_DISPLACEMENT_BUMP;material->inflate_bounds=0;
  auto &destination=input(*surface,"Surface");
  bke::node_remove_socket_links(tree,destination);
  if(auto *geometry=bke::node_find_node_by_name(tree,"__BTS_GeometrySwitch")){// Mix Shader has two identically named inputs; use its original branch index.
    bNodeSocket *branch=nullptr;int index=0;for(auto &socket:geometry->inputs)if(index++==1)branch=&socket;if(!branch)throw std::runtime_error("UV visibility branch missing");bke::node_remove_socket_links(tree,*branch);bke::node_add_link(tree,*emission,output(*emission,"Emission"),*geometry,*branch);bke::node_add_link(tree,*geometry,output(*geometry,"Shader"),*surface,destination);
  }else bke::node_add_link(tree,*emission,output(*emission,"Emission"),*surface,destination);
  BKE_ntree_update_after_single_tree_change(*preview.main,tree);
  return material;
}
void add_tiles(PreviewScene &preview,const std::set<std::pair<int,int>> &tiles,Material *material) {
  auto *object=BKE_object_add_only_object(preview.main,OB_MESH,"Texture tiles");
  auto *mesh=BKE_mesh_new_nomain(int(tiles.size())*4,0,int(tiles.size()),int(tiles.size())*4);
  auto positions=mesh->vert_positions_for_write();
  auto corners=mesh->corner_verts_for_write();
  auto offsets=mesh->face_offsets_for_write();
  auto uv=mesh->attributes_for_write().lookup_or_add_for_write_span<float2>("UVMap",bke::AttrDomain::Corner);
  int face=0;
  for (auto [u,v]:tiles) {
    offsets[face]=face*4;
    const float2 points[4]={{float(u),float(v)},{float(u+1),float(v)},{float(u+1),float(v+1)},{float(u),float(v+1)}};
    for (int c=0;c<4;++c) { const int i=face*4+c; positions[i]={points[c].x,points[c].y,0};corners[i]=i;uv.span[i]=points[c]; }
    ++face;
  }
  offsets[face]=face*4;uv.finish();mesh->uv_maps_active_set("UVMap");mesh->uv_maps_default_set("UVMap");
  bke::mesh_calc_edges(*mesh,false,false);
  auto *owned=BKE_id_new<Mesh>(preview.main,"Texture tile geometry");object->data=&owned->id;
  BKE_mesh_nomain_to_mesh(mesh,owned,object,false);
  BKE_id_material_eval_assign(&owned->id,1,material);
  BKE_collection_object_add(preview.main,preview.scene->master_collection,object);
}
}
struct UvPreview::Impl {
  const std::thread::id owner=std::this_thread::get_id();
  std::unique_ptr<PreviewScene> preview;
  std::vector<std::unique_ptr<PreviewScene>> retired;
  Material *source=nullptr;
  bool opacity_data=false,inspection=false,shader_rebuild=false;
  std::uint32_t view_uid=0;bool view_enabled=false,view_pending=false;int view_mask=-1;
  bool trace_draw_pending=false;
  void trace_view(const char *phase)const{
   if(!scalar_trace_enabled())return;
   auto *tree=source?source->nodetree:nullptr;auto *marker=tree?bke::node_find_node_by_name(*tree,"__BTS_LayeringEnabled"):nullptr;
   auto *value=marker?bke::node_find_socket(*marker,SOCK_OUT,UString("Value")):nullptr;
   const float enabled=value&&value->type==SOCK_FLOAT&&value->default_value?static_cast<bNodeSocketValueFloat *>(value->default_value)->value:-1;
   std::printf("BTS_PD149_UV_VIEW phase=%s selected_uid=%u source_uid=%u enabled=%d mask=%d pending=%d inspection=%d marker=%g opacity_data=%d rebuild=%d preview=%d\n",phase,view_uid,source?source->id.session_uid:0,view_enabled,view_mask,view_pending,inspection,enabled,opacity_data,shader_rebuild,bool(preview));std::fflush(stdout);
  }
  void sync_layering_view();
  void require_owner() const { if (owner!=std::this_thread::get_id()) throw std::logic_error("UV preview accessed outside owner thread"); }
};
UvPreview::UvPreview():impl_(std::make_unique<Impl>()) {}
UvPreview::~UvPreview() { BLI_assert(impl_->owner==std::this_thread::get_id());BLI_assert(!impl_->preview || GPU_context_active_get()); }
void UvPreview::clear() { impl_->require_owner();impl_->trace_view("clear");impl_->source=nullptr;impl_->shader_rebuild=false;impl_->view_uid=0;impl_->view_enabled=false;impl_->view_mask=-1;impl_->view_pending=false;impl_->opacity_data=false;if (impl_->preview) {require_gpu_context();impl_->preview.reset();} impl_->retired.clear(); }
void UvPreview::retire(){impl_->require_owner();impl_->trace_view("retire");impl_->source=nullptr;impl_->shader_rebuild=false;impl_->view_pending=true;if(impl_->preview){require_gpu_context();impl_->retired.push_back(std::move(impl_->preview));}}
bool UvPreview::shader_rebuild_needed()const{return impl_->shader_rebuild;}
void UvPreview::set_material_layering_view(std::uint32_t uid,bool enabled,int mask){
 impl_->require_owner();if(mask< -1||mask>2||(!enabled&&mask!=-1)||(!uid&&enabled))throw std::invalid_argument("Invalid material opacity view selection");
 if(impl_->view_uid==uid&&impl_->view_enabled==enabled&&impl_->view_mask==mask)return;
 impl_->view_uid=uid;impl_->view_enabled=enabled;impl_->view_mask=mask;impl_->view_pending=true;impl_->trace_draw_pending=scalar_trace_enabled();impl_->trace_view("select");
}
void UvPreview::Impl::sync_layering_view(){
 if(view_pending)trace_view("sync-admission");
 if(!view_pending||!preview||!source||!source->nodetree)return;
 if(inspection){opacity_data=false;view_pending=false;return;}
 if(view_uid&&view_uid!=source->id.session_uid)return;
 auto &live=*source->nodetree;
 auto *marker=bke::node_find_node_by_name(live,"__BTS_LayeringEnabled");
 if(!marker){opacity_data=false;view_pending=false;return;} // Ordinary non-layered UV stays unchanged.
 const bool layered=STREQ(marker->idname,"ShaderNodeValue")&&output(*marker,"Value").type==SOCK_FLOAT&&static_cast<bNodeSocketValueFloat *>(output(*marker,"Value").default_value)->value>.5f;
 const bool opacity=layered&&view_uid==source->id.session_uid&&view_enabled&&view_mask>=0;
 const auto name=opacity?"__BTS_LayerMask"+std::to_string(view_mask):layered?std::string("__BTS_LayeredUvColor"):bke::node_find_node_by_name(live,"__BTS_AOMaterial")?std::string("__BTS_AOMaterial"):std::string("__BTS_BaseMix0");
 const char *socket=opacity?"Result":"Color";
 auto *original=bke::node_find_node_by_name(live,name.c_str());if(!original)throw std::runtime_error("Selected UV material source is missing");
 if(opacity&&!STREQ(original->idname,"ShaderNodeClamp"))throw std::runtime_error("Selected opacity view has an invalid clamp node");
 const bNodeLink *image_link=nullptr;
 if(opacity){auto &value=input(*original,"Value");for(const auto &link:live.links)if(link.tosock==&value){if(image_link)throw std::runtime_error("Opacity view has ambiguous image links");image_link=&link;}
  if(!image_link||!(image_link->flag&NODE_LINK_VALID)||(image_link->flag&NODE_LINK_MUTED)||!STREQ(image_link->fromnode->idname,"ShaderNodeTexImage")||!image_link->fromnode->id||GS(image_link->fromnode->id->name)!=ID_IM)throw std::runtime_error("Opacity view requires its live scalar image");}
 for(Material &material:preview->main->materials){
  if(!material.nodetree)continue;auto &tree=*material.nodetree;auto *from=bke::node_find_node_by_name(tree,name.c_str());auto *emission=bke::node_find_node_by_name(tree,"__BTS_UvFlatEmission");
  if(!emission)continue;
  if(!from||!STREQ(from->idname,original->idname)||!STREQ(emission->idname,"ShaderNodeEmission")){shader_rebuild=true;return;}
  if(image_link){auto *image=bke::node_find_node_by_name(tree,image_link->fromnode->name);if(!image||!STREQ(image->idname,"ShaderNodeTexImage")||image->id!=image_link->fromnode->id)throw std::runtime_error("UV opacity view lost its live image identity");}
  trace_emission("sync-before",material);
  auto &to=input(*emission,"Color");auto &source_socket=output(*from,socket);bool same=false;int count=0;for(const auto &link:tree.links)if(link.tosock==&to){++count;same=link.fromnode==from&&link.fromsock==&source_socket;}
  if(count!=1||!same){bke::node_remove_socket_links(tree,to);bke::node_add_link(tree,*from,source_socket,*emission,to);BKE_ntree_update_after_single_tree_change(*preview->main,tree);DEG_id_tag_update_ex(preview->main,&material.id,ID_RECALC_SHADING);DEG_id_tag_update_ex(preview->main,&tree.id,ID_RECALC_NTREE_OUTPUT);}
  trace_emission("sync-after",material);
 }
 opacity_data=opacity;view_pending=false;trace_draw_pending=scalar_trace_enabled();trace_view("sync-complete");
}
void UvPreview::tag_images_updated() {
  impl_->require_owner();if (!impl_->preview) return;
  performance::Scope timing("uv_preview_image_notify");int rebound_materials=0;
  auto &preview=*impl_->preview;
  for (Material &material:preview.main->materials) {
    if (!material.nodetree) continue;
    bool rebound=false;
    if(impl_->source&&impl_->source->nodetree)for(bNode &node:material.nodetree->nodes){
      if(!node.id||GS(node.id->name)!=ID_IM)continue;
      auto *source=bke::node_find_node_by_name(*impl_->source->nodetree,node.name);
      // This private shader borrows image IDs (LIB_ID_CREATE_NO_USER_REFCOUNT).
      if(source&&source->id&&GS(source->id->name)==ID_IM&&node.id!=source->id){node.id=source->id;BKE_ntree_update_tag_node_property(material.nodetree,&node);rebound=true;}
    }
    if(impl_->source&&impl_->source->nodetree&&bke::node_find_node_by_name(*impl_->source->nodetree,"__BTS_LayeringEnabled")){auto &source=*impl_->source->nodetree;auto &target=*material.nodetree;if(!sync_layering_shader(source,target,rebound))impl_->shader_rebuild=true;else{
      impl_->view_pending=true;impl_->trace_draw_pending=scalar_trace_enabled();impl_->trace_view("source-sync"); // Reapply private emission choice after copied source synchronization.
    }}
    // GPU pixel updates retain the same image IDs and shader graph. Tagging
    // shading here discards the UV material's evaluated draw state per dab.
    // Rebinding an image or changing a node constant still invalidates it.
    if(rebound){++rebound_materials;DEG_id_tag_update_ex(preview.main,&material.id,ID_RECALC_SHADING);BKE_ntree_update_after_single_tree_change(*preview.main,*material.nodetree);}
  }
  if(performance::enabled())std::printf("BTS_UV_IMAGE_REFRESH material_rebinds=%d image_shading_tags=0\n",rebound_materials);
}
UvPreviewSummary UvPreview::rebuild(Main &main,Scene &scene,Depsgraph &depsgraph,const View3D &source_view,
                                    std::uint32_t uid,bool inspection,const SourceMeshMap *source_meshes) {
  retire();require_gpu_context();
  const ResourceAvailability memory;
  if (DEG_get_bmain(&depsgraph)!=&main || DEG_get_input_scene(&depsgraph)!=&scene || DEG_get_mode(&depsgraph)!=DAG_EVAL_VIEWPORT)
    throw std::invalid_argument("UV preview requires source viewport depsgraph");
  BKE_scene_graph_evaluated_ensure(&depsgraph,&main);
  auto *source_layer=DEG_get_input_view_layer(&depsgraph);BKE_view_layer_synced_ensure(main,&scene,source_layer);
  Material *selected=nullptr;
  for (Material &material:main.materials) if (material.id.session_uid==uid) selected=&material;
  UvPreviewSummary summary;
  if (!selected) return summary;
  impl_->source=selected;impl_->inspection=inspection;
  impl_->opacity_data=false;impl_->view_pending=true;impl_->trace_draw_pending=scalar_trace_enabled();impl_->trace_view("rebuild");
  std::set<std::pair<int,int>> tiles;
  auto contours=std::make_shared<std::vector<std::array<float,4>>>();
  auto wireframe=std::make_shared<std::vector<std::array<float,4>>>(),triangulated=std::make_shared<std::vector<std::array<float,4>>>();
  bool has_bounds=false;
  for (const Base &base:*BKE_view_layer_object_bases_get(source_layer)) {
    if (base.object->type!=OB_MESH || !BKE_base_is_visible(&source_view,&base)) continue;
    const Object *object=DEG_get_evaluated(&depsgraph,base.object);
    if (!(BKE_object_visibility(object,DAG_EVAL_VIEWPORT)&OB_VISIBLE_SELF)) continue;
    const Mesh *mesh=BKE_object_get_evaluated_mesh(object);if(source_meshes){const auto found=source_meshes->find(DEG_get_original(object)->id.session_uid);if(found!=source_meshes->end())mesh=found->second;}if (!mesh) continue;
    const auto attributes=mesh->attributes();const auto name=mesh->active_or_default_uv_map_name();
    const auto materials=*attributes.lookup_or_default<int>("material_index",bke::AttrDomain::Face,0);
    const int slots=BKE_object_material_count_eval(*object,mesh->id);
    std::vector<bool> selected_slots(std::max(1,slots),false);
    for (int slot=0;slot<slots;++slot) {
      auto *material=BKE_object_material_get_eval(*object,mesh->id,short(slot+1));
      selected_slots[slot]=material && DEG_get_original(material)==selected;
    }
    bool used=false;
    for (int face:mesh->faces().index_range()) used|=selected_slots[std::clamp(materials[face],0,int(selected_slots.size())-1)];
    if (!used) continue;
    if (name.is_empty()) {++summary.meshes_without_uvs;continue;}
    const VArraySpan<float2> uv=*attributes.lookup<float2>(name,bke::AttrDomain::Corner);
    if (uv.size()!=mesh->corners_num) {++summary.meshes_without_uvs;continue;}
    std::map<std::array<float,4>,int> edges;
    const auto faces=mesh->faces();const auto triangles=mesh->corner_tris();const auto triangle_faces=mesh->corner_tri_faces();
    std::size_t count=0;
    for (int i:triangles.index_range()) {
      if (!selected_slots[std::clamp(materials[triangle_faces[i]],0,int(selected_slots.size())-1)]) continue;
      const auto corners=triangles[i];bool valid=true;
      for (int k=0;k<3;++k) {const int c=corners[k];valid&=std::isfinite(uv[c].x)&&std::isfinite(uv[c].y)&&double(uv[c].x)>std::numeric_limits<int>::min()&&double(uv[c].x)<std::numeric_limits<int>::max()-1.&&double(uv[c].y)>std::numeric_limits<int>::min()&&double(uv[c].y)<std::numeric_limits<int>::max()-1.;}
      if (!valid) {++summary.skipped_nonfinite_triangles;continue;}
      const auto a=uv[corners[0]],b=uv[corners[1]],c=uv[corners[2]];
      if (std::abs(double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x))<1e-15) {++summary.skipped_degenerate_triangles;continue;}
      ++summary.uv_triangles;

      ++count;
      const float2 lo=math::min(a,math::min(b,c)),hi=math::max(a,math::max(b,c));
      for (int axis=0;axis<2;++axis) {
        summary.minimum_uv[axis]=has_bounds ? std::min(summary.minimum_uv[axis],lo[axis]):lo[axis];
        summary.maximum_uv[axis]=has_bounds ? std::max(summary.maximum_uv[axis],hi[axis]):hi[axis];
      }
      has_bounds=true;
      const int u0=int(std::floor(lo.x)),v0=int(std::floor(lo.y));
      const int u1=std::max(u0,int(std::ceil(hi.x))-1),v1=std::max(v0,int(std::ceil(hi.y))-1);

      for (int v=v0;v<=v1;++v) for (int u=u0;u<=u1;++u) tiles.emplace(u,v);

      const auto face=faces[triangle_faces[i]];
      for (int e=0;e<3;++e) {
        const int ia=corners[e],ib=corners[(e+1)%3];
        triangulated->push_back({uv[ia].x,uv[ia].y,uv[ib].x,uv[ib].y});
        if (std::abs(ia-ib)!=1 && !(std::min(ia,ib)==face.start()&&std::max(ia,ib)==face.last())) continue;
        std::array<float,2> x{uv[ia].x,uv[ia].y},y{uv[ib].x,uv[ib].y};if (y<x) std::swap(x,y);
        ++edges[{x[0],x[1],y[0],y[1]}];
      }
    }
    if(count)++summary.mesh_objects;
    for (const auto &[edge,count]:edges){wireframe->push_back(edge);if(count==1)contours->push_back(edge);}
  }
  if (!summary) return summary;
  summary.contours=contours;summary.wireframe=wireframe;summary.triangulated_wireframe=triangulated;
  auto preview=std::make_unique<PreviewScene>();preview->main->colorspace=main.colorspace;
  preview->scene=BKE_scene_add(preview->main,"Unlit texture view");auto &destination=*preview->scene;
  STRNCPY(destination.r.engine,"BLENDER_EEVEE");BKE_scene_copy_data_eevee(&destination,&scene);
  // This emission-only canvas has no lighting. Shadow statistics readback can
  // otherwise stall behind the full-resolution texture dispatch on NVIDIA.
  destination.eevee.flag &= ~(SCE_EEVEE_SHADOW_ENABLED|SCE_EEVEE_VOLUMETRIC_SHADOWS);
  BKE_color_managed_display_settings_copy(&destination.display_settings,&scene.display_settings);
  BKE_color_managed_view_settings_free(&destination.view_settings);
  BKE_color_managed_view_settings_copy(&destination.view_settings,&scene.view_settings);
  add_tiles(*preview,tiles,flat_material(*preview,*selected,inspection));
  auto *layer=static_cast<ViewLayer *>(destination.view_layers.first);
  BKE_view_layer_synced_ensure(*preview->main,&destination,layer);
  preview->depsgraph=DEG_graph_new(preview->main,&destination,layer,DAG_EVAL_VIEWPORT);
  DEG_graph_build_from_view_layer(preview->depsgraph);DEG_evaluate_on_refresh(preview->depsgraph);
  impl_->preview=std::move(preview);return summary;
}
bool UvPreview::draw(int width,int height,const float view[4][4],const float projection[4][4],
                     GPUOffScreen *offscreen,GPUViewport *viewport,const View3D &source_view,bool color_management) {
  impl_->require_owner();if (!impl_->preview) return false;require_gpu_context();
  if (width<1 || height<1 || !offscreen || !viewport || !view || !projection ||
      GPU_offscreen_width(offscreen)!=width || GPU_offscreen_height(offscreen)!=height)
    throw std::invalid_argument("UV preview target mismatch");
  for (int r=0;r<4;++r) for (int c=0;c<4;++c)
    if (!std::isfinite(view[r][c]) || !std::isfinite(projection[r][c])) throw std::invalid_argument("UV preview matrices must be finite");
  impl_->sync_layering_view();
  if(impl_->opacity_data)color_management=false;
  auto &preview=*impl_->preview;
  BKE_scene_graph_evaluated_ensure(preview.depsgraph,preview.main);
  if(impl_->trace_draw_pending){std::printf("BTS_PD149_UV_DRAW color_management=%d\n",color_management);impl_->trace_view("draw-evaluated");for(Material &material:preview.main->materials){trace_emission("draw-original",material);if(auto *evaluated=DEG_get_evaluated(preview.depsgraph,&material))trace_emission("draw-evaluated",*evaluated);}}
  auto shading=source_view.shading;
  shading.studiolight_background=0; // The UV canvas never displays the 3D environment.
  shading.flag|=V3D_SHADING_SCENE_LIGHTS|V3D_SHADING_SCENE_WORLD;
  shading.type=OB_MATERIAL;shading.use_compositor=V3D_SHADING_USE_COMPOSITOR_DISABLED;
  // Mirror Blender's simple offscreen adapter, but expose the native painting
  // hint: reset temporal accumulation without invalidating material shaders.
  // A flat texture canvas has no stochastic lighting to accumulate.
  View3D canvas{};ARegion region{};bke::ARegionRuntime runtime{};RegionView3D region_view{};
  canvas.regionbase.first=canvas.regionbase.last=&region;
  canvas.shading=shading;canvas.flag2=V3D_HIDE_OVERLAYS;
  canvas.clip_start=source_view.clip_start;canvas.clip_end=source_view.clip_end;
  region.runtime=&runtime;region.regiondata=&region_view;region.regiontype=RGN_TYPE_WINDOW;
  region_view.persp=RV3D_PERSP;region_view.rflag|=RV3D_PAINTING;
  ED_view3d_draw_offscreen(preview.depsgraph,preview.scene,OB_MATERIAL,&canvas,&region,nullptr,
    width,height,view,projection,false,true,"",color_management,true,offscreen,viewport);
  // The new material now owns its cached pass; releasing the old scene cannot
  // evict that shader between otherwise identical texture-set selections.
  impl_->retired.clear();
  impl_->trace_draw_pending=false;
  return true;
}
}
