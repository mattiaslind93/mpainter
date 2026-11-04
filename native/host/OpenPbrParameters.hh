#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
namespace bts {
// ASWF OpenPBR 1.1.1 parameter vocabulary/defaults, pinned to
// f8d6d947dfae4c9b599965a86c22826ea7a8dbfb. Data model, not a BSDF implementation.
// Physical ranges follow the parameter reference, not MaterialX UI limits:
// emission_color is unbounded HDR despite the reference nodedef's uimax=1.
// Geometry normals/tangents remain native internal shader inputs, not artist maps.
enum class OpenPbrType {Number,Color,Boolean};
struct OpenPbrParameter {
 const char *id,*label,*group;OpenPbrType type;std::array<float,3> defaults;
 float minimum,maximum;bool positive;
};
inline constexpr std::array<OpenPbrParameter,37> openpbr_parameters={{
 {"base_weight","Base Weight","Base",OpenPbrType::Number,{1.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"base_color","Base Color","Base",OpenPbrType::Color,{0.8f,0.8f,0.8f},0.0f,1.0f,false},
 {"base_diffuse_roughness","Base Diffuse Roughness","Base",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"base_metalness","Base Metalness","Base",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"specular_weight","Specular Weight","Specular",OpenPbrType::Number,{1.0f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),false},
 {"specular_color","Specular Color","Specular",OpenPbrType::Color,{1.0f,1.0f,1.0f},0.0f,1.0f,false},
 {"specular_roughness","Specular Roughness","Specular",OpenPbrType::Number,{0.3f,0.0f,0.0f},0.0f,1.0f,false},
 {"specular_ior","Specular Index of Refraction","Specular",OpenPbrType::Number,{1.5f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),true},
 {"specular_roughness_anisotropy","Specular Anisotropy","Specular",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"transmission_weight","Transmission Weight","Transmission",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"transmission_color","Transmission Color","Transmission",OpenPbrType::Color,{1.0f,1.0f,1.0f},0.0f,1.0f,false},
 {"transmission_depth","Transmission Depth","Transmission",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),false},
 {"transmission_scatter","Transmission Scatter","Transmission",OpenPbrType::Color,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"transmission_scatter_anisotropy","Transmission Anisotropy","Transmission",OpenPbrType::Number,{0.0f,0.0f,0.0f},-1.0f,1.0f,false},
 {"transmission_dispersion_scale","Transmission Dispersion Scale","Transmission",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"transmission_dispersion_abbe_number","Transmission Dispersion Abbe Number","Transmission",OpenPbrType::Number,{20.0f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),true},
 {"subsurface_weight","Subsurface Weight","Subsurface",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"subsurface_color","Subsurface Color","Subsurface",OpenPbrType::Color,{0.8f,0.8f,0.8f},0.0f,1.0f,false},
 {"subsurface_radius","Subsurface Radius","Subsurface",OpenPbrType::Number,{1.0f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),false},
 {"subsurface_radius_scale","Subsurface Radius Scale","Subsurface",OpenPbrType::Color,{1.0f,0.5f,0.25f},0.0f,1.0f,false},
 {"subsurface_scatter_anisotropy","Subsurface Anisotropy","Subsurface",OpenPbrType::Number,{0.0f,0.0f,0.0f},-1.0f,1.0f,false},
 {"fuzz_weight","Fuzz Weight","Fuzz",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"fuzz_color","Fuzz Color","Fuzz",OpenPbrType::Color,{1.0f,1.0f,1.0f},0.0f,1.0f,false},
 {"fuzz_roughness","Fuzz Roughness","Fuzz",OpenPbrType::Number,{0.5f,0.0f,0.0f},0.0f,1.0f,false},
 {"coat_weight","Coat Weight","Coat",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"coat_color","Coat Color","Coat",OpenPbrType::Color,{1.0f,1.0f,1.0f},0.0f,1.0f,false},
 {"coat_roughness","Coat Roughness","Coat",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"coat_roughness_anisotropy","Coat Anisotropy","Coat",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"coat_ior","Coat Index of Refraction","Coat",OpenPbrType::Number,{1.6f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),true},
 {"coat_darkening","Coat Darkening","Coat",OpenPbrType::Number,{1.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"thin_film_weight","Thin Film Weight","Thin Film",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"thin_film_thickness","Thin Film Thickness","Thin Film",OpenPbrType::Number,{0.5f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),false},
 {"thin_film_ior","Thin Film Index of Refraction","Thin Film",OpenPbrType::Number,{1.4f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),true},
 {"emission_luminance","Emission Luminance","Emission",OpenPbrType::Number,{0.0f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),false},
 {"emission_color","Emission Color","Emission",OpenPbrType::Color,{1.0f,1.0f,1.0f},0.0f,std::numeric_limits<float>::max(),false},
 {"geometry_opacity","Opacity","Geometry",OpenPbrType::Number,{1.0f,0.0f,0.0f},0.0f,1.0f,false},
 {"geometry_thin_walled","Thin Walled","Geometry",OpenPbrType::Boolean,{0.0f,0.0f,0.0f},0.0f,std::numeric_limits<float>::max(),false},
}};
struct OpenPbrParameters {
 using Value=std::array<float,3>;
 std::array<Value,openpbr_parameters.size()> values{};
 OpenPbrParameters(){for(std::size_t i=0;i<values.size();++i)values[i]=openpbr_parameters[i].defaults;}
 bool operator==(const OpenPbrParameters &)const=default;
 static std::size_t index(std::string_view id){const auto it=std::find_if(openpbr_parameters.begin(),openpbr_parameters.end(),[&](const auto &p){return id==p.id;});if(it==openpbr_parameters.end())throw std::invalid_argument("Unknown OpenPBR parameter");return std::size_t(it-openpbr_parameters.begin());}
 static void validate_value(std::size_t i,const Value &value){const auto &p=openpbr_parameters.at(i);const int count=p.type==OpenPbrType::Color?3:1;for(int c=0;c<count;++c)if(!std::isfinite(value[c])||value[c]<p.minimum||value[c]>p.maximum||(p.positive&&value[c]<=0))throw std::invalid_argument("OpenPBR parameter outside its specified range");if(p.type==OpenPbrType::Boolean&&value[0]!=0&&value[0]!=1)throw std::invalid_argument("OpenPBR boolean must be true or false");for(int c=count;c<3;++c)if(value[c]!=0)throw std::invalid_argument("Unexpected OpenPBR scalar components");}
 void validate()const{for(std::size_t i=0;i<values.size();++i)validate_value(i,values[i]);}
 const Value &get(std::string_view id)const{return values[index(id)];}
 void set(std::string_view id,Value value){const auto i=index(id);validate_value(i,value);values[i]=value;}
 QJsonObject to_json()const{validate();QJsonObject data;for(std::size_t i=0;i<values.size();++i){const auto &p=openpbr_parameters[i];const auto &v=values[i];if(p.type==OpenPbrType::Color)data[p.id]=QJsonArray{v[0],v[1],v[2]};else if(p.type==OpenPbrType::Boolean)data[p.id]=bool(v[0]);else data[p.id]=v[0];}return {{"version",1},{"specification","1.1.1"},{"values",data}};}
 static OpenPbrParameters from_json(const QJsonValue &json){if(!json.isObject())throw std::invalid_argument("Missing OpenPBR parameters");const auto o=json.toObject();if(o.size()!=3||o["version"]!=1||o["specification"]!="1.1.1"||!o["values"].isObject())throw std::invalid_argument("Unsupported OpenPBR parameter metadata");const auto data=o["values"].toObject();if(data.size()!=int(openpbr_parameters.size()))throw std::invalid_argument("Incomplete OpenPBR parameters");OpenPbrParameters result;for(std::size_t i=0;i<openpbr_parameters.size();++i){const auto &p=openpbr_parameters[i];const auto value=data[p.id];Value v{};if(p.type==OpenPbrType::Color){if(!value.isArray()||value.toArray().size()!=3)throw std::invalid_argument("OpenPBR color must have three components");for(int c=0;c<3;++c){if(!value[c].isDouble())throw std::invalid_argument("Invalid OpenPBR color component");v[c]=float(value[c].toDouble());}}else if(p.type==OpenPbrType::Boolean){if(!value.isBool())throw std::invalid_argument("Invalid OpenPBR boolean");v[0]=value.toBool();}else{if(!value.isDouble())throw std::invalid_argument("Invalid OpenPBR number");v[0]=float(value.toDouble());}validate_value(i,v);result.values[i]=v;}return result;}
};
inline double openpbr_thin_film_nanometers(const OpenPbrParameters &p){return double(p.get("thin_film_thickness")[0])*1000.;}
// Bump/Displacement retain the artist's fixed scalar export vocabulary.
// The hidden tangent-normal channel never becomes a public OpenPBR output.
inline const char *openpbr_export_channel(int channel){switch(channel){case 0:return "base_color";case 1:return "base_metalness";case 2:return "specular_roughness";case 4:return "bump";case 5:return "ambient_occlusion";case 14:return "displacement";default:return "";}}
}
