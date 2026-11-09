// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialImageBindings.hh"
#include "ResourceAvailability.hh"
#include "SceneSnapshot.hh"
#include "BKE_node_runtime.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace bts {
namespace {
using namespace blender;
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid native image binding or changed source link");}
void worker(const std::atomic_bool &cancel){
  if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Native image bindings require a worker");
  if(cancel.load())throw std::runtime_error("Native image binding inspection cancelled");
}
bNode &node(bNodeTree &tree,int identifier){
  bNode *found=nullptr;for(auto &candidate:tree.nodes)if(candidate.identifier==identifier){if(found)invalid();found=&candidate;}
  if(!found||identifier<=0)invalid();return *found;
}
bNodeSocket &socket(bNode &n,const QString &identifier,bool input){
  bNodeSocket *found=nullptr;for(auto &s:input?n.inputs:n.outputs)if(identifier==QString::fromUtf8(s.identifier)){if(found)invalid();found=&s;}
  if(!found)invalid();return *found;
}
QJsonObject source_settings(const bNode &n){
  if(!STREQ(n.idname,"ShaderNodeTexImage")||!n.id||GS(n.id->name)!=ID_IM||!n.storage)invalid();
  const auto &image=*reinterpret_cast<const Image *>(n.id);const auto &s=*static_cast<const NodeTexImage *>(n.storage);
  QJsonObject result{{"image_source",int(image.source)},{"image_type",int(image.type)},{"alpha_mode",int(image.alpha_mode)},
    {"high_precision",bool(image.flag&IMA_HIGH_BITDEPTH)},{"multiview",bool(image.flag&IMA_USE_VIEWS)},
    {"source_space",QString::fromUtf8(image.colorspace_settings.name)},{"interpolation",s.interpolation},
    {"projection",s.projection},{"projection_blend",s.projection_blend},{"extension",s.extension}};
  native_image_binding_metadata::settings(result);return result;
}
struct Context {bNodeTree *parent; bNode *group;};
struct Edge {
  bNodeTree *tree; bNode *consumer; bNodeSocket *input;
  std::vector<NativeImageGroupStep> path;
  std::vector<Context> parents;
};
QString state_key(const Edge &e){
  QJsonArray path;for(const auto &step:e.path)path.append(QJsonArray{step.node_identifier,step.group});
  return QString::fromUtf8(QJsonDocument(QJsonArray{path,e.consumer->identifier,QString::fromUtf8(e.input->identifier)}).toJson(QJsonDocument::Compact));
}
}
NativeImageBindingInventory native_material_image_bindings(const blender::Material &material,const std::atomic_bool &cancel){
  worker(cancel);NativeImageBindingInventory result;
  // The existing output chooser is read-only and follows pinned EEVEE target
  // precedence; its legacy signature does not express that const contract.
  auto *output=native_material_output(const_cast<Material &>(material));if(!material.nodetree||!output)throw std::invalid_argument("Native source has no material output");
  std::vector<Edge> work;std::set<QString> visited,bindings;std::set<const bNodeTree *> checked_trees;
  std::uint64_t used=0;
  const auto admit=[&](std::uint64_t bytes){used=resource_sum(used,bytes);};
  const auto push=[&](Edge value){
    if(checked_trees.insert(value.tree).second){value.tree->ensure_topology_cache();if(value.tree->has_available_link_cycle())throw std::invalid_argument("Cyclic native image dependency links");}
    admit(resource_sum(sizeof(Edge)+128,resource_product(value.path.size(),sizeof(NativeImageGroupStep)+sizeof(Context)+64)));
    work.push_back(std::move(value));
  };
  for(auto &input:output->inputs)if(STREQ(input.identifier,"Surface")||STREQ(input.identifier,"Displacement"))push({material.nodetree,output,&input,{},{}});
  while(!work.empty()){
    worker(cancel);auto edge=std::move(work.back());work.pop_back();const auto state=state_key(edge);if(!visited.insert(state).second)continue;admit(resource_sum(96,resource_product(state.size(),2)));
    for(const auto &link:edge.tree->links){
      if(link.tonode!=edge.consumer||link.tosock!=edge.input||!link.fromnode||!link.fromsock)continue;
      if(!link.is_used()){result.diagnostics.append("Inactive native image dependency link retained, not offered for replacement");continue;}
      auto &from=*link.fromnode;
      if(from.flag&NODE_MUTED){result.diagnostics.append("Muted source route retained, image replacement not offered: "+QString::fromUtf8(from.name));continue;}
      if(STREQ(from.idname,"ShaderNodeGroup")){
        if(!from.id||GS(from.id->name)!=ID_NT)throw std::invalid_argument("Native image route has no group tree");
        auto *child=reinterpret_cast<bNodeTree *>(from.id);
        if(child==edge.tree||child==material.nodetree||std::any_of(edge.parents.begin(),edge.parents.end(),[&](const auto &p){return p.parent==child;}))throw std::invalid_argument("Cyclic native image group path");
        bNode *group_output=nullptr,*single=nullptr;int outputs=0;for(auto &n:child->nodes)if(STREQ(n.idname,"NodeGroupOutput")){single=&n;++outputs;if(n.flag&NODE_DO_OUTPUT){if(group_output)invalid();group_output=&n;}}
        if(!group_output&&outputs==1)group_output=single;
        if(!group_output)throw std::invalid_argument("Native image group output is not active");
        auto path=edge.path;path.push_back({from.identifier,QString::fromUtf8(child->id.name+2)});
        auto parents=edge.parents;parents.push_back({edge.tree,&from});
        push({child,group_output,&socket(*group_output,QString::fromUtf8(link.fromsock->identifier),true),std::move(path),std::move(parents)});continue;
      }
      if(STREQ(from.idname,"NodeGroupInput")){
        if(edge.parents.empty())throw std::invalid_argument("Native group input has no owning occurrence");
        auto parents=edge.parents;const auto parent=parents.back();parents.pop_back();auto path=edge.path;path.pop_back();
        push({parent.parent,parent.group,&socket(*parent.group,QString::fromUtf8(link.fromsock->identifier),true),std::move(path),std::move(parents)});continue;
      }
      if(STREQ(from.idname,"ShaderNodeTexImage")){
        if(edge.input->flag&SOCK_MULTI_INPUT)throw std::invalid_argument("Native image replacement does not support multi-input links");
        NativeImageBinding binding{edge.path,from.identifier,edge.consumer->identifier,QString::fromUtf8(link.fromsock->identifier),QString::fromUtf8(edge.input->identifier),QString::fromUtf8(edge.consumer->idname),from.id?QString::fromUtf8(from.id->name+2):QString{},source_settings(from)};
        const auto key=binding.key();if(bindings.insert(key).second){admit(resource_sum(sizeof(NativeImageBinding)+512,resource_product(QJsonDocument(native_image_binding_json(binding)).toJson().size(),2)));result.bindings.push_back(std::move(binding));}
      }
      for(auto &input:from.inputs)push({edge.tree,&from,&input,edge.path,edge.parents});
    }
  }
  std::sort(result.bindings.begin(),result.bindings.end(),[](const auto &a,const auto &b){return a.key()<b.key();});return result;
}

ResolvedNativeImageBinding resolve_native_image_binding(blender::Material &material,const NativeImageBinding &binding){
  native_image_binding_json(binding);auto *tree=material.nodetree;if(!tree)invalid();std::set<bNodeTree *> visited{tree};
  for(const auto &step:binding.groups){auto &group=node(*tree,step.node_identifier);if(!STREQ(group.idname,"ShaderNodeGroup")||(group.flag&NODE_MUTED)||!group.id||GS(group.id->name)!=ID_NT||step.group!=QString::fromUtf8(group.id->name+2))invalid();tree=reinterpret_cast<bNodeTree *>(group.id);if(!visited.insert(tree).second)invalid();}
  auto &image=node(*tree,binding.node_identifier),&consumer=node(*tree,binding.consumer_identifier);
  if((&image==&consumer)||(consumer.flag&NODE_MUTED)||(image.flag&NODE_MUTED)||binding.consumer_type!=QString::fromUtf8(consumer.idname)||source_settings(image)!=binding.source_settings||binding.image!=QString::fromUtf8(image.id->name+2))invalid();
  auto &from=socket(image,binding.output,false),&to=socket(consumer,binding.input,true);int matches=0,incoming=0;
  for(const auto &link:tree->links)if(link.tonode==&consumer&&link.tosock==&to){++incoming;if(link.fromnode==&image&&link.fromsock==&from&&link.is_used())++matches;}
  if(matches!=1||incoming!=1||(to.flag&SOCK_MULTI_INPUT))invalid();return {tree,&image,&consumer};
}
}
