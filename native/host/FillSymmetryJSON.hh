#pragma once
#include "FillSymmetry.hh"
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
inline QJsonValue fill_symmetry_json(const std::shared_ptr<const FillSymmetry> &value)
{
  if(!value)return QJsonValue(QJsonValue::Null);value->validate();const auto &s=value->constraint;
  return QJsonObject{{"version",1},{"enabled",s.enabled},{"mode",int(s.mode)},{"axis",s.axis},{"copies",s.copies},{"offset",s.offset},{"center",QJsonArray{s.center[0],s.center[1],s.center[2]}},{"flip_u",value->flip_u},{"flip_v",value->flip_v},{"manipulator",s.manipulator},{"intersection",s.intersection}};
}
inline std::shared_ptr<const FillSymmetry> read_fill_symmetry(const QJsonValue &value)
{
  if(value.isUndefined()||value.isNull())return {};
  const auto fail=[](){throw std::invalid_argument("Invalid fill symmetry settings");};if(!value.isObject())fail();const auto data=value.toObject();
  const auto boolean=[&](const char *key){if(!data[key].isBool())fail();return data[key].toBool();};
  const auto number=[&](const QJsonValue &v){if(!v.isDouble()||!std::isfinite(v.toDouble()))fail();return v.toDouble();};
  const auto integer=[&](const char *key){const double v=number(data[key]);if(v!=std::floor(v)||v<0||v>32)fail();return int(v);};
  if(integer("version")!=1)fail();auto result=std::make_shared<FillSymmetry>();auto &s=result->constraint;
  s.enabled=boolean("enabled");s.mode=SymmetryMode(integer("mode"));s.axis=integer("axis");s.copies=integer("copies");s.offset=number(data["offset"]);s.manipulator=boolean("manipulator");s.intersection=boolean("intersection");result->flip_u=boolean("flip_u");result->flip_v=boolean("flip_v");
  const auto center=data["center"].toArray();if(center.size()!=3)fail();for(int i=0;i<3;++i)s.center[i]=number(center[i]);result->validate();return result;
}
}
