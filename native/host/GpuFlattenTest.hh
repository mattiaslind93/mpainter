#pragma once
#include "FlattenLayer.hh"
#include "GpuLayerPreview.hh"
#include "GeometryMask.hh"
#include <cstdio>
namespace bts {
inline int verify_gpu_flatten(){
 TextureSet original;original.texture_resolution=128;original.texture_height=192;original.size_linked=false;original.tiles={1001,1002};original.tile_resolutions[1002]=128;original.uniform_editable=true;original.precision.fill(ChannelPrecision::RGB32F);original.displacement.channel=true;original.displacement.precision=ChannelPrecision::L32F;original.ao_channel=true;
 original.layers.front().kind=LayerKind::Fill;original.layers.front().values.base_color={.13f,.24f,.4f,1};original.layers.front().values.roughness=.21f;
 TextureDocument::add_fill(original);const auto first=original.selected_layer;auto &a=TextureDocument::layer(original,first);a.values.base_color={.7f,.3f,1.4f,1};a.values.bump=-.2f;a.opacity[0]=.6f;a.displacement.enabled=true;a.displacement.value=-.003f;a.ao_enabled=true;a.values.ambient_occlusion=.35f;
 TextureDocument::add_fill(original);const auto second=original.selected_layer;auto &b=TextureDocument::layer(original,second);b.values.base_color={.2f,.8f,.1f,1};b.opacity[0]=.4f;b.channels[2]=false;b.displacement.enabled=true;b.displacement.value=1.2f;b.ao_enabled=true;b.ao_opacity=.3f;
 TextureDocument::group_layers(original,{first,second});const auto group=original.selected_layer;auto &folder=TextureDocument::layer(original,group);folder.mask.emplace();folder.mask->background=.35f;folder.opacity[0]=.7f;
 TextureDocument::add_user_channel(original,0);original.users[0].precision=ChannelPrecision::L32F;TextureDocument::layer(original,first).users[0].value=.73f;TextureDocument::layer(original,group).users[0].enabled=true;
 int cases=0;double maximum=0;std::uint64_t ticket=0;
 for(int variant=0;variant<8;++variant){auto set=original;QUuid selected=group;
  if(variant==1){TextureDocument::layer(set,second).visible=false;auto &mask=TextureDocument::layer(set,first).mask;mask.emplace();mask->background=.25f;}
  if(variant==2){selected=first;auto &root=TextureDocument::layer(set,first);root.opacity[0]=0;root.channels[2]=false;root.geometry.type=1;root.geometry.tiles_default_include=false;root.geometry.set_tile(1002,true);}
  if(variant==3){TextureDocument::add_paint(set);selected=set.selected_layer;auto &l=TextureDocument::layer(set,selected);l.blending[0]=LayerBlend::Passthrough;l.opacity[0]=.6f;LevelsEffect e;e.channel=0;e.gamma=2;l.effects.push_back(e);l.mask.emplace();l.mask->background=.4f;}
  if(variant==4){selected=second;auto &l=TextureDocument::layer(set,selected);l.blending[2]=LayerBlend::Passthrough;l.opacity[2]=.7f;l.channels[2]=true;LevelsEffect e;e.channel=2;e.gamma=2;l.effects.push_back(e);}
  if(variant==6){auto &g=TextureDocument::layer(set,group).geometry;g.default_include=false;g.set("Left",true);set.geometry_names={{1,"Left"},{2,"Right"}};for(int tile:set.tiles){auto owner=std::make_shared<GeometryOwnershipTile>();owner->width=set.width(tile);owner->height=set.rows(tile);owner->owners.resize(std::size_t(owner->width)*owner->height);for(int y=0;y<owner->height;++y)for(int x=0;x<owner->width;++x)owner->owners[y*owner->width+x]=x<owner->width/2?1:2;set.geometry_ownership[tile]=owner;}}
  if(variant==7){auto &outside=set.layers.front();LevelsEffect point;point.kind=ContentEffectKind::Anchor;point.anchor_name="Outside flatten group";outside.effects={point};outside.mask=LayerMask{};outside.mask->background=.35;MaskEffect marker;marker.kind=MaskEffectKind::Anchor;marker.anchor_name="Outside mask";outside.mask->effects={marker};auto &child=TextureDocument::layer(set,first);child.fill.anchors[0]={point.id,0,0,true};child.mask=LayerMask{};MaskEffect generator;generator.kind=MaskEffectKind::Generator;generator.generator=5;generator.input_anchors={AnchorReference{point.id,0,1,true},AnchorReference{marker.id}};child.mask->effects={generator};LevelsEffect rough;rough.kind=ContentEffectKind::Generator;rough.generator=4;rough.channel=2;rough.input_anchors[0]={point.id,0,2,true};child.effects={rough};}
  std::atomic_bool cancel=false;auto expected=flatten_layer_pixels(set,selected,cancel);auto plan=prepare_flatten_layer(set,selected);GpuLayerPreview gpu;
  for(int tile:set.tiles)for(int c=0;c<16;++c){const bool mask=c==15;if(mask?!expected->layer.mask:!expected->images[c].contains(tile))continue;if(mask&&!expected->images[0].contains(tile))continue;
   const int width=mask?plan.mask_canvas:set.width(tile),height=mask?plan.mask_canvas:set.rows(tile);LayerCaptureRequest request{selected,mask?0:c,tile,0,0,width,height,width,height,mask,!mask&&plan.contextual[c]};
   if(variant==5){request.x=17;request.y=23;request.width=71;request.height=79;}
   GpuPreviewUpdate state;for(int attempt=0;;++attempt){gpu.begin_frame();state=gpu.capture_layer(set,request,++ticket);if(state!=GpuPreviewUpdate::Pending)break;if(attempt>100)throw std::runtime_error("GPU flatten oracle never became ready");}
   if(state!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU flatten oracle unsupported");glFinish();gpu.displacement_summaries();auto actual=gpu.layer_capture_result();if(!actual||!actual->valid||actual->pixels->size()!=std::size_t(request.width)*request.height)throw std::runtime_error("GPU flatten packet incomplete");
   for(int by=0;by<request.height;++by)for(int bx=0;bx<request.width;++bx){const int x=bx+request.x,y=by+request.y;const auto index=std::size_t(y)*width+x;const auto p=(*actual->pixels)[by*request.width+bx];const auto q=mask?std::array<float,4>{TextureDocument::mask_value(set,selected,x*set.width(tile)/width,y*set.rows(tile)/height,tile),0,0,1}:(*expected->images[c].at(tile).pixels)[index];
    for(int k=0;k<4;++k){if(!mask&&k<3&&q[3]==0&&p[3]==0)continue;const double error=std::abs(p[k]-q[k]);maximum=std::max(maximum,error);if(error>2e-5){std::fprintf(stderr,"BTS_GPU_FLATTEN_DIFFERENCE variant=%d tile=%d channel=%d xy=%d,%d component=%d expected=%.9g actual=%.9g\n",variant,tile,c,x,y,k,q[k],p[k]);throw std::runtime_error("GPU flatten differs from CPU export reference");}}
   }++cases;
  }
 }
 std::printf("BTS_GPU_FLATTEN_VERIFIED cases=%d max_error=%.9g groups_masks_passthrough_udim_hdr=1 chunk_bound=256\n",cases,maximum);return cases;
}
}
