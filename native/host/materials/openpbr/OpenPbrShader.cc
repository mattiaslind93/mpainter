#include "OpenPbrShader.hh"
#include "OpenPbrCompatibility.hh"
#include "SceneSnapshot.hh"
#include "BKE_idprop.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include <vector>
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include <QJsonDocument>
namespace bts {namespace {
using namespace blender;
constexpr const char *original_key="bts_openpbr_original_sockets",*weight_name="__BTS_OpenPbrBaseWeight";
bNodeSocket &socket(bNode &node,const char *id,bool output=false){auto &list=output?node.outputs:node.inputs;for(auto &s:list)if(STREQ(s.identifier,id))return s;throw std::runtime_error(std::string("OpenPBR native socket missing: ")+id);}
bNodeLink *incoming(bNodeTree &tree,bNodeSocket &s){for(auto &link:tree.links)if(link.tosock==&s)return &link;return nullptr;}
QJsonArray read_value(bNodeSocket &s){QJsonArray result;if(!s.default_value)throw std::runtime_error("OpenPBR native socket has no default");if(s.type==SOCK_FLOAT)result.append(static_cast<bNodeSocketValueFloat *>(s.default_value)->value);else if(s.type==SOCK_BOOLEAN)result.append(bool(static_cast<bNodeSocketValueBoolean *>(s.default_value)->value));else if(s.type==SOCK_RGBA){for(float v:static_cast<bNodeSocketValueRGBA *>(s.default_value)->value)result.append(v);}else if(s.type==SOCK_VECTOR){const auto &vector=*static_cast<bNodeSocketValueVector *>(s.default_value);if(vector.dimensions<2||vector.dimensions>4)throw std::runtime_error("Invalid native OpenPBR vector dimensions");for(int c=0;c<vector.dimensions;++c)result.append(vector.value[c]);}else throw std::runtime_error("Unsupported native OpenPBR socket type");return result;}
bool write_value(bNodeTree &tree,bNodeSocket &s,QJsonArray value){const int size=s.type==SOCK_RGBA?4:s.type==SOCK_VECTOR?static_cast<bNodeSocketValueVector *>(s.default_value)->dimensions:1;if(value.size()!=size)throw std::runtime_error("Invalid saved OpenPBR socket component count");for(int c=0;c<size;++c){if(s.type==SOCK_BOOLEAN){if(!value[c].isBool())throw std::runtime_error("Invalid OpenPBR boolean socket");}else{if(!value[c].isDouble()||!std::isfinite(float(value[c].toDouble())))throw std::runtime_error("Invalid OpenPBR numeric socket");value[c]=float(value[c].toDouble());}}if(read_value(s)==value)return false;if(s.type==SOCK_FLOAT&&value.size()==1)static_cast<bNodeSocketValueFloat *>(s.default_value)->value=float(value[0].toDouble());else if(s.type==SOCK_BOOLEAN&&value.size()==1)static_cast<bNodeSocketValueBoolean *>(s.default_value)->value=value[0].toBool();else if(s.type==SOCK_RGBA&&value.size()==4){for(int c=0;c<4;++c)static_cast<bNodeSocketValueRGBA *>(s.default_value)->value[c]=float(value[c].toDouble());}else if(s.type==SOCK_VECTOR){for(int c=0;c<value.size();++c)static_cast<bNodeSocketValueVector *>(s.default_value)->value[c]=float(value[c].toDouble());}else throw std::runtime_error("Invalid saved OpenPBR socket default");BKE_ntree_update_tag_socket_property(&tree,&s);return true;}
std::vector<const char *> managed(){std::vector<const char *> names{"Base Color","Subsurface Radius","Thin Film Thickness","Thin Wall"};for(const auto &p:openpbr_number_sockets)names.push_back(p.socket);for(const auto &p:openpbr_color_sockets)names.push_back(p.socket);return names;}
QJsonObject saved(Material &material){auto *root=material.id.properties;auto *p=root?IDP_GetPropertyFromGroup(root,original_key):nullptr;if(!p)return {};if(p->type!=IDP_STRING)throw std::runtime_error("Invalid saved OpenPBR socket metadata");QJsonParseError error;auto json=QJsonDocument::fromJson(IDP_string_get(p),&error);if(error.error!=QJsonParseError::NoError||!json.isObject())throw std::runtime_error("Corrupt OpenPBR socket metadata");return json.object();}
bool link(bNodeTree &tree,bNode &from,bNodeSocket &out,bNode &to,bNodeSocket &in){auto *old=incoming(tree,in);if(old&&old->fromsock==&out)return false;bke::node_remove_socket_links(tree,in);bke::node_add_link(tree,from,out,to,in);return true;}
QJsonObject capture(Material &material,bNode &surface){auto &tree=*material.nodetree;QJsonObject sockets;for(auto *name:managed()){auto &s=socket(surface,name);QJsonObject entry{{"type",s.type},{"value",read_value(s)}};if(auto *l=incoming(tree,s)){entry["node"]=l->fromnode->name;entry["socket"]=l->fromsock->identifier;}sockets[name]=entry;}return {{"version",1},{"surface",surface.name},{"method",surface.custom2},{"sockets",sockets}};}
}
bool apply_openpbr_shader(blender::Main &main,blender::Material &material,const std::optional<OpenPbrParameters> &profile,bool subsurface_preview){
 using namespace blender;if(!material.nodetree)return false;auto prior=saved(material);if(!profile&&prior.isEmpty())return false;auto *surface=native_principled_surface(material);if(!surface)throw std::runtime_error("OpenPBR compatibility requires the supported Principled surface");auto &tree=*material.nodetree;bool changed=false;
 if(!profile){
  if(prior["version"]!=1||!prior["sockets"].isObject())throw std::runtime_error("Unsupported OpenPBR restoration metadata");auto *weight=bke::node_find_node_by_name(tree,weight_name);
  for(auto *name:managed()){auto &s=socket(*surface,name);const auto entry=prior["sockets"].toObject()[name].toObject();if(entry["type"]!=s.type||!entry["value"].isArray())throw std::runtime_error("Saved OpenPBR socket type changed");changed|=write_value(tree,s,entry["value"].toArray());
   bNode *from=nullptr;bNodeSocket *out=nullptr;
   if(STREQ(name,"Base Color")&&weight){if(auto *current=incoming(tree,socket(*weight,"Color1"))){from=current->fromnode;out=current->fromsock;}}
   if(!from&&!entry["node"].toString().isEmpty()){from=bke::node_find_node_by_name(tree,entry["node"].toString().toUtf8().constData());if(!from)throw std::runtime_error("Original material node is missing; cannot restore OpenPBR socket link");out=&socket(*from,entry["socket"].toString().toUtf8().constData(),true);}
   if(from&&out)changed|=link(tree,*from,*out,*surface,s);else if(incoming(tree,s)){bke::node_remove_socket_links(tree,s);changed=true;}
  }
  if(surface->custom2!=prior["method"].toInt()){surface->custom2=prior["method"].toInt();BKE_ntree_update_tag_node_property(&tree,surface);changed=true;}
  auto *root=material.id.properties;IDP_FreeFromGroup(root,IDP_GetPropertyFromGroup(root,original_key));if(changed)BKE_ntree_update_after_single_tree_change(main,tree);return changed;
 }
 validate_openpbr_compatibility(*profile);const auto &p=*profile;
 if(prior.isEmpty()){prior=capture(material,*surface);const auto bytes=QJsonDocument(prior).toJson(QJsonDocument::Compact);IDP_ReplaceInGroup(IDP_EnsureProperties(&material.id),IDP_NewString(StringRef(bytes.constData(),bytes.size()),original_key));}
 for(const auto &binding:openpbr_number_sockets){auto &s=socket(*surface,binding.socket);if(incoming(tree,s)){bke::node_remove_socket_links(tree,s);changed=true;}double value=p.get(binding.parameter)[0]*binding.factor;if(STREQ(binding.parameter,"subsurface_weight")&&!subsurface_preview)value=0;changed|=write_value(tree,s,QJsonArray{value});}
 for(const auto &binding:openpbr_color_sockets){auto &s=socket(*surface,binding.socket);if(incoming(tree,s)){bke::node_remove_socket_links(tree,s);changed=true;}const auto c=p.get(binding.parameter);changed|=write_value(tree,s,QJsonArray{c[0],c[1],c[2],1});}
 auto &radius=socket(*surface,"Subsurface Radius");if(incoming(tree,radius)){bke::node_remove_socket_links(tree,radius);changed=true;}const auto r=p.get("subsurface_radius_scale");changed|=write_value(tree,radius,QJsonArray{r[0],r[1],r[2]});
 auto &film=socket(*surface,"Thin Film Thickness");if(incoming(tree,film)){bke::node_remove_socket_links(tree,film);changed=true;}changed|=write_value(tree,film,QJsonArray{p.get("thin_film_weight")[0]==0?0:openpbr_thin_film_nanometers(p)});
 auto &thin=socket(*surface,"Thin Wall");if(incoming(tree,thin)){bke::node_remove_socket_links(tree,thin);changed=true;}changed|=write_value(tree,thin,QJsonArray{bool(p.get("geometry_thin_walled")[0])});
 if(surface->custom2!=SHD_SUBSURFACE_RANDOM_WALK){surface->custom2=SHD_SUBSURFACE_RANDOM_WALK;BKE_ntree_update_tag_node_property(&tree,surface);changed=true;}
 auto *weight=bke::node_find_node_by_name(tree,weight_name);if(!weight){weight=bke::node_add_node(nullptr,tree,UString("ShaderNodeMixRGB"));if(!weight)throw std::runtime_error("Cannot create portable OpenPBR base weight");STRNCPY(weight->name,weight_name);weight->custom1=MA_RAMP_MULT;BKE_ntree_update_tag_node_property(&tree,weight);changed=true;}
 auto &base=socket(*surface,"Base Color");if(auto *source=incoming(tree,base);source&&source->fromnode!=weight)changed|=link(tree,*source->fromnode,*source->fromsock,*weight,socket(*weight,"Color1"));
 const double factor=p.get("base_weight")[0];changed|=write_value(tree,socket(*weight,"Fac"),QJsonArray{1});changed|=write_value(tree,socket(*weight,"Color2"),QJsonArray{factor,factor,factor,1});changed|=link(tree,*weight,socket(*weight,"Color",true),*surface,base);
 return changed;
}
QJsonObject openpbr_shader_state(blender::Material &material){using namespace blender;QJsonObject state{{"installed",!saved(material).isEmpty()},{"backend","Principled compatibility subset, not OpenPBR BSDF conformance"}};auto *surface=native_principled_surface(material);if(!surface)return state;QJsonObject values,links;for(const auto *name:managed()){auto &input=socket(*surface,name);values[name]=read_value(input);if(auto *l=incoming(*material.nodetree,input))links[name]=QJsonArray{l->fromnode->name,l->fromsock->identifier};else links[name]=QJsonArray{};}state["sockets"]=values;state["links"]=links;state["subsurface_method"]=surface->custom2;if(state["installed"].toBool())if(auto *weight=bke::node_find_node_by_name(*material.nodetree,weight_name))state["base_weight"]=read_value(socket(*weight,"Color2"));return state;}
}
