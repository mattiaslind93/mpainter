// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceImportUsage.hh"
#include "AssetCategories.hh"
#include "SpsmAssetDispatch.hh"
#include "MaskResourceBank.hh"
#include "SmartMaterialPreset.hh"
#include <QFile>
#include <QTemporaryDir>
#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <set>

using namespace bts;
static SmartMaterialPreset recipe()
{
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;
  ImportSummary imported;imported.materials={{1,"Fixture",{1001}}};
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;
  material.name="Fixture";material.uniform_editable=true;scene.materials.push_back(material);
  TextureDocument document(options,imported,scene);auto &set=document.set(1);
  const auto child=set.layers.front().id;TextureDocument::group_layers(set,{child});
  return capture_material_folder(set,set.selected_layer);
}
int main()
{
 for(const auto *name:{"Rust.spmsk","Cavity Rust.SPMSK","masks/native.btsmask"}){assert(bts::resource_preset_usage(QString::fromUtf8(name))==4);assert(bts::resource_preset_usage_matches(QString::fromUtf8(name),4));for(int wrong:{0,1,2,3})assert(!bts::resource_preset_usage_matches(QString::fromUtf8(name),wrong));}
 for(const auto *name:{"material.spsm","material.BTSMATERIAL"})assert(bts::resource_preset_usage(QString::fromUtf8(name))==3);
 for(const auto *name:{"grunge.png","texture.exr","not-a-mask.spmsk.png"})assert(bts::resource_preset_usage(QString::fromUtf8(name))==-1);

  QTemporaryDir directory;assert(directory.isValid());
  StudioAsset source;source.name="Original source";source.filename="Original.SPSM";
  source.usages={"smartmaterial"};source.bytes=QByteArray::fromHex("894844460d0a1a0a");
  constexpr char opaque[]="opaque original source bytes\0unknown fields";
  source.bytes.append(opaque,sizeof(opaque)-1);
  assert(is_spsm_asset(source));
  {
   auto classified=source;cache_material_shelf(classified,MaterialShelf::Presets);
   assert(asset_in_category(classified,"Material presets")&&!asset_in_category(classified,"Materials"));
   classified.search_terms.append("artist-discovery-tag");assert(material_shelf(classified)==MaterialShelf::Presets);
   auto changed=classified;changed.bytes[0]=char(changed.bytes[0]^1);assert(material_shelf(changed)==MaterialShelf::None);
   changed=classified;changed.search_terms.append("spsm:policy:uniform-input=ACEScg");assert(material_shelf(changed)==MaterialShelf::None);
   assert(material_shelf(classified)==MaterialShelf::Presets);
  }
  {auto cold=source;cold.smart_material=true;
   assert(smart_material_target_ready(cold,true,false));assert(!smart_material_target_ready(cold,false,true));
   cold.filename="worn-metal.builtin";assert(!smart_material_target_ready(cold,true,false));
   assert(smart_material_target_ready(cold,true,true));cold.smart_material=false;
   assert(!smart_material_target_ready(cold,true,true));
  }

  {auto normalized=source;const auto digest=normalized.hash();const QByteArray canonical(normalized.bytes.constData(),normalized.bytes.size());
   assert(canonical.constData()!=normalized.bytes.constData());bind_verified_spsm_source(normalized,canonical,digest);
   assert(normalized.bytes.constData()==canonical.constData()&&normalized.hash()==digest);
   bool mismatch=false;try{bind_verified_spsm_source(normalized,canonical,QString(64,'f'));}catch(const std::invalid_argument &){mismatch=true;}assert(mismatch&&normalized.hash()==digest);
   mismatch=false;try{bind_verified_spsm_source(normalized,canonical,QString(64,'z'));}catch(const std::invalid_argument &){mismatch=true;}assert(mismatch);
  }

  bool rejected=false;try{hydrate_asset_presets(source);}catch(const std::runtime_error &){rejected=true;}
  assert(rejected&&!source.material_preset);
  const auto preset=std::make_shared<const SmartMaterialPreset>(recipe());
  std::atomic_int calls=0;
  std::set<QString> prepared_policies;
  set_spsm_asset_hydrator([&](StudioAsset &asset){if(prepared_policies.insert(spsm_policy_tags(asset).join("\n")).second)++calls;asset.material_preset=preset;return true;});
  AssetCatalog catalog(directory.path());const auto id=source.id;const auto hash=source.hash();
  catalog.add(source);assert(calls==1);
  const auto loaded=catalog.hydrate_preset(id,hash);assert(loaded&&loaded->material_preset==preset&&calls==1);
  assert(!catalog.hydrate_preset(id,QString(64,'f')));
  assert(!catalog.hydrate_preset(QUuid::createUuid(),hash));
  const auto project_id=catalog.retain_project(*loaded);auto retained=catalog.project_assets();
  assert(retained.size()==1&&retained[0].id==project_id&&retained[0].bytes==source.bytes);
  assert(retained[0].material_preset==preset&&!retained[0].search_terms.contains(mask_source_tag));
  assert(catalog.retain_project(*loaded)==project_id);
  auto alternate=*loaded;alternate.id=QUuid::createUuid();
  alternate.search_terms={"spsm:policy:uniform-input=ACEScg"};catalog.add(alternate);assert(calls==2);
  const auto alternate_id=catalog.retain_project(alternate);assert(alternate_id!=project_id);
  const auto variants=catalog.project_assets();assert(variants.size()==2);
  assert(variants[0].bytes.constData()==variants[1].bytes.constData());
  assert(variants[0].hash()==variants[1].hash());
  const auto cached_alternate=catalog.hydrate_preset(alternate.id,hash);assert(cached_alternate);
  auto same_policy=*cached_alternate;same_policy.id=QUuid::createUuid();
  same_policy.search_terms.append("spsm:partial-accepted");
  same_policy.search_terms.append("artist-tag");catalog.add(same_policy);assert(calls==2);
  assert(catalog.retain_project(same_policy)==alternate_id);
  // Defensive even if an old source accidentally carries the bank marker.
  retained[0].search_terms.append(mask_source_tag);
  const auto canonical=canonical_mask_resource_banks(retained);
  assert(canonical.size()==1&&canonical[0].id==project_id&&canonical[0].bytes==source.bytes);
  assert(canonical[0].filename=="Original.SPSM"&&canonical[0].hash()==hash);

  // No source conversion when writing and reloading the ordinary user library.
  auto user=*loaded;user.scope=AssetScope::User;AssetCatalog library(directory.path()+"/user");library.add(user);
  QFile blob(directory.path()+"/user/blobs/"+hash);assert(blob.open(QIODevice::ReadOnly));
  assert(blob.readAll()==source.bytes);
  AssetCatalog restarted(directory.path()+"/user");restarted.load_user_library();
  const auto reopened=restarted.hydrate_preset(id,hash);assert(reopened&&reopened->bytes==source.bytes);
  assert(reopened->material_preset==preset&&reopened->filename==source.filename);

  // A deferred GUI cache miss changes neither source bytes nor the library.
  StudioAsset deferred=source;deferred.id=QUuid::createUuid();deferred.material_preset.reset();
  std::atomic_bool ready=false;
  set_spsm_asset_hydrator([&](StudioAsset &asset){if(!ready)return false;asset.material_preset=preset;return true;});
  AssetCatalog lazy(directory.path()+"/lazy");lazy.add(deferred);
  assert(std::find_if(lazy.assets().begin(),lazy.assets().end(),[&](const auto &asset){return asset.id==deferred.id;})->smart_material);
  assert(!lazy.hydrate_preset(deferred.id,hash));
  auto stale=*loaded;stale.id=QUuid::createUuid();
  stale.search_terms={"spsm:policy:uniform-input=other"};lazy.add(stale);
  const auto deferred_stale=std::find_if(lazy.assets().begin(),lazy.assets().end(),[&](const auto &asset){return asset.id==stale.id;});
  assert(deferred_stale!=lazy.assets().end()&&!deferred_stale->material_preset);
  auto prepared=std::async(std::launch::async,[&]{auto result=deferred;ready=true;hydrate_asset_presets(result);return result;}).get();
  assert(prepared.material_preset==preset);
  const auto published=lazy.hydrate_preset(deferred.id,hash);
  assert(published&&published->material_preset==preset&&published->bytes==deferred.bytes);
  set_spsm_asset_hydrator([](StudioAsset &){return true;});
  rejected=false;try{hydrate_spsm_asset(deferred);}catch(const std::logic_error &){rejected=true;}
  assert(rejected&&!deferred.material_preset);
  set_spsm_asset_hydrator({});
  std::cout<<"SPSM dispatch: cached/deferred hydration, obsolete completion rejection, original-source project/user retention and legacy bank-marker safety PASS; synthetic backend only\n";
}
