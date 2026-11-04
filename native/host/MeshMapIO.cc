#include "SafeImagePack.hh"
#include "MeshMapIO.hh"
#include "TextureDocument.hh"
#include "BKE_idprop.hh"
#include "BKE_image.hh"
#include "BLI_string.h"
#include "DNA_ID.h"
#include "DNA_image_types.h"
#include "DNA_scene_types.h"
#include "IMB_imbuf_types.hh"
#include <algorithm>
#include <stdexcept>
namespace bts {
using namespace blender;
namespace {
std::string key(const TextureSet &set,int role,int tile){return set.id.toString(QUuid::WithoutBraces).toStdString()+"_bake_"+std::to_string(role)+"_"+std::to_string(tile);}
void remember_hash(Image &image,const std::string &hash){IDP_ReplaceInGroup(IDP_EnsureProperties(&image.id),IDP_NewString(hash.c_str(),"bts_validated_bake_hash"));}
bool unchanged_packed(const Image &image,const std::string &hash){const auto *root=image.id.properties;const auto *value=root?IDP_GetPropertyFromGroup(root,"bts_validated_bake_hash"):nullptr;return !image.packedfiles.is_empty()&&value&&value->type==IDP_STRING&&hash==IDP_string_get(value);}
struct Buffer {Image *image;ImBuf *buffer;explicit Buffer(Image *image):image(image),buffer(BKE_image_acquire_ibuf(image,nullptr,nullptr)){}~Buffer(){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);}};
}
void pack_mesh_maps(Main &main,Scene &scene,const TextureDocument &document){
  auto *root=IDP_EnsureProperties(&scene.id);const auto *previous=IDP_GetPropertyFromGroup(root,"bts_mesh_maps");if(previous&&previous->type!=IDP_GROUP)throw std::runtime_error("Invalid baked-map reference group");IDPropertyTemplate empty{};auto *references=IDP_New(IDP_GROUP,&empty,"bts_mesh_maps");
  try{for(const auto &set:document.texture_sets())for(int role=0;role<7;++role)if(set.mesh_maps[role])for(const auto &[tile,plane]:set.mesh_maps[role]->tiles){
    if(!plane.pixels||plane.pixels->resolution!=plane.resolution||plane.pixels->height!=plane.rows()||plane.pixels->hash!=plane.hash)throw std::runtime_error("Baked map data missing or changed");const auto name=key(set,role,tile);const auto *ref=previous?IDP_GetPropertyFromGroup(previous,name):nullptr;auto *id=ref&&ref->type==IDP_ID?IDP_ID_get(ref):nullptr;Image *image=id&&GS(id->name)==ID_IM?reinterpret_cast<Image *>(id):nullptr;
    if(image&&unchanged_packed(*image,plane.hash)){IDPropertyTemplate value{};value.id=&image->id;IDP_ReplaceInGroup(references,IDP_New(IDP_ID,&value,name));continue;}
    if(image){Buffer old(image);if(!old.buffer||old.buffer->x!=plane.resolution||old.buffer->y!=plane.rows())image=nullptr;}
    if(!image){const float zero[4]={0,0,0,0};image=BKE_image_add_generated(&main,plane.resolution,plane.rows(),("BTS Bake "+name).c_str(),32,1,IMA_GENTYPE_BLANK,zero,false,true,false);if(!image)throw std::runtime_error("Cannot allocate native baked map");image->flag|=IMA_HIGH_BITDEPTH;image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;}
    {Buffer held(image);if(!held.buffer||!held.buffer->float_data())throw std::runtime_error("Native baked map buffer missing");std::copy(plane.pixels->rgba.begin(),plane.pixels->rgba.end(),held.buffer->float_data_for_write());BKE_image_mark_dirty(image,held.buffer);}if(!safe_image_memorypack(image))throw std::runtime_error("Cannot pack baked map as OpenEXR");remember_hash(*image,plane.hash);IDPropertyTemplate value{};value.id=&image->id;IDP_ReplaceInGroup(references,IDP_New(IDP_ID,&value,name));
  }}catch(...){IDP_FreeProperty(references);throw;}IDP_ReplaceInGroup(root,references);
}
void read_mesh_maps(const Scene &scene,TextureDocument &document){
  const auto *root=scene.id.properties;const auto *references=root?IDP_GetPropertyFromGroup(root,"bts_mesh_maps"):nullptr;
  for(const auto &description:document.texture_sets()){auto &set=document.set(description.material_session_uid);for(int role=0;role<7;++role)if(set.mesh_maps[role])for(auto &[tile,plane]:set.mesh_maps[role]->tiles){if(!references||references->type!=IDP_GROUP)throw std::runtime_error("Baked map references missing");const auto *ref=IDP_GetPropertyFromGroup(references,key(set,role,tile));auto *id=ref&&ref->type==IDP_ID?IDP_ID_get(ref):nullptr;if(!id||GS(id->name)!=ID_IM)throw std::runtime_error("Baked map image missing");auto *image=reinterpret_cast<Image *>(id);if(image->packedfiles.is_empty())throw std::runtime_error("Baked map is not packed");Buffer held(image);if(!held.buffer||held.buffer->x!=plane.resolution||held.buffer->y!=plane.rows()||!held.buffer->float_data()||held.buffer->channels!=4)throw std::runtime_error("Baked map dimensions changed");const auto count=std::size_t(plane.resolution)*plane.rows()*4;auto data=std::make_shared<MeshMapPixels>(plane.resolution,std::vector<float>(held.buffer->float_data(),held.buffer->float_data()+count),plane.rows());if(data->hash!=plane.hash)throw std::runtime_error("Baked map checksum mismatch");remember_hash(*image,plane.hash);plane.pixels=std::move(data);}}
}
}
