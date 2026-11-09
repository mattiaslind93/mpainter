// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaskEffectContext.hh"
#include "BumpPrecision.hh"
#include "EvaluationScope.hh"
#include "FillTextureIO.hh"
#include "UniformChannels.hh"
#include "NativeCompositeJob.hh"
#include "CompositeMemory.hh"
#include "NormalMix.hh"
#include "HeightNormal.hh"
#include "AmbientOcclusion.hh"
#include "EditTiming.hh"
#include "IMB_imbuf.hh"
#include "BLI_task.hh"
#include "IMB_imbuf_types.hh"
#include <algorithm>
#include <memory>
namespace bts {
namespace {
template<class Function> void composite_rows(int height,bool preview,const Function &apply){
  const auto run=[&](blender::IndexRange range){PreviewSamplingScope sampling(preview);performance::Scope timing("composite_cpu_chunk");for(int y:range)apply(y);};
  if(preview||height<128||std::getenv("BTS_COMPOSITE_SERIAL"))run(blender::IndexRange(height));
  else blender::threading::parallel_for(blender::IndexRange(height),32,run);
}
std::array<float,4> encoded(const MaterialValues &v,int c){switch(c){case 0:return v.base_color;case 1:return {v.metallic,v.metallic,v.metallic,1};case 2:return {v.roughness,v.roughness,v.roughness,1};case 3:return {v.normal[0]*.5f+.5f,v.normal[1]*.5f+.5f,v.normal[2]*.5f+.5f,1};default:return {v.bump,v.bump,v.bump,1};}}
inline thread_local double job_padding_ms=0;
void pad(std::vector<float> &pixels,int width,int height,int tile,bool adjacent,const std::shared_ptr<TexturePadding> &padding){if(pixels.empty()||!padding)return;performance::Scope timing("padding");const auto padding_start=PropertyClock::now();using Buffer=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;Buffer buffer(blender::IMB_allocImBuf(width,height,blender::ImBufFlags::FloatData),blender::IMB_freeImBuf);if(!buffer)throw std::bad_alloc();std::copy(pixels.begin(),pixels.end(),buffer->float_data_for_write());padding->apply(*buffer,tile,adjacent);std::copy_n(buffer->float_data(),pixels.size(),pixels.begin());job_padding_ms+=property_ms(padding_start);}
}
std::uint64_t native_composite_available_bytes(){return texture_available_ram_bytes();}
std::optional<NativeCompositeResult> native_composite_job(const TextureSet &source,bool ao,std::shared_ptr<TexturePadding> padding,std::uint64_t generation,const std::atomic_bool &cancel,bool preview,CompositeChannels changed){const auto job_start=PropertyClock::now();job_padding_ms=0;PreviewSamplingScope sampling(preview);EditTiming timing(preview?"BTS_LAYER_PREVIEW_MS":"BTS_FINALIZE_MS");const auto stopped=[&]{return cancel.load(std::memory_order_relaxed);};if(stopped())return {};auto set=source;
 if(padding&&std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return (layer.kind==LayerKind::Fill&&(layer.fill.atom||fill_has_texture(layer.fill)||fill_spatial_projection(layer.fill))&&fill_surface_projection(layer.fill))||(layer.mask&&any_mask_effect(layer.mask->effects,[](const auto &effect){return effect.enabled&&effect.kind==MaskEffectKind::Fill&&effect.anchor_source.isNull()&&fill_surface_projection(effect.mapping.projection);}));})){
   for(int tile:set.tiles)if(!set.projection_tiles.contains(tile)||set.projection_tiles.at(tile)->resolution!=set.width(tile)||set.projection_tiles.at(tile)->height!=set.rows(tile)){if(stopped())return {};set.projection_tiles[tile]=padding->projection_geometry(set.width(tile),{tile},false,set.rows(tile));}
   set.projection_geometry=set.projection_tiles.begin()->second;
 }
 const auto needed=composite_dependencies(set,ao,changed);const auto budget=native_composite_available_bytes();const auto required=composite_peak_bytes(set,ao,changed);NativeCompositeResult result;result.uid=set.material_session_uid;result.generation=generation;result.preview=preview;result.channels=changed;std::atomic_bool normal_present=false,ao_present=false;std::array<bool,6> primary{};for(int c=0;c<6;++c)primary[c]=needed[c];
  std::array<std::array<float,3>,5> base;for(int c=0;c<5;++c)base[c]=precision_channel(set.channels[c]?set.base_values:MaterialValues{},c,set.precision[c]);if(set.channels[4])for(auto &component:base[4])component+=set.bump_offset();const float height_base=base[4][0];const auto normal_base=encoded(set.channels[3]?set.base_values:MaterialValues{},3);
  for(int tile:set.tiles){const int width=set.width(tile),height=set.rows(tile);result.sizes[tile]={width,height};auto &output=result.tiles[tile];for(int c=0;c<17;++c)if(needed[c])output[c].resize(std::size_t(width)*height*4);
    std::array<bool,6> constants{},variable=primary;bool has_constants=false;
    if(!std::getenv("BTS_COMPOSITE_SAMPLED"))for(int c=0;c<6;++c)if(primary[c]&&uniform_channel(set,c,tile)){constants[c]=true;variable[c]=false;has_constants=true;}
    const auto uniform=has_constants?TextureDocument::evaluate_channels(set,primary,0,0,tile):UniformComposite{};
    const bool sample_pixels=std::any_of(variable.begin(),variable.end(),[](bool on){return on;});
    composite_rows(height,preview,[&](int y){if(stopped())return;bool row_normal=false,row_ao=false;for(int x=0;x<width;++x){auto composite=sample_pixels?TextureDocument::evaluate_channels(set,variable,x,y,tile):uniform;if(has_constants&&sample_pixels)for(int c=0;c<6;++c)if(constants[c])copy_composite_channel(composite,uniform,c);const auto index=(std::size_t(y)*width+x)*4;if(needed[15]){const float mask=TextureDocument::mask_value(set,set.selected_layer,x,y,tile);for(int k=0;k<3;++k)output[15][index+k]=mask;output[15][index+3]=1;}row_normal|=composite.coverage[3]>0;row_ao|=composite.ao_coverage>0;for(int c=0;c<5;++c)if(needed[c]){auto rgba=encoded(composite.authored,c);rgba[3]=composite.coverage[c];for(int k=0;k<3;++k)rgba[k]*=rgba[3];if(c!=3){auto value=precision_channel(composite.material,c,set.precision[c]);if(c==4)value.fill(precision_bump_output(set,composite.material.bump));for(int k=0;k<3;++k)rgba[k]=value[k]-base[c][k]*(1-rgba[3]);}std::copy(rgba.begin(),rgba.end(),output[c].begin()+index);}if(needed[6]){const auto normal=precision_channel(composite.material,3,set.precision[3]);for(int k=0;k<3;++k)output[6][index+k]=normal[k];output[6][index+3]=1;}if(needed[5]){for(int k=0;k<3;++k)output[5][index+k]=composite.material.ambient_occlusion;output[5][index+3]=1;}for(int slot=0;slot<8;++slot)if(needed[7+slot]){const auto value=TextureDocument::composite_user(set,slot,x,y,tile);for(int k=0;k<3;++k)output[7+slot][index+k]=value[0]*value[1];output[7+slot][index+3]=value[1];}if(needed[16]){const auto value=TextureDocument::composite_displacement(set,x,y,tile);for(int k=0;k<3;++k)output[16][index+k]=value[0]*value[1];output[16][index+3]=value[1];}}if(row_normal)normal_present.store(true,std::memory_order_relaxed);if(row_ao)ao_present.store(true,std::memory_order_relaxed);});
    if(stopped())return {};if(!preview&&needed[4])pad(output[4],width,height,tile,set.padding_3d,padding);
  }
  const auto height_at=[&](int x,int y,int tile){int source=tile,width=set.width(tile),height=set.rows(tile);const int col=(tile-1001)%10,row=(tile-1001)/10;int cx=col,cy=row;double u=(x+.5)/width,v=(y+.5)/height;if(u<0){--cx;u+=1;}else if(u>=1){++cx;u-=1;}if(v<0){--cy;v+=1;}else if(v>=1){++cy;v-=1;}const int neighbor=1001+cx+10*cy;if(cx>=0&&cx<10&&cy>=0&&cy<100&&result.tiles.contains(neighbor))source=neighbor;else{u=(std::clamp(x,0,width-1)+.5)/width;v=(std::clamp(y,0,height-1)+.5)/height;}width=set.width(source);height=set.rows(source);const double sx=u*width-.5,sy=v*height-.5;const int ix=int(std::floor(sx)),iy=int(std::floor(sy));const auto at=[&](int px,int py){const auto &data=result.tiles.at(source)[4];const auto index=(std::size_t(std::clamp(py,0,height-1))*width+std::clamp(px,0,width-1))*4;return height_base*(1-data[index+3])+data[index];};return std::lerp(std::lerp(at(ix,iy),at(ix+1,iy),float(sx-ix)),std::lerp(at(ix,iy+1),at(ix+1,iy+1),float(sx-ix)),float(sy-iy));};
  for(auto &[tile,output]:result.tiles){const int width=set.width(tile),height=set.rows(tile);composite_rows(height,preview,[&](int y){if(stopped())return;for(int x=0;x<width;++x){const auto index=(std::size_t(y)*width+x)*4;if(needed[3]){auto *rgba=output[3].data()+index;const auto baked=sample_baked_normal(set,(x+.5)/width,(y+.5)/height,tile);NormalVector authored;const auto basis=set.channels[3]?set.base_values.normal:NormalVector{0,0,1};for(int k=0;k<3;++k)authored[k]=((basis[k]*.5f+.5f)*(1-rgba[3])+rgba[k])*2-1;auto mixed=mix_baked_normal(set,authored,normal_present.load(std::memory_order_relaxed),baked);const auto detail=height_normal(set.bump_method,width,x,y,tile,height_at,height);const bool height_effect=set.channels[4]&&(std::abs(detail[0])+std::abs(detail[1])>1e-8f||bump_normal_coverage(output[4][index+3]));if(height_effect)mixed=reoriented_normal(mixed,detail);if(baked.coverage>1e-8f||height_effect){for(int k=0;k<3;++k)rgba[k]=mixed[k]*.5f+.5f;rgba[3]=1;}std::array<float,3> full;for(int k=0;k<3;++k)full[k]=rgba[k]+normal_base[k]*(1-rgba[3]);const auto quantized=precision_rgb(full,set.precision[3]);for(int k=0;k<3;++k)rgba[k]=quantized[k]-base[3][k]*(1-rgba[3]);}if(needed[5]){const auto baked_ao=sample_baked_ao(set,(x+.5)/width,(y+.5)/height,tile);const float raw=mix_ambient_occlusion(set,output[5][index],ao_present.load(std::memory_order_relaxed),baked_ao);const float value=precision_rgb({raw,raw,raw},set.precision[5])[0];for(int k=0;k<3;++k)output[5][index+k]=value;}}});
    if(stopped())return {};
    for(int c=0;c<17;++c)if(!preview&&changed[c]&&c!=4&&c!=15&&!output[c].empty()){if(stopped())return {};pad(output[c],width,height,tile,set.padding_3d&&c!=3&&c!=6,padding);}
    for(int c=0;c<17;++c)if(!changed[c])output[c].clear();
  }result.trace.composite_ms=property_ms(job_start)-job_padding_ms;result.trace.padding_ms=job_padding_ms;return result;
}
}
