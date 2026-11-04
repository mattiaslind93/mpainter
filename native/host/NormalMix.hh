#pragma once
#include "TextureDocument.hh"
#include "NormalVector.hh"
namespace bts {
struct BakedNormalSample {NormalVector direction{0,0,1};float coverage=0;};
BakedNormalSample sample_baked_normal(const TextureSet &,double u,double v,int tile=1001);
NormalVector mix_baked_normal(const TextureSet &,NormalVector authored,bool has_authored,const BakedNormalSample &);
}
