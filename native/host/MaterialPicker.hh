#pragma once
#include "BrushSettings.hh"
#include "TextureDocument.hh"
#include "MaterialSample.hh"
namespace bts {
inline std::optional<MaterialSampleRequest> material_sample_request(const TextureSet &set,double u,double v,const BrushSettings &brush,bool color_only=false){
 if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>=10||v<0||v>=100)return {};
 const int column=int(std::floor(u)),row=int(std::floor(v)),tile=1001+column+10*row;
 if(!set.tiles.contains(tile))return {};
 MaterialSampleRequest request;request.tile=tile;request.x=std::clamp(int((u-column)*set.width(tile)),0,set.width(tile)-1);request.y=std::clamp(int((v-row)*set.rows(tile)),0,set.rows(tile)-1);
 if(set.selected_mask&&!color_only){const auto layer=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &l){return l.id==set.selected_layer;});if(layer==set.layers.end()||!layer->mask)return {};request.mask=layer->id;}
 for(int c=0;c<5;++c)request.channels[c]=set.channels[c]&&(color_only?c==0:brush.channels[c]);request.channels[5]=!color_only&&set.ao_channel&&brush.ao;
 return request;
}
inline void apply_material_sample(const MaterialSampleRequest &request,const MaterialSample &sample,BrushSettings &brush){
 if(!request.mask.isNull()){brush.mask_value=sample.mask;return;}
 const auto &v=sample.material;
 if(request.channels[0])brush.values.base_color=v.base_color;
 if(request.channels[1])brush.values.metallic=v.metallic;
 if(request.channels[2])brush.values.roughness=v.roughness;
 if(request.channels[3])brush.values.normal=v.normal;
 if(request.channels[4])brush.values.bump=v.bump;
 if(request.channels[5])brush.values.ambient_occlusion=v.ambient_occlusion;
}
// CPU oracle/export only. Interactive picking uses the asynchronous GPU packet.
// Match authoring channel precision, before final display/lighting transforms.
inline bool sample_brush_material(const TextureSet &set,int resolution,double u,double v,BrushSettings &brush) {
  const auto request=material_sample_request(set,u,v,brush);if(!request)return false;
  MaterialSample sample;if(!request->mask.isNull())sample.mask=TextureDocument::mask_value(set,request->mask,request->x,request->y,request->tile);
  else sample.material=TextureDocument::evaluate(set,request->x,request->y,request->tile).material;
  apply_material_sample(*request,sample,brush);
  return true;
}
}
