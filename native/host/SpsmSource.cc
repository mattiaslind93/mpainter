#include <QStandardPaths>
#include "SpsmSource.hh"
#include "SpsmAssetDispatch.hh"
#include "SpsmNativeSemantics.hh"
#include "SpsmNativeBitmaps.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>
#include "IMB_colormanagement.hh"
#include <future>
#include <map>
#include <mutex>
#include <tuple>
#include <QDateTime>
#if defined(__unix__) || defined(__APPLE__)
# include <sys/stat.h>
#endif
namespace bts {
namespace {
using Source=std::shared_ptr<const PreparedSpsmSource>;
using BufferKey=std::tuple<quintptr,qsizetype,QString>;
struct FileFingerprint {
 std::uint64_t bytes=0,device=0,inode=0;
 std::array<std::int64_t,4> times{}; // Native mtime and ctime seconds/nanoseconds.
 bool cacheable=false;
 bool operator==(const FileFingerprint &)const=default;
};
struct FileProof {QString sha;FileFingerprint fingerprint;};
struct BufferEntry {std::weak_ptr<const PreparedSpsmSource> source;QByteArray identity;};
struct State {
 std::mutex mutex;
 std::map<QString,std::weak_ptr<const PreparedSpsmSource>> ready;
 std::map<QString,std::shared_future<Source>> pending;
 std::map<QString,std::shared_future<QString>> materializing;
 std::map<QString,std::weak_ptr<const QByteArray>> originals;
 std::map<BufferKey,BufferEntry> buffers;
 std::map<QString,FileProof> files;
 SpsmBitmapTextureCache bitmap_cache;
};
std::shared_ptr<State> state_owner(){static const auto value=std::make_shared<State>();return value;}
State &state(){return *state_owner();}
void prune_buffers(State &s){std::erase_if(s.buffers,[](const auto &e){return e.second.source.expired()||e.second.identity.isDetached();});std::erase_if(s.files,[&](const auto &e){const auto it=s.originals.find(e.second.sha);return it==s.originals.end()||it->second.expired();});}
bool gui_thread(){const auto *app=QCoreApplication::instance();return app&&QThread::currentThread()==app->thread();}
// Native preparation is memory-only; disk cache contains verified original HDF bytes.
// Keep the native interpretation revision in every prepared/buffer cache key.
QString policies_key(const QStringList &tags){QString result="native-ordered-levels-channels-v8:";for(const auto &tag:tags){result+=QString::number(tag.size());result+=QLatin1Char(':');result+=tag;}return result;}
BufferKey buffer_key(const StudioAsset &a,const QString &policies){return {reinterpret_cast<quintptr>(a.bytes.constData()),a.bytes.size(),policies};}
Source lookup(const StudioAsset &a,const QString &policies){
 auto &s=state();const std::lock_guard lock(s.mutex);
 if(const auto it=s.buffers.find(buffer_key(a,policies));it!=s.buffers.end())if(auto found=it->second.source.lock())return found;
 return {};
}
void bind_source(StudioAsset &asset,const Source &source){
 const auto policy=policies_key(source->policy_tags);const auto incoming=buffer_key(asset,policy);auto &s=state();
 // Keep a COW identity guard while the catalog still holds a separate incoming
 // allocation. Otherwise a recycled pointer could impersonate verified bytes.
 {const std::lock_guard lock(s.mutex);s.buffers[incoming]={source,asset.bytes};s.buffers[{reinterpret_cast<quintptr>(source->original_bytes->constData()),source->original_bytes->size(),policy}]={source,*source->original_bytes};}
 bind_verified_spsm_source(asset,*source->original_bytes,source->source_sha256);
 const auto &archive=source->prepared.source->source();const auto &preset=archive.presets.at(*source->prepared.source->primary_preset);
 const auto *root=std::get_if<spsm::Object>(&preset.nodes.at(preset.root).value);
 if(is_spmsk_asset(asset)){
  if(!root||root->class_name!="DataSmartMask"||source->prepared.material.set.layers.size()!=1||!source->prepared.material.set.layers.front().mask)throw spsm::Error("SPMSK source is not one standalone DataSmartMask");
  asset.smart_mask=std::shared_ptr<const LayerMask>(source,&*source->prepared.material.set.layers.front().mask);asset.material_preset.reset();asset.smart_material=false;
 }else{
  if(!root||root->class_name!="DataLayersPreset")throw spsm::Error("SPSM material source is not DataLayersPreset");
  asset.material_preset=std::shared_ptr<const SmartMaterialPreset>(source,&source->prepared.material);asset.smart_material=true;
 }
 if(gui_thread()&&std::get<0>(incoming)!=reinterpret_cast<quintptr>(asset.bytes.constData())){const std::lock_guard lock(s.mutex);s.buffers.erase(incoming);}
}

QString digest(const QByteArray &bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QString file_digest(const QString &path,qsizetype size){
 QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()!=size)return {};
 // Fixed streaming scratch size controls IO batching, not admitted file size.
 const auto scratch_size=std::min<qsizetype>(size,64*1024);require_texture_ram(std::uint64_t(scratch_size),"SPSM source checksum scratch");QByteArray scratch(scratch_size,Qt::Uninitialized);QCryptographicHash hash(QCryptographicHash::Sha256);
 while(true){const auto n=file.read(scratch.data(),scratch.size());if(n<0)throw spsm::Error(("Cannot checksum SPSM cache: "+file.errorString()).toStdString());if(!n)break;hash.addData(QByteArrayView(scratch.constData(),n));}
 return QString::fromLatin1(hash.result().toHex());
}
std::optional<FileFingerprint> fingerprint(const QString &path){
#if defined(__unix__) || defined(__APPLE__)
 struct stat info{};const auto name=QFile::encodeName(path);if(::stat(name.constData(),&info)||info.st_size<0)return {};
 FileFingerprint f;f.bytes=std::uint64_t(info.st_size);f.device=std::uint64_t(info.st_dev);f.inode=std::uint64_t(info.st_ino);f.cacheable=true;
# if defined(__APPLE__)
 f.times={info.st_mtimespec.tv_sec,info.st_mtimespec.tv_nsec,info.st_ctimespec.tv_sec,info.st_ctimespec.tv_nsec};
# else
 f.times={info.st_mtim.tv_sec,info.st_mtim.tv_nsec,info.st_ctim.tv_sec,info.st_ctim.tv_nsec};
# endif
 return f;
#else
 // No millisecond-only reuse: platforms lacking this native identity proof
 // conservatively rehash instead of inventing a TTL or trusting coarse mtime.
 const QFileInfo info(path);if(!info.isFile()||info.size()<0)return {};FileFingerprint f;f.bytes=std::uint64_t(info.size());f.times[0]=info.lastModified().toMSecsSinceEpoch();return f;
#endif
}
bool proved(const QString &path,const QString &sha,const FileFingerprint &f){
 if(!f.cacheable)return false;auto &s=state();const std::lock_guard lock(s.mutex);const auto it=s.files.find(path);return it!=s.files.end()&&it->second.sha==sha&&it->second.fingerprint==f;
}
void record_file(const QString &path,const QString &sha,const FileFingerprint &f){
 if(!f.cacheable)return;require_texture_ram(resource_sum(resource_product(resource_sum(path.size(),sha.size()),16),sizeof(FileProof)+128),"SPSM verified file identity");auto &s=state();const std::lock_guard lock(s.mutex);s.files[path]={sha,f};
}
bool checksum_file(const QString &path,const QString &sha,qsizetype size,const FileFingerprint &before){
 if(before.bytes!=std::uint64_t(size)||file_digest(path,size)!=sha)return false;const auto after=fingerprint(path);if(!after||*after!=before)throw spsm::Error("SPSM source cache identity changed during checksum");record_file(path,sha,*after);return true;
}
void validate_file(const QString &path,const QString &sha,qsizetype size){
 const auto current=fingerprint(path);if(!current||current->bytes!=std::uint64_t(size)||(!proved(path,sha,*current)&&!checksum_file(path,sha,size,*current)))throw spsm::Error("Original SPSM source cache changed during preparation");
}
QString persist(const QString &sha,const QByteArray &bytes){
 const auto root=qEnvironmentVariable("BTS_RESOURCE_CACHE",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/resources");
 const auto folder=QDir(root).filePath("spsm");if(!QDir().mkpath(folder))throw spsm::Error(("Cannot create SPSM source cache: "+folder).toStdString());
 const auto path=QDir(folder).absoluteFilePath(sha+".spsm");const auto before=fingerprint(path);
 if(before&&before->bytes==std::uint64_t(bytes.size())&&(proved(path,sha,*before)||checksum_file(path,sha,bytes.size(),*before)))return path;
 QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())throw spsm::Error(("Cannot atomically persist original SPSM source: "+output.errorString()).toStdString());
 // The immutable input already has a verified SHA. An exact atomic write does
 // not need another full readback; subsequent reuse checks native identity.
 const auto written=fingerprint(path);if(!written||written->bytes!=std::uint64_t(bytes.size()))throw spsm::Error("Persisted SPSM source has an invalid byte extent");record_file(path,sha,*written);return path;
}
QString materialize_source(const QString &sha,const QByteArray &bytes){
 auto &s=state();std::shared_future<QString> waiting;std::shared_ptr<std::promise<QString>> owner;
 {const std::lock_guard lock(s.mutex);if(const auto it=s.materializing.find(sha);it!=s.materializing.end())waiting=it->second;else{owner=std::make_shared<std::promise<QString>>();waiting=owner->get_future().share();s.materializing.emplace(sha,waiting);}}
 if(!owner)return waiting.get();
 try{const auto path=persist(sha,bytes);owner->set_value(path);{const std::lock_guard lock(s.mutex);s.materializing.erase(sha);}return path;
 }catch(...){const auto error=std::current_exception();{const std::lock_guard lock(s.mutex);s.materializing.erase(sha);}owner->set_exception(error);std::rethrow_exception(error);}
}
std::string canonical(const QString &name){
 const auto bytes=name.toUtf8();if(bytes.isEmpty()||bytes.contains('\0')||std::any_of(bytes.begin(),bytes.end(),[](unsigned char c){return c<32||c==127;}))throw spsm::Error("Invalid SPSM uniform input color policy");
 const auto *space=blender::IMB_colormanagement_space_get_named(bytes.constData());if(!space)space=blender::IMB_colormanagement_space_from_interop_id(std::string(bytes.constData(),std::size_t(bytes.size())));
 if(!space||blender::IMB_colormanagement_space_is_data(space))throw spsm::Error(("Unknown or data-only SPSM uniform color space: "+name).toStdString());return blender::IMB_colormanagement_colorspace_get_name(space);
}
std::shared_ptr<const QByteArray> intern_original(const QString &sha,const QByteArray &bytes){
 auto &s=state();std::shared_ptr<const QByteArray> original;
 {const std::lock_guard lock(s.mutex);if(const auto it=s.originals.find(sha);it!=s.originals.end())original=it->second.lock();}
 if(original){if(*original!=bytes)throw spsm::Error("Verified SPSM SHA identity has conflicting original bytes");return original;}
 require_texture_ram(sizeof(QByteArray)+128,"SPSM original source owner");auto candidate=std::make_shared<const QByteArray>(bytes);
 {const std::lock_guard lock(s.mutex);auto &entry=s.originals[sha];if(auto existing=entry.lock())original=std::move(existing);else{entry=candidate;original=std::move(candidate);}}
 if(*original!=bytes)throw spsm::Error("Verified SPSM SHA identity has conflicting original bytes");return original;
}
QString report(const PreparedSpsm &prepared,const QString &sha,const QString &space,const QString &path,const SpsmSourcePolicy &policy){
 std::size_t assumptions=0,omissions=0;for(const auto &d:prepared.diagnostics){if(d.kind==SpsmDiagnosticKind::Assumption)++assumptions;if(d.kind==SpsmDiagnosticKind::Omission)++omissions;}
 QString text="Direct SPSM preparation (partial; appearance unverified)\nOriginal source SHA-256: "+sha+"\nOriginal cache: "+path+"\nUniform color policy: "+space+" -> ACEScg "+(space=="Linear Rec.709"?QString("(inferred default; source colorimetry unverified)"):QString("(explicit override; source colorimetry unverified)"))+"\n";
 text+="Bitmap policy: "+policy.bitmap_summary+"\nMesh-map policy: "+policy.mesh_map_summary+"\n";
 text+=QString("Native layers: %1; source streams retained: %2; assumptions: %3; omissions: %4\nNo compiled packages execute. Native generators and filters approximate recognized names and typed tweaks; unsupported subactions are omitted independently while authored visibility and supported base content are retained.\n\n").arg(prepared.material.set.layers.size()).arg(prepared.source->source().presets.size()).arg(assumptions).arg(omissions);
 for(const auto &d:prepared.diagnostics){const auto original_path=prepared.source->source().presets.at(d.preset).dataset+":"+prepared.diagnostic_path(d);const auto bytes=resource_sum(resource_sum(original_path.size(),d.code.size()),d.detail.size());require_texture_ram(resource_sum(resource_product(std::uint64_t(text.size()),4),resource_product(bytes,8)),"SPSM source report text");
  text+=QString::fromStdString(original_path)+" ["+QString::fromUtf8(d.code.data(),qsizetype(d.code.size()))+"] "+(d.code=="uniform-color-policy"?QString("Source RGB uses explicit host policy %1 -> ACEScg; source colorimetry remains unverified").arg(space):QString::fromUtf8(d.detail.data(),qsizetype(d.detail.size())))+"\n";
 }return text;
}
Source prepare(const StudioAsset &asset,const QString &sha,const QStringList &tags){
 const auto policy=parse_spsm_source_policy(tags);const auto input=policy.uniform_input_space;const auto from=canonical(input),to=canonical("ACEScg");
 auto original=intern_original(sha,asset.bytes);const auto path=materialize_source(sha,*original);
 SpsmImportOptions options;options.uniform_rgb_to_scene_linear=[from,to](std::array<float,3> rgb){float pixel[4]={rgb[0],rgb[1],rgb[2],1};if(from!=to)blender::IMB_colormanagement_transform_float(pixel,1,1,4,from.c_str(),to.c_str(),false);return std::array<float,3>{pixel[0],pixel[1],pixel[2]};};
 options.native_actions=false;const auto uniform_transform=options.uniform_rgb_to_scene_linear;
 auto prepared=prepare_spsm_material(std::filesystem::path(path.toStdString()),std::move(options));
 SpsmBitmapUse bitmap_use;bitmap_use.roughness=policy.roughness_raw_r_ignore_a;
 if(policy.bitmap_color!=SpsmBitmapColorInput::Unknown)
  bitmap_use.color=prepare_linear_color_transform(policy.bitmap_color==SpsmBitmapColorInput::NativeLinearRec709RGBLevelsIgnoreA?LinearColorInput::Rec709:LinearColorInput::ACEScg);
 prepared=enhance_prepared_spsm_bitmaps(prepared,policy.bitmaps,[](const auto &descriptor,const auto &pixels,auto budget){return state().bitmap_cache.load(descriptor,pixels,budget);},bitmap_use);
 prepared=enhance_prepared_spsm_mesh_maps(prepared,policy.mesh_maps);
 hydrate_spsm_native_bitmaps(prepared,policy.bitmaps,[](const auto &descriptor,const auto &pixels,auto budget){return state().bitmap_cache.load(descriptor,pixels,budget);},bitmap_use);
 lower_spsm_native_actions(prepared,uniform_transform);
 const auto &primary=prepared.source->source().presets.at(*prepared.source->primary_preset);
 const auto *root=std::get_if<spsm::Object>(&primary.nodes.at(primary.root).value);
 if(root&&root->class_name=="DataSmartMask"){
  for(auto &layer:prepared.material.set.layers)if(layer.mask)for(auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Generator){effect.surface_fallback=true;}
  prepared.partial=true;prepared.diagnostics.push_back({*prepared.source->primary_preset,primary.root,SpsmDiagnosticKind::Assumption,"spmsk-native-surface-policy","Standalone smart-mask generators use provisional surface fallback until baked maps are installed. Procedural grunge retains native UV coordinates unless explicitly changed to 3D; unmapped authored projection is retained and diagnosed. Original procedural graphs are not executed and source appearance parity is not claimed"});
 }
 validate_file(path,sha,original->size());
 const std::weak_ptr<State> weak_state=state_owner();
 auto result=std::shared_ptr<PreparedSpsmSource>(new PreparedSpsmSource,[weak_state](PreparedSpsmSource *p){delete p;if(auto s=weak_state.lock()){const std::lock_guard lock(s->mutex);prune_buffers(*s);}});result->report=report(prepared,sha,input,path,policy);result->policy=policy;result->prepared=std::move(prepared);result->original_bytes=std::move(original);result->source_sha256=sha;result->cache_path=path;result->uniform_input_space=input;result->policy_tags=tags;return result;
}
bool hydrate(StudioAsset &asset){
 if(!is_spsm_archive_asset(asset))throw spsm::Error("Not an original SPSM material or SPMSK mask");const auto tags=spsm_policy_tags(asset);const auto policy=policies_key(tags);
 if(auto ready=lookup(asset,policy)){bind_source(asset,ready);return true;}
 if(gui_thread())return false;
 if(asset.bytes.isEmpty())throw spsm::Error("SPSM original source bytes are empty");
 require_texture_ram(resource_sum(resource_product(resource_sum(asset.name.size(),policy.size()),16),sizeof(PreparedSpsmSource)+sizeof(State)+sizeof(std::promise<Source>)+256),"SPSM source preparation metadata");
 const auto sha=digest(asset.bytes),key=sha+":"+policy;auto &s=state();std::shared_future<Source> waiting;std::shared_ptr<std::promise<Source>> owner;
 Source cached;
 {const std::lock_guard lock(s.mutex);std::erase_if(s.ready,[](const auto &e){return e.second.expired();});std::erase_if(s.originals,[](const auto &e){return e.second.expired();});prune_buffers(s);if(const auto it=s.ready.find(key);it!=s.ready.end())cached=it->second.lock();
  if(!cached){if(const auto it=s.pending.find(key);it!=s.pending.end())waiting=it->second;else{owner=std::make_shared<std::promise<Source>>();waiting=owner->get_future().share();s.pending.emplace(key,waiting);}}}
 if(cached){bind_source(asset,cached);return true;}
 if(!owner){bind_source(asset,waiting.get());return true;}
 Source result;
 try{result=prepare(asset,sha,tags);
  {const std::lock_guard lock(s.mutex);std::erase_if(s.ready,[](const auto &e){return e.second.expired();});std::erase_if(s.originals,[](const auto &e){return e.second.expired();});prune_buffers(s);s.ready[key]=result;s.pending.erase(key);}
  owner->set_value(result);
 }catch(...){const auto failure=std::current_exception();{const std::lock_guard lock(s.mutex);s.pending.erase(key);}owner->set_exception(failure);std::rethrow_exception(failure);}
 bind_source(asset,result);return true;
}
}
SpsmSourcePolicy parse_spsm_source_policy(const QStringList &tags){
 SpsmSourcePolicy result;
 // Application defaults are our native interpretation, never source-enum claims.
 const QStringList defaults{
  "spsm:policy:bitmap-roughness=raw-r-ignore-a",
  "spsm:policy:bitmap-color=native-linear-rec709-rgb-levels-ignore-a",
  "spsm:policy:bitmap-bytes=little-endian", "spsm:policy:bitmap-rows=first-row-v0",
  "spsm:policy:bitmap-alpha=data-components",
  "spsm:policy:bitmap-uv=projection0-centered-repeat-factors",
  "spsm:policy:bitmap-sampling=hq-repeat", "spsm:policy:levels=native-scalar",
  "spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a",
  "spsm:policy:meshmap-ao-color=native-ao-generator-uv-identity-nearest"};
 QStringList effective=tags;
 for(const auto &tag:defaults){const auto key=tag.left(tag.indexOf('=')+1);
  if(std::none_of(tags.begin(),tags.end(),[&](const auto &existing){return existing.startsWith(key);}))effective.append(tag);
 }
 std::map<QString,QString> fields;
 const QString prefix="spsm:policy:";
 for(const auto &tag:effective){
  if(tag.contains(QChar::Null))throw spsm::Error("NUL in SPSM policy tag");
  require_texture_ram(resource_product(std::uint64_t(tag.size()),16),"SPSM policy text");
  if(!tag.startsWith(prefix))throw spsm::Error(("Unsupported SPSM policy tag: "+tag).toStdString());
  const auto equals=tag.indexOf('=',prefix.size());
  if(equals<0)throw spsm::Error(("Unsupported SPSM policy tag: "+tag).toStdString());
  const auto key=tag.mid(prefix.size(),equals-prefix.size()),value=tag.mid(equals+1);
  if(const auto found=fields.find(key);found!=fields.end()&&found->second!=value)
   throw spsm::Error((key=="uniform-input"?QString("Conflicting SPSM uniform input color policies"):QString("Conflicting SPSM bitmap policies: ")+key).toStdString());
  fields[key]=value;
  bool valid=true;
  if(key=="uniform-input")result.uniform_input_space=value;
  else if(key=="bitmap-roughness"){
   if(value=="raw-r-ignore-a")result.roughness_raw_r_ignore_a=true;
   else if(value!="unknown")valid=false;
  }else if(key=="bitmap-color"){
   if(value=="native-linear-rec709-rgb-levels-ignore-a")result.bitmap_color=SpsmBitmapColorInput::NativeLinearRec709RGBLevelsIgnoreA;
   else if(value=="native-acescg-rgb-levels-ignore-a")result.bitmap_color=SpsmBitmapColorInput::NativeACEScgRGBLevelsIgnoreA;
   else if(value!="unknown")valid=false;
  }else if(key=="bitmap-bytes"){
   if(value=="little-endian")result.bitmaps.pixels.bytes=SpsmByteOrder::LittleEndian;
   else if(value=="big-endian")result.bitmaps.pixels.bytes=SpsmByteOrder::BigEndian;
   else if(value!="unknown")valid=false;
  }else if(key=="bitmap-rows"){
   if(value=="first-row-v0")result.bitmaps.pixels.rows=SpsmRowOrder::FirstRowIsV0;
   else if(value=="first-row-v1")result.bitmaps.pixels.rows=SpsmRowOrder::FirstRowIsV1;
   else if(value!="unknown")valid=false;
  }else if(key=="bitmap-alpha"){
   if(value=="data-components")result.bitmaps.pixels.alpha=SpsmAlpha::DataComponents;
   else if(value!="unknown")valid=false;
  }else if(key=="bitmap-uv"){
   if(value=="projection0-centered-repeat-factors")result.bitmaps.uv=SpsmBitmapUV::Projection0CenteredRepeatFactors;
   else if(value!="unknown")valid=false;
  }else if(key=="bitmap-sampling"){
   if(value=="hq-repeat")result.bitmaps.sampling=SpsmBitmapSampling::HQRepeat;
   else if(value=="sharp-repeat")result.bitmaps.sampling=SpsmBitmapSampling::SharpRepeat;
   else if(value=="nearest-repeat")result.bitmaps.sampling=SpsmBitmapSampling::NearestRepeat;
   else if(value!="unknown")valid=false;
  }else if(key=="levels"){
   if(value=="native-scalar")result.bitmaps.levels=SpsmLevelsPolicy::NativeScalarLevels;
   else if(value!="unknown")valid=false;
  }else if(key=="mask-bitmap"){
   if(value=="native-ordered-fill-levels-raw-r-ignore-a")result.bitmaps.mask=SpsmMaskBitmapPolicy::NativeOrderedFillLevelsRawRIgnoreA;
   else if(value!="unknown")valid=false;
  }else if(key=="meshmap-ao-color"){
   if(value=="native-ao-generator-uv-identity-nearest")result.mesh_maps.policy=SpsmMeshMapPolicy::NativeAOGeneratorUVIdentityNearest;
   else if(value!="unknown")valid=false;
  }else throw spsm::Error(("Unsupported SPSM policy tag: "+tag).toStdString());
  if(!valid)throw spsm::Error(("Unsupported SPSM bitmap policy value: "+tag).toStdString());
 }
 // Channel use is its own required opt-in, not an implication of byte order.
 if(!result.roughness_raw_r_ignore_a&&result.bitmap_color==SpsmBitmapColorInput::Unknown)result.bitmaps.uv=SpsmBitmapUV::Unknown;
 const auto value=[&](const char *key){const auto found=fields.find(QString::fromLatin1(key));return found==fields.end()?QString("unknown (missing)"):found->second;};
 result.bitmap_summary="Color="+value("bitmap-color")+"; Roughness="+value("bitmap-roughness")+"; bytes="+value("bitmap-bytes")+"; rows="+value("bitmap-rows")+"; alpha="+value("bitmap-alpha")+"; UV="+value("bitmap-uv")+"; sampling="+value("bitmap-sampling")+"; Mask="+value("mask-bitmap")+"; Levels="+value("levels")+". Native defaults are interpretation choices, not identified source colorimetry. Raw RGB and source-space RGB Levels precede the named conversion to ACEScg. Data channels use raw components with ignored alpha. Supported direct image fills and mask Fill/Levels/group actions are independent; unsupported sibling subactions do not mute the layer. Source ordinal meanings and appearance parity remain unverified.";
 result.mesh_map_summary="AO alias to Base Color="+value("meshmap-ao-color")+". Native generators resolve live matching-tile AO, Curvature, World Normal, Position and Thickness. Recognized package names and typed tweaks select original native semantic approximations. Missing inputs leave the generator pending until baking; authored base content and visibility are retained. No compiled package is read or executed, and proprietary pixel equivalence is not claimed.";
 return result;
}
void configure_spsm_source_hydration(){set_spsm_asset_hydrator(hydrate);}
std::shared_ptr<const PreparedSpsmSource> prepared_spsm_source(const StudioAsset &asset){if(!is_spsm_archive_asset(asset))return {};return lookup(asset,policies_key(spsm_policy_tags(asset)));}
QString spsm_source_report(const StudioAsset &asset){const auto source=prepared_spsm_source(asset);return source?source->report:QString{};}
QString spsm_source_summary(const StudioAsset &asset){const auto source=prepared_spsm_source(asset);if(!source)return "SPSM source has not been prepared";std::size_t visible=0,generators=0,omissions=0;for(const auto &l:source->prepared.material.set.layers){visible+=l.kind!=LayerKind::Folder&&l.visible;for(const auto &e:l.effects)generators+=e.kind==ContentEffectKind::Generator;if(l.mask)for(const auto &e:l.mask->effects)generators+=e.kind==MaskEffectKind::Generator;}for(const auto &d:source->prepared.diagnostics)omissions+=d.kind==SpsmDiagnosticKind::Omission;return QString("SPSM %1: %2 authored visible layers; %3 native generators; %4 reported omissions. Native semantic approximations; appearance parity unverified. Inspect SPSM import report… in the Shelf context menu. Source SHA-256: %5").arg(asset.name).arg(visible).arg(generators).arg(omissions).arg(source->source_sha256);}
bool spsm_source_partial(const StudioAsset &asset){const auto source=prepared_spsm_source(asset);return !source||source->prepared.partial;}
}
