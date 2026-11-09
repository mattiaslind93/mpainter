// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmAssetDispatch.hh"
#include "SmartMaterialPreset.hh"
#include "FillTexturePool.hh"
#include "FillTextureIO.hh"
#include "ImageColorMetadata.hh"
#include "BakedMapShelf.hh"
#include "FillTextureJSON.hh"
#include "ImageSequence.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "BLI_task.hh"
#include "BLI_index_range.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "IMB_metadata.hh"
#include <OpenImageIO/sysutil.h>
#include "Preferences.hh"
#include <QDateTime>
#include <QUuid>
#include <atomic>
#include <future>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <tuple>
namespace bts {
namespace {
using namespace blender;
using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
constexpr auto decode_flags=ImBufFlags::AlphaChannelPacked|ImBufFlags::NoColorspaceConvert|ImBufFlags::Metadata;
struct Source {int tile=0;QString filename,path,hash;QByteArray bytes;int width=0,height=0;std::shared_ptr<const std::vector<FillTexture::Pixel>> cached_pixels;};
Buffer load(const Source &source,bool header){const auto flags=header?decode_flags|ImBufFlags::Test:decode_flags;if(!source.path.isEmpty()){const auto path=source.path.toUtf8();return {IMB_load_image_from_filepath(path.constData(),flags),IMB_freeImBuf};}const auto name=source.filename.toUtf8();return {IMB_load_image_from_memory(reinterpret_cast<const unsigned char *>(source.bytes.constData()),source.bytes.size(),flags,name.constData(),name.constData()),IMB_freeImBuf};}
void check_cancel(const TextureDecodeControl &control){if(control.cancel&&control.cancel->load())throw std::runtime_error("Texture load cancelled");}
std::string canonical_space(const std::string &name){const auto *space=IMB_colormanagement_space_get_named(name.c_str());if(!space)space=IMB_colormanagement_space_from_interop_id(name);if(!space)throw std::invalid_argument("Unknown input color space/interop ID: "+name);return IMB_colormanagement_colorspace_get_name(space);}
std::shared_ptr<const FillTexture> decode_uncached(const StudioAsset &asset,const Source &source,int index,int total,bool raw,const std::string &override_space,const TextureDecodeControl &control){
 check_cancel(control);TextureDecodeProgress progress{source.tile,index,total,source.filename,"Reading"};const auto started=performance::Clock::now();if(control.progress)control.progress(progress);
 if(control.verify_files&&!source.path.isEmpty()&&hash_texture_file(source.path,control.cancel)!=source.hash)throw std::runtime_error("Source checksum changed; refresh/reimport this resource");
 auto image=load(source,false);if(!image||image->x!=source.width||image->y!=source.height||image->channels<1||image->channels>4||(!image->float_data()&&!image->byte_data()))throw std::runtime_error("Image is corrupt, unreadable or changed during load");progress.read_ms=performance::ms(started);const auto convert_start=performance::Clock::now();progress.phase="Converting";if(control.progress)control.progress(progress);
 auto texture=std::make_shared<FillTexture>();texture->asset=asset.id.toString(QUuid::WithoutBraces).toStdString();texture->hash=source.hash.toStdString();texture->name=asset.name.toStdString();texture->raw=raw;texture->source_space=override_space;texture->width=image->x;texture->height=image->y;
 auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(image->x)*image->y);const auto *floats=image->float_data();const auto *bytes=image->byte_data();const int channels=image->channels,width=image->x,height=image->y;
 threading::parallel_for(IndexRange(height),32,[&](const IndexRange rows){check_cancel(control);for(int y:rows)for(int x=0;x<width;++x){const auto i=std::size_t(y)*width+x;auto &p=(*pixels)[i];if(floats){const auto *v=floats+i*channels;if(channels<3)p={v[0],v[0],v[0],channels==2?v[1]:1.f};else p={v[0],v[1],v[2],channels==4?v[3]:1.f};}else{const auto *v=bytes+i*4;for(int c=0;c<4;++c)p[c]=v[c]/255.f;}}});
 const auto interop=image_color_metadata(image->metadata,"colorInteropID");std::optional<std::string> metadata_space;if(!raw&&override_space.empty()){metadata_space=image_color_metadata(image->metadata,"oiio:ColorSpace");if(!metadata_space)metadata_space=interop;}
 std::string from;
 if(!raw){if(!override_space.empty())from=canonical_space(override_space);else if(metadata_space&&!metadata_space->empty())from=canonical_space(*metadata_space);else{const char *automatic=floats?IMB_colormanagement_get_float_colorspace(image.get()):IMB_colormanagement_get_byte_colorspace(image.get());if(!automatic||!*automatic)throw std::runtime_error("Image has no source color space");from=canonical_space(automatic);}const auto target=canonical_space("ACEScg");if(from!=target)threading::parallel_for(IndexRange(height),32,[&](const IndexRange rows){check_cancel(control);IMB_colormanagement_transform_float(reinterpret_cast<float *>(pixels->data()+std::size_t(rows.start())*width),width,rows.size(),4,from.c_str(),target.c_str(),false);});}
 texture->input_space=raw?"Non-Color/raw":from;texture->floating_point=bool(floats);texture->data_units=floats&&interop&&*interop=="data";
 std::atomic_bool invalid=false;std::vector<std::pair<float,float>> ranges(height,{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()});
 threading::parallel_for(IndexRange(height),32,[&](const IndexRange rows){check_cancel(control);for(int y:rows)for(int x=0;x<width;++x){const auto &p=(*pixels)[std::size_t(y)*width+x];for(float v:p)if(!std::isfinite(v))invalid=true;if(p[3]<0||p[3]>1)invalid=true;ranges[y].first=std::min(ranges[y].first,p[0]);ranges[y].second=std::max(ranges[y].second,p[0]);}});if(invalid)throw std::runtime_error("Image contains nonfinite values or alpha outside 0–1");texture->range_valid=true;texture->minimum=ranges.front().first;texture->maximum=ranges.front().second;for(const auto &[low,high]:ranges){texture->minimum=std::min(texture->minimum,low);texture->maximum=std::max(texture->maximum,high);}texture->pixels=intern_fill_texture_pixels(*texture,std::move(pixels));texture->validate();progress.convert_ms=performance::ms(convert_start);progress.phase="Ready";progress.input_space=QString::fromStdString(texture->input_space);progress.decoded_bytes=std::uint64_t(width)*height*sizeof(FillTexture::Pixel);if(control.progress)control.progress(progress);std::printf("BTS_TEXTURE_LOAD tile=%d file=%s read_ms=%.3f convert_ms=%.3f decoded_bytes=%llu raw=%d input_space=%s\n",source.tile,qPrintable(source.filename),progress.read_ms,progress.convert_ms,(unsigned long long)progress.decoded_bytes,int(raw),texture->input_space.c_str());return texture;
}
// Shared immutable decoded storage, independent of per-use asset UUID/name.
// Hash includes encoded bit depth and metadata. Raw and color interpretations
// are separate resources; identical usages share one in-flight decode as well.
struct SharedDecodeEntry {
 std::weak_ptr<const std::vector<FillTexture::Pixel>> pixels;
 int width=0,height=0;std::string input_space;bool floating_point=false,data_units=false;float minimum=0,maximum=0;
 std::shared_future<std::shared_ptr<const FillTexture>> inflight;
 bool loading=false;
};
struct SharedDecodeCancelled:std::runtime_error {SharedDecodeCancelled():std::runtime_error("Shared texture decode cancelled") {}};
std::mutex shared_decode_mutex;
std::map<std::string,SharedDecodeEntry> shared_decode_entries;
std::map<QString,QString> verified_source_versions;
void verify_source(const Source &source,const TextureDecodeControl &control){
 if(!control.verify_files||source.path.isEmpty())return;
 const QFileInfo info(source.path);const auto stamp=QString::number(info.size())+":"+QString::number(info.lastModified().toMSecsSinceEpoch())+":"+source.hash;
 {std::lock_guard lock(shared_decode_mutex);const auto found=verified_source_versions.find(info.absoluteFilePath());if(found!=verified_source_versions.end()&&found->second==stamp)return;}
 if(hash_texture_file(source.path,control.cancel)!=source.hash)throw std::runtime_error("Source checksum changed; refresh/reimport this resource");
 const QFileInfo after(source.path);if(after.size()!=info.size()||after.lastModified()!=info.lastModified())throw std::runtime_error("Source changed during checksum validation");
 std::lock_guard lock(shared_decode_mutex);verified_source_versions[info.absoluteFilePath()]=stamp;
}
std::string decode_cache_key(const Source &source,bool raw,const std::string &space){return source.hash.toStdString()+":"+(raw?"raw":"ACEScg")+":"+(space.empty()?"auto":canonical_space(space));}
std::shared_ptr<const FillTexture> decode_one(const StudioAsset &asset,const Source &source,int index,int total,bool raw,const std::string &override_space,const TextureDecodeControl &control){
 const auto key=decode_cache_key(source,raw,override_space);
 const auto borrowed=[&](std::shared_ptr<const std::vector<FillTexture::Pixel>> pixels,int width,int height,const std::string &input,bool floating_point,bool data_units,float minimum,float maximum){
  auto result=std::make_shared<FillTexture>();result->asset=asset.id.toString(QUuid::WithoutBraces).toStdString();result->hash=source.hash.toStdString();result->name=asset.name.toStdString();result->raw=raw;result->source_space=override_space;result->input_space=input;result->width=width;result->height=height;result->pixels=std::move(pixels);result->range_valid=true;result->floating_point=floating_point;result->data_units=data_units;result->minimum=minimum;result->maximum=maximum;result->validate();
  if(control.progress){TextureDecodeProgress progress{source.tile,index,total,source.filename,"Ready"};progress.input_space=QString::fromStdString(input);progress.decoded_bytes=result->pixels->size()*sizeof(FillTexture::Pixel);control.progress(progress);}
  std::printf("BTS_TEXTURE_SHARED reused=1 tile=%d file=%s hash=%s\n",source.tile,qPrintable(source.filename),qPrintable(source.hash));return result;
 };
 for(;;){
  check_cancel(control);verify_source(source,control);
  std::shared_ptr<std::promise<std::shared_ptr<const FillTexture>>> owner;
  std::shared_future<std::shared_ptr<const FillTexture>> waiting;
  std::shared_ptr<const std::vector<FillTexture::Pixel>> pixels;int width=0,height=0;std::string space;bool floating_point=false,data_units=false;float minimum=0,maximum=0;
  {
   std::lock_guard lock(shared_decode_mutex);auto &entry=shared_decode_entries[key];pixels=entry.pixels.lock();
   if(pixels){width=entry.width;height=entry.height;space=entry.input_space;floating_point=entry.floating_point;data_units=entry.data_units;minimum=entry.minimum;maximum=entry.maximum;}
   else if(entry.loading)waiting=entry.inflight;
   else{owner=std::make_shared<std::promise<std::shared_ptr<const FillTexture>>>();waiting=owner->get_future().share();entry.inflight=waiting;entry.loading=true;}
  }
  if(pixels)return borrowed(std::move(pixels),width,height,space,floating_point,data_units,minimum,maximum);
  if(!owner){try{const auto result=waiting.get();check_cancel(control);return borrowed(result->pixels,result->width,result->height,result->input_space,result->floating_point,result->data_units,result->minimum,result->maximum);}catch(const SharedDecodeCancelled &){check_cancel(control);continue;}}
  try{
   auto unchecked=control;unchecked.verify_files=false;auto result=decode_uncached(asset,source,index,total,raw,override_space,unchecked);
   {std::lock_guard lock(shared_decode_mutex);auto &entry=shared_decode_entries.at(key);entry.pixels=result->pixels;entry.width=result->width;entry.height=result->height;entry.input_space=result->input_space;entry.floating_point=result->floating_point;entry.data_units=result->data_units;entry.minimum=result->minimum;entry.maximum=result->maximum;entry.loading=false;entry.inflight={};}
   owner->set_value(result);return result;
  }catch(...){
   const auto failed=(control.cancel&&control.cancel->load())?std::make_exception_ptr(SharedDecodeCancelled{}):std::current_exception();
   {std::lock_guard lock(shared_decode_mutex);auto &entry=shared_decode_entries.at(key);entry.loading=false;entry.inflight={};}
   owner->set_exception(failed);std::rethrow_exception(failed);
  }
 }
}

}
std::uint64_t texture_decode_available_bytes(){
 return texture_available_ram_bytes();
}
std::shared_ptr<const FillTexture> decode_fill_texture(const StudioAsset &asset,bool raw,const std::string &source_space,const TextureDecodeControl &control){
 if((raw&&!source_space.empty())||source_space.find(char(0))!=std::string::npos)throw std::invalid_argument("Data textures must keep Non-Color interpretation");if(!source_space.empty())canonical_space(source_space);if(!asset.usages.contains("texture"))throw std::invalid_argument("Choose a Texture resource");if(is_btsmap(asset.bytes)&&asset.texture_files.empty())return decode_btsmap(asset.id.toString(QUuid::WithoutBraces),asset.bytes,asset.name);
 std::vector<Source> sources;bool sequence=false;
 if(!asset.texture_files.empty()){validate_texture_sources(asset.texture_files);sequence=asset.texture_files.front().tile!=0;for(const auto &file:asset.texture_files){if(!QFileInfo(file.path).isFile()||QFileInfo(file.path).size()!=file.size)throw std::runtime_error((QString("tile %1: %2: source missing or changed").arg(file.tile).arg(file.filename)).toStdString());sources.push_back({file.tile,file.filename,file.path,file.sha256,{}});}}
 else if(is_image_sequence(asset.bytes)){sequence=true;for(const auto &part:read_image_sequence(asset.bytes))sources.push_back({part.tile,part.filename,{},QString::fromLatin1(QCryptographicHash::hash(part.bytes,QCryptographicHash::Sha256).toHex()),part.bytes});}
 else{if(asset.bytes.isEmpty())throw std::invalid_argument("Texture resource has no source file or encoded bytes");sources.push_back({0,asset.filename,{},asset.hash(),asset.bytes});}
 const auto available=texture_decode_available_bytes();const auto memory=available;std::uint64_t permanent=0,scratch=0;std::set<std::string> new_sources;

 for(auto &source:sources){check_cancel(control);auto header=load(source,true);if(!header||header->x<1||header->y<1)throw std::invalid_argument((QString("tile %1: %2: corrupt or unsupported image header").arg(source.tile).arg(source.filename)).toStdString());source.width=header->x;source.height=header->y;
  const auto count=std::uint64_t(header->x)*header->y;if(count>std::numeric_limits<std::size_t>::max()/sizeof(FillTexture::Pixel))throw std::overflow_error("Dimensions overflow addressable pixel storage");const auto size=count*sizeof(FillTexture::Pixel);const auto cache_key=decode_cache_key(source,raw,source_space);{std::lock_guard lock(shared_decode_mutex);if(const auto found=shared_decode_entries.find(cache_key);found!=shared_decode_entries.end())source.cached_pixels=found->second.pixels.lock();}
  if(source.cached_pixels||!new_sources.insert(cache_key).second)continue;
  permanent+=size;scratch=std::max(scratch,size);
 }
 const auto workers_count=std::min<std::size_t>(sources.size(),scratch&&memory>permanent?std::clamp<std::uint64_t>((memory-permanent)/scratch,1,2):1);
 std::printf("BTS_TEXTURE_MEMORY tiles=%zu decoded_bytes=%llu scratch_bytes=%llu available_ram_bytes=%llu free_vram_bytes=%llu\n",sources.size(),(unsigned long long)permanent,(unsigned long long)(scratch*workers_count),(unsigned long long)memory,(unsigned long long)texture_free_vram.load());
 std::vector<std::shared_ptr<const FillTexture>> results(sources.size());std::atomic_size_t next=0;std::atomic_bool abort=false;std::exception_ptr failure;std::mutex mutex;std::vector<std::future<void>> workers;
 for(std::size_t worker=0;worker<workers_count;++worker)workers.push_back(std::async(std::launch::async,[&]{while(!abort){const auto index=next.fetch_add(1);if(index>=sources.size())break;try{results[index]=decode_one(asset,sources[index],int(index),int(sources.size()),raw,source_space,control);}catch(const std::exception &error){std::lock_guard lock(mutex);if(!failure)failure=std::make_exception_ptr(std::runtime_error((QString("tile %1: %2: ").arg(sources[index].tile).arg(sources[index].filename)).toStdString()+error.what()));abort=true;}catch(...){std::lock_guard lock(mutex);if(!failure)failure=std::current_exception();abort=true;}}}));
 for(auto &worker:workers)worker.get();if(failure)std::rethrow_exception(failure);check_cancel(control);if(!sequence)return results.front();auto result=std::make_shared<FillTexture>(*results.front());result->hash=asset.hash().toStdString();auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(std::size_t i=0;i<sources.size();++i){members->emplace(sources[i].tile,results[i]);result->minimum=std::min(result->minimum,results[i]->minimum);result->maximum=std::max(result->maximum,results[i]->maximum);result->floating_point|=results[i]->floating_point;result->data_units|=results[i]->data_units;}result->tiles=std::move(members);result->validate();return result;
}
QStringList fill_texture_color_spaces(){QStringList result;for(int index=0;index<std::numeric_limits<int>::max();++index){const char *name=blender::IMB_colormanagement_colorspace_get_indexed_name(index);if(!name||!*name)break;result.append(QString::fromUtf8(name));}return result;}
void resolve_material_asset_textures(std::vector<StudioAsset> &resources){
 for(auto &material:resources)if(material.material&&material.fill_preset&&material.search_terms.contains("blender-images"))for(int channel=0;channel<6;++channel){
  auto &slot=fill_channel_texture(*material.fill_preset,channel);if(!slot)continue;const auto expected=slot;
  const auto source=std::find_if(resources.begin(),resources.end(),[&](const auto &asset){return asset.id.toString(QUuid::WithoutBraces).toStdString()==expected->asset&&asset.usages.contains("texture")&&asset.hash().toStdString()==expected->hash;});
  if(source==resources.end())throw std::runtime_error("Blender material image resource is missing or changed");auto decoded=decode_fill_texture(*source,channel!=0,expected->source_space);
  if(fill_texture_json(decoded)!=fill_texture_json(expected))throw std::runtime_error("Blender material image descriptor changed");slot=std::move(decoded);
 }
}
void resolve_fill_textures(TextureDocument &document,const std::vector<StudioAsset> &resources){
 std::map<std::tuple<std::string,bool,std::string,std::string>,std::shared_ptr<const FillTexture>> embedded;
 const auto retain=[&](const std::shared_ptr<const FillTexture> &texture){if(texture)embedded.emplace(std::make_tuple(texture->asset,texture->raw,texture->source_space,texture->hash),texture);};
 for(const auto &asset:resources){
  if(asset.usages.contains("smartmask")){const auto preset=asset.smart_mask?asset.smart_mask:std::make_shared<const LayerMask>(read_mask_preset(asset.bytes));for(const auto &effect:mask_effects_recursive(preset->effects))retain(effect.mapping.projection.texture);}
  if(asset.usages.contains("smartmaterial")){
   // An original SPSM is never a Studio JSON preset. Cold GUI sources may be
   // deferred: already persisted uniform layers need no original pixel decode.
   // Any actual unresolved bitmap binding still fails below instead of silently
   // fabricating data; its source must first be prepared on a worker.
   if(is_spsm_asset(asset)&&!asset.material_preset)continue;
   const auto preset=asset.material_preset?asset.material_preset:std::make_shared<const SmartMaterialPreset>(read_material_preset(asset.bytes));for(const auto &layer:preset->set.layers){for(int channel=0;channel<15;++channel)retain(fill_channel_texture(layer.fill,channel));for(const auto &effect:layer.effects)if(effect.bitmap_fill)retain(effect.bitmap_fill->projection.texture);if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))retain(effect.mapping.projection.texture);}}
 }
 const auto embedded_texture=[&](const FillTexture &expected)->std::shared_ptr<const FillTexture>{
  if(const auto found=embedded.find(std::make_tuple(expected.asset,expected.raw,expected.source_space,expected.hash));found!=embedded.end())return bind_fill_texture_descriptor(expected,*found->second);
  for(const auto &[key,texture]:embedded)if(texture->hash==expected.hash&&texture->raw==expected.raw&&texture->source_space==expected.source_space&&texture->width==expected.width&&texture->height==expected.height&&bool(texture->tiles)==bool(expected.tiles))return bind_fill_texture_descriptor(expected,*texture);
  return {};
 };
 std::map<std::tuple<std::string,bool,std::string,std::string>,std::shared_ptr<const FillTexture>> cache;
 for(const auto &description:document.texture_sets()){auto &set=document.set(description.material_session_uid);for(auto &layer:set.layers)for(int channel=0;channel<15;++channel){auto &slot=fill_channel_texture(layer.fill,channel);if(!slot)continue;const auto &expected=*slot;const bool raw=channel!=0||raw_linear_color_source(layer);auto found=cache.find({expected.asset,raw,expected.source_space,expected.hash});if(found==cache.end()){if(auto packed=embedded_texture(expected))found=cache.emplace(std::make_tuple(expected.asset,raw,expected.source_space,expected.hash),std::move(packed)).first;else{const auto asset=std::find_if(resources.begin(),resources.end(),[&](const auto &a){return a.id.toString(QUuid::WithoutBraces).toStdString()==expected.asset;});if(asset==resources.end()||asset->hash().toStdString()!=expected.hash)throw std::runtime_error("Packed fill texture is missing or has changed");found=cache.emplace(std::make_tuple(expected.asset,raw,expected.source_space,expected.hash),decode_fill_texture(*asset,raw,expected.source_space)).first;}}if(fill_texture_json(found->second)!=fill_texture_json(slot))throw std::runtime_error("Packed fill texture dimensions, sequence manifest or checksum changed");if(layer.fill.legacy_alpha_policy){const auto source=std::find_if(resources.begin(),resources.end(),[&](const auto &a){return a.id.toString(QUuid::WithoutBraces).toStdString()==expected.asset;});if(source!=resources.end()&&source->search_terms.contains("flattened"))layer.fill.alpha_mask[channel]=true;}slot=found->second;}}
 // Each ordered bitmap Fill is independent of the initial layer mapping.
 for(const auto &description:document.texture_sets())for(auto &layer:document.set(description.material_session_uid).layers)for(auto &effect:layer.effects)if(effect.bitmap_fill){
  auto &slot=effect.bitmap_fill->projection.texture;if(!slot)throw std::runtime_error("Ordered bitmap Fill has no source");const auto key=std::make_tuple(slot->asset,slot->raw,slot->source_space,slot->hash);auto found=cache.find(key);
  if(found==cache.end()){
   if(auto packed=embedded_texture(*slot))found=cache.emplace(key,std::move(packed)).first;
   else{const auto asset=std::find_if(resources.begin(),resources.end(),[&](const auto &a){return a.id.toString(QUuid::WithoutBraces).toStdString()==slot->asset;});if(asset==resources.end()||asset->hash().toStdString()!=slot->hash)throw std::runtime_error("Packed ordered bitmap Fill source is missing or has changed");found=cache.emplace(key,decode_fill_texture(*asset,slot->raw,slot->source_space)).first;}
  }
  if(fill_texture_json(found->second)!=fill_texture_json(slot))throw std::runtime_error("Packed ordered bitmap Fill descriptor changed");slot=found->second;
 }
 // Bitmap masks share resources with fills or with a self-contained applied
 // smart-mask preset. Cross-project reuse must not depend on its old filename.
 for(const auto &description:document.texture_sets()){auto &set=document.set(description.material_session_uid);for(auto &layer:set.layers)if(layer.mask)for(auto &effect:mask_effects_recursive(layer.mask->effects)){auto &slot=effect.mapping.projection.texture;if(!slot)continue;const auto key=std::make_tuple(slot->asset,slot->raw,slot->source_space,slot->hash);auto found=cache.find(key);
  if(found==cache.end()){
   if(auto packed=embedded_texture(*slot))found=cache.emplace(key,std::move(packed)).first;
   else {const auto asset=std::find_if(resources.begin(),resources.end(),[&](const auto &a){return a.id.toString(QUuid::WithoutBraces).toStdString()==slot->asset;});if(asset==resources.end()||asset->hash().toStdString()!=slot->hash)throw std::runtime_error("Packed bitmap mask texture is missing or has changed");found=cache.emplace(key,decode_fill_texture(*asset,slot->raw,slot->source_space)).first;}
  }
  if(fill_texture_json(found->second)!=fill_texture_json(slot))throw std::runtime_error("Packed bitmap mask texture descriptor changed");slot=found->second;
 }}
 for(const auto &description:document.texture_sets()){auto &set=document.set(description.material_session_uid);for(auto &material:set.material_layering.materials)for(int channel=0;channel<6;++channel){auto &slot=material.textures[channel];if(!slot)continue;const auto &expected=*slot;const bool raw=channel!=0;auto found=cache.find({expected.asset,raw,expected.source_space,expected.hash});if(found==cache.end()){if(auto packed=embedded_texture(expected))found=cache.emplace(std::make_tuple(expected.asset,raw,expected.source_space,expected.hash),std::move(packed)).first;else{const auto asset=std::find_if(resources.begin(),resources.end(),[&](const auto &a){return a.id.toString(QUuid::WithoutBraces).toStdString()==expected.asset;});if(asset==resources.end()||asset->hash().toStdString()!=expected.hash)throw std::runtime_error("Packed shader material texture is missing or has changed");found=cache.emplace(std::make_tuple(expected.asset,raw,expected.source_space,expected.hash),decode_fill_texture(*asset,raw,expected.source_space)).first;}}if(fill_texture_json(found->second)!=fill_texture_json(slot))throw std::runtime_error("Packed shader material texture descriptor changed");slot=found->second;}set.material_layering.validate();}
 document.resolve_instances();
}
}
