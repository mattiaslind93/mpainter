// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayerSourceChannels.hh"
#include "NativeMaterialParameters.hh"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

using namespace bts;
namespace {
template<class F>void rejected(F &&action){bool failed=false;try{action();}catch(const std::invalid_argument &){failed=true;}assert(failed);}
NativeMaterialDescription sample(){NativeMaterialDescription d;d.material="Owned source";d.group="Top";d.surface_type="ShaderNodeBsdfPrincipled";d.channels={0,1,2,5};return d;}
}
int main(){
 // Exercise the same source-capture policy used by GPU capture and CPU Flatten
 // against the actual Height admission guard, including an invalid authored flag.
 for(bool authored_normal:{false,true}){
  std::array<bool,5> authored{};authored[3]=authored_normal;
  const auto capture=layer_source_channels(authored,true);
  assert(capture[0]&&capture[1]&&capture[2]&&capture[4]);
  assert(capture[3]==authored_normal&&authored[0]==false&&authored[3]==authored_normal);
  for(int mode=0;mode<=6;++mode){
   if(authored_normal)rejected([&]{require_native_height_fill(NativeHeightPolicy::RawAuthoredFloatV1,.5f,mode,capture[3]);});
   else require_native_height_fill(NativeHeightPolicy::RawAuthoredFloatV1,.5f,mode,capture[3]);
  }
  const auto legacy=layer_source_channels(authored,false);assert(std::all_of(legacy.begin(),legacy.end(),[](bool enabled){return enabled;}));
 }
 validate_native_height_requirements({});
 validate_native_height_requirements(NativeHeightRequirements{.images=true,.scalar_image=true});
 for(auto restricted:{NativeHeightRequirements{.images=true,.global_luminance=true,.scalar_image=true},NativeHeightRequirements{.images=true,.view=true,.scalar_image=true},NativeHeightRequirements{.images=true,.ao=true,.scalar_image=true}})rejected([&]{validate_native_height_requirements(restricted);});
 for(int projection=0;projection<=6;++projection){require_native_height_fill(NativeHeightPolicy::None,0,projection,true);require_native_height_fill(NativeHeightPolicy::RawAuthoredFloatV1,.5f,projection,false);}
 for(int projection:{-1,7,std::numeric_limits<int>::max()})for(auto policy:{NativeHeightPolicy::None,NativeHeightPolicy::RawAuthoredFloatV1})rejected([&]{require_native_height_fill(policy,.5f,projection,false);});
 rejected([]{require_native_height_fill(NativeHeightPolicy::RawAuthoredFloatV1,.5f,0,true);});
 rejected([]{require_native_height_fill(NativeHeightPolicy::RawAuthoredFloatV1,0,1,false);});
 const NativeHeightColorContext context{"Recorded linear",{1,0,0,0,1,0,0,0,1}};require_native_height_color_context(context,context,context);
 auto forged_context=context;forged_context.scene_linear_to_xyz[0]=2;rejected([&]{require_native_height_color_context(context,forged_context,context);});rejected([&]{require_native_height_color_context(context,context,forged_context);});
 forged_context=context;forged_context.name="Other linear";rejected([&]{require_native_height_color_context(context,forged_context,context);});
 forged_context=context;forged_context.scene_linear_to_xyz.fill(0);rejected([&]{require_native_height_color_context(forged_context,forged_context,forged_context);});
 require_native_height_origin(NativeHeightPolicy::None,0);require_native_height_origin(NativeHeightPolicy::None,.5f);require_native_height_origin(NativeHeightPolicy::RawAuthoredFloatV1,.5f);
 rejected([]{require_native_height_origin(NativeHeightPolicy::RawAuthoredFloatV1,0);});
 require_native_height_image_replacement(NativeHeightPolicy::None);rejected([]{require_native_height_image_replacement(NativeHeightPolicy::RawAuthoredFloatV1);});
 for(const auto requirements:{NativeHeightRequirements{.images=true},NativeHeightRequirements{.global_luminance=true},NativeHeightRequirements{.global_blackbody=true},NativeHeightRequirements{.global_wavelength=true},NativeHeightRequirements{.view=true},NativeHeightRequirements{.ao=true}})rejected([&]{validate_native_height_requirements(requirements);});
 auto source=sample();const auto original=native_material_description_json(source);assert(original["version"]==2&&!original.contains("height_policy"));require_native_source_policy(source);
 for(int version:{1,2,3}){auto old=original;old["version"]=version;const auto restored=read_native_material_description(old);assert(restored.height_policy==NativeHeightPolicy::None);old["height_policy"]="authored-height-raw-v1";rejected([&]{read_native_material_description(old);});}
 auto raw=source;raw.height_policy=NativeHeightPolicy::RawAuthoredFloatV1;const auto json=native_material_description_json(raw);assert(json["version"]==4&&json["height_policy"]=="authored-height-raw-v1");assert(native_material_description_json(read_native_material_description(json))==json);assert(native_material_description_json(source)==original);rejected([&]{require_native_source_policy(raw);});
 // A policy never manufactures a source capability from a channels snapshot.
 assert(raw.channels==source.channels&&std::find(raw.channels.begin(),raw.channels.end(),4)==raw.channels.end());
 for(const auto value:{QJsonValue(),QJsonValue(7),QJsonValue(true),QJsonValue("unknown"),QJsonValue("authored-height-raw-v2")}){auto invalid=json;invalid["height_policy"]=value;rejected([&]{read_native_material_description(invalid);});}
 auto forged=raw;forged.height_policy=static_cast<NativeHeightPolicy>(99);rejected([&]{forged.validate();});
 NativeMaterialParameter mapping;mapping.identifier="coords";mapping.name="Coordinates";mapping.type=NativeParameterType::Vector;mapping.mapping=true;mapping.source_node="ShaderNodeMapping";mapping.source_socket="Vector";mapping.minimum=-100;mapping.maximum=100;mapping.mapping_transform.offset[1]=.25;raw.parameters.push_back(mapping);const auto mapped=native_material_description_json(raw);assert(mapped["version"]==4&&native_material_description_json(read_native_material_description(mapped))==mapped);
 raw.height_policy=NativeHeightPolicy::None;const auto reset=native_material_description_json(raw);assert(reset["version"]==3&&!reset.contains("height_policy"));assert(read_native_material_description(reset).height_policy==NativeHeightPolicy::None);
 std::cout<<"Native Height policy/schema/source isolation PASS\n";
}
