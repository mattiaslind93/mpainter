// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "StrokeArchive.hh"
#include "MaskPreset.hh"
#include "FilterPanel.hh"
#include "GpuLayerEffects.hh"
#include "LayerEditDiff.hh"
#include "NativeMaterialApply.hh"
#include <QApplication>
#include <cassert>
#include <iostream>
using namespace bts;
int main(int argc,char **argv){
 QApplication app(argc,argv);
 FilterSettings invert{1,4},clamp{2,4},posterize{3,4};assert(filter_value(invert,2)==-1&&filter_value(clamp,-1)==0&&filter_value(clamp,2)==1);assert(std::abs(filter_value(posterize,.4)-1.f/3)<1e-6);
 SceneSnapshot scene;MaterialSummary m;m.session_id=1;m.name="Filters";m.uniform_editable=true;scene.materials={m};ImportSummary imported;imported.materials={{1,m.name}};TextureDocument doc({},imported,scene);auto &set=doc.set(1);TextureDocument::set_resolution(set,128);set.precision[0]=ChannelPrecision::RGB32F;set.precision[2]=ChannelPrecision::L32F;TextureDocument::add_fill(set);auto &layer=TextureDocument::layer(set,set.selected_layer);layer.values.base_color={.2,.4,.8,1};TextureDocument::add_mask(set,0);MaskEffect fill;fill.kind=MaskEffectKind::Fill;fill.fill=.25;layer.mask->effects.push_back(fill);TextureDocument::add_filter_effect(set);auto &filter=layer.mask->effects.back();assert(TextureDocument::mask_value(set,layer.id,64,64)==.25);filter.filter=invert;assert(TextureDocument::mask_value(set,layer.id,64,64)==.75);filter.opacity=.5;assert(TextureDocument::mask_value(set,layer.id,64,64)==.5);filter.opacity=1;TextureDocument::move_effect(set,-1);assert(TextureDocument::mask_value(set,layer.id,64,64)==.25);TextureDocument::move_effect(set,1);assert(TextureDocument::mask_value(set,layer.id,64,64)==.75);assert(mask_effects_json(read_mask_preset(write_mask_preset(*layer.mask)).effects)==mask_effects_json(layer.mask->effects));
 set.selected_mask=false;set.selected_effect=QUuid();TextureDocument::add_filter_effect(set);auto &content=layer.effects.back();content.filter=invert;const auto output=TextureDocument::evaluate(set,64,64).authored.base_color;assert(std::abs(output[0]-.8)<1e-6&&std::abs(output[1]-.6)<1e-6&&std::abs(output[2]-.2)<1e-6);content.channel=2;layer.values.roughness=.3;assert(std::abs(TextureDocument::evaluate(set,64,64).authored.roughness-.7)<1e-6);const auto json=doc.to_json();std::vector<TextureSetBinding> bindings{{set.id,1,set.name}};assert(TextureDocument::from_json(json,bindings).to_json()==json);
 MaskEffect compare;compare.kind=MaskEffectKind::Compare;compare.left=2;compare.right=3;compare.operation=0;compare.constant=.5;compare.hardness=.9;layer.mask->effects.push_back(compare);assert(!layer.mask->channel_dependent());assert(TextureDocument::mask_value(set,layer.id,64,64)==0);layer.mask->effects.back().operation=2;assert(TextureDocument::mask_value(set,layer.id,64,64)==1);layer.mask->effects.back().left=1;assert(layer.mask->channel_dependent());
 FilterSettings hsv;hsv.resource=4;hsv.hue=0;hsv.saturation=0;hsv.value_scale=1;const auto gray=filter_rgb(hsv,{1,0,0});assert(std::abs(gray[0]-gray[1])<1e-5&&std::abs(gray[1]-gray[2])<1e-5);FilterSettings contrast;contrast.resource=5;contrast.contrast=2;contrast.brightness=0;assert(std::abs(filter_value(contrast,1.f)-1.5f)<1e-5);FilterSettings levels;levels.resource=6;levels.output_high=.25f;assert(std::abs(filter_value(levels,1.f)-.25f)<1e-5);FilterSettings blur;blur.resource=7;blur.radius=2;assert(filter_value(blur,.4f)==.4f);const auto stored=filter_json(levels);assert(read_filter(stored).output_high==.25f&&read_filter(filter_json(blur)).resource==7);

 // Exercise the artist controls against a native-material Fill descriptor. The
 // CPU witness is a known colored input; native graph execution stays on GPU.
 {
  NativeMaterialDescription native;native.material="Filter witness";native.surface_type="ShaderNodeGroup";
  auto asset=native_material_asset("Filter witness",QByteArray("test source"),native);
  asset.search_terms.append("native-channel:0");
  auto witness=doc;auto &target=witness.set(1);target.layers.resize(1);target.selected_layer=target.layers.front().id;target.selected_mask=false;target.selected_effect={};
  const auto id=apply_native_material_asset(target,asset);
  auto &fill_layer=TextureDocument::layer(target,id);fill_layer.values.base_color={.2f,.4f,.8f,1};
  TextureDocument::add_filter_effect(target,0);auto &effect=fill_layer.effects.back();
  FilterPanel panel(nullptr);panel.synchronize(&target,&effect.filter,0);
  int publications=0;
  panel.changed=[&](const FilterSettings &settings,int channel,float opacity,LayerBlend blend){
   effect.filter=settings;effect.channel=channel;effect.opacity=opacity;effect.blending=blend;++publications;
  };
  auto before=witness;const auto old_state=witness.edit_state();
  panel.findChild<QAction *>("filterHsv")->trigger();
  panel.findChild<QDoubleSpinBox *>("filterHue")->setValue(.5);
  assert(publications==2&&effect.filter.resource==4&&effect.filter.hue==.5f);
  const auto changed=TextureDocument::evaluate(target,64,64).authored.base_color;
  assert(std::abs(changed[0]-.8f)<1e-6&&std::abs(changed[1]-.6f)<1e-6&&std::abs(changed[2]-.2f)<1e-6);
  const auto flags=layer_edit_channels(before.set(1),target,edit_set_state(old_state,target.id),edit_set_state(witness.edit_state(),target.id));assert(flags[0]);
  const auto rows=gpu_content_effects(fill_layer,0);assert(rows.size()==1&&rows[0][1][0]==4&&rows[0][2][0]==.5f);
  for(int kind=1;kind<=6;++kind){effect.filter={};effect.filter.resource=kind;effect.filter.hue=.5;effect.filter.contrast=1.5;effect.filter.output_high=.7;
   const std::array<float,3> input{-.2f,.4f,1.2f};fill_layer.values.base_color={input[0],input[1],input[2],1};
   const auto expected=filter_rgb(effect.filter,input);const auto actual=TextureDocument::evaluate(target,64,64).authored.base_color;
   for(int c=0;c<3;++c)assert(std::abs(expected[c]-actual[c])<1e-6);
  }
  // A bitmap material Fill must filter its sampled source, not its white tint.
  auto image=std::make_shared<FillTexture>();image->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();image->hash=std::string(64,'a');image->name="Material input";image->width=image->height=1;
  image->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(std::vector<FillTexture::Pixel>{{.2,.4,.8,1}});
  fill_layer.fill.native_material.reset();fill_layer.fill.texture=image;fill_layer.values.base_color={1,1,1,1};
  effect.filter={};effect.filter.resource=4;effect.filter.hue=.5;
  const auto sample=TextureDocument::evaluate(target,64,64).authored.base_color;
  assert(std::abs(sample[0]-.8)<1e-6&&std::abs(sample[1]-.6)<1e-6&&std::abs(sample[2]-.2)<1e-6);
  effect.blending=LayerBlend::Disabled;assert(std::abs(TextureDocument::evaluate(target,64,64).authored.base_color[0]-.2)<1e-6);effect.blending=LayerBlend::Normal;
  effect.opacity=0;assert(std::abs(TextureDocument::evaluate(target,64,64).authored.base_color[0]-.2)<1e-6);effect.opacity=1;
  // A scalar mask has no hue. HSV Value still changes its coverage.
  fill_layer.mask=LayerMask{};fill_layer.mask->background=.6;target.selected_mask=true;TextureDocument::add_filter_effect(target);
  auto &mask=fill_layer.mask->effects.back();mask.filter.resource=4;mask.filter.hue=.5;
  assert(std::abs(TextureDocument::mask_value(target,id,64,64)-.6)<1e-6);
  mask.filter.value_scale=.5;assert(std::abs(TextureDocument::mask_value(target,id,64,64)-.3)<1e-6);
 }
 std::cout<<"Empty filters, signed invert/clamp/posterize, HSV, contrast, levels, ordered mask/content targets, opacity, metadata/preset and context-free comparison fast path passed\n";
}
