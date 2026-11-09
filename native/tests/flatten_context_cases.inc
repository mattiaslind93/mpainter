// SPDX-License-Identifier: GPL-3.0-or-later
 {
  TextureSet set;set.texture_resolution=128;set.precision.fill(ChannelPrecision::RGB32F);set.precision[4]=ChannelPrecision::L32F;
  auto &external=set.layers.front();external.kind=LayerKind::Fill;external.visible=false;external.values.base_color={.2,.4,.6,1};LevelsEffect point;point.kind=ContentEffectKind::Anchor;point.anchor_name="Outside group";external.effects={point};MaskEffect mask_point;mask_point.kind=MaskEffectKind::Anchor;mask_point.anchor_name="Outside mask";external.mask=LayerMask{};external.mask->background=.35;external.mask->effects={mask_point};
  TextureDocument::add_fill(set);const auto child=set.selected_layer;auto &consumer=TextureDocument::layer(set,child);consumer.values.base_color={1,1,1,1};consumer.fill.anchors[0]={point.id,0,0,true};consumer.mask=LayerMask{};consumer.mask->background=.15;MaskEffect generator;generator.kind=MaskEffectKind::Generator;generator.generator=3;generator.input_anchors[0]={mask_point.id};consumer.mask->effects={generator};LevelsEffect rough;rough.kind=ContentEffectKind::Generator;rough.generator=4;rough.channel=2;rough.input_anchors[0]={point.id,0,2,true};consumer.effects={rough};
  TextureDocument::group_layers(set,{child});const auto selected=set.selected_layer;TextureDocument::layer(set,selected).mask=LayerMask{};TextureDocument::layer(set,selected).mask->background=.4;
  auto baked=flatten_layer_pixels(set,selected,cancel);assert(baked&&AnchorEvaluationScope::source()==nullptr);bind(*baked);auto after=set;install_flattened_layer(after,baked->layer,selected);same(set,after);
  // Memory depends on actual RAM, not a per-sequence 16M texel/512MiB cap.
  auto large=set;large.texture_resolution=4096;large.tiles={1001,1002};const auto plan=prepare_flatten_layer(large,selected);assert(plan.snapshot_bytes>512ULL*1024*1024);
  {TextureRamQueryScope ram(+[]()->std::uint64_t{return 32ULL*1024*1024*1024;});assert(plan.snapshot_bytes<flatten_available_ram_bytes());}
  // Exercise admission independently of workload size: the old refusal probe
  // never evaluated its33M anchored pixels. Keep that size as a planning check.
  {TextureRamQueryScope ram(+[]()->std::uint64_t{return 0;});const auto unrestricted=flatten_layer_pixels(set,selected,cancel);assert(unrestricted&&AnchorEvaluationScope::source()==nullptr);
   for(int channel=0;channel<15;++channel){assert(unrestricted->images[channel].size()==baked->images[channel].size());for(const auto &[tile,image]:baked->images[channel]){const auto &actual=unrestricted->images[channel].at(tile);assert(actual.width==image.width&&actual.height==image.height&&*actual.pixels==*image.pixels);}}
  }
 }
