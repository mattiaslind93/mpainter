// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore>
#include "MaterialLayeringSourceSnapshots.hh"
#include "MaterialLayeringSourceColorProcessor.hh"
#include "BKE_image.hh"
#include "IMB_colormanagement.hh"
#include "opencolorio/intern/libocio/libocio_config.hh"
#include "BLI_math_matrix.hh"
#include <cmath>
#include "BKE_image_hosted.hh"
#include "BKE_main.hh"
#include "BKE_blendfile.hh"
#include "BKE_idprop.hh"
#include "BKE_lib_query.hh"
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_string.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"
#include "TextureDocument.hh"
#include "MaterialLayerShader.hh"
#include "MaterialLayerUdim.hh"
#include "FillTextureJSON.hh"
#include "ResourceIO.hh"
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include "DNA_packedFile_types.h"
#include <set>
#include "BLI_listbase.h"
#include "DNA_image_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <map>
#include <cstdint>
#include <memory>
#include <stdexcept>
namespace bts {
using namespace blender;
MaterialLayerSourceColorTransform material_layer_source_color_transform(const FillTexture &source){
 if(source.raw||source.tiles)throw std::invalid_argument("A scalar Color processor requires one nonraw image");
 const auto *from=IMB_colormanagement_space_get_named(source.input_space.c_str());const auto *to=IMB_colormanagement_space_get_named("ACEScg");
 if(!from||!to)throw std::invalid_argument("Reusable source input color space is unavailable");
 const auto interop=IMB_colormanagement_space_get_interop_id(from);const bool srgb=interop=="srgb_rec709_display"||interop=="srgb_rec709_scene";
 if(!srgb&&interop!="lin_ap1_scene"&&interop!="lin_rec709_scene")throw std::invalid_argument("Reusable Color supports verified linear AP1/Rec.709 or the verified sRGB four-tap contract");
 // First nonlinear input contract is byte-encoded RGB in [0,1]. Do not pretend
 // that arbitrary float/HDR moncurves have the same finite GPU pow domain.
 if(srgb&&source.floating_point)throw std::invalid_argument("sRGB Color currently requires a byte image; float/HDR nonlinear inputs remain unsupported");
 const auto *config=dynamic_cast<const ocio::LibOCIOConfig *>(&IMB_colormanagement_get_config());if(!config)throw std::invalid_argument("Reusable source persistence requires an inspectable OpenColorIO configuration");
 const auto processor=config->get_ocio_config()->getProcessor(IMB_colormanagement_colorspace_get_name(from),IMB_colormanagement_colorspace_get_name(to));
 return material_layer_processor_transform(processor->createGroupTransform(),processor->getCacheID(),srgb);
}
MaterialLayerSourceColorTransform material_layer_linear_color_transform(const FillTexture &source){auto result=material_layer_source_color_transform(source);if(result.srgb)throw std::invalid_argument("This caller requires a linear Color source");return result;}
MaterialLayerColorTransforms material_layer_color_source_transforms(const FillTexture &source){
 source.validate();if(source.raw)throw std::invalid_argument("A data source has no Color processors");MaterialLayerColorTransforms result;
 if(source.tiles){for(const auto &[tile,member]:*source.tiles)result.emplace(tile,material_layer_linear_color_transform(*member));}
 else result.emplace(0,material_layer_source_color_transform(source));return result;
}
QJsonArray material_layer_color_members_json(const FillTexture &source){
 const auto transforms=material_layer_color_source_transforms(source);QJsonArray result;
 for(const auto &[tile,transform]:transforms){const auto &member=source.tiles?*source.tiles->at(tile):source;QJsonArray matrix;for(float value:transform.to_aces)matrix.append(value);result.append(QJsonObject{{"tile",tile},{"input_space",QString::fromStdString(member.input_space)},{"processor",QString::fromStdString(transform.processor)},{"to_ap1",matrix}});}return result;
}
MaterialLayerSourceSnapshotOwners retain_material_layer_source_snapshot_headers(Main &live,Main &snapshot){
 MaterialLayerSourceSnapshotOwners owners;std::map<std::uint32_t,Image *> images;
 // PartialWriteContext::id_add(ADD_DEPENDENCIES) preserves session UIDs and
 // shares dependencies. DUPLICATE_DEPENDENCIES is deliberately not supported.
 for(Image &image:snapshot.images)if(!images.emplace(image.id.session_uid,&image).second)throw std::runtime_error("Duplicate Image identity in save snapshot");
 for(Image &image:live.images)if(image.runtime->gpuflag&IMA_HOSTED_IMMUTABLE_SOURCE){
  const auto found=images.find(image.id.session_uid);if(found==images.end())continue;
  std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)> buffer(BKE_image_hosted_source_snapshot(&image,1001),IMB_freeImBuf);
  if(!buffer)throw std::runtime_error("Cannot snapshot an unavailable reusable material map");
  owners.push_back(buffer->float_buffer.sharing_info);
  found->second->runtime->gpuflag|=IMA_HOSTED_IMMUTABLE_SOURCE;
  if(!BKE_image_assign_hosted_immutable_source(found->second,1001,buffer.get()))throw std::runtime_error("Cannot retain reusable material map snapshot pixels");
 }
 return owners;
}
std::size_t pack_material_layer_source_snapshots(Main &snapshot){
 std::size_t count=0;
 // One canonical Image per interpretation, shared by every input/set. Original
 // encoded project assets are preserved independently: TWO representations.
 for(Image &image:snapshot.images)if(image.runtime->gpuflag&IMA_HOSTED_IMMUTABLE_SOURCE){
  if(!BKE_image_memorypack(&image))throw std::runtime_error("Cannot pack reusable material source snapshot");++count;
 }
 return count;
}

namespace {
Material &source_material(Main &main,std::uint32_t uid){for(Material &material:main.materials)if(material.id.session_uid==uid){if(!material.nodetree||BKE_id_owner_get(&material.nodetree->id,false)!=&material.id)break;return material;}throw std::invalid_argument("Reusable source material is missing");}
Image &source_image(Main &main,std::uint32_t uid){for(Image &image:main.images)if(image.id.session_uid==uid)return image;throw std::invalid_argument("Reusable source Image is missing");}
Image &source_image_id(Main &main,ID *id){for(Image &image:main.images)if(&image.id==id)return image;throw std::invalid_argument("Reusable source Image is outside its Main");}
std::string sample_name(int input,int channel){const auto prefix="__BTS_LayerMaterial"+std::to_string(input);return prefix+(channel==0?"Color":channel==3?"Normal":"ScalarMap"+std::to_string(channel));}
bNode &source_sample(Material &material,int input,int channel,int tap=0,bool closest=false){auto *node=bke::node_find_node_by_name(*material.nodetree,sample_name(input,channel)+(tap?"Tap"+std::to_string(tap):""));if(!node||!STREQ(node->idname,"ShaderNodeTexImage")||!node->storage||(node->flag&NODE_MUTED))throw std::invalid_argument("Reusable source sample node changed");const auto &settings=*static_cast<const NodeTexImage *>(node->storage);if(settings.extension!=SHD_IMAGE_EXTENSION_REPEAT||settings.interpolation!=(closest?SHD_INTERP_CLOSEST:SHD_INTERP_LINEAR)||settings.projection!=SHD_PROJ_FLAT)throw std::invalid_argument("Reusable source sampling changed");const auto &user=settings.iuser;
 if(user.scene||user.frames!=100||user.sfra!=1||user.offset||user.cycl||user.multiview_eye||user.pass||user.multi_index||user.view||user.layer||(user.tile!=0&&user.tile!=1001)||(user.flag&~(IMA_NEED_FRAME_RECALC|IMA_USER_FRAME_IN_RANGE)))throw std::invalid_argument("Reusable source ImageUser settings changed");
 const auto &mapping=settings.base.tex_mapping;
 if(mapping.ob||mapping.type!=TEXMAP_TYPE_POINT||mapping.projx!=PROJ_X||mapping.projy!=PROJ_Y||mapping.projz!=PROJ_Z||mapping.mapping!=MTEX_FLAT||(mapping.flag&~TEXMAP_UNIT_MATRIX))throw std::invalid_argument("Reusable source hidden texture mapping changed");
 for(int i=0;i<3;++i)if(mapping.loc[i]!=0||mapping.rot[i]!=0||mapping.size[i]!=1)throw std::invalid_argument("Reusable source hidden texture transform changed");
 for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(mapping.mat[c][r]!=(c==r?1.f:0.f))throw std::invalid_argument("Reusable source hidden texture matrix changed");
 return *node;}
void validate_mapping(Material &material,bNode &sample,int index,const std::array<double,2> &tiling,const std::array<double,2> &offset,double rotation,bool four_tap=false){
 auto &tree=*material.nodetree;auto *mapping=bke::node_find_node_by_name(tree,"__BTS_LayerMaterial"+std::to_string(index)+"Mapping"),*coordinates=bke::node_find_node_by_name(tree,"__BTS_LayerUvCoordinates");
 if(!mapping||!coordinates||!STREQ(mapping->idname,"ShaderNodeMapping")||!STREQ(coordinates->idname,"ShaderNodeTexCoord")||(mapping->flag&NODE_MUTED)||(coordinates->flag&NODE_MUTED)||mapping->custom1!=NODE_MAPPING_TYPE_POINT||coordinates->id||coordinates->custom1||coordinates->custom2)throw std::invalid_argument("Reusable source mapping node changed");
 const auto socket=[](bNode &node,const char *name)->bNodeSocket &{auto *socket=bke::node_find_enabled_input_socket(node,name);if(!socket)throw std::invalid_argument("Reusable source mapping socket changed");return *socket;};
 const auto edge=[&](bNode &from,const char *output,bNodeSocket &to){int count=0;for(const auto &link:tree.links)if(link.tosock==&to){if(link.fromnode!=&from||!STREQ(link.fromsock->identifier,output)||(link.flag&NODE_LINK_MUTED)||!(link.flag&NODE_LINK_VALID))throw std::invalid_argument("Reusable source mapping edge changed");++count;}if(count!=1)throw std::invalid_argument("Reusable source mapping edge is missing");};
 if(!four_tap)edge(*mapping,"Vector",socket(sample,"Vector"));edge(*coordinates,"UV",socket(*mapping,"Vector"));
 const auto value=[&](const char *name,std::array<float,3> expected){auto &s=socket(*mapping,name);if(s.type!=SOCK_VECTOR||!s.default_value)throw std::invalid_argument("Reusable source mapping value type changed");for(const auto &link:tree.links)if(link.tosock==&s)throw std::invalid_argument("Reusable source mapping value cannot be linked");const auto *v=static_cast<const bNodeSocketValueVector *>(s.default_value)->value;if(!std::equal(expected.begin(),expected.end(),v))throw std::invalid_argument("Reusable source mapping differs from its document");};
 value("Location",{float(offset[0]),float(offset[1]),0});value("Rotation",{0,0,float(rotation*3.14159265358979323846/180)});value("Scale",{float(tiling[0]),float(tiling[1]),1});
}
void validate_fanout(Material &material,bNode &sample,int input,int channel){
 const auto prefix="__BTS_LayerMaterial"+std::to_string(input);int count=0;
 for(const auto &link:material.nodetree->links)if(link.fromnode==&sample){
  if(!STREQ(link.fromsock->identifier,"Color")||!link.tonode||(link.flag&NODE_LINK_MUTED)||!(link.flag&NODE_LINK_VALID))throw std::invalid_argument("Reusable source has an unowned output use");
  const std::string target=link.tonode->name;
  const bool known=channel==0?(target==prefix+"ColorToScene0"||target==prefix+"ColorToScene1"||target==prefix+"ColorToScene2"):channel==3?(target==prefix+"NormalDirectXScale"||target==prefix+"NormalDecodeScale"):(target==prefix+"ScalarMap"+std::to_string(channel)+"Split");
  if(!known||link.tosock!=link.tonode->inputs.first)throw std::invalid_argument("Reusable source has an unowned output use");
  if(channel!=0&&channel!=3&&(!STREQ(link.tonode->idname,"ShaderNodeSeparateXYZ")||!STREQ(link.tosock->identifier,"Vector")))throw std::invalid_argument("Reusable scalar source decoder changed");
  if(channel==3&&(!STREQ(link.tonode->idname,"ShaderNodeVectorMath")||link.tonode->custom1!=NODE_VECTOR_MATH_MULTIPLY))throw std::invalid_argument("Reusable normal source decoder changed");++count;
 }
 if(count!=(channel==0?3:1))throw std::invalid_argument("Reusable source output ownership changed");
}
bool four_tap_color(const FillTexture &source,int channel){return channel==0&&material_layer_source_color_transform(source).srgb;}
std::vector<bNode *> source_samples(Material &material,int input,int channel,const FillTexture &source){
 const bool four=four_tap_color(source,channel);std::vector<bNode *> result;
 for(int tap=0;tap<(four?4:1);++tap)result.push_back(&source_sample(material,input,channel,tap,four));return result;
}
std::vector<bNode *> binding_samples(Main &main,Material &material,int input,int channel,const FillTexture &member,const std::shared_ptr<const FillTexture> &sequence={},int tile=0){
 if(!sequence)return source_samples(material,input,channel,member);
 if(!sequence->tiles||sequence->raw!=(channel!=0)||!sequence->tiles->contains(tile)||fill_texture_json(sequence->tiles->at(tile))!=fill_texture_json(std::make_shared<const FillTexture>(member)))throw std::invalid_argument("UDIM binding member differs from its parent/role");
 if(channel!=0){const auto samples=MaterialLayerShader::raw_udim_samples(main,material,input,channel,*sequence);return {samples.at(tile)};}
 // The codec audits the whole graph first; keep its public primary-sample API
 // and collect the canonical audited siblings for every ID-slot operation.
 const auto samples=MaterialLayerShader::color_udim_samples(main,material,input,*sequence);auto *primary=samples.at(tile);std::vector<bNode *> result{primary};
 for(int tap=1;tap<4;++tap){auto *node=bke::node_find_node_by_name(*material.nodetree,std::string(primary->name)+"Tap"+std::to_string(tap));if(!node||!STREQ(node->idname,"ShaderNodeTexImage")||node->id!=primary->id)throw std::invalid_argument("Linear Color UDIM audited tap identity changed");result.push_back(node);}return result;
}
void validate_source_path(Main &main,Material &material,int input,int channel,const FillTexture &source,const std::vector<bNode *> &samples,const std::array<double,2> &tiling,const std::array<double,2> &offset,double rotation,bool encoded,const std::shared_ptr<const FillTexture> &sequence={}){
 const bool four=four_tap_color(source,channel);validate_mapping(material,*samples.front(),input,tiling,offset,rotation,four||bool(sequence));
 if(!four&&!sequence)validate_fanout(material,*samples.front(),input,channel);
 if(channel==0)MaterialLayerShader::color_source_representation(main,material,input,sequence?*sequence:source,encoded,true);
}
int representation_version(const QJsonArray &rows){int version=1;for(const auto &row:rows){if(row.toObject().contains("sampling"))version=std::max(version,2);if(row.toObject().contains("source_tile"))version=std::max(version,row.toObject()["sampling"]=="linear-color-udim-extend-float-bilinear-v1"?4:3);}return version;}
std::string resource_key(const FillTexture &source,const ProjectResourceImages &resources,int tile=0){
 if(tile){const auto key=source.asset+"_"+std::to_string(tile);if(!resources.by_key.contains(key)||!resources.sha256.contains(key)||resources.sha256.at(key).toStdString()!=source.hash)throw std::invalid_argument("Raw UDIM original member association is missing or changed");return key;}
 std::string key;for(const auto &[candidate,image]:resources.by_key)if(candidate==source.asset||candidate.starts_with(source.asset+"_")){const auto hash=resources.sha256.find(candidate);if(hash!=resources.sha256.end()&&hash->second.toStdString()==source.hash){if(!key.empty()&&resources.by_key.at(key)!=image)throw std::invalid_argument("Reusable source encoded carrier is ambiguous");key=candidate;}}
 if(key.empty())throw std::invalid_argument("Reusable source original packed resource is missing");return key;
}
void require_source_set(const TextureSet &set){if(set.disabled||!set.uniform_editable)throw std::invalid_argument("Packed reusable source inputs require an enabled editable Texture Set");}
std::set<ID **> manifest_slots(Main &main,const Scene &scene,const ProjectResourceImages &resources){
 auto *root=scene.id.properties;auto *group=root?IDP_GetPropertyFromGroup(root,"bts_resource_images"):nullptr;if(!group||group->type!=IDP_GROUP)throw std::invalid_argument("Packed reusable source manifest is missing");
 std::set<ID **> result;for(const auto &[key,pointer]:resources.by_key){auto &image=source_image_id(main,reinterpret_cast<ID *>(pointer));auto *property=IDP_GetPropertyFromGroup(group,key.c_str());if(!property||property->type!=IDP_ID||IDP_ID_get(property)!=&image.id)throw std::invalid_argument("Packed source association differs from its validated manifest");result.insert(reinterpret_cast<ID **>(&property->data.pointer));}return result;
}
void unique_carrier(const FillTexture &source,Image &image,const ProjectResourceImages &resources){
 for(const auto &[key,hash]:resources.sha256)if(hash.toStdString()==source.hash){const auto found=resources.by_key.find(key);if(found==resources.by_key.end()||found->second!=&image)throw std::invalid_argument("One reusable source encoding references different packed Image IDs");}
}
struct CarrierDescriptor {int width=0,height=0,tile=0,view=0,type=0;std::string sha256;};
CarrierDescriptor carrier_descriptor(Image &image,bool cold){
 const auto *tile=static_cast<const ImageTile *>(image.tiles.first);const auto *part=static_cast<const ImagePackedFile *>(image.packedfiles.first);
 if(image.source!=IMA_SRC_FILE||(image.type!=IMA_TYPE_IMAGE&&!(image.type==IMA_TYPE_UV_TEST&&!cold))||!tile||image.tiles.first!=image.tiles.last||tile->tile_number!=1001||(image.flag&IMA_USE_VIEWS)||!image.views.is_empty()||!part||image.packedfiles.first!=image.packedfiles.last||part->view!=0||part->tile_number!=1001||!part->packedfile||part->packedfile->size<1||!part->packedfile->data)throw std::invalid_argument("Reusable source carrier is not one ordinary FILE Image/tile/view");
 if(cold&&(image.alpha_mode!=IMA_ALPHA_CHANNEL_PACKED||!STREQ(image.colorspace_settings.name,IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA))))throw std::invalid_argument("Saved reusable source raw carrier interpretation changed");
 if(cold&&!(image.flag&IMA_HIGH_BITDEPTH))throw std::invalid_argument("Saved reusable source raw carrier no longer requests full float precision");
 const auto *packed=part->packedfile;std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)> header(IMB_load_image_from_memory(static_cast<const unsigned char *>(packed->data),packed->size,ImBufFlags::Test|ImBufFlags::AlphaChannelPacked|ImBufFlags::NoColorspaceConvert,"original-source","original-source"),IMB_freeImBuf);
 if(!header||header->x<1||header->y<1)throw std::invalid_argument("Reusable source encoding is not a supported ordinary stock image");
 CarrierDescriptor result{header->x,header->y,part->tile_number,part->view,int(image.type),{}};
 if(!cold)result.sha256=QCryptographicHash::hash(QByteArrayView(static_cast<const char *>(packed->data),packed->size),QCryptographicHash::Sha256).toHex().toStdString();return result;
}
void binding_dimensions(const FillTexture &source,const CarrierDescriptor &descriptor){if(source.width!=descriptor.width||source.height!=descriptor.height)throw std::invalid_argument("Reusable typed source dimensions differ from its original packed image");}
QJsonObject source_row(const MaterialLayerSourceBinding &binding,const std::string &resource){
 QJsonObject row{{"set",binding.set.toString(QUuid::WithoutBraces)},{"input",binding.input},{"channel",binding.channel},{"resource",QString::fromStdString(resource)},{"texture",fill_texture_json(binding.source)}};
 if(!binding.source->raw){const auto transform=material_layer_source_color_transform(*binding.source);QJsonArray matrix;for(float value:transform.to_aces)matrix.append(value);row["processor"]=QString::fromStdString(transform.processor);row["to_ap1"]=matrix;if(binding.sequence)row["input_space"]=QString::fromStdString(binding.source->input_space);
  if(transform.srgb){QJsonArray taps;for(int value:binding.tap_identifiers)taps.append(value);row["sampling"]=QJsonObject{{"contract","srgb-closest-lod0-float-bilinear-v1"},{"taps",taps},{"gamma",transform.gamma},{"offset",transform.offset},{"break",transform.breakpoint},{"slope",transform.slope},{"negative","linear"}};}
 }if(binding.sequence){row["source_tile"]=binding.tile;row["sequence_sha256"]=QString::fromStdString(binding.sequence->hash);row["sampling"]=binding.source->raw?QStringLiteral("raw-udim-extend-linear-v1"):QStringLiteral("linear-color-udim-extend-float-bilinear-v1");if(!binding.source->raw){QJsonArray taps;for(int identifier:binding.tap_identifiers)taps.append(identifier);row["taps"]=taps;}}row["node"]=binding.node_identifier;row["uv"]=QJsonArray{binding.tiling[0],binding.tiling[1],binding.offset[0],binding.offset[1],binding.rotation};return row;
}
void audit_users(Main &main,const std::set<ID *> &ids,const std::set<ID **> &allowed){
 for(auto *id:ids)if(id->flag&ID_FLAG_FAKEUSER)throw std::invalid_argument("Reusable source Image has a foreign fake user");
 struct Audit{const std::set<ID *> &ids;const std::set<ID **> &allowed;bool foreign=false;}audit{ids,allowed};
 const auto visit=[](LibraryIDLinkCallbackData *data){auto &audit=*static_cast<Audit *>(data->user_data);if(audit.ids.contains(*data->id_pointer)&&!audit.allowed.contains(data->id_pointer))audit.foreign=true;return IDWALK_RET_NOP;};
 ID *id;FOREACH_MAIN_ID_BEGIN(&main,id){BKE_library_foreach_ID_link(&main,id,visit,&audit,IDWALK_READONLY|IDWALK_INCLUDE_UI);}FOREACH_MAIN_ID_END;
 if(audit.foreign)throw std::invalid_argument("Reusable source Image has a foreign node, property or UI user; original encoding cannot replace it");
 // PartialWrite adds synthetic EXTRAUSER. It is NOT a genuine inbound ID slot
 // or fake user and must not make every otherwise-unused alias unprunable.
}
}
MaterialLayerSourceBindings capture_material_layer_source_bindings(Main &main,const TextureDocument &document){
 MaterialLayerSourceBindings result;
 for(const auto &set:document.texture_sets())if(set.material_layering.enabled)for(int input=0;input<4;++input)for(int channel=0;channel<6;++channel)if(const auto &source=set.material_layering.materials[input].textures[channel]){
  require_source_set(set);source->validate();const auto &mapping=set.material_layering.materials[input];require_material_layer_udim_coverage(mapping,set.tiles);
  if(!source->raw)material_layer_color_source_transforms(*source);
  auto &material=source_material(main,set.material_session_uid);
  const auto capture=[&](const std::shared_ptr<const FillTexture> &member,int tile){
   if(!member->pixels)throw std::invalid_argument("Packed reusable source member is not hydrated");const auto sequence=source->tiles?source:std::shared_ptr<const FillTexture>{};
   auto samples=binding_samples(main,material,input,channel,*member,sequence,tile);auto &sample=*samples.front();validate_source_path(main,material,input,channel,*member,samples,mapping.tiling,mapping.offset,mapping.rotation,false,sequence);auto &image=source_image_id(main,sample.id);
   for(auto *tap:samples)if(tap->id!=&image.id)throw std::invalid_argument("Reusable taps do not share their immutable alias");
   if(!(image.runtime->gpuflag&IMA_HOSTED_IMMUTABLE_SOURCE))throw std::invalid_argument("Reusable source is not an owned immutable alias");
   std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)> header(BKE_image_hosted_source_snapshot(&image,1001),IMB_freeImBuf);
   if(!header||header->x!=member->width||header->y!=member->height||header->float_data()!=member->pixels->front().data()||!header->colorspace_is_data())throw std::invalid_argument("Reusable source alias does not match its immutable typed pixels");
   MaterialLayerSourceBinding binding{set.id,set.material_session_uid,image.id.session_uid,input,channel,member};binding.sequence=sequence;binding.tile=tile;binding.node_identifier=sample.identifier;for(std::size_t tap=0;tap<samples.size();++tap)binding.tap_identifiers[tap]=samples[tap]->identifier;binding.tiling=mapping.tiling;binding.offset=mapping.offset;binding.rotation=mapping.rotation;result.push_back(std::move(binding));
  };
  if(source->tiles)for(const auto &[tile,member]:*source->tiles)capture(member,tile);else capture(source,0);
 }
 return result;
}
void pack_material_layer_source_bindings(bke::blendfile::PartialWriteContext &context,Scene &scene,const MaterialLayerSourceBindings &bindings,const ProjectResourceImages &resources){
 if(bindings.empty()){if(auto *root=scene.id.properties)if(auto *old=IDP_GetPropertyFromGroup(root,"bts_material_layer_sources"))IDP_FreeFromGroup(root,old);return;}auto &main=context.bmain;std::set<ID *> aliases,carriers;std::set<ID **> alias_uses;const auto carrier_uses=manifest_slots(main,scene,resources);QJsonArray rows;
 // Validate the WHOLE batch before any ID deletion or metadata mutation.
 for(const auto &binding:bindings){auto &material=source_material(main,binding.material_uid);auto samples=binding_samples(main,material,binding.input,binding.channel,*binding.source,binding.sequence,binding.tile);auto &alias=source_image(main,binding.image_uid);
  if(samples.front()->identifier!=binding.node_identifier)throw std::invalid_argument("Save snapshot reusable source reference changed");
  for(std::size_t tap=0;tap<samples.size();++tap){if(samples[tap]->id!=&alias.id||samples[tap]->identifier!=binding.tap_identifiers[tap])throw std::invalid_argument("Save snapshot reusable source tap changed");alias_uses.insert(&samples[tap]->id);}
  validate_source_path(main,material,binding.input,binding.channel,*binding.source,samples,binding.tiling,binding.offset,binding.rotation,false,binding.sequence);aliases.insert(&alias.id);
  const auto key=resource_key(*binding.source,resources,binding.tile);auto &carrier=source_image_id(main,reinterpret_cast<ID *>(resources.by_key.at(key)));if(aliases.contains(&carrier.id))throw std::invalid_argument("Original encoding carrier cannot be a runtime alias");unique_carrier(*binding.source,carrier,resources);carriers.insert(&carrier.id);rows.append(source_row(binding,key));
 }
 audit_users(main,aliases,alias_uses);audit_users(main,carriers,carrier_uses);
 // Reused resource identity hints are not proof of current packed bytes. Verify
 // each participating carrier exactly once, without an encoded QByteArray copy.
 std::map<Image *,CarrierDescriptor> checked;
 for(const auto &binding:bindings){auto *image=resources.by_key.at(resource_key(*binding.source,resources,binding.tile));auto [at,fresh]=checked.try_emplace(image);if(fresh)at->second=carrier_descriptor(*image,false);binding_dimensions(*binding.source,at->second);if(at->second.sha256!=binding.source->hash)throw std::invalid_argument("Reusable source original packed checksum changed");}
 // PartialWrite remaps copied dependencies without rebuilding their real user
 // counts. Audit the closed snapshot first, then count its owned references
 // before using normal per-edge refcounting; never recount the live Main.
 if(!context.is_valid())throw std::runtime_error("Reusable source save snapshot has invalid ID references before rebinding");
 BKE_main_id_refcount_recompute(&main,false);
 for(const auto &binding:bindings){auto &material=source_material(main,binding.material_uid);auto samples=binding_samples(main,material,binding.input,binding.channel,*binding.source,binding.sequence,binding.tile);auto &carrier=*resources.by_key.at(resource_key(*binding.source,resources,binding.tile));
  BKE_image_free_buffers(&carrier);carrier.source=IMA_SRC_FILE;carrier.type=IMA_TYPE_IMAGE;carrier.flag|=IMA_HIGH_BITDEPTH;carrier.alpha_mode=IMA_ALPHA_CHANNEL_PACKED;STRNCPY(carrier.colorspace_settings.name,IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA));
  for(auto *sample:samples){id_us_min(sample->id);sample->id=&carrier.id;id_us_plus(sample->id);BKE_ntree_update_tag_node_property(material.nodetree,sample);}
  if(binding.channel==0&&!binding.sequence)MaterialLayerShader::color_source_representation(main,material,binding.input,*binding.source,true);
  BKE_ntree_update_after_single_tree_change(main,*material.nodetree);
 }
 // A Color sequence is one owned graph. All member edges must be retargeted
 // before its representation is converted; never expose a partially converted
 // graph to validation of the next member.
 std::set<std::pair<std::uint32_t,int>> converted;
 for(const auto &binding:bindings)if(binding.channel==0&&binding.sequence&&converted.emplace(binding.material_uid,binding.input).second){auto &material=source_material(main,binding.material_uid);MaterialLayerShader::color_source_representation(main,material,binding.input,*binding.sequence,true);BKE_ntree_update_after_single_tree_change(main,*material.nodetree);}
 audit_users(main,aliases,{});
 for(auto *alias:aliases)context.id_delete(alias);
 if(!context.is_valid())throw std::runtime_error("Reusable source save snapshot retains invalid ID references");
 const auto json=QJsonDocument(QJsonObject{{"version",representation_version(rows)},{"bindings",rows}}).toJson(QJsonDocument::Compact);
 if(json.size()>std::numeric_limits<decltype(IDProperty::len)>::max()-1)throw std::length_error("Reusable source metadata exceeds Blender IDProperty storage");
 IDP_ReplaceInGroup(IDP_EnsureProperties(&scene.id),IDP_NewString(json.constData(),"bts_material_layer_sources"));
}
void prune_unused_material_resource_copies(bke::blendfile::PartialWriteContext &context,const ProjectResourceImages &resources){
 auto &main=context.bmain;std::set<ID *> candidates,used;
 for(auto uid:resources.retired_uids)for(Image &image:main.images)if(image.id.session_uid==uid&&!(image.id.flag&ID_FLAG_FAKEUSER))candidates.insert(&image.id);
 struct Uses{const std::set<ID *> &candidates;std::set<ID *> &used;}uses{candidates,used};
 const auto visit=[](LibraryIDLinkCallbackData *data){auto &uses=*static_cast<Uses *>(data->user_data);if(uses.candidates.contains(*data->id_pointer))uses.used.insert(*data->id_pointer);return IDWALK_RET_NOP;};
 ID *id;FOREACH_MAIN_ID_BEGIN(&main,id){BKE_library_foreach_ID_link(&main,id,visit,&uses,IDWALK_READONLY|IDWALK_INCLUDE_UI);}FOREACH_MAIN_ID_END;
 // Every candidate is a former manifest dependency copied into this context.
 // Preserve foreign uses; ignore only PartialWrite's synthetic EXTRAUSER.
 for(auto *candidate:candidates)if(!used.contains(candidate))context.id_delete(candidate);
 if(!context.is_valid())throw std::runtime_error("Resource retirement invalidated the save snapshot");
}
bool validate_packed_material_layer_sources(Main &main,const Scene &scene,const TextureDocument &document,const ProjectResourceImages &resources){
 const auto *root=scene.id.properties;const auto *property=root?IDP_GetPropertyFromGroup(root,"bts_material_layer_sources"):nullptr;if(!property)return false;
 for(Material &material:main.materials)if(material.nodetree)BKE_ntree_update_after_single_tree_change(main,*material.nodetree);
 if(property->type!=IDP_STRING||property->len<1||IDP_string_get(property)[property->len-1]!=0)throw std::invalid_argument("Invalid reusable source persistence metadata");
 QJsonParseError error;const auto saved=QJsonDocument::fromJson(QByteArray::fromRawData(IDP_string_get(property),property->len-1),&error);if(error.error!=QJsonParseError::NoError||!saved.isObject())throw std::invalid_argument("Invalid reusable source persistence JSON");QJsonArray rows;
 auto allowed=manifest_slots(main,scene,resources);std::set<ID *> carriers;std::map<Image *,CarrierDescriptor> descriptors;
 for(const auto &set:document.texture_sets())if(set.material_layering.enabled)for(int input=0;input<4;++input)for(int channel=0;channel<6;++channel)if(const auto &source=set.material_layering.materials[input].textures[channel]){
  require_source_set(set);source->validate();const auto &mapping=set.material_layering.materials[input];require_material_layer_udim_coverage(mapping,set.tiles);
  const auto validate=[&](const std::shared_ptr<const FillTexture> &member,int tile){
   if(!member->pixels)throw std::invalid_argument("Saved reusable source member is not hydrated");const auto sequence=source->tiles?source:std::shared_ptr<const FillTexture>{};const auto key=resource_key(*member,resources,tile);auto &image=source_image_id(main,reinterpret_cast<ID *>(resources.by_key.at(key)));unique_carrier(*member,image,resources);
   auto [descriptor,fresh]=descriptors.try_emplace(&image);if(fresh)descriptor->second=carrier_descriptor(image,true);binding_dimensions(*member,descriptor->second);
   auto &material=source_material(main,set.material_session_uid);auto samples=binding_samples(main,material,input,channel,*member,sequence,tile);auto &sample=*samples.front();validate_source_path(main,material,input,channel,*member,samples,mapping.tiling,mapping.offset,mapping.rotation,true,sequence);
   for(auto *tap:samples){if(tap->id!=&image.id)throw std::invalid_argument("Saved reusable source references another carrier");allowed.insert(&tap->id);}carriers.insert(&image.id);
   MaterialLayerSourceBinding binding{set.id,set.material_session_uid,0,input,channel,member};binding.sequence=sequence;binding.tile=tile;binding.node_identifier=sample.identifier;for(std::size_t tap=0;tap<samples.size();++tap)binding.tap_identifiers[tap]=samples[tap]->identifier;binding.tiling=mapping.tiling;binding.offset=mapping.offset;binding.rotation=mapping.rotation;rows.append(source_row(binding,key));
  };
  if(source->tiles)for(const auto &[tile,member]:*source->tiles)validate(member,tile);else validate(source,0);
 }
 if(rows.isEmpty()||saved.object()!=QJsonObject{{"version",representation_version(rows)},{"bindings",rows}})throw std::invalid_argument("Reusable source persistence provenance differs from its typed document");audit_users(main,carriers,allowed);return true;
}
}
