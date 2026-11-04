#include "NativeMaterialParameters.hh"
#include "NativeMaterialColor.hh"
#include <QJsonDocument>
#include <cassert>
#include <limits>
#include <iostream>
namespace {
unsigned memory_queries=0;
std::uint64_t generous_memory(){++memory_queries;return 4ULL*1024*1024*1024;}
std::uint64_t scarce_memory(){return 1024;}
}
int main(){
 bts::NativeMaterialDescription material;material.material="Example";material.group="Top shader";material.surface_type="ShaderNodeGroup";
 bts::NativeMaterialParameter scale;scale.identifier="Socket_1";scale.name="Scale";scale.minimum=-2;scale.maximum=10;scale.value[0]=1.5;material.parameters.push_back(scale);
 auto color=scale;color.identifier="Socket_2";color.name="Color";color.type=bts::NativeParameterType::Color;color.value={.2,.4,2,1};color.minimum=0;color.maximum=65504;material.parameters.push_back(color);
 auto mapping=scale;mapping.identifier="Socket_3";mapping.name="Mapping";mapping.type=bts::NativeParameterType::Vector;mapping.value={1,2,3,0};mapping.mapping=true;mapping.source_node="ShaderNodeTexCoord";mapping.source_socket="Generated";material.parameters.push_back(mapping);
 auto ramp=color;ramp.identifier="Socket_4";ramp.name="Ramp";ramp.ramp={{"stops",QJsonArray{QJsonObject{{"position",.25},{"color",QJsonArray{1,0,0,1}}}}}};material.parameters.push_back(ramp);
 material.node_types={"ShaderNodeGroup","ShaderNodeTexNoise"};const auto original=bts::native_material_description_json(material);assert(bts::native_material_description_json(bts::read_native_material_description(original))==original);
 const auto rejects=[](auto fn){try{fn();return false;}catch(const std::invalid_argument &){return true;}};
 auto bad=original;bad["version"]=4;assert(rejects([&]{bts::read_native_material_description(bad);}));
 auto parameters=original["parameters"].toArray();auto p=parameters.first().toObject();p["type"]=.5;parameters.replace(0,p);bad=original;bad["parameters"]=parameters;assert(rejects([&]{bts::read_native_material_description(bad);}));
 auto duplicate=material;duplicate.parameters.push_back(scale);assert(rejects([&]{duplicate.validate();}));
 auto nonfinite=material;nonfinite.parameters[0].value[0]=std::numeric_limits<double>::infinity();assert(rejects([&]{nonfinite.validate();}));
 parameters=original["parameters"].toArray();p=parameters.first().toObject();p["value"]=QJsonArray{"1,5"};parameters.replace(0,p);bad=original;bad["parameters"]=parameters;assert(rejects([&]{bts::read_native_material_description(bad);}));
 material.channels={0,1,2,4,14};material.color_to_aces={2,0,0,0,3,0,0,0,4};material.color_from_aces={.5,0,0,0,1.f/3,0,0,0,.25};
 const auto transformed=bts::native_material_description_json(material);const auto restored=bts::read_native_material_description(transformed);assert(restored.channels==material.channels&&restored.color_to_aces==material.color_to_aces);
 const std::array<float,4> signed_hdr{-1,2,4,.25};const auto aces=bts::native_material_color(material.color_to_aces,signed_hdr),roundtrip=bts::native_material_color(material.color_from_aces,aces);for(int i=0;i<4;++i)assert(std::abs(roundtrip[i]-signed_hdr[i])<1e-6);assert(aces[0]==-2&&aces[2]==16&&aces[3]==.25);
 auto old=transformed;old["version"]=1;old.remove("channels");old.remove("color_to_aces");old.remove("color_from_aces");auto legacy=bts::read_native_material_description(old);assert(legacy.channels.empty()&&legacy.color_to_aces[0]==1&&legacy.color_from_aces[4]==1);
 bad=transformed;bad["channels"]=QJsonArray{0,0};assert(rejects([&]{bts::read_native_material_description(bad);}));bad=transformed;bad["color_to_aces"]=QJsonArray{1};assert(rejects([&]{bts::read_native_material_description(bad);}));bad=transformed;bad["channels"]=QJsonArray{.5};assert(rejects([&]{bts::read_native_material_description(bad);}));
 // Generic description metadata remains complete beyond native producer labels.
 auto labelled=material;const QString long_text(10000,QChar(0x03bb));labelled.material=long_text;labelled.group=long_text;labelled.surface_type=long_text;labelled.node_types={long_text};labelled.warnings={long_text};
 auto &labelled_parameter=labelled.parameters.front();labelled_parameter.identifier=long_text;labelled_parameter.name=long_text;labelled_parameter.source_node=long_text;labelled_parameter.source_socket=long_text;labelled_parameter.ramp={{long_text,QJsonArray{long_text}}};
 {bts::TextureRamQueryScope memory(generous_memory);memory_queries=0;labelled.validate();assert(memory_queries==1);labelled_parameter.validate();assert(memory_queries==1);const auto json=bts::native_material_description_json(labelled);assert(bts::native_material_description_json(bts::read_native_material_description(json))==json);}
 {bts::TextureRamQueryScope memory(scarce_memory);bool denied=false;try{labelled.validate();}catch(const std::length_error &e){denied=std::string(e.what()).find("RAM")!=std::string::npos;}assert(denied);}
 // Text in warning lists and ramp JSON participates in the same admission.
 for(int field=0;field<2;++field){auto text_only=bts::NativeMaterialDescription{};text_only.material="M";if(field==0)text_only.warnings={long_text};else{auto parameter=scale;parameter.ramp={{"message",long_text}};text_only.parameters={parameter};}bts::TextureRamQueryScope memory(scarce_memory);bool denied=false;try{text_only.validate();}catch(const std::length_error &){denied=true;}assert(denied);}
 std::cout<<"Native material descriptor: long Unicode text/ramp roundtrip, aggregate RAM rejection and one query per description PASS\n";
 const std::array<float,9> rec709_to_aces{.6130974f,.07019372f,.02061559f,.33952315f,.91635388f,.10956977f,.04737945f,.0134524f,.86981463f};const auto red=bts::native_material_color(rec709_to_aces,{1,0,0,1});assert(std::abs(red[0]-.6130974f)<1e-7&&red[3]==1);

}
