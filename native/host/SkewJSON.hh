#pragma once
#include "TextureExtent.hh"
#include "SkewField.hh"
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
inline QJsonObject skew_field_json(const SkewField &field){
 field.settings.validate();const auto &s=field.settings;QJsonArray tiles;
 if(field.loaded){for(const auto &[tile,pixels]:field.pixels.tiles())if(pixels.page_count(2))tiles.append(QJsonObject{{"number",tile},{"pages",qint64(pixels.page_count(2))},{"hash",QString::number(pixels.hash(2),16)}});}
 else for(const auto &[tile,data]:field.pending_tiles)tiles.append(QJsonObject{{"number",tile},{"pages",qint64(data.pages)},{"hash",QString::number(data.hash,16)}});
 return {{"resolution",field.resolution},{"enabled",s.enabled},{"edge_protection",s.edge_protection},{"edge_distance",s.edge_distance},{"edge_contrast",s.edge_contrast},{"vectors",s.vectors},{"vector_length",s.vector_length},{"vector_density",s.vector_density},{"vector_opacity",s.vector_opacity},{"tiles",tiles}};
}
inline SkewField read_skew_field_json(const QJsonValue &value,const std::set<int> &allowed){
 const auto fail=[](){throw std::invalid_argument("Invalid skew correction metadata");};
 if(!value.isObject())fail();const auto o=value.toObject();SkewField field;auto &s=field.settings;
 const auto number=[&](const char *key){if(!o[key].isDouble()||!std::isfinite(o[key].toDouble()))fail();return o[key].toDouble();};
 const auto boolean=[&](const char *key){if(!o[key].isBool())fail();return o[key].toBool();};
 const double resolution=number("resolution");require_texture_resolution_limit(resolution,"Skew map");if(resolution<128||resolution>max_texture_resolution||resolution!=int(resolution)||(int(resolution)&(int(resolution)-1)))fail();field.resolution=int(resolution);
 s.enabled=boolean("enabled");s.edge_protection=boolean("edge_protection");s.vectors=boolean("vectors");s.edge_distance=number("edge_distance");s.edge_contrast=number("edge_contrast");s.vector_length=number("vector_length");s.vector_opacity=number("vector_opacity");const double density=number("vector_density");if(density<1||density>128||density!=int(density))fail();s.vector_density=int(density);s.validate();
 if(!o["tiles"].isArray()||o["tiles"].toArray().size()>int(allowed.size()))fail();
 for(const auto &entry:o["tiles"].toArray()){
  if(!entry.isObject())fail();const auto t=entry.toObject();const auto tile=t["number"].toDouble(-1),pages=t["pages"].toDouble(-1);bool valid=false;const auto hash=t["hash"].toString();const auto decoded=hash.toULongLong(&valid,16);
  if(tile<1001||tile>2000||tile!=int(tile)||!allowed.contains(int(tile))||pages<1||pages>double(sparse_texture_page_count(field.resolution))||!std::isfinite(pages)||pages!=std::floor(pages)||!valid||QString::number(decoded,16)!=hash||!field.pending_tiles.emplace(int(tile),SkewField::TileDescriptor{std::size_t(pages),decoded}).second)fail();
 }
 field.loaded=field.pending_tiles.empty();return field;
}
}
