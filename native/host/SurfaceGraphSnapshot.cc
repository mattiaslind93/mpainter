// SPDX-License-Identifier: GPL-3.0-or-later
#include "SurfaceGraphSnapshot.hh"
#include "SceneSnapshot.hh"
#include "BKE_attribute.hh"
#include "BKE_layer.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_object.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DEG_depsgraph_query.hh"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_view3d_types.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts::detail {
namespace {
using namespace blender;
void require(bool value){if(!value)throw std::runtime_error("Unsupported surface diagnostic graph");}
SurfaceGraphNode node(const bNode &n){return {n.identifier,n.name,n.idname,bool(n.flag&NODE_MUTED)};}
SurfaceGraphSocket socket(const bNodeTree &tree,const bNode &owner,const bNodeSocket &s){
 SurfaceGraphSocket r;r.identifier=s.identifier;r.type=s.type;r.available=!(s.flag&SOCK_UNAVAIL);
 require(r.available&&(s.default_value||s.type==SOCK_SHADER));switch(s.type){
 case SOCK_FLOAT:r.components=1;r.value[0]=static_cast<const bNodeSocketValueFloat *>(s.default_value)->value;break;
 case SOCK_RGBA:{r.components=4;const auto *v=static_cast<const bNodeSocketValueRGBA *>(s.default_value);std::copy_n(v->value,4,r.value.begin());break;}
 case SOCK_VECTOR:{r.components=3;const auto *v=static_cast<const bNodeSocketValueVector *>(s.default_value);std::copy_n(v->value,3,r.value.begin());break;}
 case SOCK_SHADER:r.components=0;break;
 default:require(false);
 }
 for(int i=0;i<r.components;++i)require(std::isfinite(r.value[i]));
 for(const auto &link:tree.links)if(link.tonode==&owner&&link.tosock==&s){
  require(link.fromnode&&link.fromsock);r.incoming.push_back({node(*link.fromnode),node(owner),link.fromsock->identifier,s.identifier,
   bool(link.flag&NODE_LINK_VALID),bool(link.flag&NODE_LINK_MUTED),!(link.fromsock->flag&SOCK_UNAVAIL)&&r.available});
 }
 require(r.incoming.size()<=1);for(const auto &l:r.incoming)require(l.valid&&!l.muted&&l.available&&!l.from.muted&&!l.to.muted);
 return r;
}
const bNodeSocket &input(const bNode &n,const char *id){for(const auto &s:n.inputs)if(STREQ(s.identifier,id))return s;throw std::runtime_error("Missing surface socket");}
SurfaceGraphState graph(const Material &material){
 SurfaceGraphState r;require(material.nodetree);
 const auto &tree=*material.nodetree;auto *p=native_principled_surface(const_cast<Material &>(material));auto *out=native_material_output(const_cast<Material &>(material));
 require(p&&out);r.principled=node(*p);r.output=node(*out);require(!r.principled.muted&&!r.output.muted);
 const auto *current=out;const auto *destination=&input(*out,"Surface");
 for(int depth=0;depth<3;++depth){
  auto s=socket(tree,*current,*destination);require(s.incoming.size()==1);r.route.push_back(s);
  const bNode *from=nullptr;for(const auto &link:tree.links)if(link.tonode==current&&link.tosock==destination)from=link.fromnode;
  require(from);if(from==p){r.valid=true;break;}
  require(STREQ(from->idname,"ShaderNodeMixShader")&&
   (STREQ(from->name,"__BTS_ViewSwitch")||STREQ(from->name,"__BTS_GeometrySwitch")));
  std::vector<const bNodeSocket *> sockets;for(const auto &s:from->inputs)sockets.push_back(&s);require(sockets.size()==3);
  const auto fac=socket(tree,*from,*sockets[0]);r.route.push_back(fac);
  // A linked/nonzero ViewSwitch could route to an inspection shader instead.
  if(STREQ(from->name,"__BTS_ViewSwitch"))require(fac.components==1&&fac.value[0]==0&&fac.incoming.empty());
  r.route.push_back(socket(tree,*from,*sockets[2]));current=from;destination=sockets[1];
 }
 require(r.valid);
 for(const char *id:{"Base Color","Metallic","Roughness","Normal"})r.inputs.push_back(socket(tree,*p,input(*p,id)));
 return r;
}
}
SurfaceGraphSnapshot capture_surface_graph(const blender::Main &main,const blender::Depsgraph &depsgraph,
 blender::ViewLayer &layer,const blender::View3D &view,std::uint32_t uid) noexcept{
 SurfaceGraphSnapshot r;r.selected_material_uid=uid;
 try{
  using namespace blender;const Material *original=nullptr;for(const auto &m:main.materials)if(m.id.session_uid==uid)original=&m;
  if(!uid||!original){r.status=SurfaceGraphStatus::NoSelection;return r;}
  const auto *evaluated=DEG_get_evaluated(&depsgraph,original);
  r.distinct_evaluated=evaluated&&evaluated!=original&&DEG_is_evaluated(evaluated);
  r.original_backlink_matches=evaluated&&DEG_get_original(evaluated)==original;
  if(!r.distinct_evaluated||!r.original_backlink_matches){r.status=SurfaceGraphStatus::NoEvaluated;return r;}
  r.distinct_evaluated_tree=original->nodetree&&evaluated->nodetree&&original->nodetree!=evaluated->nodetree&&DEG_is_evaluated(evaluated->nodetree);
  r.tree_original_backlink_matches=evaluated->nodetree&&DEG_get_original(evaluated->nodetree)==original->nodetree;
  if(!r.distinct_evaluated_tree||!r.tree_original_backlink_matches){r.status=SurfaceGraphStatus::NoEvaluated;return r;}
  const auto *eval_layer=DEG_get_evaluated_view_layer(&depsgraph);
  r.material_override=layer.mat_override||(eval_layer&&eval_layer->mat_override);
  if(!eval_layer){r.status=SurfaceGraphStatus::NoEvaluated;return r;}
  if(r.material_override){r.status=SurfaceGraphStatus::MaterialOverride;return r;}
  r.status=SurfaceGraphStatus::UnsupportedGraph;r.original=graph(*original);r.evaluated=graph(*evaluated);
  bool selected=false;
  for(const Base &base:*BKE_view_layer_object_bases_get(&layer)){
   if(!base.object||base.object->type!=OB_MESH||!BKE_base_is_visible(&view,&base))continue;
   auto *object=DEG_get_evaluated(&depsgraph,base.object);
   if(!object||object==base.object||!DEG_is_evaluated(object)||DEG_get_original(object)!=base.object){r.status=SurfaceGraphStatus::NoEvaluated;return r;}
   const auto *mesh=BKE_object_get_evaluated_mesh(object);if(!mesh)continue;
   const int count=BKE_object_material_count_eval(object);if(count<1)continue;
   const VArraySpan<int> indices=*mesh->attributes().lookup_or_default<int>("material_index",bke::AttrDomain::Face,0);
   std::vector<int> faces(count);for(int face=0;face<mesh->faces_num;++face)++faces[std::clamp(indices[face],0,count-1)];
   for(int slot=0;slot<count;++slot)if(faces[slot]){
    const auto *m=BKE_object_material_get_eval(object,short(slot+1));const auto *source=m?DEG_get_original(m):nullptr;
    const bool match=m==evaluated&&source==original;selected|=match;
    r.visible_slots.push_back({base.object->id.session_uid,source?source->id.session_uid:0,slot+1,faces[slot],true,m&&DEG_is_evaluated(m),match});
   }
  }
  r.status=selected?SurfaceGraphStatus::Captured:SurfaceGraphStatus::NoVisibleSelectedSlot;
 }catch(...){if(r.status!=SurfaceGraphStatus::UnsupportedGraph)r.status=SurfaceGraphStatus::Failed;}
 return r;
}
}
