#include "ResourceAvailability.hh"
#include "DisplacementPreview.hh"
#include "SubdivisionPolicy.hh"
#include "TextureDocument.hh"
#include "SceneSnapshot.hh"
#include <QJsonArray>
#include <bit>
#include <cstring>
#include <cstdio>
#include "BKE_attribute.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_object.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_node_tree_interface_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "GPU_material.hh"

namespace bts {
using namespace blender;
namespace {
constexpr const char *smooth_name="__BTS_PreviewSmooth",*subdivision_name="__BTS_PreviewSubdivision";
bool owned(const ModifierData &modifier){return (modifier.type==eModifierType_Nodes&&STREQ(modifier.name,smooth_name))||(modifier.type==eModifierType_Subsurf&&STREQ(modifier.name,subdivision_name));}
bNodeSocket &geometry(bNode &node,bool output){auto &sockets=output?node.outputs:node.inputs;for(bNodeSocket &socket:sockets)if(socket.type==SOCK_GEOMETRY)return socket;throw std::runtime_error("Preview node geometry socket is unavailable");}
bNodeTree *smooth_tree(Main &main){
  auto *tree=bke::node_tree_add_tree(&main,"__BTS_PreviewSmooth","GeometryNodeTree");
  tree->tree_interface.add_socket("Geometry","Source geometry","NodeSocketGeometry",NODE_INTERFACE_SOCKET_INPUT,nullptr);
  tree->tree_interface.add_socket("Geometry","Smooth preview","NodeSocketGeometry",NODE_INTERFACE_SOCKET_OUTPUT,nullptr);
  BKE_ntree_update_tag_all(tree);BKE_ntree_update_after_single_tree_change(main,*tree);
  auto *input=bke::node_add_node(nullptr,*tree,UString("NodeGroupInput"));auto *face=bke::node_add_node(nullptr,*tree,UString("GeometryNodeSetShadeSmooth"));auto *edge=bke::node_add_node(nullptr,*tree,UString("GeometryNodeSetShadeSmooth"));auto *output=bke::node_add_node(nullptr,*tree,UString("NodeGroupOutput"));
  if(!input||!face||!edge||!output)throw std::runtime_error("Cannot create smooth displacement preview nodes");
  face->custom1=int16_t(bke::AttrDomain::Face);edge->custom1=int16_t(bke::AttrDomain::Edge);output->flag|=NODE_DO_OUTPUT;
  BKE_ntree_update_tag_node_property(tree,face);BKE_ntree_update_tag_node_property(tree,edge);BKE_ntree_update_after_single_tree_change(main,*tree);
  bke::node_add_link(*tree,*input,geometry(*input,true),*face,geometry(*face,false));bke::node_add_link(*tree,*face,geometry(*face,true),*edge,geometry(*edge,false));bke::node_add_link(*tree,*edge,geometry(*edge,true),*output,geometry(*output,false));
  BKE_ntree_update_after_single_tree_change(main,*tree);id_us_min(&tree->id);return tree;
}
std::uint64_t triangles(const Mesh &mesh,int level){if(level==0)return mesh.corner_tris().size();std::uint64_t count=2*std::uint64_t(mesh.corners_num);for(int i=1;i<level;++i)count=resource_product(count,4);return count;}
std::uint64_t fingerprint(const Mesh &mesh,bool uv_only=false){
  std::uint64_t hash=1469598103934665603ULL;const auto add=[&](std::uint32_t value){for(int i=0;i<4;++i){hash^=(value>>(i*8))&255;hash*=1099511628211ULL;}};
  if(!uv_only){add(mesh.verts_num);for(const auto &point:mesh.vert_positions())for(int axis=0;axis<3;++axis)add(std::bit_cast<std::uint32_t>(point[axis]));for(int vertex:mesh.corner_verts())add(vertex);}
  const auto name=mesh.active_or_default_uv_map_name();if(!name.is_empty()){const VArraySpan<float2> uv(*mesh.attributes().lookup<float2>(name,bke::AttrDomain::Corner));for(const auto &point:uv){add(std::bit_cast<std::uint32_t>(point.x));add(std::bit_cast<std::uint32_t>(point.y));}}
  return hash;
}
bool remove_owned(Object &object){bool changed=false;for(auto *modifier=static_cast<ModifierData *>(object.modifiers.first),*next=modifier?modifier->next:nullptr;modifier;modifier=next,next=modifier?modifier->next:nullptr){if(!owned(*modifier))continue;BLI_remlink(&object.modifiers,modifier);BKE_modifier_free(modifier);changed=true;}return changed;}
}
DisplacementPreview::~DisplacementPreview(){free_sources();}
void DisplacementPreview::free_sources(){for(const auto &[uid,mesh]:source_meshes_)BKE_id_free_ex(nullptr,const_cast<Mesh *>(mesh),LIB_ID_FREE_NO_MAIN|LIB_ID_FREE_NO_USER_REFCOUNT,true);source_meshes_.clear();source_edge_lengths_.clear();material_levels_.clear();edge_scans_=0;}
void DisplacementPreview::reset(Main &main){for(Object &object:main.objects)remove_owned(object);free_sources();requests_.clear();levels_.clear();smooth_group_=nullptr;}
bool DisplacementPreview::apply(Main &main,Depsgraph &graph,const TextureSet &set,bool content){
  requests_[set.material_session_uid]={set.displacement,content&&!set.disabled,set.disabled};return refresh(main,graph);
}
bool DisplacementPreview::sync_contents(Main &main,Depsgraph &graph,const std::function<bool(std::uint32_t)> &content){bool changed=false;for(auto &[uid,request]:requests_){const bool current=content(uid)&&!request.disabled;if(current!=request.content){request.content=current;changed=true;}}return changed?refresh(main,graph):false;}
bool DisplacementPreview::refresh(Main &main,Depsgraph &graph){
  bool changed=false,relations=false;material_levels_.clear();
  for(Object &object:main.objects){if(object.type!=OB_MESH||!object.data)continue;int requested=-1,requested_type=SUBSURF_TYPE_SIMPLE;const auto uid=object.id.session_uid;std::vector<std::uint32_t> active_materials;
    const auto longest_edge=[&]{if(const auto found=source_edge_lengths_.find(uid);found!=source_edge_lengths_.end())return found->second;const Mesh *mesh=nullptr;if(const auto found=source_meshes_.find(uid);found!=source_meshes_.end())mesh=found->second;else mesh=BKE_object_get_evaluated_mesh(DEG_get_evaluated(&graph,&object));if(!mesh)throw std::runtime_error("Edge Length source mesh is unavailable");++edge_scans_;double longest=0;const auto positions=mesh->vert_positions();for(const auto &edge:mesh->edges()){const auto &a=positions[edge[0]],&b=positions[edge[1]];longest=std::max(longest,std::hypot(double(a.x)-b.x,double(a.y)-b.y,double(a.z)-b.z));}source_edge_lengths_[uid]=longest;return longest;};
    for(int slot=1;slot<=object.totcol;++slot){const auto *material=BKE_object_material_get(&object,slot);if(!material)continue;const auto found=requests_.find(material->id.session_uid);if(found==requests_.end())continue;const auto &request=found->second;const auto &settings=request.settings;if(settings.channel&&settings.enabled&&request.content){active_materials.push_back(material->id.session_uid);requested=std::max(requested,settings.mode==1?edge_length_subdivision(longest_edge(),settings.max_length):settings.subdivision);if(settings.mode==2)requested_type=SUBSURF_TYPE_CATMULL_CLARK;}}
    if(requested<0){if(remove_owned(object)){DEG_id_tag_update(&object.id,ID_RECALC_GEOMETRY);changed=relations=true;}levels_.erase(uid);continue;}
    if(!source_meshes_.contains(uid)){auto *evaluated=DEG_get_evaluated(&graph,&object);const auto *mesh=BKE_object_get_evaluated_mesh(evaluated);if(!mesh)throw std::runtime_error("Displacement preview source mesh is unavailable");auto *copy=BKE_mesh_copy_for_eval(*mesh);for(int i=0;i<copy->totcol;++i)if(copy->mat[i])copy->mat[i]=DEG_get_original(copy->mat[i]);source_meshes_[uid]=copy;}
    int level=requested;const auto count=triangles(*source_meshes_.at(uid),level);for(auto material:active_materials)material_levels_[material]=std::max(material_levels_[material],level);
    if((!levels_.contains(uid)||levels_[uid]!=level)&&level!=requested)std::printf("BTS_PREVIEW_SUBDIVISION_CLAMP object=%s requested=%d applied=%d triangles=%llu\n",object.id.name+2,requested,level,static_cast<unsigned long long>(triangles(*source_meshes_.at(uid),level)));
    bool object_changed=false;
    if(!BKE_modifiers_findby_name(&object,smooth_name)){if(!smooth_group_)smooth_group_=smooth_tree(main);auto *modifier=BKE_modifier_new(eModifierType_Nodes);STRNCPY(modifier->name,smooth_name);modifier->mode=eModifierMode_Realtime|eModifierMode_Render;auto *nodes=reinterpret_cast<NodesModifierData *>(modifier);nodes->node_group=smooth_group_;id_us_plus(&smooth_group_->id);BKE_modifiers_persistent_uid_init(object,*modifier);BLI_addtail(&object.modifiers,modifier);object_changed=relations=true;}
    auto *modifier=BKE_modifiers_findby_name(&object,subdivision_name);if(!modifier){modifier=BKE_modifier_new(eModifierType_Subsurf);STRNCPY(modifier->name,subdivision_name);modifier->mode=eModifierMode_Realtime|eModifierMode_Render;BKE_modifiers_persistent_uid_init(object,*modifier);BLI_addtail(&object.modifiers,modifier);object_changed=relations=true;}
    auto &subdivision=*reinterpret_cast<SubsurfModifierData *>(modifier);if(subdivision.levels!=level){subdivision.levels=level;object_changed=true;}if(subdivision.subdivType!=requested_type||subdivision.uv_smooth!=SUBSURF_UV_SMOOTH_NONE||subdivision.quality!=3||subdivision.boundary_smooth!=SUBSURF_BOUNDARY_SMOOTH_ALL||!(subdivision.flags&eSubsurfModifierFlag_UseCrease))object_changed=true;
    subdivision.subdivType=requested_type;subdivision.renderLevels=level;subdivision.uv_smooth=SUBSURF_UV_SMOOTH_NONE;subdivision.quality=3;subdivision.boundary_smooth=SUBSURF_BOUNDARY_SMOOTH_ALL;subdivision.flags|=eSubsurfModifierFlag_UseCrease;
    if(object_changed){DEG_id_tag_update(&object.id,ID_RECALC_GEOMETRY);changed=true;}levels_[uid]=level;
  }
  if(relations)DEG_relations_tag_update(&main);return changed;
}
QJsonObject DisplacementPreview::state(Main &main,Depsgraph &graph)const {
  QJsonArray objects;for(Object &object:main.objects){if(object.type!=OB_MESH||!object.data)continue;auto *evaluated=DEG_get_evaluated(&graph,&object);const auto *mesh=BKE_object_get_evaluated_mesh(evaluated);const auto *original=reinterpret_cast<const Mesh *>(object.data);const auto source=source_meshes_.find(object.id.session_uid);const auto *base=source==source_meshes_.end()?mesh:source->second;if(!mesh||!base)continue;const auto *subdivision=reinterpret_cast<const SubsurfModifierData *>(BKE_modifiers_findby_name(&object,subdivision_name));int modifiers=0;for(const ModifierData &modifier:object.modifiers)modifiers+=owned(modifier);objects.append(QJsonObject{{"object",object.id.name+2},{"source_vertices",base->verts_num},{"evaluated_vertices",mesh->verts_num},{"subdivision_type",subdivision?int(subdivision->subdivType):-1},{"uv_smooth",subdivision?int(subdivision->uv_smooth):-1},{"quality",subdivision?int(subdivision->quality):-1},{"use_creases",subdivision&&bool(subdivision->flags&eSubsurfModifierFlag_UseCrease)},{"boundary_smooth",subdivision?int(subdivision->boundary_smooth):-1},{"preview_modifiers",modifiers},{"level",levels_.contains(object.id.session_uid)?levels_.at(object.id.session_uid):-1},{"original_mesh_hash",QString::number(fingerprint(*original),16)},{"source_uv_hash",QString::number(fingerprint(*base,true),16)},{"source_geometry_hash",QString::number(fingerprint(*base),16)},{"evaluated_geometry_hash",QString::number(fingerprint(*mesh),16)}});}QJsonArray materials;for(Material &material:main.materials){if(!requests_.contains(material.id.session_uid))continue;auto *output=native_material_output(material);auto *socket=output?bke::node_find_enabled_input_socket(*output,"Displacement"):nullptr;auto *evaluated=DEG_get_evaluated(&graph,&material);int gpu_outputs=0;for(Material *owner:std::set<Material *>{&material,evaluated})if(owner)for(LinkData &link:owner->gpumaterial)gpu_outputs+=GPU_material_has_displacement_output(static_cast<GPUMaterial *>(link.data));materials.append(QJsonObject{{"evaluated_method",evaluated?int(evaluated->displacement_method):-1},{"gpu_displacement_outputs",gpu_outputs},{"subdivision_level",material_levels_.contains(material.id.session_uid)?material_levels_.at(material.id.session_uid):-1},{"material",material.id.name+2},{"method",int(material.displacement_method)},{"inflate_bounds",material.inflate_bounds},{"linked",bool(socket&&socket->link)},{"content",requests_.at(material.id.session_uid).content}});}return {{"objects",objects},{"materials",materials},{"edge_scans",qint64(edge_scans_)}};
}
DisplacementPreview::SaveScope::SaveScope(Main &main){for(Object &object:main.objects)for(auto *modifier=static_cast<ModifierData *>(object.modifiers.first),*next=modifier?modifier->next:nullptr;modifier;modifier=next,next=modifier?modifier->next:nullptr){if(owned(*modifier)){BLI_remlink(&object.modifiers,modifier);detached_.emplace_back(&object,modifier);}}}
DisplacementPreview::SaveScope::~SaveScope(){for(const auto &[object,modifier]:detached_)BLI_addtail(&object->modifiers,modifier);}
}
