#include "BakeQuality.hh"
#include <cassert>
int main(){
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
