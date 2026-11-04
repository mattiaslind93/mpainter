#pragma once
#include "TextureDocument.hh"
namespace bts {
struct BakedAoSample {float value=1,coverage=0;};
BakedAoSample sample_baked_ao(const TextureSet &,double u,double v,int tile=1001);
float mix_ambient_occlusion(const TextureSet &,float authored,bool has_authored,const BakedAoSample &);
}
