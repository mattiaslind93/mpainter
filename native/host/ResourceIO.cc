#include <QStandardPaths>
#include "SpsmAssetDispatch.hh"
#include "MaskResourceBank.hh"
#include "ResourceIO.hh"
#include "NativeMaterialImageDependencies.hh"
#include "BitmapMask.hh"
#include "FillTextureIO.hh"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include "BKE_idprop.hh"
#include "BKE_blendfile.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include <string_view>
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_ID.h"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"
#include "MEM_guardedalloc.h"
namespace bts {
using namespace blender;
namespace {
Image *referenced_image(IDProperty *group,const std::string &key){auto *ref=group?IDP_GetPropertyFromGroup(group,key):nullptr;auto *id=ref&&ref->type==IDP_ID?IDP_ID_get(ref):nullptr;return id&&GS(id->name)==ID_IM?reinterpret_cast<Image *>(id):nullptr;}
void reference(IDProperty *group,const std::string &key,Image *image){IDPropertyTemplate value{};value.id=&image->id;IDP_ReplaceInGroup(group,IDP_New(IDP_ID,&value,key));}
Image *new_resource_image(Main &main,const std::string &key,const QString &filename,bke::blendfile::PartialWriteContext *snapshot){const auto path=("//bts_resources/"+QString::fromStdString(key)+"/"+filename).toUtf8();if(path.size()>=qsizetype(sizeof(Image::filepath)))throw std::length_error((QString("Packed resource path needs %1 UTF-8 bytes; Image.filepath stores at most %2 bytes plus NUL").arg(path.size()).arg(sizeof(Image::filepath)-1)).toStdString());const float clear[4]={};auto *image=snapshot?reinterpret_cast<Image *>(snapshot->id_create(ID_IM,"BTS Resource "+key,nullptr,{})):BKE_image_add_generated(&main,1,1,("BTS Resource "+key).c_str(),32,1,IMA_GENTYPE_BLANK,clear,false,true,false);if(!image)throw std::runtime_error("Cannot allocate project resource image");if(!snapshot)id_us_min(&image->id); /* Manifest ID properties own the references. */ BKE_image_free_buffers(image);image->source=IMA_SRC_FILE;STRNCPY(image->filepath,path.constData());return image;}
QString resource_cache_file(const TextureSourceFile &file){const QString root=qEnvironmentVariable("BTS_RESOURCE_CACHE",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/resources");const auto folder=QDir(root).filePath(file.sha256);if(!QDir().mkpath(folder))throw std::runtime_error("Cannot create packed texture cache");return QDir(folder).filePath(file.filename);}
bool matches_source(const Image *image,const QString &sha){if(!image)return false;const auto *properties=image->id.properties;const auto *property=properties?IDP_GetPropertyFromGroup(properties,"bts_source_sha256"):nullptr;return property&&property->type==IDP_STRING&&QString::fromUtf8(IDP_string_get(property))==sha;}
// Legacy packed resources predate the identity hint. Their validated bytes are
// already present in the detached snapshot: reuse that one payload rather than
// create another 179 MiB image solely because an optional hint is absent.
bool matches_legacy_bytes(Image *image,const QByteArray &bytes){
 if(!image||image->packedfiles.first!=image->packedfiles.last||!image->packedfiles.first)return false;
 const auto *packed=static_cast<ImagePackedFile *>(image->packedfiles.first)->packedfile;
 return packed&&packed->size==bytes.size()&&std::equal(bytes.constData(),bytes.constData()+bytes.size(),static_cast<const char *>(packed->data));
}
void source_identity(Image *image,const QString &sha){IDP_ReplaceInGroup(IDP_EnsureProperties(&image->id),IDP_NewString(sha.toUtf8().constData(),"bts_source_sha256"));}
const PackedFile *packed_source(const Image *image,const QString &name){const auto *part=image?static_cast<const ImagePackedFile *>(image->packedfiles.first):nullptr;if(!part||!part->packedfile||part->packedfile->size<=0)throw std::runtime_error((name+": packed source is missing").toStdString());return part->packedfile;}
}
void pack_project_assets(Main &main,Scene &scene,const std::vector<StudioAsset> &assets,ProjectResourceImages *images,bke::blendfile::PartialWriteContext *snapshot){
 if(snapshot && &snapshot->bmain!=&main)throw std::logic_error("Resource snapshot Main differs");ProjectResourceImages associations;
 for(const auto &asset:assets)if(asset.scope==AssetScope::Project)validate_native_material_dependencies(asset,assets);
 auto *root=IDP_EnsureProperties(&scene.id);auto *previous=IDP_GetPropertyFromGroup(root,"bts_resource_images");if(previous&&previous->type!=IDP_GROUP)throw std::runtime_error("Invalid project resource group");IDPropertyTemplate empty{};auto *references=IDP_New(IDP_GROUP,&empty,"bts_resource_images");QJsonArray metadata;std::map<QString,Image *> content;std::vector<Image *> created;
 const auto allocate_resource=[&](const std::string &key,const QString &filename){auto *image=new_resource_image(main,key,filename,snapshot);try{created.push_back(image);}catch(...){BKE_id_delete(&main,image);throw;}return image;};
 const auto rollback=[&]{IDP_FreeProperty(references);for(auto *image:created)if(image->id.us==0)BKE_id_delete(&main,image);};
 std::set<Image *> former; if(previous)for(IDProperty &property:previous->data.group){if(property.type==IDP_ID){auto *id=IDP_ID_get(&property);if(id&&GS(id->name)==ID_IM)former.insert(reinterpret_cast<Image *>(id));}}
 try{for(const auto &asset:canonical_mask_resource_banks(assets)){if(asset.scope!=AssetScope::Project)continue;const auto info=asset_metadata(asset);asset_metadata_read(info);const auto key=asset.id.toString(QUuid::WithoutBraces).toStdString();
  if(!asset.texture_files.empty()){
   for(const auto &file:asset.texture_files){const auto tile_key=key+"_"+std::to_string(file.tile);auto found=content.find(file.sha256);auto *image=found==content.end()?referenced_image(previous,tile_key):found->second;if(!matches_source(image,file.sha256))image=nullptr;
    if(!image){
    // PackedFile::size (DNA) and BKE_packedfile_new_from_memory are signed int,
    // even though BKE_image_packfiles_from_mem accepts size_t. This is the
    // encoded Blender ABI limit, not a RAM/history allowance.
    if(file.size>std::numeric_limits<decltype(PackedFile::size)>::max())throw std::length_error("Encoded resource exceeds Blender PackedFile::size (signed 32-bit, maximum 2147483647 encoded bytes per packed file)");
    QFile input(file.path);if(!input.open(QIODevice::ReadOnly)||input.size()!=file.size)throw std::runtime_error((file.filename+": source missing or changed before Save").toStdString());auto *data=MEM_new_array_uninitialized<char>(std::size_t(file.size),__func__);QCryptographicHash digest(QCryptographicHash::Sha256);qint64 offset=0;try{while(offset<file.size){const auto count=input.read(data+offset,std::min<qint64>(4*1024*1024,file.size-offset));if(count<=0)throw std::runtime_error((file.filename+": incomplete source read").toStdString());digest.addData(QByteArrayView(data+offset,count));offset+=count;}if(QString::fromLatin1(digest.result().toHex())!=file.sha256)throw std::runtime_error((file.filename+": source checksum changed before Save").toStdString());image=allocate_resource(tile_key,file.filename);}catch(...){MEM_delete(data);throw;}BKE_image_packfiles_from_mem(nullptr,image,data,file.size);if(image->packedfiles.is_empty())throw std::runtime_error("Cannot pack texture tile");}
    source_identity(image,file.sha256);content[file.sha256]=image;reference(references,tile_key,image);associations.by_key.emplace(tile_key,image);associations.sha256.emplace(tile_key,file.sha256);
   }
  }else{
   if(asset.bytes.isEmpty()||asset.bytes.size()>std::numeric_limits<decltype(PackedFile::size)>::max())throw std::runtime_error("Invalid project resource bytes");const auto hash=info["sha256"].toString();auto found=content.find(hash);auto *image=found==content.end()?referenced_image(previous,key):found->second;if(!matches_source(image,hash)&&!matches_legacy_bytes(image,asset.bytes))image=nullptr;if(!image){image=allocate_resource(key,asset.filename);auto *data=MEM_new_array_uninitialized<char>(asset.bytes.size(),__func__);std::copy_n(asset.bytes.constData(),asset.bytes.size(),data);BKE_image_packfiles_from_mem(nullptr,image,data,asset.bytes.size());if(image->packedfiles.is_empty())throw std::runtime_error("Cannot pack project resource");}source_identity(image,hash);content[hash]=image;reference(references,key,image);associations.by_key.emplace(key,image);associations.sha256.emplace(key,hash);
  }
  metadata.append(info);
 }}catch(...){rollback();throw;}
 QByteArray json;try{
  json=QJsonDocument(QJsonObject{{"version",2},{"resources",metadata}}).toJson(QJsonDocument::Compact);
  if(json.size()>std::numeric_limits<decltype(IDProperty::len)>::max()-1)throw std::length_error("Encoded project manifest exceeds Blender IDProperty::len (signed int including NUL)");

 }catch(...){rollback();throw;}
 IDP_ReplaceInGroup(root,references);IDP_ReplaceInGroup(root,IDP_NewString(json.constData(),"bts_resources"));
 // Retire only former private payloads whose last ID reference was removed.
 // Other material/scene users and fake users keep their images alive.
 if(!snapshot)for(auto *image:former)if(image->id.us==0&&std::string_view(image->id.name+2).starts_with("BTS Resource "))BKE_id_delete(&main,image);
 if(snapshot)for(auto *image:former)if(std::string_view(image->id.name+2).starts_with("BTS Resource ")&&std::none_of(associations.by_key.begin(),associations.by_key.end(),[&](const auto &entry){return entry.second==image;}))associations.retired_uids.push_back(image->id.session_uid);
 if(images)*images=std::move(associations);
}
std::vector<StudioAsset> read_project_assets(const Scene &scene,ProjectResourceImages *images){
 ProjectResourceImages associations;
 const auto *root=scene.id.properties;const auto *property=root?IDP_GetPropertyFromGroup(root,"bts_resources"):nullptr;if(!property){if(images)*images={};return {};}if(property->type!=IDP_STRING||property->len<1)throw std::runtime_error("Invalid project resource manifest");const auto *manifest=IDP_string_get(property);if(!manifest||manifest[property->len-1]!=0)throw std::runtime_error("Invalid project resource manifest terminator");
 // IDProperty::len is the real signed-int representation (including NUL).
 // Decode the exact span without an unbounded strlen or another encoded copy.
 QJsonParseError error;const auto document=QJsonDocument::fromJson(QByteArray::fromRawData(manifest,property->len-1),&error);const auto metadata=document.object();if(error.error!=QJsonParseError::NoError||(metadata["version"].toInt()!=1&&metadata["version"].toInt()!=2)||!metadata["resources"].isArray())throw std::runtime_error("Unsupported project resource manifest");const auto *references=IDP_GetPropertyFromGroup(root,"bts_resource_images");if(!references||references->type!=IDP_GROUP)throw std::runtime_error("Project resource references missing");
 const auto resources=metadata["resources"].toArray();
 std::vector<StudioAsset> result;result.reserve(resources.size());std::set<QUuid> ids;std::uint64_t total=0;std::map<const Image *,std::pair<QByteArray,QString>> encoded_cache;
 for(const auto &entry:resources){const auto object=entry.toObject();auto asset=asset_metadata_read(object);if(!ids.insert(asset.id).second)throw std::runtime_error("Duplicate project resource identity");const auto key=asset.id.toString(QUuid::WithoutBraces).toStdString();
  if(!asset.texture_files.empty()){
   for(auto &file:asset.texture_files){auto *image=referenced_image(const_cast<IDProperty *>(references),key+"_"+std::to_string(file.tile));const auto *packed=packed_source(image,file.filename);if(packed->size!=file.size)throw std::runtime_error((file.filename+": packed tile size mismatch").toStdString());const QByteArrayView bytes(static_cast<const char *>(packed->data),packed->size);if(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=file.sha256)throw std::runtime_error((file.filename+": packed tile checksum mismatch").toStdString());const auto path=resource_cache_file(file);if(QFileInfo(path).size()!=file.size||hash_texture_file(path)!=file.sha256){QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)||output.write(bytes.data(),bytes.size())!=bytes.size()||!output.commit())throw std::runtime_error((file.filename+": cannot hydrate packed texture cache").toStdString());}file.path=path;associations.by_key.emplace(key+"_"+std::to_string(file.tile),image);associations.sha256.emplace(key+"_"+std::to_string(file.tile),file.sha256);
   }
  }else{
   auto *image=referenced_image(const_cast<IDProperty *>(references),key);const auto *packed=packed_source(image,asset.name);auto [cached,fresh]=encoded_cache.try_emplace(image);if(fresh){cached->second.first=QByteArray(static_cast<const char *>(packed->data),packed->size);cached->second.second=QString::fromLatin1(QCryptographicHash::hash(cached->second.first,QCryptographicHash::Sha256).toHex());}asset.bytes=cached->second.first;if(fresh&&!asset.usages.contains("texture"))total=resource_sum(total,std::uint64_t(asset.bytes.size()));if(cached->second.second!=object["sha256"].toString())throw std::runtime_error("Project resource checksum mismatch");if(is_spsm_asset(asset))bind_verified_spsm_source(asset,asset.bytes,cached->second.second);associations.by_key.emplace(key,image);associations.sha256.emplace(key,cached->second.second);
  }
  asset.scope=AssetScope::Project;hydrate_asset_presets(asset);if(asset.usages.contains("alpha")){BrushAlpha alpha;alpha.kind=AlphaKind::Bitmap;alpha.bitmap=read_bitmap_alpha_bytes(asset.bytes,asset.name);total=resource_sum(total,resource_product(alpha.bitmap->pixels.size(),sizeof(float)));asset.alpha=alpha;}result.push_back(std::move(asset));
 }
 for(const auto &asset:result)validate_native_material_dependencies(asset,result);
 resolve_material_asset_textures(result);if(images)*images=std::move(associations);return result;
}
}
