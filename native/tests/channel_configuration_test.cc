// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelConfiguration.hh"
#include "TextureMemory.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  SceneSnapshot scene;ImportSummary imported;
  for(unsigned uid:{1,2,3}){MaterialSummary material;material.session_id=uid;material.name="Set"+std::to_string(uid);material.uniform_editable=true;scene.materials.push_back(material);imported.materials.push_back({uid,material.name});}
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);
  document.set(3).disabled=true;document.set(2).channels[1]=false;
  auto &layer=document.set(1).layers.front();PaintPixels::Stamp stamp;stamp.center={.5,.5};stamp.axis_x={.1,0};stamp.axis_y={0,.1};stamp.channels={true,true,true,true,true};layer.pixels.stamp(128,stamp);
  const auto layer_hash=layer.pixels.hash(0);const auto identity=document.set(1).id;auto original=document;
  ChannelConfigurationEdit edit{channel_configuration(document.set(1)),false};edit.configuration.channels[1].enabled=false;edit.configuration.channels[5].enabled=true;edit.configuration.channels[5].precision=ChannelPrecision::L16F;edit.configuration.channels[6].enabled=true;edit.configuration.channels[14].enabled=true;
  auto changed=apply_channel_configuration(document,identity,edit);assert(changed==std::vector<std::uint32_t>{1});assert(!document.set(1).channels[1]&&document.set(1).ao_channel&&document.set(1).displacement.channel);assert(document.set(1).users[0].allocated&&document.set(1).users[0].present);assert(channel_configuration(document.set(2))==channel_configuration(original.set(2)));assert(document.set(1).layers.front().pixels.hash(0)==layer_hash);
  edit.all_sets=true;changed=apply_channel_configuration(document,identity,edit);assert(changed==std::vector<std::uint32_t>{2});assert(channel_configuration(document.set(2))==edit.configuration);assert(channel_configuration(document.set(3))==channel_configuration(original.set(3)));assert(apply_channel_configuration(document,identity,edit).empty());
  edit.configuration.channels[6].enabled=false;edit.configuration.channels[14].enabled=false;apply_channel_configuration(document,identity,edit);assert(document.set(1).users[0].allocated&&!document.set(1).users[0].present&&document.set(1).displacement.allocated&&!document.set(1).displacement.channel);assert(document.set(1).layers.front().pixels.hash(0)==layer_hash);
  auto saved=document.to_json();std::vector<TextureSetBinding> bindings;for(const auto &set:document.texture_sets())bindings.push_back({set.id,set.material_session_uid,set.name});auto restored=TextureDocument::from_json(saved,bindings);for(const auto &set:document.texture_sets())assert(channel_configuration(restored.set(set.material_session_uid))==channel_configuration(set));
  auto invalid=edit;invalid.configuration.channels[2].precision=ChannelPrecision(999);bool rejected=false;try{apply_channel_configuration(document,identity,invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&document.to_json()==saved);
  // The first set is affordable; simulated low RAM on the second must roll back both.
  TextureRamQueryScope memory_pressure([]()->std::uint64_t{return 4ULL*1024*1024*1024;});
  TextureDocument::set_resolution(document.set(2),4096);const auto budget_before=document.to_json();auto oversized=edit;for(auto &entry:oversized.configuration.channels)entry.enabled=true;rejected=false;try{apply_channel_configuration(document,identity,oversized);}catch(const std::length_error &){rejected=true;}assert(!rejected&&document.to_json()!=budget_before);
  std::cout<<"Batch channel isolation/all/skipped/no-op/remove-readd/data preservation/precision/persistence/late budget rollback PASS\n";
}
