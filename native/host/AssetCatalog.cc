#include "SmartMaterialPreset.hh"
#include "ResourceAvailability.hh"
#include "BlendMaterialBindings.hh"
#include "AssetCatalog.hh"
#include "BuiltinBrushPresets.hh"
#include "BuiltinSmartMasks.hh"
#include "BuiltinProceduralAssets.hh"
#include "BuiltinStandardMaterials.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialImageDependencies.hh"
#include "SpsmAssetDispatch.hh"
#include "MaterialValuesJson.hh"
#include "FillProjection.hh"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <atomic>
#include <set>
#include <stdexcept>
namespace bts {
namespace {std::atomic_uint64_t asset_revision{1};}

FillProjection asset_fill_projection(const StudioAsset &asset,bool automatic){auto p=asset.fill_preset.value_or(FillProjection{});if(asset.physical_size_cm){p.asset_size_cm=*asset.physical_size_cm;p.custom_size=false;p.physical=automatic;}validate_fill_projection(p);return p;}
QString StudioAsset::hash() const{
 if(!texture_files.empty())return texture_sources_hash(texture_files);
 if(cached_hash_.isEmpty()||hashed_bytes_.constData()!=bytes.constData()||hashed_bytes_.size()!=bytes.size()){
  cached_hash_=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());hashed_bytes_=bytes;
 }
 return cached_hash_;
}
void bind_verified_spsm_source(StudioAsset &asset,const QByteArray &canonical,const QString &sha256){
 if(!is_spsm_archive_asset(asset)||canonical.isEmpty()||sha256.size()!=64||std::any_of(sha256.begin(),sha256.end(),[](QChar c){return !((c>='0'&&c<='9')||(c>='a'&&c<='f'));}))throw std::invalid_argument("Invalid verified SPSM source identity");
 const bool current=!asset.cached_hash_.isEmpty()&&asset.hashed_bytes_.constData()==asset.bytes.constData()&&asset.hashed_bytes_.size()==asset.bytes.size();
 if(current&&asset.cached_hash_!=sha256)throw std::invalid_argument("Verified SPSM digest does not match the current source");
 // The worker proved canonical equality before publishing its identity token.
 // No hashing or memcmp belongs in this GUI COW ownership transfer.
 asset.bytes=canonical;asset.hashed_bytes_=canonical;asset.cached_hash_=sha256;
}
void hydrate_asset_presets(StudioAsset &asset){
 const bool brush=asset.usages.contains("brush");
 if(brush){if(asset.scope==AssetScope::Builtin&&asset.bytes.isEmpty())return;const auto hash=asset.hash();if(asset.brush_preset&&asset.hydrated_hash_==hash)return;auto preset=read_brush_preset(asset.bytes);asset.brush_preset=std::move(preset.brush);asset.hydrated_hash_=hash;return;}
 const bool mask=asset.usages.contains("smartmask"),material=asset.usages.contains("smartmaterial");if(!mask&&!material)return;
 // A cold SPSM can be hundreds of MiB. The host dispatcher performs a cheap
 // GUI identity lookup and defers IO/digests/preparation to its worker cache.
 if(is_spsm_archive_asset(asset)){
  asset.smart_material=is_spsm_asset(asset);
  if(!hydrate_spsm_asset(asset)){asset.material_preset.reset();asset.smart_mask.reset();}
  asset.hydrated_hash_.clear();asset.hydrated_material_=nullptr;return;
 }
 const auto hash=asset.hash();if(asset.hydrated_hash_==hash&&asset.hydrated_mask_==asset.smart_mask.get()&&asset.hydrated_material_==asset.material_preset.get()&&((mask&&asset.smart_mask)||(material&&asset.material_preset)))return;
 if(mask)asset.smart_mask=std::make_shared<const LayerMask>(read_mask_preset(asset.bytes));
 if(material){asset.material_preset=std::make_shared<const SmartMaterialPreset>(read_material_preset(asset.bytes));asset.smart_material=true;}
 asset.hydrated_hash_=hash;asset.hydrated_mask_=asset.smart_mask.get();asset.hydrated_material_=asset.material_preset.get();
}
QString asset_scope_name(AssetScope scope){return QStringList{"builtin","session","project","user"}.at(int(scope));}
QJsonObject asset_metadata(const StudioAsset &asset){QJsonObject result{{"id",asset.id.toString(QUuid::WithoutBraces)},{"name",asset.name},{"filename",asset.filename},{"path",asset.path},{"usages",QJsonArray::fromStringList(asset.usages)},{"sha256",asset.hash()},{"physical_size_cm",asset.physical_size_cm?QJsonValue(QJsonArray{(*asset.physical_size_cm)[0],(*asset.physical_size_cm)[1],(*asset.physical_size_cm)[2]}):QJsonValue(QJsonValue::Null)}};if(!asset.texture_files.empty())result["texture_files"]=texture_sources_json(asset.texture_files);if(asset.material)result["material"]=material_values_json(*asset.material);if(asset.material&&asset.fill_preset&&asset.search_terms.contains("blender-images"))result["material_textures"]=blend_material_bindings_json(*asset.fill_preset);if(is_native_material_asset(asset)){if(!asset.material||!asset.fill_preset||!asset.fill_preset->native_material)throw std::invalid_argument("Native material resource interface is missing");require_native_source_policy(*asset.fill_preset->native_material);result["native_material"]=native_material_fill_json(*asset.fill_preset);}if(!asset.search_terms.isEmpty())result["tags"]=QJsonArray::fromStringList(asset.search_terms);return result;}
StudioAsset asset_metadata_read(const QJsonObject &object){
  StudioAsset asset;asset.id=QUuid(object["id"].toString());asset.name=object["name"].toString();asset.filename=object["filename"].toString();asset.path=object["path"].toString();const auto usages=object["usages"].toArray();
  if(asset.id.isNull()||asset.name.isEmpty()||asset.name.contains(QChar(0))||asset.filename.isEmpty()||asset.filename.contains('/')||asset.filename.contains('\\')||asset.filename.contains(QChar(0))||asset.path.contains(QChar(0))||usages.isEmpty())throw std::invalid_argument("Invalid resource metadata");
  const ResourceAvailability text_memory;std::uint64_t text_units=resource_sum(resource_sum(asset.name.size(),asset.filename.size()),asset.path.size());
  for(const auto &usage:usages){const auto value=usage.toString();if(!QStringList{"alpha","texture","environment","procedural","material","smartmask","smartmaterial","grunge","hardsurface","skin","filter","brush","particle","tool","colorprofile"}.contains(value)||asset.usages.contains(value))throw std::invalid_argument("Invalid resource usage");asset.usages.append(value);}
  asset.smart_material=asset.usages.contains("smartmaterial");
  if((asset.smart_material||asset.usages.contains("smartmask")||asset.usages.contains("brush"))&&asset.usages.size()!=1)throw std::invalid_argument("Preset resources require one dedicated usage");
  if(object.contains("texture_files")){if(!asset.usages.contains("texture"))throw std::invalid_argument("Disk texture files require Texture usage");asset.texture_files=read_texture_sources(object["texture_files"]);if(texture_sources_hash(asset.texture_files)!=object["sha256"].toString())throw std::invalid_argument("Texture file manifest checksum mismatch");}
  if(object.contains("material")){if(!asset.usages.contains("material"))throw std::invalid_argument("Material values require material usage");asset.material=read_material_values(object["material"]);}
  if(object.contains("tags")){if(!object["tags"].isArray())throw std::invalid_argument("Invalid resource tags");for(const auto &entry:object["tags"].toArray()){const auto tag=entry.toString();if(!entry.isString()||tag.isEmpty()||std::any_of(tag.begin(),tag.end(),[](QChar c){return c.unicode()<32||c.unicode()==127;})||asset.search_terms.contains(tag))throw std::invalid_argument("Invalid resource tag");text_units=resource_sum(text_units,tag.size());asset.search_terms.append(tag);}}
  if(object.contains("material_textures")){if(!asset.material||!asset.filename.endsWith(".btsuniform")||!asset.search_terms.contains("blender-images"))throw std::invalid_argument("Material textures require a Blender channel preset");asset.fill_preset=read_blend_material_bindings(object["material_textures"]);}
  if(object.contains("native_material")){
   if(!is_native_material_asset(asset)||!asset.material||!asset.search_terms.contains("blender-native")||object.contains("material_textures"))throw std::invalid_argument("Native interface requires a native Blender material archive");
   FillProjection fill;fill.material_resource=asset.id.toString(QUuid::WithoutBraces).toStdString();read_native_material_fill(fill,object["native_material"]);if(!fill.native_material)throw std::invalid_argument("Native material interface is empty");require_native_source_policy(*fill.native_material);asset.fill_preset=std::move(fill);
  }else if(is_native_material_asset(asset))throw std::invalid_argument("Native material archive has no interface");
  const auto physical=object["physical_size_cm"];if(!physical.isUndefined()&&!physical.isNull()){if(!physical.isArray()||physical.toArray().size()!=3)throw std::invalid_argument("Invalid physical resource dimensions");std::array<double,3> size;for(int i=0;i<3;++i){const auto value=physical.toArray()[i];if(!value.isDouble()||!std::isfinite(value.toDouble())||value.toDouble()<1e-6||value.toDouble()>1e9)throw std::invalid_argument("Invalid physical resource dimensions");size[i]=value.toDouble();}asset.physical_size_cm=size;}
  const auto hash=object["sha256"].toString();if(hash.size()!=64||QByteArray::fromHex(hash.toLatin1()).size()!=32)throw std::invalid_argument("Invalid resource hash");return asset;
}
namespace {
void write_bytes(const QString &path,const QByteArray &bytes){QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("Cannot write resource library");}
void validate_resources(const std::vector<StudioAsset> &resources){
 std::set<QUuid> ids;std::set<std::pair<const void *,qsizetype>> buffers;std::set<const AlphaBitmap *> alphas;std::uint64_t total=resource_product(resources.size(),sizeof(StudioAsset)+12*sizeof(void *));

 for(const auto &asset:resources){if(!ids.insert(asset.id).second)throw std::invalid_argument("Duplicate resource identity");
  const auto characters=resource_sum(resource_sum(asset.name.size(),asset.filename.size()),asset.path.size());
  total=resource_sum(total,resource_product(characters,sizeof(QChar)));
  total=resource_sum(total,resource_product(asset.texture_files.size(),sizeof(TextureSourceFile)));
  for(const auto &file:asset.texture_files)total=resource_sum(total,resource_product(resource_sum(resource_sum(file.filename.size(),file.path.size()),file.sha256.size()),sizeof(QChar)));
  for(const auto &tag:asset.search_terms)total=resource_sum(total,resource_sum(sizeof(QString),resource_product(tag.size(),sizeof(QChar))));
  if(buffers.emplace(asset.bytes.constData(),asset.bytes.size()).second)total=resource_sum(total,asset.bytes.size());
  if(asset.alpha&&asset.alpha->bitmap&&alphas.insert(asset.alpha->bitmap.get()).second)total=resource_sum(total,resource_product(asset.alpha->bitmap->pixels.size(),sizeof(float)));
  if(asset.brush_preset){const auto account=[&](const BrushAlpha &a){if(a.bitmap&&alphas.insert(a.bitmap.get()).second)total=resource_sum(total,resource_product(a.bitmap->pixels.size(),sizeof(float)));};account(asset.brush_preset->alpha);if(asset.brush_preset->stencil.resource)account(*asset.brush_preset->stencil.resource);if(asset.brush_preset->projected&&asset.brush_preset->projected->transform.resource)account(*asset.brush_preset->projected->transform.resource);}

 }

}
}
AssetCatalog::AssetCatalog(QString directory):directory_(std::move(directory)){
  {StudioAsset asset;asset.name="Square To Circle";asset.filename="square-to-circle.builtin";asset.scope=AssetScope::Builtin;asset.usages={"alpha"};asset.search_terms={"dynamicstroke"};BrushAlpha alpha;alpha.kind=AlphaKind::DynamicOutline;asset.alpha=alpha;assets_.push_back(std::move(asset));}
  for(int kind=0;kind<2;++kind){StudioAsset asset;asset.name=kind==0?"Shape":"Sign Plus";asset.filename=asset.name+".builtin";asset.scope=AssetScope::Builtin;asset.usages={"alpha"};BrushAlpha alpha;alpha.kind=kind==0?AlphaKind::Shape:AlphaKind::Plus;asset.alpha=alpha;assets_.push_back(std::move(asset));}
  for(int simple=0;simple<2;++simple){StudioAsset asset;asset.name=simple?"Atom Simple":"Atom";asset.filename=asset.name+".builtin";asset.scope=AssetScope::Builtin;asset.usages={"alpha","procedural"};auto bitmap=std::make_shared<AlphaBitmap>();bitmap->width=bitmap->height=128;bitmap->name=asset.name.toStdString();bitmap->pixels.resize(128*128);FillProjection p;p.middle_dot=!simple;p.border_dots=!simple;for(int y=0;y<128;++y)for(int x=0;x<128;++x)bitmap->pixels[y*128+x]=fill_uv_sample(p,x,y,128);BrushAlpha alpha;alpha.kind=AlphaKind::Bitmap;alpha.bitmap=bitmap;asset.alpha=alpha;assets_.push_back(std::move(asset));}
  for(auto preset:builtin_brush_presets()){StudioAsset asset;asset.id=QUuid::createUuidV5(QUuid("{557ad8f1-4d31-4d95-8b22-e1a56ca507b3}"),preset.name.toUtf8());asset.name=preset.name;asset.filename=QString("stock-brush-%1.btsbrush").arg(assets_.size());asset.scope=AssetScope::Builtin;asset.usages={"brush"};asset.brush_preset=std::move(preset.brush);asset.bytes=write_brush_preset({asset.name,*asset.brush_preset});hydrate_asset_presets(asset);assets_.push_back(std::move(asset));}
  const auto &masks=builtin_smart_mask_assets();assets_.insert(assets_.end(),masks.begin(),masks.end());
}
void AssetCatalog::add(StudioAsset asset){asset.revision=asset_revision.fetch_add(1);
  hydrate_asset_presets(asset);
  if(asset.name.isEmpty()||asset.id.isNull()||((asset.scope==AssetScope::Project||asset.scope==AssetScope::User)&&asset.bytes.isEmpty()&&asset.texture_files.empty()))throw std::invalid_argument("Resource has no name or source bytes");asset_metadata_read(asset_metadata(asset));auto candidate=assets_;candidate.push_back(asset);validate_resources(candidate);
  if(asset.scope==AssetScope::User&&asset.texture_files.empty()){QDir dir(directory_);if(!dir.mkpath("blobs"))throw std::runtime_error("Cannot create user resource library");write_bytes(dir.filePath("blobs/"+asset.hash()),asset.bytes);}
  const auto previous=assets_;assets_=std::move(candidate);try{if(asset.scope==AssetScope::User)save_user_library();}catch(...){assets_=previous;throw;}
}
PreparedUserAsset AssetCatalog::prepare_user(StudioAsset asset,QString directory){
 if(asset.scope!=AssetScope::User||asset.name.isEmpty()||asset.id.isNull()||(asset.bytes.isEmpty()&&asset.texture_files.empty()))throw std::invalid_argument("Invalid prepared user resource");
 hydrate_asset_presets(asset);
 asset_metadata_read(asset_metadata(asset));validate_resources({asset});QDir dir(directory);
 if(!dir.mkpath("blobs"))throw std::runtime_error("Cannot create user resource library");
 if(asset.texture_files.empty())write_bytes(dir.filePath("blobs/"+asset.hash()),asset.bytes);
 else for(auto &file:asset.texture_files){
   const auto destination=dir.absoluteFilePath("blobs/"+file.sha256+"."+QFileInfo(file.filename).suffix());
   QFile source(file.path);if(!source.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read imported texture");
   QSaveFile output(destination);if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot retain imported texture");
   QCryptographicHash hash(QCryptographicHash::Sha256);
   while(!source.atEnd()){const auto bytes=source.read(1024*1024);if(bytes.isEmpty()&&source.error()!=QFileDevice::NoError)throw std::runtime_error("Cannot read imported texture");hash.addData(bytes);if(output.write(bytes)!=bytes.size())throw std::runtime_error("Cannot retain imported texture");}
   if(QString::fromLatin1(hash.result().toHex())!=file.sha256)throw std::runtime_error("Texture changed during import");
   if(!output.commit())throw std::runtime_error("Cannot commit imported texture");file.path=destination;
 }
 if(!asset.texture_files.empty())asset.path=asset.texture_files.front().path;
 return PreparedUserAsset(std::move(asset),std::move(directory));
}
void AssetCatalog::add_prepared(PreparedUserAsset prepared){
 if(QDir(prepared.directory_).absolutePath()!=QDir(directory_).absolutePath())throw std::invalid_argument("Prepared resource belongs to another library");
 auto asset=std::move(prepared.asset_);asset.revision=asset_revision.fetch_add(1);auto candidate=assets_;candidate.push_back(std::move(asset));validate_resources(candidate);
 const auto previous=assets_;assets_=std::move(candidate);try{save_user_library();}catch(...){assets_=previous;throw;}
}
QUuid AssetCatalog::retain_project(const StudioAsset &asset){
 validate_native_material_dependencies(asset,assets_);
 const auto hash=asset.hash();for(const auto &existing:assets_)if(existing.scope==AssetScope::Project&&existing.hash()==hash&&existing.usages==asset.usages&&(!is_spsm_archive_asset(asset)||spsm_policy_tags(existing)==spsm_policy_tags(asset)))return existing.id;
 const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto &existing){return existing.id==asset.id&&existing.hash()==hash;});
 if(found==assets_.end())throw std::invalid_argument("Project resource must originate from the validated catalog");
 auto retained=*found;
 hydrate_asset_presets(retained);
 if(retained.material&&retained.fill_preset&&retained.search_terms.contains("blender-images")){
  // Copy descriptors before recursive retention can reallocate the catalog.
  std::map<std::string,StudioAsset> dependencies;
  for(int channel=0;channel<6;++channel)if(const auto &texture=fill_channel_texture(*retained.fill_preset,channel)){
   const auto source=std::find_if(assets_.begin(),assets_.end(),[&](const auto &candidate){return candidate.id.toString(QUuid::WithoutBraces).toStdString()==texture->asset&&candidate.usages.contains("texture")&&candidate.hash().toStdString()==texture->hash;});
   if(source==assets_.end())throw std::invalid_argument("Material image resource is missing from the catalog");dependencies.try_emplace(texture->asset,*source);
  }
  std::map<std::string,QUuid> retained_ids;for(const auto &[id,source]:dependencies)retained_ids[id]=retain_project(source);
  for(int channel=0;channel<6;++channel){auto &texture=fill_channel_texture(*retained.fill_preset,channel);if(texture)texture=rebind_material_texture(*texture,retained_ids.at(texture->asset));}
 }
 if(is_native_material_asset(retained)){
  const auto bundle=read_native_material_bundle(retained.bytes);std::map<QString,StudioAsset> dependencies;
  for(const auto &reference:bundle.images){const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto &source){return source.usages.contains("texture")&&source.hash()==reference.sha256;});if(found==assets_.end())throw std::invalid_argument((reference.image+": native material source texture missing from catalog").toStdString());dependencies.try_emplace(reference.sha256,*found);}
  for(const auto &[hash,source]:dependencies)retain_project(source);
 }
 retained.id=QUuid::createUuid();if(is_native_material_asset(retained)&&retained.fill_preset)retained.fill_preset->material_resource=retained.id.toString(QUuid::WithoutBraces).toStdString();retained.scope=AssetScope::Project;if((retained.smart_mask||retained.material_preset)&&!is_spsm_archive_asset(retained)&&!retained.search_terms.contains("internal:smart-mask-textures"))retained.search_terms.append("internal:smart-mask-textures");retained.revision=asset_revision.fetch_add(1);const auto id=retained.id;auto candidate=assets_;candidate.push_back(std::move(retained));validate_resources(candidate);assets_=std::move(candidate);return id;
}
std::optional<StudioAsset> AssetCatalog::hydrate_preset(const QUuid &id,const QString &expected_hash){
 const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto &asset){return asset.id==id;});
 if(found==assets_.end()||found->hash()!=expected_hash)return {};
 hydrate_asset_presets(*found);if(!found->material_preset&&!found->smart_mask&&!found->brush_preset)return {};return *found;
}
void AssetCatalog::rename(const QUuid &id,const QString &value){const auto name=value.trimmed();if(name.isEmpty()||name.contains(QChar::Null))throw std::invalid_argument("Invalid asset name");const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto &a){return a.id==id;});if(found==assets_.end()||found->scope==AssetScope::Builtin)throw std::invalid_argument("Built-in resource names are read-only");const auto previous=*found;found->name=name;found->revision=asset_revision.fetch_add(1);try{if(found->scope==AssetScope::User)save_user_library();}catch(...){*found=previous;throw;}}
bool AssetCatalog::replace(StudioAsset asset,const QString &expected_hash){asset.revision=asset_revision.fetch_add(1);
  const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto &a){return a.id==asset.id;});
  if(found==assets_.end()||found->hash()!=expected_hash)return false;
  if(found->scope==AssetScope::Builtin||found->scope!=asset.scope||(asset.bytes.isEmpty()&&asset.texture_files.empty()))throw std::invalid_argument("Invalid resource replacement");
  hydrate_asset_presets(asset);asset_metadata_read(asset_metadata(asset));auto candidate=assets_;candidate[found-assets_.begin()]=asset;validate_resources(candidate);
  if(asset.scope==AssetScope::User&&asset.texture_files.empty()){QDir dir(directory_);if(!dir.mkpath("blobs"))throw std::runtime_error("Cannot create user resource library");write_bytes(dir.filePath("blobs/"+asset.hash()),asset.bytes);}
  const auto previous=assets_;assets_=std::move(candidate);try{if(asset.scope==AssetScope::User)save_user_library();}catch(...){assets_=previous;throw;}return true;
}
bool AssetCatalog::replace_prepared(PreparedUserAsset prepared,const QString &expected_hash,std::uint64_t expected_revision){
 if(QDir(prepared.directory_).absolutePath()!=QDir(directory_).absolutePath())throw std::invalid_argument("Prepared resource belongs to another library");
 auto asset=std::move(prepared.asset_);const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto &a){return a.id==asset.id;});
 if(found==assets_.end()||found->hash()!=expected_hash||found->revision!=expected_revision)return false;
 if(found->scope!=AssetScope::User||asset.scope!=AssetScope::User)throw std::invalid_argument("Only User resources can be overwritten");
 asset.revision=asset_revision.fetch_add(1);auto candidate=assets_;candidate[found-assets_.begin()]=std::move(asset);validate_resources(candidate);const auto previous=assets_;assets_=std::move(candidate);try{save_user_library();}catch(...){assets_=previous;throw;}return true;
}
bool AssetCatalog::remove_user(const QUuid &id,const QString &expected_hash,std::uint64_t expected_revision){
 const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto &a){return a.id==id;});if(found==assets_.end()||found->hash()!=expected_hash||found->revision!=expected_revision)return false;if(found->scope!=AssetScope::User)throw std::invalid_argument("Only User resources can be deleted");const auto previous=assets_;assets_.erase(found);try{save_user_library();}catch(...){assets_=previous;throw;}return true;
}
void AssetCatalog::save_user_library() const {if(!QDir().mkpath(directory_))throw std::runtime_error("Cannot create resource library");QJsonArray entries;for(const auto &asset:assets_)if(asset.scope==AssetScope::User)entries.append(asset_metadata(asset));write_bytes(QDir(directory_).filePath("library.json"),QJsonDocument(QJsonObject{{"version",1},{"resources",entries}}).toJson());}
void AssetCatalog::load_user_library(){
  QFile file(QDir(directory_).filePath("library.json"));if(!file.exists())return;if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read user resource library");QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);const auto root=document.object();if(error.error!=QJsonParseError::NoError||root["version"].toInt()!=1||!root["resources"].isArray())throw std::runtime_error("Invalid user resource library");auto candidate=assets_;std::erase_if(candidate,[](const auto &a){return a.scope==AssetScope::User;});
  for(const auto &entry:root["resources"].toArray()){const auto metadata=entry.toObject();auto asset=asset_metadata_read(metadata);asset.revision=asset_revision.fetch_add(1);if(!asset.texture_files.empty()){asset.scope=AssetScope::User;candidate.push_back(std::move(asset));continue;}QFile source(QDir(directory_).filePath("blobs/"+metadata["sha256"].toString()));if(!source.open(QIODevice::ReadOnly)||source.size()<=0)throw std::runtime_error("User resource bytes missing");asset.bytes=source.readAll();if(asset.hash()!=metadata["sha256"].toString())throw std::runtime_error("User resource checksum mismatch");asset.scope=AssetScope::User;hydrate_asset_presets(asset);candidate.push_back(std::move(asset));}resolve_user_material_textures(candidate);validate_resources(candidate);assets_=std::move(candidate);
}
void AssetCatalog::replace_project(std::vector<StudioAsset> project){auto candidate=assets_;std::erase_if(candidate,[](const auto &a){return a.scope==AssetScope::Project;});for(auto &asset:project){asset.revision=asset_revision.fetch_add(1);asset.scope=AssetScope::Project;hydrate_asset_presets(asset);candidate.push_back(std::move(asset));}validate_resources(candidate);assets_=std::move(candidate);}
std::vector<StudioAsset> AssetCatalog::project_assets() const{
 std::vector<StudioAsset> result;for(const auto &asset:assets_)if(asset.scope==AssetScope::Project)result.push_back(asset);
 // A packed project must retain the exact immutable original Reset identity,
 // even when it came from Session/User scope. Export COW descriptors only;
 // don't change the catalog's original scope or manufacture duplicate blobs.
 const auto add=[&](const StudioAsset &source){if(std::none_of(result.begin(),result.end(),[&](const auto &a){return a.id==source.id;})){auto copy=source;copy.scope=AssetScope::Project;result.push_back(std::move(copy));}};
 for(const auto &asset:assets_)if(asset.scope==AssetScope::Project&&is_native_material_asset(asset)){
  validate_native_material_dependencies(asset,assets_);
  if(const auto *original=native_material_original_source(asset,assets_)){
   add(*original);for(const auto &ref:read_native_material_bundle(original->bytes).images){if(std::any_of(result.begin(),result.end(),[&](const auto &a){return a.usages.contains("texture")&&a.hash()==ref.sha256;}))continue;const auto image=std::find_if(assets_.begin(),assets_.end(),[&](const auto &a){return a.usages.contains("texture")&&a.hash()==ref.sha256;});add(*image);}
  }
 }
 return result;
}
QString AssetCatalog::materialize(const StudioAsset &asset) const{if(!asset.texture_files.empty()){const auto &file=asset.texture_files.front();if(!QFileInfo(file.path).isFile())throw std::runtime_error((file.filename+": source file missing").toStdString());return file.path;}if(asset.bytes.isEmpty())throw std::runtime_error("Resource has no file content");QDir dir(directory_);if(!dir.mkpath("cache"))throw std::runtime_error("Cannot create resource cache");const auto path=dir.filePath("cache/"+asset.hash()+"_"+asset.filename);write_bytes(path,asset.bytes);return path;}
}
