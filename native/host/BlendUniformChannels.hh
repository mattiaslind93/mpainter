#pragma once
#include "TextureDocument.hh"
namespace bts {
// A three-channel preset must never flatten authored Bump or legacy internal
// Normal data, including when replacing values in an existing fill layer.
inline void apply_blend_uniform_channels(TextureLayer &layer,const MaterialValues &values,bool fresh)
{
  layer.values.base_color=values.base_color;
  layer.values.metallic=values.metallic;
  layer.values.roughness=values.roughness;
  for(int channel:{0,1,2})fill_channel_texture(layer.fill,channel).reset();
  layer.fill.atom=false;
  if(fresh){layer.channels={true,true,true,false,false};layer.ao_enabled=false;}
}
}
