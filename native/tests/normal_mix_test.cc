// SPDX-License-Identifier: GPL-3.0-or-later
#include "NormalMix.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
bool close(bts::NormalVector a,bts::NormalVector b){for(int c=0;c<3;++c)if(std::abs(a[c]-b[c])>1e-5)return false;return true;}
int main(){
  const auto base=bts::normalized_normal({.4f,0,.9f}),detail=bts::normalized_normal({0,.6f,.8f});assert(close(bts::reoriented_normal(base,{0,0,1}),base)&&close(bts::reoriented_normal({0,0,1},detail),detail));const auto combined=bts::reoriented_normal(base,detail);assert(!close(combined,base)&&!close(combined,detail)&&std::abs(combined[0]*combined[0]+combined[1]*combined[1]+combined[2]*combined[2]-1)<1e-5);
  bts::TextureSet set;bts::BakedNormalSample baked{base,1};assert(close(bts::mix_baked_normal(set,detail,false,baked),base));assert(close(bts::mix_baked_normal(set,detail,true,baked),combined));set.normal_mixing=bts::NormalMixing::Replace;assert(close(bts::mix_baked_normal(set,detail,true,baked),detail)&&close(bts::mix_baked_normal(set,detail,false,baked),base));set.channels[3]=false;assert(close(bts::mix_baked_normal(set,detail,true,baked),base));assert(close(bts::mix_baked_normal(set,detail,true,{}),detail));
  std::vector<float> rgba(64*32*4);for(std::size_t i=0;i<rgba.size();i+=4){rgba[i]=.5;rgba[i+1]=.8;rgba[i+2]=.9;rgba[i+3]=1;}auto pixels=std::make_shared<bts::MeshMapPixels>(64,std::move(rgba),32);bts::MeshMap map;map.name="test normal";map.tiles[1001]={64,pixels->hash,pixels,32};set.mesh_maps[0]=map;const auto sample=bts::sample_baked_normal(set,.2,.8);assert(sample.coverage==1&&sample.direction[1]>.5);set.normal_format=bts::NormalMapFormat::DirectX;assert(bts::sample_baked_normal(set,.2,.8).direction[1]<-.5);assert(bts::sample_baked_normal(set,.2,.8,1002).coverage==0);
  bts::SceneSnapshot scene;bts::MaterialSummary material;material.session_id=1;material.name="Normal";material.uniform_editable=true;scene.materials.push_back(material);bts::ImportSummary imported;imported.materials={{1,"Normal"}};bts::TextureDocument doc({},imported,scene);doc.set(1).normal_mixing=bts::NormalMixing::Replace;const auto json=doc.to_json();const std::array<bts::TextureSetBinding,1> bindings{{{doc.set(1).id,1,"Normal"}}};assert(bts::TextureDocument::from_json(json,bindings).to_json()==json);auto legacy=json;legacy["schema"]=11;assert(bts::TextureDocument::from_json(legacy,bindings).set(1).normal_mixing==bts::NormalMixing::Combine);
  const auto unchanged=doc.to_json();doc.set_normal_map_format(bts::NormalMapFormat::DirectX);assert(doc.settings().normal_map_format==bts::NormalMapFormat::DirectX&&doc.set(1).normal_format==bts::NormalMapFormat::DirectX);auto changed=doc.to_json();changed["normal_format"]="OpenGL";assert(changed==unchanged);bool invalid=false;try{doc.set_normal_map_format(bts::NormalMapFormat(99));}catch(const std::invalid_argument &){invalid=true;}assert(invalid&&doc.settings().normal_map_format==bts::NormalMapFormat::DirectX);
  std::puts("normal mixing: RNM identities/nonflat pair/unit vectors, Replace whole-channel/empty fallback, removed channel, rectangular sampling, DirectX decoding and schema12/11 metadata PASS");
}
