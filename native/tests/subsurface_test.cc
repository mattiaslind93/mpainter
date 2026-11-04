#include "ShaderInstances.hh"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace bts;
int main(){
 SubsurfacePreview display;assert(!display.enabled&&display.samples==16);display.enabled=true;display.samples=64;assert(SubsurfacePreview::from_json(display.to_json())==display);assert(SubsurfacePreview::from_json({})==SubsurfacePreview{});
 for(auto bad:{0,65}){auto j=display.to_json();j["samples"]=bad;bool failed=false;try{SubsurfacePreview::from_json(j);}catch(...){failed=true;}assert(failed);}auto fractional=display.to_json();fractional["samples"]=4.5;bool failed=false;try{SubsurfacePreview::from_json(fractional);}catch(...){failed=true;}assert(failed);
 const auto project=QUuid::createUuid(),a=QUuid::createUuid(),b=QUuid::createUuid();auto library=ShaderInstances::defaults(project,{a,b});const auto main=library.default_instance;auto parameters=library.for_set(a).parameters;assert(parameters.subsurface.enabled&&parameters.subsurface.type==0&&parameters.subsurface.scale==.5f);parameters.subsurface.scale=.125f;parameters.subsurface.color={.9f,.2f,.1f,1};library.parameters(main,parameters);const auto other=library.create(main,"Separate skin");library.assign(b,other);parameters.subsurface.type=1;parameters.subsurface.enabled=false;library.parameters(other,parameters);assert(library.for_set(a).parameters.subsurface.type==0&&library.for_set(b).parameters.subsurface.type==1);assert(ShaderInstances::from_json(library.to_json())==library);
 auto old=library.to_json();old["version"]=1;auto entries=old["instances"].toArray();for(int i=0;i<entries.size();++i){auto instance=entries[i].toObject();auto p=instance["parameters"].toObject();p.remove("subsurface");instance["parameters"]=p;entries[i]=instance;}old["instances"]=entries;const auto migrated=ShaderInstances::from_json(old);assert(migrated.for_set(a).parameters.subsurface==SubsurfaceParameters{});assert(migrated.for_set(a).parameters.normal_intensity==library.for_set(a).parameters.normal_intensity);
 for(auto scale:{-1.f,std::numeric_limits<float>::infinity()}){auto bad=parameters;bad.subsurface.scale=scale;failed=false;try{library.parameters(main,bad);}catch(...){failed=true;}assert(failed);}std::puts("Subsurface defaults, global sample bounds, shared/separate shader state, exact JSON and legacy shader migration PASS");
}
