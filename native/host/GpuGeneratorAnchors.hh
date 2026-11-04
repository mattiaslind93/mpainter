#pragma once
#include "GpuLayerEffects.hh"
#include "GpuMaskComparison.hh"
namespace bts {
using GpuGeneratorInputs=std::array<GpuAnchorInput,2>;
inline std::optional<GpuGeneratorInputs> gpu_generator_inputs(const MeshGeneratorSettings &settings,const QUuid &consumer,const GpuAnchorResolver &resolve){
 GpuGeneratorInputs result;
 for(int slot=0;slot<generator_input_count(settings.generator);++slot)if(!settings.input_anchors[slot].source.isNull()){
   auto input=resolve(consumer,settings.input_anchors[slot]);if(!input)return {};result[slot]=*input;
 }
 return result;
}
inline void gpu_generator_dependencies(GpuStackNode &node,const MeshGeneratorSettings &settings,const GpuGeneratorInputs &inputs,int &page_slot){
 for(int slot=0;slot<generator_input_count(settings.generator);++slot){const auto &ref=settings.input_anchors[slot];if(ref.source.isNull())continue;
   node.parameters.append(reinterpret_cast<const char *>(&ref.channel),sizeof(ref.channel));node.parameters.append(reinterpret_cast<const char *>(&ref.component),sizeof(ref.component));node.parameters.append(reinterpret_cast<const char *>(&ref.coverage),sizeof(ref.coverage));gpu_anchor_dependencies(node,inputs[slot],page_slot);
 }
}
inline void gpu_generator_options(GpuEffectRow &row,const MeshGeneratorSettings &settings,const GpuGeneratorInputs &inputs){
 for(int slot=0;slot<generator_input_count(settings.generator);++slot){const auto &ref=settings.input_anchors[slot];if(ref.source.isNull())continue;
   row[4+slot]={float(ref.component),float(ref.coverage),float(inputs[slot].mask),float(inputs[slot].enabled)};row[6][slot]=1;if(!inputs[slot].enabled)row[2][1]=0;
 }
}
inline void gpu_generator_maps(std::array<blender::gpu::Texture *,6> &maps,const MeshGeneratorSettings &settings,const GpuGeneratorInputs &inputs){
 // Spatial generator 1/6 is a separate stage and never consumes Thickness.
 // Preserve its original Position/WorldNormal sampler there before an Anchor
 // replaces the semantic input. The no-atlas noise frame remains mesh-derived,
 // matching generator_surface() on CPU; Anchor coverage/components stay local.
 if((settings.triplanar_grunge||settings.surface_fallback)&&(settings.generator==1||settings.generator==6)&&!settings.input_anchors[0].source.isNull()){
  blender::gpu::Texture *original=nullptr;const int role=generator_input_role(settings.generator,0);
  for(int i=0;i<6;++i)if(gpu_mask_map_roles[i]==role)original=maps[i];
  for(int i=0;i<6;++i)if(gpu_mask_map_roles[i]==6)maps[i]=original;
 }
 for(int slot=0;slot<generator_input_count(settings.generator);++slot)if(!settings.input_anchors[slot].source.isNull()){
   const int role=generator_input_role(settings.generator,slot);for(int i=0;i<6;++i)if(gpu_mask_map_roles[i]==role)maps[i]=inputs[slot].image;
 }
}
}
