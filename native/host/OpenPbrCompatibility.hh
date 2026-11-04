#pragma once
#include "OpenPbrParameters.hh"
#include <QStringList>
namespace bts {
// Explicit portable subset, not a claim that Principled is the OpenPBR BSDF.
// Base color/metalness/roughness are supplied by the existing GPU layer stack.
struct OpenPbrSocket {const char *parameter,*socket;double factor=1;};
inline constexpr std::array openpbr_number_sockets={
 OpenPbrSocket{"base_diffuse_roughness","Diffuse Roughness"},
 OpenPbrSocket{"specular_weight","Specular IOR Level",.5},
 OpenPbrSocket{"specular_ior","IOR"},
 OpenPbrSocket{"specular_roughness_anisotropy","Anisotropic"},
 OpenPbrSocket{"transmission_weight","Transmission Weight"},
 OpenPbrSocket{"subsurface_weight","Subsurface Weight"},
 OpenPbrSocket{"subsurface_radius","Subsurface Scale"},
 OpenPbrSocket{"subsurface_scatter_anisotropy","Subsurface Anisotropy"},
 OpenPbrSocket{"coat_weight","Coat Weight"},
 OpenPbrSocket{"coat_roughness","Coat Roughness"},
 OpenPbrSocket{"coat_ior","Coat IOR"},
 OpenPbrSocket{"fuzz_weight","Sheen Weight"},
 OpenPbrSocket{"fuzz_roughness","Sheen Roughness"},
 OpenPbrSocket{"emission_luminance","Emission Strength"},
 OpenPbrSocket{"thin_film_ior","Thin Film IOR"},
 OpenPbrSocket{"geometry_opacity","Alpha"}
};
inline constexpr std::array openpbr_color_sockets={
 OpenPbrSocket{"specular_color","Specular Tint"},
 OpenPbrSocket{"coat_color","Coat Tint"},
 OpenPbrSocket{"fuzz_color","Sheen Tint"},
 OpenPbrSocket{"emission_color","Emission Color"}
};
inline bool openpbr_layer_parameter(std::string_view id){return id=="base_color"||id=="base_metalness"||id=="specular_roughness";}
inline bool openpbr_compatibility_parameter(std::string_view id){
 for(const auto &p:openpbr_number_sockets)if(id==p.parameter)return true;
 for(const auto &p:openpbr_color_sockets)if(id==p.parameter)return true;
 return id=="base_weight"||id=="subsurface_radius_scale"||id=="geometry_thin_walled"||id=="thin_film_weight"||id=="thin_film_thickness";
}
inline QStringList openpbr_unsupported_values(const OpenPbrParameters &p){
 p.validate();QStringList result;
 for(std::size_t i=0;i<openpbr_parameters.size();++i){const auto &definition=openpbr_parameters[i];if(!openpbr_compatibility_parameter(definition.id)&&p.values[i]!=definition.defaults)result.append(definition.id);}
 for(std::size_t i=0;i<openpbr_parameters.size();++i){const auto &definition=openpbr_parameters[i];for(int c=0;c<(definition.type==OpenPbrType::Color?3:1);++c)if(p.values[i][c]>1e6||(definition.positive&&p.values[i][c]<1e-6f)){result.append(QString(definition.id)+" outside portable preview range");break;}}
 if(p.get("specular_weight")[0]>2)result.append("specular_weight > 2");
 const auto film=p.get("thin_film_weight")[0];if(film!=0&&film!=1)result.append("fractional thin_film_weight");
 return result;
}
inline void validate_openpbr_compatibility(const OpenPbrParameters &p){const auto bad=openpbr_unsupported_values(p);if(!bad.isEmpty())throw std::invalid_argument((QString("Portable Principled preview does not support: ")+bad.join(", ")).toStdString());}
}
