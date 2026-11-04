#pragma once
#include <QJsonObject>
#include "LensProjection.hh"
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
namespace blender {struct Main;struct Scene;struct View3D;}
namespace bts {
struct DisplayPostSettings;
struct DisplayPostNumber {const char *key,*label,*object,*group;float DisplayPostSettings::*member;float low,high,step;int decimals;};
struct DisplayPostSettings {
 bool enabled=false,vignette=false,color_correction=false,tone_mapping=false,dof=false,glare=false,lens=false;
 float vignette_strength=.2f,saturation=1,contrast=1,brightness=1,bias=0,sepia=0,temperature=6500,exposure=1,gamma=1,mapping_factor=32;
 float focus_distance=3.725126f,aperture=0,glare_luminance=1,glare_threshold=.9f,glare_remap=3;
 float lens_power=1,lens_fov=45,lens_roundness=.05f,lens_smoothness=1;
 int tone_function=0,glare_shape=0;
 bool operator==(const DisplayPostSettings &)const=default;
 static std::span<const DisplayPostNumber> numbers();
 void validate()const{for(const auto &number:numbers()){const auto value=this->*number.member;if(!std::isfinite(value)||value<number.low||value>number.high)throw std::invalid_argument(std::string("Invalid display post value: ")+number.key);}if(glare_shape<0||glare_shape>2)throw std::invalid_argument("Invalid glare shape");if(tone_function<0||tone_function>2)throw std::invalid_argument("Invalid tone mapping function");}
 bool color_active()const{return color_correction&&(saturation!=1||contrast!=1||brightness!=1||bias!=0||sepia!=0||temperature!=6500);}
 bool tone_active()const{return tone_mapping&&(tone_function!=0||exposure!=1||gamma!=1);}
 bool dof_active()const{return dof&&aperture>0;}
 bool glare_active()const{return glare&&glare_luminance>0;}
 bool lens_active()const{return lens&&(lens_power!=1||lens_roundness>0||lens_smoothness>0);}
 LensProjection projection()const{return {enabled&&lens_active(),LensProjection::coefficient(lens_power,lens_fov),lens_roundness,lens_smoothness};}
 bool active()const{return enabled&&((vignette&&vignette_strength>0)||color_active()||tone_active()||dof_active()||glare_active()||lens_active());}
 QJsonObject to_json()const{validate();QJsonObject result{{"version",4},{"enabled",enabled},{"vignette",vignette},{"color_correction",color_correction},{"tone_mapping",tone_mapping},{"tone_function",tone_function},{"dof",dof},{"glare",glare},{"glare_shape",glare_shape},{"lens",lens}};for(const auto &n:numbers())result[n.key]=this->*n.member;return result;}
 static DisplayPostSettings from_json(const QJsonObject &o){if(o.isEmpty())return {};DisplayPostSettings result;const auto boolean=[&](const char *key){if(!o[key].isBool())throw std::invalid_argument("Invalid display effect toggle");return o[key].toBool();};if(o["version"].toDouble()==1){if(o.size()!=4||!o["vignette_strength"].isDouble())throw std::invalid_argument("Invalid legacy display effects");result.enabled=boolean("enabled");result.vignette=boolean("vignette");result.vignette_strength=float(o["vignette_strength"].toDouble());}else{if((o["version"].toDouble()!=2&&o["version"].toDouble()!=3&&o["version"].toDouble()!=4)||o.size()!=(o["version"].toDouble()==2?16:o["version"].toDouble()==3?24:result.to_json().size())||!o["tone_function"].isDouble()||o["tone_function"].toDouble()!=std::floor(o["tone_function"].toDouble()))throw std::invalid_argument("Unsupported display post effects settings");result.enabled=boolean("enabled");result.vignette=boolean("vignette");result.color_correction=boolean("color_correction");result.tone_mapping=boolean("tone_mapping");result.tone_function=o["tone_function"].toInt(-1);if(o["version"].toDouble()>=3){result.dof=boolean("dof");result.glare=boolean("glare");if(!o["glare_shape"].isDouble()||o["glare_shape"].toDouble()!=std::floor(o["glare_shape"].toDouble()))throw std::invalid_argument("Invalid glare shape");result.glare_shape=o["glare_shape"].toInt(-1);}if(o["version"].toDouble()>=4)result.lens=boolean("lens");for(const auto &n:numbers()){if(o["version"].toDouble()<4&&QString(n.group)=="lens")continue;if(o["version"].toDouble()==2&&(QString(n.group)=="dof"||QString(n.group)=="glare"))continue;if(!o[n.key].isDouble())throw std::invalid_argument("Missing display post value");result.*n.member=float(o[n.key].toDouble());}}result.validate();return result;}
 void edit(const QString &key,double value){auto next=*this;if(!std::isfinite(value))throw std::invalid_argument("Invalid display post edit");if(key=="reset")next={};else if(key.endsWith("_reset")){const auto group=key.chopped(6);const DisplayPostSettings defaults;for(const auto &n:numbers())if(group==n.group)next.*n.member=defaults.*n.member;if(group=="tone")next.tone_function=0;if(group=="glare")next.glare_shape=0;}else if(key=="enabled"||key=="vignette"||key=="color_correction"||key=="tone_mapping"||key=="dof"||key=="glare"||key=="lens"){if(value!=0&&value!=1)throw std::invalid_argument("Invalid display post toggle");if(key=="enabled")next.enabled=value;else if(key=="vignette")next.vignette=value;else if(key=="color_correction")next.color_correction=value;else if(key=="tone_mapping")next.tone_mapping=value;else if(key=="dof")next.dof=value;else if(key=="glare")next.glare=value;else next.lens=value;}else if(key=="glare_shape"){if(value!=std::floor(value)||value<0||value>2)throw std::invalid_argument("Invalid glare shape");next.glare_shape=int(value);}else if(key=="tone_function"){if(value!=std::floor(value)||value<0||value>2)throw std::invalid_argument("Invalid tone function");next.tone_function=int(value);}else{bool found=false;for(const auto &n:numbers())if(key==n.key){next.*n.member=float(value);found=true;break;}if(!found)throw std::invalid_argument("Unknown display post setting");}next.validate();*this=next;}
};
inline std::span<const DisplayPostNumber> DisplayPostSettings::numbers(){
 static constexpr std::array values{
  DisplayPostNumber{"saturation","Saturation","postColorSaturation","color",&DisplayPostSettings::saturation,0,2,.05,3},
  DisplayPostNumber{"contrast","Contrast","postColorContrast","color",&DisplayPostSettings::contrast,.01,1.99,.05,3},
  DisplayPostNumber{"brightness","Brightness","postColorBrightness","color",&DisplayPostSettings::brightness,0,2,.05,3},
  DisplayPostNumber{"bias","Bias","postColorBias","color",&DisplayPostSettings::bias,-1,1,.01,3},
  DisplayPostNumber{"sepia","Sepia Tone Ratio","postColorSepia","color",&DisplayPostSettings::sepia,0,1,.05,3},
  DisplayPostNumber{"temperature","White Balance Temperature (K)","postColorTemperature","color",&DisplayPostSettings::temperature,1800,12000,100,0},
  DisplayPostNumber{"exposure","Exposure","postToneExposure","tone",&DisplayPostSettings::exposure,.0625,16,.1,3},
  DisplayPostNumber{"gamma","Gamma","postToneGamma","tone",&DisplayPostSettings::gamma,.1,4,.05,3},
  DisplayPostNumber{"mapping_factor","Mapping Factor","postToneMappingFactor","tone",&DisplayPostSettings::mapping_factor,1,128,1,3},
  DisplayPostNumber{"focus_distance","Focus distance (scene units)","postDofFocus","dof",&DisplayPostSettings::focus_distance,.001,1000000,.1,3},
  DisplayPostNumber{"aperture","Aperture (0 = sharp)","postDofAperture","dof",&DisplayPostSettings::aperture,0,8,.1,3},
  DisplayPostNumber{"glare_luminance","Luminance","postGlareLuminance","glare",&DisplayPostSettings::glare_luminance,0,4,.1,3},
  DisplayPostNumber{"glare_threshold","Threshold","postGlareThreshold","glare",&DisplayPostSettings::glare_threshold,0,16,.05,3},
  DisplayPostNumber{"glare_remap","Remap Factor","postGlareRemap","glare",&DisplayPostSettings::glare_remap,1,8,.1,3},
  DisplayPostNumber{"lens_power","Power","postLensPower","lens",&DisplayPostSettings::lens_power,0,2,.05,3},
  DisplayPostNumber{"lens_fov","FOV","postLensFov","lens",&DisplayPostSettings::lens_fov,1,160,1,1},
  DisplayPostNumber{"lens_roundness","Edge Roundness","postLensRoundness","lens",&DisplayPostSettings::lens_roundness,0,.5,.01,3},
  DisplayPostNumber{"lens_smoothness","Edge Smoothness","postLensSmoothness","lens",&DisplayPostSettings::lens_smoothness,0,2,.05,3},
  DisplayPostNumber{"vignette_strength","Strength","postVignetteStrength","vignette",&DisplayPostSettings::vignette_strength,0,1,.05,3}};
 return values;
}
DisplayPostSettings display_post_settings(const blender::Scene &);
void set_display_post_settings(blender::Main &,blender::Scene &,blender::View3D &,DisplayPostSettings);
void prepare_display_post_effects(blender::Main &,blender::Scene &,blender::View3D &,int width,int height);
}
