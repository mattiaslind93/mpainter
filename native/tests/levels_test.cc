// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include <cassert>
#include <iostream>
int main(){using namespace bts;
  const auto near=[](float a,float b){assert(std::abs(a-b)<2e-5f);};LevelsEffect effect;
  for(int i=-100;i<300;++i)near(levels_value(effect,i/100.f),i/100.f);
  effect.input_low=.2;effect.input_high=.8;effect.gamma=2;near(levels_value(effect,.35),.5);effect.output_low=1;effect.output_high=0;near(levels_value(effect,.35),.5);effect={};effect.clamp=true;near(levels_value(effect,2),1);near(levels_value(effect,-1),0);
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Levels";material.uniform_editable=true;scene.materials={material};ImportSummary imported;imported.materials={{1,"Levels"}};TextureDocument doc({},imported,scene);auto &set=doc.set(1);set.precision[0]=ChannelPrecision::RGB32F;set.precision[2]=ChannelPrecision::L32F;TextureDocument::add_fill(set);auto &fill=TextureDocument::layer(set,set.selected_layer);fill.values.base_color={.25,.5,.75,1};fill.values.roughness=.2;TextureDocument::add_paint(set);auto &paint=TextureDocument::layer(set,set.selected_layer);effect={};effect.output_low=1;effect.output_high=0;paint.effects={effect};set.selected_effect=effect.id;
  // An empty Normal paint layer's effect does not see the lower fill.
  auto sample=TextureDocument::evaluate(set,10,10);near(sample.material.base_color[0],.25);near(sample.coverage[0],1);
  paint.blending[0]=LayerBlend::Passthrough;sample=TextureDocument::evaluate(set,10,10);near(sample.material.base_color[0],.75);near(sample.material.base_color[1],.5);near(sample.material.base_color[2],.25);near(sample.material.roughness,.2);
  near(TextureDocument::effect_input(set,effect.id,10,10)[0],.25);
  paint.opacity[0]=.5;near(TextureDocument::evaluate(set,10,10).material.base_color[0],.5);paint.mask=LayerMask{};paint.mask->background=.25;near(TextureDocument::evaluate(set,10,10).material.base_color[0],.3125);paint.mask->enabled=false;near(TextureDocument::evaluate(set,10,10).material.base_color[0],.5);
  paint.effects[0].enabled=false;near(TextureDocument::evaluate(set,10,10).material.base_color[0],.25);paint.effects[0].enabled=true;paint.visible=false;near(TextureDocument::evaluate(set,10,10).material.base_color[0],.25);paint.visible=true;
  const auto data=doc.to_json();std::vector<TextureSetBinding> bindings{{set.id,1,set.name}};assert(TextureDocument::from_json(data,bindings).to_json()==data);
  auto invalid=effects_json(paint.effects);auto invalid_effect=invalid[0].toObject();invalid_effect["gamma"]=0;invalid[0]=invalid_effect;bool rejected=false;try{read_effects(invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
  // A content adjustment processes its own fill even in Normal mode, and chains in order.
  paint.visible=false;auto &lower=set.layers[set.layers.size()-2];lower.effects={effect};near(TextureDocument::evaluate(set,10,10).material.base_color[0],.75);lower.effects.push_back(effect);near(TextureDocument::evaluate(set,10,10).material.base_color[0],.25);
  // Original paint pages remain untouched; adjustments never rasterize the lower stack.
  assert(paint.pixels.page_count(0)==0);std::cout<<"Levels identity/gamma/HDR/clamp, empty Normal/Passthrough, opacity/mask/visibility, channel isolation, chains, input histogram and strict packed metadata passed\n";
}
