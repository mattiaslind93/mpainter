#pragma once
#include <array>
#include <optional>
namespace bts {
using MaterialIdColor=std::array<int,3>;
// Categorical RGB codes: no color management, interpolation or dithering.
inline constexpr std::array<MaterialIdColor,30> material_id_palette{{
 {230,25,75},{60,180,75},{255,225,25},{0,130,200},{245,130,48},
 {145,30,180},{70,240,240},{240,50,230},{210,245,60},{250,190,212},
 {0,128,128},{220,190,255},{170,110,40},{255,250,200},{128,0,0},
 {170,255,195},{128,128,0},{255,215,180},{0,0,128},{128,128,128},
 {255,255,255},{65,65,65},{85,107,47},{0,70,70},{90,0,90},
 {0,90,255},{255,100,140},{100,60,0},{180,180,255},{110,200,150}
}};
template<class Layers> MaterialIdColor next_material_id(const Layers &layers){
 for(const auto &color:material_id_palette){bool used=false;for(const auto &layer:layers)used|=layer.material_id&&*layer.material_id==color;if(!used)return color;}
 return material_id_palette[layers.size()%material_id_palette.size()];
}
inline std::array<float,3> material_id_rgb(const MaterialIdColor &color){return {color[0]/255.f,color[1]/255.f,color[2]/255.f};}
}
