#pragma once
#include <QJsonObject>
#include <QUuid>
#include <vector>
#include <cmath>
#include <stdexcept>
namespace bts {
inline void validate_bake_quality(const QJsonObject &quality) {
  for(auto it=quality.begin();it!=quality.end();++it) {
    if(it.key()=="diffusion") {if(!it.value().isBool())throw std::invalid_argument("Invalid bake diffusion setting");continue;}
    int low=0,high=0;
    if(it.key()=="dilation")high=64;
    else if(it.key()=="rays"){low=1;high=512;}
    else if(it.key()=="sampling"){low=1;high=8;}
    else throw std::invalid_argument("Unknown per-map bake parameter");
    const double value=it.value().toDouble(-1);
    if(!it.value().isDouble()||!std::isfinite(value)||value!=std::floor(value)||value<low||value>high)
      throw std::invalid_argument("Invalid per-map bake parameter");
    if(it.key()=="sampling"&&(int(value)&(int(value)-1)))throw std::invalid_argument("Invalid bake sampling count");
  }
}
inline void validate_map_bake_options(const QJsonValue &value) {
  if(!value.isObject())throw std::invalid_argument("Invalid per-map bake settings");
  const auto sets=value.toObject();
  for(auto set=sets.begin();set!=sets.end();++set) {
    if(QUuid(set.key()).isNull()||QUuid(set.key()).toString()!=set.key()||!set.value().isObject())throw std::invalid_argument("Invalid bake Texture Set key");
    const auto maps=set.value().toObject();
    for(auto map=maps.begin();map!=maps.end();++map) {
      if(map.key().size()!=1||map.key()[0]<'0'||map.key()[0]>'6'||!map.value().isObject())
        throw std::invalid_argument("Invalid bake map role");
      validate_bake_quality(map.value().toObject());
    }
  }
}
inline QJsonObject bake_quality(const QJsonObject &recipe,const QUuid &set,int role) {
  if(set.isNull()||role<0||role>6)throw std::invalid_argument("Invalid bake quality selection");
  QJsonObject result{{"dilation",recipe["dilation"].toInt(32)},{"rays",recipe["rays"].toInt(256)},
                     {"sampling",recipe["sampling"].toInt(1)},{"diffusion",recipe["diffusion"].toBool(false)}};
  const auto overrides=recipe["map_options"].toObject()[set.toString()].toObject()[QString::number(role)].toObject();
  for(auto it=overrides.begin();it!=overrides.end();++it)result[it.key()]=it.value();
  validate_bake_quality(result);return result;
}
inline QJsonObject with_bake_quality(QJsonObject recipe,const QUuid &set,int role,const QJsonObject &quality) {
  if(set.isNull()||role<0||role>6)throw std::invalid_argument("Invalid bake quality selection");
  validate_bake_quality(quality);auto sets=recipe["map_options"].toObject();auto maps=sets[set.toString()].toObject();
  maps[QString::number(role)]=quality;sets[set.toString()]=maps;recipe["map_options"]=sets;return recipe;
}
inline QJsonObject synchronized_bake_quality(QJsonObject recipe,const QUuid &source,int role,const std::vector<QUuid> &targets) {
  const auto quality=bake_quality(recipe,source,role);
  for(const auto &target:targets)recipe=with_bake_quality(std::move(recipe),target,role,quality);
  return recipe;
}
}
