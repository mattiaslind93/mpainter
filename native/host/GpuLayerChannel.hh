#pragma once
#include "TextureDocument.hh"
namespace bts {
// View of authored channel metadata; the GPU owns evaluation and cached pixels.
struct GpuLayerChannel {
 bool enabled=false;float opacity=0;LayerBlend blend=LayerBlend::Normal;
 std::array<float,3> value{};
 const TilePaintPixels *pixels=nullptr;
 int plane=0,fill_channel=0;
};
inline bool gpu_scalar_channel(int channel){return (channel>=7&&channel<=14)||channel==16;}
inline bool gpu_authored_channel(const TextureSet &set,int channel){
 if(channel==0||channel==1||channel==2||channel==4)return true;
 if(channel>=7&&channel<=14)return true;
 if(channel==16)return true;
 if(channel==6)return true;
 if(channel==5)return true;
 return false; // Derived normal has its own GPU derivation pass.
}
inline bool gpu_channel_enabled(const TextureSet &set,int channel){
 if(channel==6)return set.channels[3];
 if(channel==5)return set.ao_channel;
 if(channel==16)return set.displacement.channel;
 if(channel>=7&&channel<=14)return set.users[channel-7].present;
 return set.channels.at(channel);
}
inline ChannelPrecision gpu_channel_precision(const TextureSet &set,int channel){
 if(channel>=7&&channel<=14)return set.users[channel-7].precision;
 if(channel==16)return set.displacement.precision;
 if(channel==6)return set.precision[3];
 return set.precision.at(channel);
}
inline std::array<float,3> gpu_channel_base(const TextureSet &set,int channel){
 if(gpu_scalar_channel(channel))return {};
 if(channel==6){auto result=precision_channel(set.channels[3]?set.base_values:MaterialValues{},3,set.precision[3]);for(auto &v:result)v=v*2-1;return result;}
 if(channel==5)return precision_channel(set.ao_channel?set.base_values:MaterialValues{},5,set.precision[5]);
 return precision_channel(set.channels[channel]?set.base_values:MaterialValues{},channel,gpu_channel_precision(set,channel));
}
inline GpuLayerChannel gpu_layer_channel(const TextureLayer &layer,int channel){
 GpuLayerChannel out;if(channel==6)channel=3;out.fill_channel=channel;
 if(gpu_scalar_channel(channel)){
  const auto &scalar=channel==16?layer.displacement:layer.users.at(channel-7);
  out.enabled=scalar.enabled;out.opacity=scalar.opacity;out.blend=scalar.blending;out.value.fill(scalar.value);out.pixels=&scalar.pixels;out.plane=2;out.fill_channel=channel==16?14:channel-1;
 }else if(channel==5){
  out.enabled=layer.ao_enabled;out.opacity=layer.ao_opacity;out.blend=layer.ao_blending;out.value.fill(layer.values.ambient_occlusion);out.pixels=&layer.ao_pixels;out.plane=2;
 }else{
  out.enabled=layer.channels.at(channel);out.opacity=layer.opacity.at(channel);out.blend=layer.blending.at(channel);out.pixels=&layer.pixels;out.plane=channel;
  if(channel==3)out.value=layer.values.normal;else if(channel==0)out.value={layer.values.base_color[0],layer.values.base_color[1],layer.values.base_color[2]};else out.value.fill(channel==1?layer.values.metallic:channel==2?layer.values.roughness:layer.values.bump);
 }
 return out;
}
}
