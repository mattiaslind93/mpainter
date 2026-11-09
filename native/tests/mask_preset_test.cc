// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaskPreset.hh"
#include "TextureDocument.hh"
#include "AssetCatalog.hh"
#include <QDir>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
#include "PaintRecording.hh"
#include "MaskResourceBank.hh"
int main(){using namespace bts;LayerMask mask;mask.background=0;PaintPixels::Stamp stamp{{.25,.5},{.1,0},{0,.1},{}};stamp.channels={false,false,true,false,false};stamp.values.roughness=1;stamp.hardness=1;mask.pixels.stamp(128,stamp);MaskEffect e;e.enabled=false;e.colors={{1,0,0}};mask.effects={e};const auto bytes=write_mask_preset(mask);
 for(int size:{8192,8193,16384,2147483647}){
  auto raw=qUncompress(bytes);
  const auto metadata_size=qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(raw.constData()+4));
  qToLittleEndian<qint32>(size,reinterpret_cast<uchar *>(raw.data()+16+metadata_size));
  bool rejected=false;
  try{const auto restored=read_mask_preset(qCompress(raw,1));assert(size==8192&&restored.author_resolution==8192);}
  catch(const std::invalid_argument &error){rejected=std::string(error.what()).find("8192 (8K)")!=std::string::npos;}
  assert(rejected==(size>8192));
 }
 auto decoded=read_mask_preset(bytes);assert(decoded.pixels.hash(2)==mask.pixels.hash(2)&&mask_effects_json(decoded.effects)==mask_effects_json(mask.effects));for(auto bad:{bytes.left(bytes.size()/2),QByteArray("broken"),QByteArray::fromHex("7fffffff0000")}){bool rejected=false;try{read_mask_preset(bad);}catch(const std::exception &){rejected=true;}assert(rejected);}QTemporaryDir dir(QDir::current().filePath("mask-preset-library-XXXXXX"));assert(dir.isValid());StudioAsset asset;asset.name="Painted mask";asset.filename="painted.btsmask";asset.bytes=bytes;asset.usages={"smartmask"};asset.scope=AssetScope::User;{AssetCatalog catalog(dir.path());catalog.add(asset);}AssetCatalog reopened(dir.path());reopened.load_user_library();const auto &restored=reopened.assets().back();assert(restored.smart_mask&&restored.smart_mask->pixels.hash(2)==mask.pixels.hash(2));
 SceneSnapshot scene;MaterialSummary m;m.session_id=1;m.name="Mask";m.uniform_editable=true;scene.materials={m};ImportSummary imported;imported.materials={{1,"Mask"}};TextureDocument doc({},imported,scene);auto &set=doc.set(1);TextureDocument::apply_mask_preset(set,set.selected_layer,decoded,false);auto &target=*set.layers[0].mask;assert(target.background==0&&target.pixels.hash(2)==mask.pixels.hash(2)&&target.effects[0].id!=mask.effects[0].id);target.effects.clear();target.pixels={};target.author_resolution=256;stamp.center={.75,.5};target.pixels.stamp(256,stamp);MaskEffect original;original.enabled=false;target.effects={original};TextureDocument::apply_mask_preset(set,set.selected_layer,decoded,false);assert(target.pixels.resolution()==256&&target.value(64,128,1001,256)>.9&&target.value(192,128,1001,256)>.9&&target.effects.size()==3&&target.effects[0].id==original.id&&target.effects[1].kind==MaskEffectKind::Paint&&target.effects[2].id!=e.id);target.enabled=false;TextureDocument::apply_mask_preset(set,set.selected_layer,decoded,true);assert(!target.enabled&&target.pixels.hash(2)==mask.pixels.hash(2)&&target.effects.size()==1&&target.effects[0].id!=e.id);auto bad=decoded;stamp.center={1.25,.5};bad.pixels.stamp(128,stamp,{1002});const auto before=doc.to_json();bool rejected=false;try{TextureDocument::apply_mask_preset(set,set.selected_layer,bad,false);}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&doc.to_json()==before);mask.background=1;auto white=read_mask_preset(write_mask_preset(mask));assert(white.background==1&&white.pixels.hash(2)==mask.pixels.hash(2));TextureDocument::apply_mask_preset(set,set.selected_layer,white,true);assert(set.layers[0].mask->background==1);mask.background=2;rejected=false;try{write_mask_preset(mask);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);// Both legacy metadata versions omit background; their defined value remains black.
 const auto legacy=[&](int version){const auto raw=qUncompress(bytes);QDataStream input(raw);input.setVersion(QDataStream::Qt_6_8);input.setByteOrder(QDataStream::LittleEndian);quint32 magic,size;input>>magic>>size;auto metadata=raw.mid(8,size);auto root=QJsonDocument::fromJson(metadata).object();root["version"]=version;if(version<3)root.remove("background");metadata=QJsonDocument(root).toJson(QJsonDocument::Compact);QByteArray replacement;QDataStream out(&replacement,QIODevice::WriteOnly);out.setVersion(QDataStream::Qt_6_8);out.setByteOrder(QDataStream::LittleEndian);out<<magic<<quint32(metadata.size());out.writeRawData(metadata.data(),metadata.size());const auto tail=raw.mid(8+size);out.writeRawData(tail.data(),tail.size());return qCompress(replacement,1);};
 for(int version:{1,2,3,4,5}){const auto restored=read_mask_preset(legacy(version));assert(restored.background==0&&restored.pixels.hash(2)==decoded.pixels.hash(2));}
 // Applying GPU-authored presets must not rasterize recipes, including when
 // the destination uses another resolution. The Paint effect owns resampling.
 LayerMask gpu_mask;gpu_mask.background=0;gpu_mask.author_resolution=128;stamp.center={.5,.5};
 {PaintRecordingScope record;gpu_mask.pixels.stamp(128,stamp);}
 const auto materializations=PaintPixels::recipe_materializations();
 {GpuPersistenceScope forbid;TextureDocument::apply_mask_preset(set,set.selected_layer,gpu_mask,false);}
 assert(PaintPixels::recipe_materializations()==materializations);
 assert(set.layers[0].mask->effects.back().kind==MaskEffectKind::Paint);
 assert(set.layers[0].mask->effects.back().paint.pixels.tile(1001).recipe());
 // Bitmap/UDIM inputs are embedded once and remain shared across effects.
 LayerMask bitmap_mask;bitmap_mask.background=0;
 auto image=std::make_shared<FillTexture>();image->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();image->hash=std::string(64,'a');image->name="Packed mask";image->width=image->height=2;image->raw=true;
 image->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(std::vector<FillTexture::Pixel>{{-.25f,0,0,0},{1.5f,0,0,1},{.25f,0,0,1},{2,0,0,1}});
 auto second=std::make_shared<FillTexture>(*image);second->hash=std::string(64,'b');second->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{.75f,0,0,1});
 auto sequence=std::make_shared<FillTexture>(*image);sequence->hash=std::string(64,'c');sequence->tiles=std::make_shared<const std::map<int,std::shared_ptr<const FillTexture>>>(std::map<int,std::shared_ptr<const FillTexture>>{{1001,image},{1002,second}});
 for(int i=0;i<3;++i){MaskEffect fill;fill.kind=MaskEffectKind::Fill;fill.mapping.projection.texture=sequence;fill.opacity=.5f;bitmap_mask.effects.push_back(fill);}
 const auto packed=write_mask_preset(bitmap_mask);auto bitmap_copy=read_mask_preset(packed);assert(mask_effects_json(bitmap_copy.effects)==mask_effects_json(bitmap_mask.effects));
 const auto shared=bitmap_copy.effects[0].mapping.projection.texture;assert(shared&&shared->tiles&&shared->raw&&shared->tiles->size()==2);
 for(const auto &effect:bitmap_copy.effects)assert(effect.mapping.projection.texture==shared);
 assert(*shared->tiles->at(1001)->pixels==*image->pixels&&*shared->tiles->at(1002)->pixels==*second->pixels);assert(shared->tiles->at(1001)->pixels==image->pixels&&shared->tiles->at(1002)->pixels==second->pixels);
 auto wrong_pixels=std::make_shared<std::vector<FillTexture::Pixel>>(*image->pixels);(*wrong_pixels)[0][0]=.9f;bool conflict=false;try{intern_fill_texture_pixels(*image,wrong_pixels);}catch(const std::invalid_argument &){conflict=true;}assert(conflict);
 const auto short_bitmap=qCompress(qUncompress(packed).chopped(4),1);bool short_rejected=false;try{read_mask_preset(short_bitmap);}catch(const std::exception &){short_rejected=true;}assert(short_rejected);
 // Applied presets persist one source bank even if their other parameters
 // differ. Bank IDs/bytes are stable across repeated saves and are hidden UI data.
 std::vector<StudioAsset> preset_sources;
 for(int i=0;i<3;++i){auto source=std::make_shared<LayerMask>(bitmap_mask);source->effects[0].opacity=float(i+1)/4;StudioAsset asset;asset.scope=AssetScope::Project;asset.name="Applied preset";asset.filename="applied.btsmask";asset.usages={"smartmask"};asset.search_terms={mask_source_tag};asset.smart_mask=source;asset.bytes=write_mask_preset(*source);preset_sources.push_back(std::move(asset));}
 const auto one_bank=canonical_mask_resource_banks({preset_sources.front()}),three_banks=canonical_mask_resource_banks(preset_sources);assert(one_bank.size()==1&&three_banks.size()==1&&one_bank[0].bytes==three_banks[0].bytes&&one_bank[0].id==three_banks[0].id);
 assert(canonical_mask_resource_banks(three_banks)[0].bytes.constData()==three_banks[0].bytes.constData());
 auto alias=*sequence;alias.asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(const auto &[tile,member]:*sequence->tiles){auto copy=std::make_shared<FillTexture>(*member);copy->asset=alias.asset;members->emplace(tile,copy);}alias.tiles=members;
 const auto rebound=bind_fill_texture_descriptor(alias,*shared);assert(fill_texture_json(rebound)==fill_texture_json(std::make_shared<const FillTexture>(alias))&&rebound->pixels==shared->pixels);
 // Admission follows actual available RAM, not the former 128 MiB cap.
 {TextureRamQueryScope query(+[]()->std::uint64_t{return 16ULL*1024*1024*1024;});assert(mask_preset_memory_budget()==std::numeric_limits<quint32>::max()-1ULL);}
 {TextureRamQueryScope query(+[]()->std::uint64_t{return 8*1024;});bool low_memory=false;try{write_mask_preset(bitmap_mask);}catch(const std::exception &){low_memory=true;}assert(!low_memory);}
 if(qEnvironmentVariableIsSet("BTS_MASK_PRESET_LARGE_TEST")){
  auto large=std::make_shared<FillTexture>(*image);large->width=large->height=4096;large->hash=std::string(64,'d');large->input_space="Non-Color/raw";large->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(std::size_t(4096)*4096,FillTexture::Pixel{.25f,.5f,.75f,1});
  std::vector<StudioAsset> sources;
  for(int i=0;i<3;++i){auto mask=std::make_shared<LayerMask>();MaskEffect fill;fill.kind=MaskEffectKind::Fill;fill.opacity=float(i+1)/4;fill.mapping.projection.texture=large;mask->effects.push_back(fill);StudioAsset asset;asset.scope=AssetScope::Project;asset.name="4K preset";asset.filename="large.btsmask";asset.usages={"smartmask"};asset.search_terms={mask_source_tag};asset.smart_mask=mask;asset.bytes=write_mask_preset(*mask);sources.push_back(std::move(asset));}
  const auto single=canonical_mask_resource_banks({sources.front()}),multiple=canonical_mask_resource_banks(sources);assert(single.size()==1&&multiple.size()==1&&single[0].bytes==multiple[0].bytes);
  const auto reopened=read_mask_preset(multiple[0].bytes);assert(reopened.effects[0].mapping.projection.texture->pixels==large->pixels);
  std::cout<<"4K preset source bank raw_bytes="<<large->pixels->size()*sizeof(FillTexture::Pixel)<<" encoded_bytes="<<multiple[0].bytes.size()<<" presets=3 source_banks=1 extra_bytes=0 shared_pixels=1\n";
 }
 LayerMask many;for(int i=0;i<80;++i){MaskEffect effect;effect.enabled=false;effect.colors.resize(300,{.1f,.2f,.3f});many.effects.push_back(std::move(effect));}const auto many_copy=read_mask_preset(write_mask_preset(many));assert(many_copy.effects.size()==80&&many_copy.effects[0].colors.size()==300);
 std::cout<<"Smart-mask codec/corruption bounds, user-library reload, black-mask creation, sparse merge across resolutions, effect order/IDs, Ctrl replacement, destination toggle and atomic tile rejection passed\n";
}
