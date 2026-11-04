#include "MaterialLayerParameterSurface.hh"
#include "SceneSnapshot.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node_tree_update.hh"
#include "IMB_colormanagement.hh"
#include "BKE_node_runtime.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_image_types.h"
#include "DNA_texture_types.h"
#include "NOD_socket_declarations.hh"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace bts {
void require_material_parameter_mask_sampling(const blender::bNode &sample){
 using namespace blender;
 if(!STREQ(sample.idname,"ShaderNodeTexImage")||(sample.flag&NODE_MUTED)||!sample.storage)throw std::invalid_argument("Parameter opacity native image settings are unavailable");
 const auto &settings=*static_cast<const NodeTexImage *>(sample.storage);
 if(settings.interpolation!=SHD_INTERP_LINEAR||settings.extension!=SHD_IMAGE_EXTENSION_REPEAT||settings.projection!=SHD_PROJ_FLAT)throw std::invalid_argument("Parameter opacity native image sampling changed");
 const auto &user=settings.iuser;
 if(user.scene||user.frames!=100||user.sfra!=1||user.offset||user.cycl||user.multiview_eye||user.pass||user.multi_index||user.view||user.layer||(user.tile!=0&&user.tile!=1001)||(user.flag&~(IMA_NEED_FRAME_RECALC|IMA_USER_FRAME_IN_RANGE)))throw std::invalid_argument("Parameter opacity ImageUser settings changed");
 const auto &mapping=settings.base.tex_mapping;
 if(mapping.ob||mapping.type!=TEXMAP_TYPE_POINT||mapping.projx!=PROJ_X||mapping.projy!=PROJ_Y||mapping.projz!=PROJ_Z||mapping.mapping!=MTEX_FLAT||(mapping.flag&~TEXMAP_UNIT_MATRIX))throw std::invalid_argument("Parameter opacity hidden texture mapping changed");
 for(int i=0;i<3;++i)if(mapping.loc[i]!=0||mapping.rot[i]!=0||mapping.size[i]!=1)throw std::invalid_argument("Parameter opacity hidden texture transform changed");
 for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(mapping.mat[c][r]!=(c==r?1.f:0.f))throw std::invalid_argument("Parameter opacity hidden texture matrix changed");
}
void require_material_parameter_surface(const blender::Main &main,const blender::Material &candidate){
 using namespace blender;
 const auto fail=[](const std::string &reason){throw std::invalid_argument("Parameter package: "+reason);};
 const Material *material=nullptr;for(const auto &item:main.materials)if(&item==&candidate){material=&item;break;}
 if(!material)fail("target material is outside its owner Main");
 if(material->adt||!material->nodetree)fail("animated or missing target shader");
 const auto &tree=*material->nodetree;if(!(tree.id.flag&ID_FLAG_EMBEDDED_DATA)||BKE_id_owner_get(const_cast<ID *>(&tree.id),false)!=&material->id||tree.adt)fail("shader tree is not an unanimated owned embedded tree");
 const auto *surface=native_principled_surface(const_cast<Material &>(*material));
 if(!surface||!surface->runtime||!surface->declaration()||(surface->flag&NODE_MUTED))fail("ordinary Principled declaration unavailable");
 if(surface->custom1!=SHD_GLOSSY_MULTI_GGX||surface->custom2!=SHD_SUBSURFACE_RANDOM_WALK)fail("retained Principled distribution/subsurface method is outside this package");
 // Match the same native factory-default -> working-space conversion used by
 // New. In particular, Blender converts Subsurface Radius as color-like data,
 // including its native rounding/snap rules. Only this detached Main is changed.
 std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);
 auto *canonical_material=BKE_material_add(reference.get(),"Parameter canonical defaults");
 if(!canonical_material||!canonical_material->nodetree)throw std::bad_alloc();
 auto *canonical=bke::node_add_node(nullptr,*canonical_material->nodetree,UString("ShaderNodeBsdfPrincipled"));
 if(!canonical)throw std::bad_alloc();
 BKE_ntree_update_after_single_tree_change(*reference,*canonical_material->nodetree);
 IMB_colormanagement_working_space_convert(reference.get(),&main);
 const auto &declaration=*surface->declaration();
 for(const auto &socket:surface->inputs){
  if(STREQ(socket.identifier,"Base Color")||STREQ(socket.identifier,"Metallic")||STREQ(socket.identifier,"Roughness")||STREQ(socket.identifier,"Normal"))continue;
  const std::string name=socket.identifier;
  for(const auto &link:tree.links)if(link.tosock==&socket)fail("retained input '"+name+"' is linked and is not exported");
  const auto found=std::find_if(declaration.inputs.begin(),declaration.inputs.end(),[&](const auto *input){return input->identifier==socket.identifier;});
  if(found==declaration.inputs.end()||(*found)->socket_type!=socket.type||!socket.default_value)fail("retained input '"+name+"' has an unsupported declaration");
  const auto *expected=*found;const auto *default_socket=bke::node_find_socket(*canonical,SOCK_IN,UString(socket.identifier));
  if(!default_socket||default_socket->type!=socket.type||!default_socket->default_value)fail("canonical input '"+name+"' is unavailable");bool equal=false;
  if(dynamic_cast<const nodes::decl::Float *>(expected)){const float actual=static_cast<const bNodeSocketValueFloat *>(socket.default_value)->value;equal=std::isfinite(actual)&&actual==static_cast<const bNodeSocketValueFloat *>(default_socket->default_value)->value;}
  else if(dynamic_cast<const nodes::decl::Bool *>(expected))equal=static_cast<const bNodeSocketValueBoolean *>(socket.default_value)->value==static_cast<const bNodeSocketValueBoolean *>(default_socket->default_value)->value;
  else if(dynamic_cast<const nodes::decl::Vector *>(expected)){const auto &actual=*static_cast<const bNodeSocketValueVector *>(socket.default_value);const auto &wanted=*static_cast<const bNodeSocketValueVector *>(default_socket->default_value);equal=actual.dimensions==3&&wanted.dimensions==3;for(int c=0;c<3;++c)equal&=std::isfinite(actual.value[c])&&actual.value[c]==wanted.value[c];}
  else if(dynamic_cast<const nodes::decl::Color *>(expected)){const auto &actual=*static_cast<const bNodeSocketValueRGBA *>(socket.default_value);const auto &wanted=*static_cast<const bNodeSocketValueRGBA *>(default_socket->default_value);equal=true;for(int c=0;c<4;++c)equal&=std::isfinite(actual.value[c])&&actual.value[c]==wanted.value[c];}
  if(!equal)fail("retained input '"+name+"' differs from its working-space canonical default and is not exported");
 }
}
}
