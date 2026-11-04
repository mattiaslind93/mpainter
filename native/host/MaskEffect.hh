#include "ResourceAvailability.hh"
#include "ResourceStack.hh"
#pragma once
#include "MeshGenerator.hh"
#include "MaskFillMapping.hh"
#include "LayerEffect.hh"
#include "LayerBlend.hh"
#include <QUuid>
#include <QJsonArray>
#include <QJsonObject>
#include <set>
#include <algorithm>
#include <limits>
#include <map>
#include <memory>
namespace bts {
// Mask effects evaluate bottom to top, separately from the material-content chain.
enum class MaskEffectKind {ColorSelection,Fill,Levels,Generator,Compare,Paint,Filter,Anchor,Group};
struct MaskAnchorSample {float value=0;int layer=-1;};
using MaskAnchorSamples=std::map<QUuid,MaskAnchorSample>;
struct MaskCompareInputs {AnchorSampleContext reference;std::array<float,6> below{},layer{};int layer_index=-1;mutable std::shared_ptr<MaskAnchorSamples> anchors;const FillSurfaceAtlas *projection=nullptr;double cm_per_native=1;std::array<double,2> native_per_uv{1,1};};
struct MaskEffect :MeshGeneratorSettings {
  QUuid anchor_source;QString anchor_name;int anchor_channel=0,anchor_component=1;bool anchor_coverage=true;
  AnchorReference anchor_reference()const{return {anchor_source,anchor_channel,anchor_component,anchor_coverage};}
  std::shared_ptr<const std::vector<MaskEffect>> group_effects;
  EffectPaint paint;
  FilterSettings filter;
  MaskFillMapping mapping;
  MaskEffectKind kind=MaskEffectKind::ColorSelection;
  int compare_channel=4,left=1,right=0,operation=2;float constant=.5f;
  // A negative width preserves legacy comparisons; authored ramps are centered on offset.
  float compare_offset=0,compare_width=-1;
  bool compare_invert=false;
  std::array<float,5> compare_ramp{0,.25f,.5f,.75f,1};
  float fill=.5f,opacity=1;
  LayerBlend blending=LayerBlend::Normal;
  LevelsEffect levels;
  QUuid id=QUuid::createUuid();
  bool enabled=true,use_id_map=true;
  float output=1,hardness=1,tolerance=.1f;
  std::vector<std::array<float,3>> colors;
  std::string name;
};
inline float color_selection(const MaskEffect &effect,const std::array<float,4> &sample){
  if(!effect.use_id_map||sample[3]<=1e-8f||effect.colors.empty())return 0;
  float distance=std::numeric_limits<float>::infinity();for(const auto &color:effect.colors){float square=0;for(int c=0;c<3;++c)square+=(color[c]-sample[c])*(color[c]-sample[c]);distance=std::min(distance,std::sqrt(square/3));}
  if(distance<=effect.tolerance*effect.hardness)return effect.output;if(distance>=effect.tolerance)return 0;
  const float t=(effect.tolerance-distance)/std::max(1e-8f,effect.tolerance*(1-effect.hardness));return effect.output*t*t*(3-2*t);
}

inline float mask_compare_operand(const MaskEffect &effect,int source,float previous,const MaskCompareInputs *inputs){if(source==3)return effect.constant;if(source==2)return previous;if(!inputs)return 0;return source==0?inputs->below.at(effect.compare_channel):inputs->layer.at(effect.compare_channel);}
inline float compare_mask_values(const MaskEffect &effect,float source,float target){
  if(effect.compare_width>=0){
    const float delta=(effect.operation==0?target-source:effect.operation==1?effect.tolerance-std::abs(source-target):source-target)-effect.compare_offset;
    const float t=effect.compare_width<=1e-7f?(delta>=0?1.f:0.f):std::clamp(.5f+delta/effect.compare_width,0.f,1.f);
    const float position=t*4;const int i=std::min(3,int(position));
    const float value=std::lerp(effect.compare_ramp[i],effect.compare_ramp[i+1],position-i);
    return effect.compare_invert?1-value:value;
  }
  const auto smooth=[](float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);};
  if(effect.operation==1){const float delta=std::abs(source-target),inner=effect.tolerance*effect.hardness;if(delta<=inner)return 1;if(delta>=effect.tolerance)return 0;return smooth((effect.tolerance-delta)/std::max(1e-8f,effect.tolerance-inner));}
  const float delta=effect.operation==0?target-source:source-target,width=1-effect.hardness;if(width<=1e-7f)return delta>=0?1.f:0.f;return smooth((delta+width)/width);
}
// Traversal preserves the tree; mutable traversal detaches immutable child
// owners before yielding references, so edits cannot modify sibling presets.
template<class Effect> struct MaskEffectTree {
 std::vector<Effect *> entries;
 struct Iterator {typename std::vector<Effect *>::const_iterator cursor;Effect &operator*()const{return **cursor;}Iterator &operator++(){++cursor;return *this;}bool operator!=(const Iterator &other)const{return cursor!=other.cursor;}};
 Iterator begin()const{return {entries.begin()};}Iterator end()const{return {entries.end()};}
};
inline MaskEffectTree<const MaskEffect> mask_effects_recursive(const std::vector<MaskEffect> &effects){
 MaskEffectTree<const MaskEffect> result;for(const auto &effect:effects)result.entries.push_back(&effect);
 for(std::size_t i=0;i<result.entries.size();++i)if(const auto &group=result.entries[i]->group_effects){for(const auto &child:*group)result.entries.push_back(&child);}return result;
}
inline MaskEffectTree<MaskEffect> mask_effects_recursive(std::vector<MaskEffect> &effects){
 MaskEffectTree<MaskEffect> result;for(auto &effect:effects)result.entries.push_back(&effect);
 for(std::size_t i=0;i<result.entries.size();++i)if(const auto &group=result.entries[i]->group_effects){auto children=std::make_shared<std::vector<MaskEffect>>(*group);result.entries[i]->group_effects=children;for(auto &child:*children)result.entries.push_back(&child);}return result;
}
inline void renew_mask_effect_id(MaskEffect &effect){effect.id=QUuid::createUuid();effect.levels.id=effect.id;}
inline void renew_mask_effect_ids(std::vector<MaskEffect> &effects){for(auto &effect:effects)if(effect.group_effects){auto children=std::make_shared<std::vector<MaskEffect>>(*effect.group_effects);renew_mask_effect_ids(*children);effect.group_effects=std::move(children);}std::map<QUuid,QUuid> ids;for(auto &effect:effects){const auto old=effect.id;renew_mask_effect_id(effect);ids[old]=effect.id;}for(auto &effect:effects){if(const auto found=ids.find(effect.anchor_source);found!=ids.end())effect.anchor_source=found->second;for(auto &ref:effect.input_anchors)if(const auto found=ids.find(ref.source);found!=ids.end())ref.source=found->second;}}
inline QJsonArray mask_effects_json(const std::vector<MaskEffect> &);
inline std::vector<MaskEffect> read_mask_effects(const QJsonValue &);
inline QJsonArray mask_effects_json(const std::vector<MaskEffect> &effects){require_resource_stack(32768,"Mask group encoding");
  QJsonArray result;for(const auto &e:effects){QJsonObject o{{"id",e.id.toString(QUuid::WithoutBraces)},{"enabled",e.enabled}};
    if(e.kind==MaskEffectKind::Group){o["type"]="group";o["effects"]=mask_effects_json(e.group_effects?*e.group_effects:std::vector<MaskEffect>{});o["opacity"]=e.opacity;o["blending"]=int(e.blending);}
    else if(e.kind==MaskEffectKind::ColorSelection){QJsonArray colors;for(const auto &c:e.colors)colors.append(QJsonArray{c[0],c[1],c[2]});o["type"]="color_selection";o["use_id_map"]=e.use_id_map;o["output"]=e.output;o["hardness"]=e.hardness;o["tolerance"]=e.tolerance;o["colors"]=colors;}
    else if(e.kind==MaskEffectKind::Anchor){o["type"]="anchor";o["name"]=e.anchor_name;}
    else if(e.kind==MaskEffectKind::Filter){o["type"]="filter";o["filter"]=filter_json(e.filter);o["opacity"]=e.opacity;o["blending"]=layer_blend_names[int(e.blending)];}
    else if(e.kind==MaskEffectKind::Paint){o["type"]="paint";o["paint"]=effect_paint_json(e.paint);o["opacity"]=e.opacity;o["blending"]=layer_blend_names[int(e.blending)];}
    else if(e.kind==MaskEffectKind::Fill){o["type"]="fill";o["anchor_source"]=e.anchor_source.isNull()?QString{}:e.anchor_source.toString(QUuid::WithoutBraces);if(e.anchor_channel!=0)o["anchor_channel"]=e.anchor_channel;if(e.anchor_component!=1)o["anchor_component"]=e.anchor_component;if(!e.anchor_coverage)o["anchor_coverage"]=false;o["mapping"]=mask_fill_mapping_json(e.mapping);o["value"]=e.fill;o["opacity"]=e.opacity;o["blending"]=layer_blend_names[int(e.blending)];}
    else if(e.kind==MaskEffectKind::Compare){o["type"]="compare";if(e.compare_width!=-1||e.compare_offset!=0||e.compare_invert||e.compare_ramp!=std::array<float,5>{0,.25f,.5f,.75f,1}){o["ramp_offset"]=e.compare_offset;o["ramp_width"]=e.compare_width;o["ramp_invert"]=e.compare_invert;QJsonArray ramp;for(float v:e.compare_ramp)ramp.append(v);o["ramp"]=ramp;}o["channel"]=e.compare_channel;o["left"]=e.left;o["right"]=e.right;o["operation"]=e.operation;o["constant"]=e.constant;o["hardness"]=e.hardness;o["tolerance"]=e.tolerance;o["opacity"]=e.opacity;o["blending"]=layer_blend_names[int(e.blending)];}
    else if(e.kind==MaskEffectKind::Generator){const auto settings=mesh_generator_json(e);for(auto i=settings.begin();i!=settings.end();++i)o[i.key()]=i.value();o["type"]="generator";o["opacity"]=e.opacity;o["blending"]=layer_blend_names[int(e.blending)];}
    else{auto levels=e.levels;levels.id=e.id;levels.channel=2;levels.opacity=e.opacity;levels.blending=e.blending;o["type"]="levels";o["levels"]=effects_json({levels})[0];}result.append(o);
  }for(int n=0;n<int(effects.size());++n)if(!effects[n].name.empty()){auto object=result[n].toObject();object["display_name"]=QString::fromStdString(effects[n].name);result[n]=object;}return result;
}
inline std::vector<MaskEffect> read_mask_effects(const QJsonValue &value){require_resource_stack(32768,"Mask group decoding");
  const auto fail=[](){throw std::invalid_argument("Invalid mask effects");};if(!value.isArray())fail();const auto entries=value.toArray();std::uint64_t text_bytes=0;for(const auto &entry:entries){const auto object=entry.toObject();for(const char *key:{"display_name","name"})text_bytes=resource_sum(text_bytes,resource_product(object[key].toString().size(),8));}std::vector<MaskEffect> result;std::set<QUuid> ids;
  for(const auto &entry:value.toArray()){if(!entry.isObject())fail();const auto o=entry.toObject();MaskEffect e;if(o.contains("display_name")){if(!o["display_name"].isString()||o["display_name"].toString().contains(QChar(0)))fail();e.name=o["display_name"].toString().toStdString();}if(!o["type"].isString()||!o["id"].isString()||!o["enabled"].isBool())fail();e.id=QUuid(o["id"].toString());if(e.id.isNull()||!ids.insert(e.id).second)fail();e.enabled=o["enabled"].toBool();const auto unit=[&](const QJsonValue &v){if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<0||v.toDouble()>1)fail();return float(v.toDouble());};
    if(o["type"]=="group"){e.kind=MaskEffectKind::Group;e.opacity=unit(o["opacity"]);if(!o["blending"].isDouble()||o["blending"].toDouble()!=o["blending"].toInt(-1)||o["blending"].toInt()<0||o["blending"].toInt()>=int(layer_blend_names.size()))fail();e.blending=LayerBlend(o["blending"].toInt());e.group_effects=std::make_shared<const std::vector<MaskEffect>>(read_mask_effects(o["effects"]));}
    else if(o["type"]=="color_selection"){if(!o["use_id_map"].isBool())fail();e.use_id_map=o["use_id_map"].toBool();e.output=unit(o["output"]);e.hardness=unit(o["hardness"]);e.tolerance=unit(o["tolerance"]);if(!o["colors"].isArray())fail();for(const auto &v:o["colors"].toArray()){if(!v.isArray()||v.toArray().size()!=3)fail();std::array<float,3> rgb;for(int c=0;c<3;++c)rgb[c]=unit(v.toArray()[c]);e.colors.push_back(rgb);}}
    else if(o["type"]=="anchor"){e.kind=MaskEffectKind::Anchor;if(!o["name"].isString())fail();e.anchor_name=o["name"].toString();if(e.anchor_name.trimmed().isEmpty()||e.anchor_name.contains(QChar::Null))fail();}
    else if(o["type"]=="filter"){e.kind=MaskEffectKind::Filter;e.filter=read_filter(o["filter"]);e.opacity=unit(o["opacity"]);if(!o["blending"].isString())fail();const auto name=o["blending"].toString();const auto mode=std::find_if(layer_blend_names.begin(),layer_blend_names.end(),[&](const char *label){return name==label;});if(mode==layer_blend_names.end())fail();e.blending=LayerBlend(mode-layer_blend_names.begin());}
    else if(o["type"]=="paint"){e.kind=MaskEffectKind::Paint;e.paint=read_effect_paint(o["paint"]);e.opacity=unit(o["opacity"]);if(!o["blending"].isString())fail();const auto name=o["blending"].toString();const auto mode=std::find_if(layer_blend_names.begin(),layer_blend_names.end(),[&](const char *label){return name==label;});if(mode==layer_blend_names.end())fail();e.blending=LayerBlend(mode-layer_blend_names.begin());}
    else if(o["type"]=="fill"){e.kind=MaskEffectKind::Fill;if(!o["anchor_source"].isUndefined()){if(!o["anchor_source"].isString())fail();const auto text=o["anchor_source"].toString();if(!text.isEmpty()){e.anchor_source=QUuid(text);if(e.anchor_source.isNull())fail();}}{QJsonObject reference{{"source",e.anchor_source.isNull()?QString{}:e.anchor_source.toString(QUuid::WithoutBraces)}};for(const auto &pair:{std::pair{"anchor_channel","channel"},std::pair{"anchor_component","component"},std::pair{"anchor_coverage","coverage"}})if(o.contains(pair.first))reference[pair.second]=o[pair.first];const auto ref=read_anchor_reference(reference);e.anchor_channel=ref.channel;e.anchor_component=ref.component;e.anchor_coverage=ref.coverage;}e.mapping=read_mask_fill_mapping(o["mapping"]);e.fill=unit(o["value"]);e.opacity=unit(o["opacity"]);if(!o["blending"].isString())fail();const auto name=o["blending"].toString();const auto mode=std::find_if(layer_blend_names.begin(),layer_blend_names.end(),[&](const char *label){return name==label;});if(mode==layer_blend_names.end())fail();e.blending=LayerBlend(mode-layer_blend_names.begin());}
    else if(o["type"]=="compare"){e.kind=MaskEffectKind::Compare;
      if(o.contains("ramp")){
        const auto offset=o["ramp_offset"],width=o["ramp_width"];
        if(!offset.isDouble()||!std::isfinite(offset.toDouble())||std::abs(offset.toDouble())>65504||!width.isDouble()||!std::isfinite(width.toDouble())||width.toDouble() < -1||width.toDouble()>65504||!o["ramp_invert"].isBool()||!o["ramp"].isArray()||o["ramp"].toArray().size()!=5)fail();
        e.compare_offset=float(offset.toDouble());e.compare_width=float(width.toDouble());e.compare_invert=o["ramp_invert"].toBool();for(int i=0;i<5;++i)e.compare_ramp[i]=unit(o["ramp"].toArray()[i]);
      }
      const auto integer=[&](const char *key,int maximum){const auto v=o[key];if(!v.isDouble()||v.toDouble()!=v.toInt(-1)||v.toInt()<0||v.toInt()>maximum)fail();return v.toInt();};e.compare_channel=integer("channel",5);e.left=integer("left",3);e.right=integer("right",3);e.operation=integer("operation",2);const auto c=o["constant"];if(!c.isDouble()||!std::isfinite(c.toDouble())||std::abs(c.toDouble())>65504)fail();e.constant=float(c.toDouble());e.hardness=unit(o["hardness"]);e.tolerance=unit(o["tolerance"]);e.opacity=unit(o["opacity"]);if(!o["blending"].isString())fail();const auto name=o["blending"].toString();const auto mode=std::find_if(layer_blend_names.begin(),layer_blend_names.end(),[&](const char *label){return name==label;});if(mode==layer_blend_names.end())fail();e.blending=LayerBlend(mode-layer_blend_names.begin());}
    else if(o["type"]=="generator"){e.kind=MaskEffectKind::Generator;static_cast<MeshGeneratorSettings &>(e)=read_mesh_generator(o);e.opacity=unit(o["opacity"]);if(!o["blending"].isString())fail();const auto name=o["blending"].toString();const auto mode=std::find_if(layer_blend_names.begin(),layer_blend_names.end(),[&](const char *label){return name==label;});if(mode==layer_blend_names.end())fail();e.blending=LayerBlend(mode-layer_blend_names.begin());}
    else if(o["type"]=="levels"){e.kind=MaskEffectKind::Levels;const auto levels=read_effects(QJsonArray{o["levels"]});e.levels=levels.front();e.opacity=e.levels.opacity;e.blending=e.levels.blending;if(e.levels.id!=e.id||e.levels.channel!=2)fail();}
    else fail();result.push_back(std::move(e));
  }return result;
}
}
