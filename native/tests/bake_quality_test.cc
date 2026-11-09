// SPDX-License-Identifier: GPL-3.0-or-later
#include "BakeQuality.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
int main(){
 int rays=256,sampling=4;bool denoise=true;
 bts::apply_bake_preset(bts::BakePreset::Draft,rays,sampling,denoise);assert(rays==64&&sampling==1&&!denoise);
 assert(bts::detect_bake_preset(rays,sampling,denoise)==bts::BakePreset::Draft);
 bts::apply_bake_preset(bts::BakePreset::Final,rays,sampling,denoise);assert(rays==256&&denoise);
 assert(bts::detect_bake_preset(17,1,true)==bts::BakePreset::Custom);
 const auto a=QUuid::createUuid(),b=QUuid::createUuid();QJsonObject legacy{{"rays",16},{"sampling",2},{"dilation",8},{"diffusion",true}};
 assert(bts::bake_quality(legacy,a,3)["rays"].toInt()==16);
 auto recipe=bts::with_bake_quality(legacy,a,3,{{"rays",128},{"dilation",2}});
 assert(bts::bake_quality(bts::with_bake_quality(legacy,a,3,{{"rays",512},{"sampling",8}}),a,3)["rays"].toInt()==512);
 assert(bts::bake_quality(bts::with_bake_quality(legacy,a,3,{{"rays",512},{"sampling",8}}),a,3)["sampling"].toInt()==8);
 assert(bts::bake_quality(recipe,a,3)["rays"].toInt()==128&&bts::bake_quality(recipe,a,3)["sampling"].toInt()==2);
 assert(bts::bake_quality(recipe,b,3)["rays"].toInt()==16&&bts::bake_quality(recipe,a,6)["rays"].toInt()==16);
 const auto synced=bts::synchronized_bake_quality(recipe,a,3,{b});
 assert(bts::bake_quality(synced,b,3)==bts::bake_quality(recipe,a,3));
 assert(bts::bake_quality(synced,b,6)==bts::bake_quality(recipe,b,6));
 bts::validate_map_bake_options(synced["map_options"]);
 const auto rejects=[](auto f){bool failed=false;try{f();}catch(const std::invalid_argument &){failed=true;}assert(failed);};
 rejects([]{bts::validate_bake_quality({{"sampling",3}});});
 assert(bts::bake_quality(bts::with_bake_quality(legacy,a,3,{{"rays",17}}),a,3)["rays"].toInt()==17);
 assert(bts::bake_quality(bts::with_bake_quality(legacy,a,3,{{"rays",511}}),a,3)["rays"].toInt()==511);
 rejects([]{bts::validate_bake_quality({{"rays",0}});});
 rejects([]{bts::validate_bake_quality({{"rays",513}});});
 rejects([]{bts::validate_bake_quality({{"rays",1.5}});});
 rejects([]{bts::validate_bake_quality({{"diffusion",1}});});
 rejects([]{bts::validate_bake_quality({{"unbounded",true}});});
 rejects([]{bts::validate_map_bake_options(QJsonObject{{"invalid-uuid",QJsonObject{}}});});
 rejects([&]{bts::with_bake_quality(recipe,a,7,{});});
 assert(recipe!=synced&&bts::bake_quality(recipe,b,3)["rays"].toInt()==16);
}
