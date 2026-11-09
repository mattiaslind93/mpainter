// SPDX-License-Identifier: GPL-3.0-or-later
#include "AmbientOcclusion.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <QJsonArray>
#include <cassert>
#include <cstdio>
int main(){
  bts::SceneSnapshot scene;bts::MaterialSummary material;material.session_id=1;material.name="AO";material.uniform_editable=true;scene.materials.push_back(material);bts::ImportSummary imported;imported.materials={{1,"AO"}};bts::NewProjectOptions settings;settings.resolution=bts::DocumentResolution::Pixels128;bts::TextureDocument document(settings,imported,scene);auto &set=document.set(1);set.ao_channel=true;auto before=document;
  std::vector<float> rgba(128*128*4);for(int y=0;y<128;++y)for(int x=0;x<64;++x){const auto i=(y*128+x)*4;rgba[i]=.5;rgba[i+3]=1;}set.layers[0].ao_pixels.assign_channel(128,2,rgba);const auto left=bts::TextureDocument::composite(set,32,64),right=bts::TextureDocument::composite(set,96,64);assert(left.ao_coverage==1&&left.material.ambient_occlusion==.5&&right.ao_coverage==0&&right.material.ambient_occlusion==1);assert(set.layers[0].pixels.empty()&&before.set(1).layers[0].ao_pixels.empty());
  bts::BakedAoSample baked{.3,1};assert(bts::mix_ambient_occlusion(set,.5,true,baked)==.15f);set.ao_mixing=bts::AoMixing::Replace;assert(bts::mix_ambient_occlusion(set,.5,true,baked)==.5&&bts::mix_ambient_occlusion(set,1,true,baked)==1&&bts::mix_ambient_occlusion(set,1,false,baked)==.3f);set.ao_channel=false;assert(bts::mix_ambient_occlusion(set,.5,true,baked)==.3f&&bts::TextureDocument::composite(set,32,64).material.ambient_occlusion==1);set.ao_channel=true;
  set.layers[0].ao_opacity=.5;bts::TextureDocument::add_mask(set,.5);assert(bts::TextureDocument::composite(set,32,64).material.ambient_occlusion==.875);bts::TextureDocument::group_layers(set,{set.layers[0].id});bts::TextureDocument::layer(set,set.selected_layer).ao_opacity=.5;assert(bts::TextureDocument::composite(set,32,64).material.ambient_occlusion==.9375);
  const auto json=document.to_json();const std::array<bts::TextureSetBinding,1> bindings{{{set.id,1,"AO"}}};auto restored=bts::TextureDocument::from_json(json,bindings);assert(restored.set(1).ao_channel&&restored.set(1).ao_mixing==bts::AoMixing::Replace&&restored.set(1).layers[0].ao_opacity==.5f);auto legacy=json;legacy["schema"]=13;assert(!bts::TextureDocument::from_json(legacy,bindings).set(1).ao_channel);
  std::puts("ambient occlusion: separate COW authoring, neutral exterior, Multiply/whole-channel Replace/empty fallback, disabled channel, masks/folder opacity and schema14 migration PASS");
}
