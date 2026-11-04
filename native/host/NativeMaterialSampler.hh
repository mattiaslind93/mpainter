#pragma once
#include "GPU_texture.hh"
namespace bts {
// Resolve the material's anisotropy request before entering the sampler cache.
// The caller supplies the global U compute policy; no Scene is available here.
// Match PassBase::material_set without mutating the stored/shared sampler.
inline blender::GPUSamplerState native_material_image_sampler(blender::GPUSamplerState state,blender::GPUSamplerFiltering anisotropic_filtering,bool mipmaps_suspended){
 using namespace blender;
 if(state.type!=GPU_SAMPLER_STATE_TYPE_PARAMETERS)return state;
 if(state.filtering&GPU_SAMPLER_FILTERING_ANISOTROPIC_ENABLE){
  state.disable_filtering_flag(GPU_SAMPLER_FILTERING_ANISOTROPIC_MASK);
  state.enable_filtering_flag(anisotropic_filtering);
 }
 if(mipmaps_suspended)state.disable_filtering_flag(GPU_SAMPLER_FILTERING_MIPMAP);
 return state;
}
}
