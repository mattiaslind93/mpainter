{
 // Content anchors expose a channel-local prefix before their marker. The raw
 // cache is independent of parent visibility, mix, mask and enabled channels.
 set=TextureSet{};set.texture_resolution=512;set.precision.fill(ChannelPrecision::RGB32F);set.precision[4]=ChannelPrecision::L32F;set.displacement.channel=true;set.users[0].present=true;
 set.layers.front().kind=LayerKind::Fill;auto &producer=set.layers.front();producer.visible=false;producer.opacity.fill(0);producer.channels.fill(false);producer.values.base_color={.2,.4,.6,1};producer.values.bump=-.25;producer.displacement.value=-.002;producer.users[0].value=2.25;producer.mask=LayerMask{};producer.mask->background=0;
 LevelsEffect point;point.kind=ContentEffectKind::Anchor;point.anchor_name="Raw source";producer.effects={point};LevelsEffect post;post.kind=ContentEffectKind::Filter;post.filter.resource=1;producer.effects.push_back(post);
 TextureDocument::add_fill(set);set.layers.back().values.base_color={.8,.3,.1,1};set.layers.back().mask=LayerMask{};MaskEffect link;link.kind=MaskEffectKind::Fill;link.anchor_source=point.id;set.layers.back().mask->effects={link};
 target=texture("BTS GPU content anchors",512,512,gpu::TextureFormat::SFLOAT_16_16_16_16);
 const auto anchor_cases_start=cases;
 for(int channel:{0,2,4,5,6,14})for(int component=0;component<=4;++component)for(bool coverage:{false,true}){
  auto &ref=set.layers.back().mask->effects.front();ref.anchor_channel=channel;ref.anchor_component=component;ref.anchor_coverage=coverage;
  if(channel==5)set.ao_channel=true;
  check(0);check(2);
 }
 for(int source_channel:{0,2,4,5,6,14}){preview.begin_frame();const AnchorReference reference{point.id,source_channel,0,false};const auto input=preview.anchor_input(set,set.layers.back().id,reference,1001);if(!input||!input->enabled||input->mask)throw std::runtime_error("Raw content anchor unavailable");const std::array<std::array<int,3>,1> position{{{std::min(256,GPU_texture_width(input->image)-1),std::min(256,GPU_texture_height(input->image)-1),0}}};const auto actual=probe_gpu_texture(input->image,position).front(),expected=TextureDocument::content_anchor_source(set,point.id,source_channel,256,256);for(int c=0;c<4;++c)if(std::abs(actual[c]-expected[c])>1e-6f*std::max(1.f,std::abs(expected[c])))throw std::runtime_error("Raw anchor channel lost signed/HDR/coverage values");}
 auto &content_link=set.layers.back().mask->effects.front();content_link.anchor_channel=0;content_link.anchor_component=2;content_link.anchor_coverage=true;check(0);
 set.layers[0].values.base_color[1]=.7;check(0);
 set.layers[0].effects.back().filter.resource=2;const auto content_anchor_work=preview.impl_->mask_dispatch_texels;check(0);if(preview.impl_->mask_dispatch_texels!=content_anchor_work)throw std::runtime_error("Content post-anchor change invalidated consumer");
 set.layers[0].kind=LayerKind::Paint;set.layers[0].pixels={};check(0);content_link.anchor_coverage=false;check(0);content_link.anchor_coverage=true;
 auto content_ink=mask;content_ink.values.base_color={.2,.4,.6,1};content_ink.channels={true,false,false,false,false};content_ink.flow=.5;content_ink.center={.05,.05};content_ink.axis_x={.01,0};content_ink.axis_y={0,.01};set.layers[0].pixels.stamp(512,content_ink);check(0);
 const auto old_content=set.layers[0].pixels;content_ink.center={.1,.1};set.layers[0].pixels.stamp(512,content_ink);const auto content_dirty_start=preview.impl_->mask_dispatch_texels;check(0);const auto content_dirty=preview.impl_->mask_dispatch_texels-content_dirty_start;if(!content_dirty||content_dirty>=512*512)throw std::runtime_error("Content anchor did not preserve sparse chunk invalidation");set.layers[0].pixels=old_content;check(0);
 set.layers[0].effects.front().enabled=false;check(0);set.layers[0].effects.front().enabled=true;check(0);
 set.channels[0]=false;check(2);set.channels[0]=true;check(0);std::swap(set.layers[0],set.layers[1]);check(0);std::swap(set.layers[0],set.layers[1]);check(0);
 // A folder anchor exposes its child composite even when the folder's own
 // visibility, opacity and mask remove it from the ordinary material stack.
 auto leaf=set.layers.front(),consumer=set.layers.back();leaf.kind=LayerKind::Fill;leaf.visible=true;leaf.opacity.fill(1);leaf.channels.fill(true);leaf.mask.reset();leaf.effects.clear();leaf.values.base_color={.25,.5,.75,1};TextureLayer folder;folder.kind=LayerKind::Folder;folder.visible=false;folder.opacity.fill(0);folder.mask=LayerMask{};folder.effects={point};leaf.parent=folder.id;leaf.depth=1;set.layers={leaf,folder,consumer};
 for(int source_channel:{0,2,4,6,14}){auto &ref=set.layers.back().mask->effects.front();ref.anchor_channel=source_channel;ref.anchor_component=1;check(0);}
 set.layers[0].values.base_color[0]=.7;set.layers.back().mask->effects.front().anchor_channel=0;check(0);
 set.tiles={1001,1002,1011};set.layers[0].kind=LayerKind::Paint;set.layers[0].pixels={};auto tile_ink=content_ink;tile_ink.center={1.5,.5};tile_ink.axis_x={.3,0};tile_ink.axis_y={0,.3};tile_ink.flow=1;tile_ink.values.base_color={.3,.8,.2,1};set.layers[0].pixels.stamp(512,tile_ink,set.tiles);
 for(int tile:set.tiles){preview.begin_frame();if(preview.update(set,0,target.get(),tile)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Content anchor UDIM tile deferred");++cases;}
 std::printf("BTS_GPU_CONTENT_ANCHORS_VERIFIED cases=%d channels=6 components=5 coverage_modes=2 hidden_disabled_masked_source=1 prefix_only=1 signed_hdr=1 post_anchor_unchanged=1 folder_prefix=1 udim=3 dirty_texels=%llu undo_broken_forward=1\n",cases-anchor_cases_start,(unsigned long long)content_dirty);
}
