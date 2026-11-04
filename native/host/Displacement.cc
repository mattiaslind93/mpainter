#include "Displacement.hh"
namespace bts {
void displacement_import_range(DisplacementSettings &settings,const FillTexture &texture){
 if(texture.range_valid&&texture.floating_point&&(texture.minimum<0||texture.data_units)){settings.midlevel=0;settings.strength=1;}
 else if(texture.range_valid&&texture.minimum>=0&&texture.maximum<=1)settings.midlevel=.5f;
}
QJsonObject displacement_json(const DisplacementSettings &value){value.validate();return {{"channel",value.channel},{"allocated",value.allocated},{"enabled",value.enabled},{"source","displacement"},{"strength",value.strength},{"midlevel",value.midlevel},{"subdivision",value.subdivision},{"mode",value.mode},{"max_length",value.max_length},{"precision",int(value.precision)}};}
DisplacementSettings read_displacement(const QJsonValue &value){
  const auto fail=[](){throw std::invalid_argument("Invalid displacement metadata");};if(!value.isObject())fail();const auto object=value.toObject();DisplacementSettings result;
  const auto boolean=[&](const char *key){if(!object[key].isBool())fail();return object[key].toBool();};
  const auto number=[&](const char *key){if(!object[key].isDouble()||!std::isfinite(object[key].toDouble()))fail();return object[key].toDouble();};
  const auto integer=[&](const char *key){const double v=number(key);if(v!=std::floor(v)||v<0||v>100)fail();return int(v);};
  if(object["source"]!="displacement")fail();result.channel=boolean("channel");result.allocated=boolean("allocated");result.enabled=boolean("enabled");result.strength=number("strength");result.midlevel=number("midlevel");result.subdivision=integer("subdivision");result.mode=integer("mode");result.max_length=number("max_length");result.precision=ChannelPrecision(integer("precision"));result.validate();return result;
}
}
