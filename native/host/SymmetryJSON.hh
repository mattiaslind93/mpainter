#pragma once
#include "Symmetry.hh"
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
inline QJsonObject symmetry_json(const SymmetrySettings &s) {
  return {{"version",1},{"enabled",s.enabled},{"mode",int(s.mode)},{"axis",s.axis},{"offset",s.offset},{"copies",s.copies},{"center",QJsonArray{s.center[0],s.center[1],s.center[2]}},{"manipulator",s.manipulator},{"intersection",s.intersection}};
}
inline SymmetrySettings read_symmetry_json(const QJsonObject &object) {
  const auto number=[&](const char *key){const auto value=object[key];if(!value.isDouble()||!std::isfinite(value.toDouble()))throw std::runtime_error("Invalid project symmetry number");return value.toDouble();};
  const auto integer=[&](const char *key){const auto n=number(key);if(n!=std::floor(n)||n<0||n>32)throw std::runtime_error("Invalid project symmetry index");return int(n);};
  const auto boolean=[&](const char *key){if(!object[key].isBool())throw std::runtime_error("Invalid project symmetry flag");return object[key].toBool();};
  if(integer("version")!=1)throw std::runtime_error("Unsupported project symmetry version");
  SymmetrySettings s;s.enabled=boolean("enabled");s.manipulator=boolean("manipulator");s.intersection=boolean("intersection");s.mode=SymmetryMode(integer("mode"));s.axis=integer("axis");s.copies=integer("copies");s.offset=number("offset");
  const auto center=object["center"].toArray();if(center.size()!=3)throw std::runtime_error("Invalid project symmetry center");for(int i=0;i<3;++i){if(!center[i].isDouble())throw std::runtime_error("Invalid project symmetry center value");s.center[i]=center[i].toDouble();}s.validate();return s;
}
}
