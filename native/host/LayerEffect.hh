#include "ResourceBudget.hh"
#pragma once
#include "EffectPaint.hh"
#include "LinearColorTransform.hh"
#include "MeshGenerator.hh"
#include "AnchorReference.hh"
#include "FilterEffect.hh"
#include "MaskFillMapping.hh"
#include <optional>
#include <QUuid>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>
#include <string>
namespace bts {
struct LevelCurve {
  float input_low=0,gamma=1,input_high=1,output_low=0,output_high=1;
};
enum class ContentEffectKind {Levels,Paint,Filter,Generator,Anchor,LinearColorTransform,Fill};
struct LevelsEffect:LevelCurve,MeshGeneratorSettings {
  ContentEffectKind kind=ContentEffectKind::Levels;
  std::shared_ptr<const LinearColorTransform> linear_color;
  EffectPaint paint;
  FilterSettings filter;
  std::array<float,3> fill_value{.5f,.5f,.5f};
  std::optional<std::array<float,3>> generator_color; // Scene-linear source tint, before blend/coverage.
  std::optional<MaskFillMapping> bitmap_fill; // Independent raw UV source for ordered bitmap Fill actions.
  bool initialize_content=false,source_coverage=false;
  float opacity=1;LayerBlend blending=LayerBlend::Normal;
  QUuid id=QUuid::createUuid();
  bool enabled=true;
  int channel=0,component=0; // Editor selection: Color/Lum, R, G, B.
  bool clamp=false;
  std::array<LevelCurve,3> rgb_curves{};
  std::string name;QString anchor_name;
};
inline std::array<float,3> content_generator_color(const LevelsEffect &effect){
 if(!effect.generator_color)return {1,1,1};
 if(effect.kind!=ContentEffectKind::Generator||effect.channel!=0)throw std::invalid_argument("Generator source color requires the Color channel");
 for(float value:*effect.generator_color)if(!std::isfinite(value)||std::abs(value)>65504)throw std::invalid_argument("Invalid generator source color");
 return *effect.generator_color;
}
inline bool rgb_levels_channel(int channel){return channel==0||channel==3;}
inline LevelCurve &edited_levels_curve(LevelsEffect &effect){return effect.component>0&&rgb_levels_channel(effect.channel)?effect.rgb_curves.at(effect.component-1):static_cast<LevelCurve &>(effect);}
inline const LevelCurve &edited_levels_curve(const LevelsEffect &effect){return effect.component>0&&rgb_levels_channel(effect.channel)?effect.rgb_curves.at(effect.component-1):static_cast<const LevelCurve &>(effect);}
inline float levels_curve_value(const LevelCurve &curve,float value){
  if(curve.output_low==curve.output_high)return curve.output_low;
  double mapped=(double(value)-curve.input_low)/(double(curve.input_high)-curve.input_low);
  // Preserve signed working values, as Blender's gamma shader does for negatives.
  if(mapped>0)mapped=std::pow(mapped,1/curve.gamma);
  const double output=double(curve.output_low)+(double(curve.output_high)-curve.output_low)*mapped;
  return float(std::clamp(output,-65504.,65504.));
}
inline float levels_value(const LevelsEffect &effect,float value){const float output=levels_curve_value(effect,value);return effect.clamp?std::clamp(output,0.f,1.f):output;}
inline std::array<float,3> levels_rgb(const LevelsEffect &effect,std::array<float,3> value){for(int c=0;c<3;++c){value[c]=levels_curve_value(effect,value[c]);if(rgb_levels_channel(effect.channel))value[c]=levels_curve_value(effect.rgb_curves[c],value[c]);if(effect.clamp)value[c]=std::clamp(value[c],0.f,1.f);}return value;}
// The unblended curve is also used by the editor/histogram. Stack evaluation
// applies the effect mix afterwards without changing input coverage.
inline std::array<float,3> mixed_levels_rgb(const LevelsEffect &effect,int channel,const std::array<float,3> &value){
  if(effect.blending==LayerBlend::Disabled||effect.opacity<=0)return value;
  const auto adjusted=levels_rgb(effect,value);
  const auto blended=channel==0?blend_rgb(effect.blending,adjusted,value):std::array<float,3>{blend_value(effect.blending,adjusted[0],value[0]),blend_value(effect.blending,adjusted[1],value[1]),blend_value(effect.blending,adjusted[2],value[2])};
  std::array<float,3> result;for(int c=0;c<3;++c)result[c]=std::lerp(value[c],blended[c],effect.opacity);return result;
}
inline QJsonObject level_curve_json(const LevelCurve &c){return {{"input_low",c.input_low},{"gamma",c.gamma},{"input_high",c.input_high},{"output_low",c.output_low},{"output_high",c.output_high}};}
inline LevelCurve read_level_curve(const QJsonObject &o){const auto number=[&](const char *key,float low,float high){const auto v=o[key];if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<low||v.toDouble()>high)throw std::invalid_argument("Invalid Levels curve");return float(v.toDouble());};LevelCurve c;c.input_low=number("input_low",-65504,65504);c.input_high=number("input_high",-65504,65504);c.gamma=number("gamma",.01,100);c.output_low=number("output_low",-65504,65504);c.output_high=number("output_high",-65504,65504);if(c.input_high-c.input_low<1e-6f)throw std::invalid_argument("Levels input endpoints must be ordered");return c;}
inline QJsonArray effects_json(const std::vector<LevelsEffect> &effects){QJsonArray result;for(const auto &e:effects){if(e.generator_color)content_generator_color(e);if(e.kind==ContentEffectKind::Fill){QJsonObject o{{"type","fill"},{"id",e.id.toString(QUuid::WithoutBraces)},{"enabled",e.enabled},{"channel",e.channel},{"value",QJsonArray{e.fill_value[0],e.fill_value[1],e.fill_value[2]}},{"initialize_content",e.initialize_content},{"source_coverage",e.source_coverage},{"opacity",e.opacity},{"blending",int(e.blending)}};if(e.bitmap_fill){o["type"]="bitmap_fill";o["bitmap_fill"]=mask_fill_mapping_json(*e.bitmap_fill);if(e.linear_color)o["bitmap_color"]=linear_color_transform_json(*e.linear_color);}result.append(o);continue;}if(e.kind==ContentEffectKind::LinearColorTransform){if(e.channel!=0||!e.linear_color)throw std::invalid_argument("Invalid linear Color effect");result.append(QJsonObject{{"type","linear_color_transform"},{"id",e.id.toString(QUuid::WithoutBraces)},{"enabled",e.enabled},{"channel",0},{"transform",linear_color_transform_json(*e.linear_color)}});continue;}if(e.kind==ContentEffectKind::Anchor){result.append(QJsonObject{{"type","anchor"},{"id",e.id.toString(QUuid::WithoutBraces)},{"enabled",e.enabled},{"name",e.anchor_name}});continue;}if(e.kind==ContentEffectKind::Generator){auto o=mesh_generator_json(e);o["type"]=e.generator_color?"colored_generator":"generator";if(e.generator_color){const auto color=content_generator_color(e);o["color"]=QJsonArray{color[0],color[1],color[2]};}o["id"]=e.id.toString(QUuid::WithoutBraces);o["enabled"]=e.enabled;o["channel"]=e.channel;o["opacity"]=e.opacity;o["blending"]=int(e.blending);result.append(o);continue;}if(e.kind==ContentEffectKind::Filter){result.append(QJsonObject{{"type","filter"},{"id",e.id.toString(QUuid::WithoutBraces)},{"enabled",e.enabled},{"filter",filter_json(e.filter)},{"channel",e.channel},{"opacity",e.opacity},{"blending",int(e.blending)}});continue;}if(e.kind==ContentEffectKind::Paint){result.append(QJsonObject{{"type","paint"},{"id",e.id.toString(QUuid::WithoutBraces)},{"enabled",e.enabled},{"paint",effect_paint_json(e.paint)},{"opacity",e.opacity},{"blending",int(e.blending)}});continue;}auto o=level_curve_json(e);QJsonArray rgb;for(const auto &curve:e.rgb_curves)rgb.append(level_curve_json(curve));o["type"]="levels";o["id"]=e.id.toString(QUuid::WithoutBraces);o["enabled"]=e.enabled;o["channel"]=e.channel;o["component"]=e.component;o["rgb_curves"]=rgb;o["clamp"]=e.clamp;if(e.opacity!=1)o["opacity"]=e.opacity;if(e.blending!=LayerBlend::Normal)o["blending"]=int(e.blending);result.append(o);}for(int n=0;n<int(effects.size());++n)if(!effects[n].name.empty()){auto object=result[n].toObject();object["display_name"]=QString::fromStdString(effects[n].name);result[n]=object;}return result;}
inline std::vector<LevelsEffect> read_effects(const QJsonValue &value){
  const auto fail=[](){throw std::invalid_argument("Invalid layer effects");};if(!value.isArray())fail();const auto entries=value.toArray();std::uint64_t text_bytes=0;for(const auto &entry:entries){const auto object=entry.toObject();for(const char *key:{"display_name","name"})text_bytes=resource_sum(text_bytes,resource_product(object[key].toString().size(),8));}require_texture_ram(resource_sum(resource_product(entries.size(),sizeof(LevelsEffect)),text_bytes),"Effect metadata");std::vector<LevelsEffect> result;std::set<QUuid> ids;
  for(const auto &entry:value.toArray()){
    if(!entry.isObject())fail();const auto o=entry.toObject();LevelsEffect e;if(o.contains("display_name")){if(!o["display_name"].isString()||o["display_name"].toString().contains(QChar(0)))fail();e.name=o["display_name"].toString().toStdString();}
    if(o["type"]=="linear_color_transform"){
      for(auto f=o.begin();f!=o.end();++f)if(f.key()!="type"&&f.key()!="id"&&f.key()!="enabled"&&f.key()!="channel"&&f.key()!="transform"&&f.key()!="display_name")fail();
      if(!o["id"].isString()||!o["enabled"].isBool()||o["channel"]!=0)fail();
      e.id=QUuid(o["id"].toString());if(e.id.isNull()||!ids.insert(e.id).second)fail();
      e.kind=ContentEffectKind::LinearColorTransform;e.enabled=o["enabled"].toBool();e.channel=0;
      require_texture_ram(resource_product(o["transform"].toObject()["input_space"].toString().size(),8),"Linear color transform metadata");
      e.linear_color=read_linear_color_transform(o["transform"]);result.push_back(std::move(e));continue;
    }
    if(o["type"]=="anchor"){if(!o["id"].isString()||!o["enabled"].isBool()||!o["name"].isString())fail();e.id=QUuid(o["id"].toString());e.anchor_name=o["name"].toString();if(e.id.isNull()||!ids.insert(e.id).second||e.anchor_name.trimmed().isEmpty()||e.anchor_name.contains(QChar(0)))fail();e.kind=ContentEffectKind::Anchor;e.enabled=o["enabled"].toBool();result.push_back(std::move(e));continue;}
    if(o["type"]=="fill"||o["type"]=="bitmap_fill"){
      if((o["type"]=="bitmap_fill")!=o.contains("bitmap_fill"))fail();
      if(!o["id"].isString()||!o["enabled"].isBool()||!o["channel"].isDouble()||o["channel"].toInt(-1)<0||o["channel"].toInt()>14||o["channel"].toDouble()!=o["channel"].toInt())fail();
      e.id=QUuid(o["id"].toString());if(e.id.isNull()||!ids.insert(e.id).second)fail();e.kind=ContentEffectKind::Fill;e.enabled=o["enabled"].toBool();e.channel=o["channel"].toInt();for(const char *flag:{"initialize_content","source_coverage"})if(o.contains(flag)&&!o[flag].isBool())fail();e.initialize_content=o["initialize_content"].toBool();e.source_coverage=o["source_coverage"].toBool();
      if(o.contains("bitmap_fill")){e.bitmap_fill=read_mask_fill_mapping(o["bitmap_fill"]);const auto &mapping=e.bitmap_fill->projection;if(mapping.mode!=0||!mapping.texture||!mapping.texture->raw||mapping.alpha_mask[0]||mapping.atom||e.source_coverage||e.bitmap_fill->filtering!=0||e.bitmap_fill->wrap!=0)fail();}
      if(o.contains("bitmap_color")){if(!e.bitmap_fill||e.channel!=0)fail();e.linear_color=read_linear_color_transform(o["bitmap_color"]);}
      const auto values=o["value"].toArray();if(values.size()!=3)fail();for(int c=0;c<3;++c){if(!values[c].isDouble()||!std::isfinite(values[c].toDouble())||std::abs(values[c].toDouble())>65504)fail();e.fill_value[c]=float(values[c].toDouble());}
      if(!o["opacity"].isDouble()||o["opacity"].toDouble()<0||o["opacity"].toDouble()>1||!std::isfinite(o["opacity"].toDouble()))fail();e.opacity=float(o["opacity"].toDouble());
      if(!o["blending"].isDouble()||o["blending"].toInt(-1)<0||o["blending"].toInt()>=int(layer_blend_names.size())||o["blending"].toDouble()!=o["blending"].toInt())fail();e.blending=LayerBlend(o["blending"].toInt());result.push_back(std::move(e));continue;
    }
    if(o["type"]=="generator"||o["type"]=="colored_generator"){if((o["type"]=="colored_generator")!=o.contains("color"))fail();if(!o["id"].isString()||!o["enabled"].isBool())fail();e.id=QUuid(o["id"].toString());if(e.id.isNull()||!ids.insert(e.id).second)fail();e.kind=ContentEffectKind::Generator;e.enabled=o["enabled"].toBool();static_cast<MeshGeneratorSettings &>(e)=read_mesh_generator(o);const auto integer=[&](const char *key,int high){const auto v=o[key];if(!v.isDouble()||v.toDouble()!=v.toInt(-1)||v.toInt()<0||v.toInt()>high)fail();return v.toInt();};e.channel=integer("channel",14);e.blending=LayerBlend(integer("blending",int(layer_blend_names.size())-1));if(!o["opacity"].isDouble()||!std::isfinite(o["opacity"].toDouble())||o["opacity"].toDouble()<0||o["opacity"].toDouble()>1)fail();e.opacity=float(o["opacity"].toDouble());if(o.contains("color")){if(!o["color"].isArray()||o["color"].toArray().size()!=3)fail();std::array<float,3> color;for(int c=0;c<3;++c){const auto v=o["color"].toArray()[c];if(!v.isDouble()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>65504)fail();color[c]=float(v.toDouble());}e.generator_color=color;content_generator_color(e);}result.push_back(std::move(e));continue;}
    if(o["type"]=="filter"){if(!o["id"].isString()||!o["enabled"].isBool())fail();e.id=QUuid(o["id"].toString());if(e.id.isNull()||!ids.insert(e.id).second)fail();e.kind=ContentEffectKind::Filter;e.enabled=o["enabled"].toBool();e.filter=read_filter(o["filter"]);const auto integer=[&](const char *key,int high){const auto v=o[key];if(!v.isDouble()||v.toDouble()!=v.toInt(-1)||v.toInt()<0||v.toInt()>high)fail();return v.toInt();};e.channel=integer("channel",14);e.blending=LayerBlend(integer("blending",int(layer_blend_names.size())-1));if(!o["opacity"].isDouble()||!std::isfinite(o["opacity"].toDouble())||o["opacity"].toDouble()<0||o["opacity"].toDouble()>1)fail();e.opacity=o["opacity"].toDouble();result.push_back(std::move(e));continue;}
    if(o["type"]=="paint"){if(!o["id"].isString()||!o["enabled"].isBool())fail();e.id=QUuid(o["id"].toString());if(e.id.isNull()||!ids.insert(e.id).second)fail();if(!o["opacity"].isDouble()||!std::isfinite(o["opacity"].toDouble())||o["opacity"].toDouble()<0||o["opacity"].toDouble()>1||!o["blending"].isDouble()||o["blending"].toDouble()!=o["blending"].toInt(-1)||o["blending"].toInt()<0||o["blending"].toInt()>=int(layer_blend_names.size()))fail();e.opacity=float(o["opacity"].toDouble());e.blending=LayerBlend(o["blending"].toInt());e.kind=ContentEffectKind::Paint;e.enabled=o["enabled"].toBool();e.paint=read_effect_paint(o["paint"]);result.push_back(std::move(e));continue;}
    if(o["type"]!="levels"||!o["id"].isString()||!o["enabled"].isBool()||!o["clamp"].isBool())fail();e.id=QUuid(o["id"].toString());if(e.id.isNull()||!ids.insert(e.id).second)fail();e.enabled=o["enabled"].toBool();e.clamp=o["clamp"].toBool();
    const auto integer=[&](const QJsonValue &v,int high){if(!v.isDouble()||v.toDouble()!=v.toInt(-1)||v.toInt()<0||v.toInt()>high)fail();return v.toInt();};e.channel=integer(o["channel"],14);static_cast<LevelCurve &>(e)=read_level_curve(o);
    if(o.contains("opacity")){const auto v=o["opacity"];if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<0||v.toDouble()>1)fail();e.opacity=float(v.toDouble());}
    if(o.contains("blending"))e.blending=LayerBlend(integer(o["blending"],int(layer_blend_names.size())-1));
    if(!o["component"].isUndefined())e.component=integer(o["component"],3);
    if(!o["rgb_curves"].isUndefined()){if(!o["rgb_curves"].isArray()||o["rgb_curves"].toArray().size()!=3)fail();for(int c=0;c<3;++c){const auto v=o["rgb_curves"].toArray()[c];if(!v.isObject())fail();e.rgb_curves[c]=read_level_curve(v.toObject());}}
    result.push_back(std::move(e));
  }return result;
}
inline std::array<float,3> apply_levels(const std::vector<LevelsEffect> &effects,int channel,std::array<float,3> value,const QUuid *stop=nullptr,std::array<float,3> *input=nullptr){for(const auto &effect:effects){if(effect.kind!=ContentEffectKind::Levels||effect.channel!=channel)continue;if(stop&&effect.id==*stop){if(input)*input=value;}if(effect.enabled)value=mixed_levels_rgb(effect,channel,value);}return value;}
inline std::array<float,3> blend_generated_content(const MeshGeneratorSettings &settings,const MeshMaps *maps,int channel,std::array<float,3> value,float &coverage,int x,int y,int tile,int width,int height,ChannelPrecision precision,float opacity,LayerBlend mode,const AnchorSampleContext *context=nullptr,std::array<float,3> color={1,1,1}){
 if(x<0||y<0||opacity<=0||mode==LayerBlend::Disabled)return value;const auto generated=generated_mask(settings,maps,(x+.5)/width,(y+.5)/height,tile,context);if(!generated)return value;const auto source=precision_rgb({*generated*color[0],*generated*color[1],*generated*color[2]},precision);const float alpha=opacity;
 if(mode==LayerBlend::Replace){const float result=alpha+coverage*(1-opacity);for(int c=0;c<3;++c)value[c]=result>0?(source[c]*alpha+value[c]*coverage*(1-opacity))/result:0;coverage=result;return value;}
 auto blended=channel==0?blend_rgb(mode,source,value):std::array<float,3>{};if(channel==3&&normal_blend_mode(mode)){const auto normal=blend_normal(mode,{source[0]*2-1,source[1]*2-1,source[2]*2-1},{value[0]*2-1,value[1]*2-1,value[2]*2-1});for(int c=0;c<3;++c)blended[c]=normal[c]*.5f+.5f;}else if(channel!=0)for(int c=0;c<3;++c)blended[c]=blend_value(mode,source[c],value[c]);const float result=alpha+coverage*(1-alpha);for(int c=0;c<3;++c)value[c]=((1-alpha)*value[c]*coverage+alpha*((1-coverage)*source[c]+coverage*blended[c]))/result;coverage=result;return value;
}
// Bitmap Fill actions own their UV transform and raw resource. They never
// resample or modify the layer's initial image or its named color boundary.
inline std::array<float,3> content_fill_value(const LevelsEffect &effect,int x,int y,int tile,int width,int height){
 if(!effect.bitmap_fill)return effect.fill_value;
 if(x<0||y<0||width<=0||height<=0)return {};
 auto mapping=effect.bitmap_fill->projection;mapping.sample_alpha=false;
 const auto pixel=fill_texture_uv_sample(mapping,x,y,width,tile,height);
 std::array<float,3> result{pixel[0],pixel[1],pixel[2]};
 if(effect.channel==0&&effect.linear_color)result=linear_color_value(*effect.linear_color,result);
 else if(effect.channel!=0&&effect.channel!=3)result.fill(pixel[0]);
 for(int c=0;c<3;++c)result[c]+=effect.fill_value[c];
 return result;
}
inline std::array<float,3> apply_content_effects(const std::vector<LevelsEffect> &effects,int channel,std::array<float,3> value,float &coverage,int x,int y,int tile,int width,int height,ChannelPrecision precision,const QUuid *stop=nullptr,std::array<float,3> *input=nullptr,const MeshMaps *maps=nullptr,const AnchorSampleContext *context=nullptr,const std::array<float,3> *linear_tint=nullptr){for(const auto &effect:effects){if(effect.kind==ContentEffectKind::Anchor){if(stop&&effect.id==*stop){if(input)*input=value;return value;}continue;}if(effect.kind==ContentEffectKind::Paint){if(effect.enabled)value=blend_effect_paint(effect.paint,channel,value,coverage,x,y,tile,width,height,precision,effect.opacity,effect.blending);continue;}if(effect.channel!=channel)continue;if(stop&&effect.id==*stop&&input)*input=value;if(effect.kind==ContentEffectKind::LinearColorTransform){if(channel!=0||!effect.linear_color)throw std::invalid_argument("Invalid linear Color effect");value=linear_color_value(*effect.linear_color,value,linear_tint?*linear_tint:std::array<float,3>{1,1,1},effect.enabled);continue;}if(!effect.enabled)continue;if(effect.kind==ContentEffectKind::Fill){if(effect.blending!=LayerBlend::Disabled){if(effect.initialize_content){if(!effect.source_coverage)value=precision_rgb(content_fill_value(effect,x,y,tile,width,height),precision);coverage=effect.opacity;continue;}const auto ink=precision_rgb(content_fill_value(effect,x,y,tile,width,height),precision);const auto blended=channel==0||channel==3?blend_rgb(effect.blending,ink,value):std::array<float,3>{blend_value(effect.blending,ink[0],value[0]),blend_value(effect.blending,ink[1],value[1]),blend_value(effect.blending,ink[2],value[2])};const float alpha=effect.opacity;const float next=alpha+coverage*(1-alpha);if(next>1e-8f)for(int c=0;c<3;++c)value[c]=(alpha*std::lerp(ink[c],blended[c],coverage)+(1-alpha)*coverage*value[c])/next;coverage=next;}continue;}if(effect.kind==ContentEffectKind::Generator){value=blend_generated_content(effect,maps,channel,value,coverage,x,y,tile,width,height,precision,effect.opacity,effect.blending,context,content_generator_color(effect));continue;}if(effect.kind==ContentEffectKind::Filter){if(effect.filter.resource&&effect.blending!=LayerBlend::Disabled&&effect.filter.resource<7){std::array<float,3> filtered=channel==0?filter_rgb(effect.filter,value):std::array<float,3>{filter_value(effect.filter,value[0]),filter_value(effect.filter,value[1]),filter_value(effect.filter,value[2])};const auto blended=channel==0?blend_rgb(effect.blending,filtered,value):std::array<float,3>{blend_value(effect.blending,filtered[0],value[0]),blend_value(effect.blending,filtered[1],value[1]),blend_value(effect.blending,filtered[2],value[2])};for(int c=0;c<3;++c)value[c]=std::lerp(value[c],blended[c],effect.opacity);}}else value=mixed_levels_rgb(effect,channel,value);}return value;}

}
