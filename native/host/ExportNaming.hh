#pragma once
#include "ResourceBudget.hh"
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <stdexcept>
#include <algorithm>
#include <utility>
namespace bts {
// Normalize only the channel suffix before expanding project-owned mesh/set names.
inline QString canonical_export_suffix(QString value,bool openpbr=false){
  value.replace(QRegularExpression("([A-Z])([A-Z][a-z])"),"\\1_\\2");
  value.replace(QRegularExpression("([a-z0-9])([A-Z])"),"\\1_\\2");
  value=value.toLower();value.replace(QRegularExpression("[\\s-]+"),"_");
  value.replace(QRegularExpression("_+"),"_");
  for(const auto &[old_name,new_name]:{std::pair{"base_color","color"},{"height","bump"},{"normal_open_gl","normal_opengl"},{"normal_direct_x","normal_directx"}})
    if(!openpbr||QString(old_name)!="base_color")value.replace(QRegularExpression(QString("(^|_)%1(?=_|$)").arg(old_name)),QString("\\1%1").arg(new_name));
  return value;
}
inline QString canonical_export_pattern(QString pattern,bool openpbr=false){
  const int file=pattern.lastIndexOf('/')+1;int suffix=file;
  for(const auto token:{QString("$mesh"),QString("$textureSet")}){const int index=pattern.lastIndexOf(token);if(index>=file)suffix=std::max(suffix,index+int(token.size()));}
  auto tail=pattern.mid(suffix);
  tail.replace("$channel",QString(QChar(0xe000))).replace("$udim",QString(QChar(0xe001)));
  tail=canonical_export_suffix(tail,openpbr);
  tail.replace(QChar(0xe000),"$channel").replace(QChar(0xe001),"$udim");
  return pattern.left(suffix)+tail;
}
// Convert only a known terminal channel suffix, before an optional UDIM token.
// Project-owned mesh/set names are still tokens here and cannot be rewritten.
inline QString export_convention_pattern(QString pattern,const QString &classic,const QString &modern,bool openpbr){
 pattern=canonical_export_pattern(pattern,openpbr);if(!openpbr||classic==modern||pattern.contains("$channel"))return pattern;
 QString tile;for(const auto &ending:{QString("_$udim"),QString(".$udim")})if(pattern.endsWith(ending)){tile=ending;pattern.chop(ending.size());break;}
 if(pattern!=modern&&!pattern.endsWith("_"+modern)){if(pattern==classic)pattern=modern;else if(pattern.endsWith("_"+classic)){pattern.chop(classic.size());pattern+=modern;}}
 return pattern+tile;
}
inline QString export_token_component(QString value){
  value.replace(QRegularExpression("[^\\p{L}\\p{N}_-]"),"_");if(value.isEmpty()||value=="."||value=="..")value="Unnamed";return value;
}
// Templates are relative paths; tokens are single safe path components.
inline QString export_output_name(QString pattern,QString mesh,QString texture_set,QString channel,int tile,QString extension,bool udim_asset=false,bool openpbr=false,const ResourceBudget *budget=nullptr){
  // Count repeated tokens before replacement: a short template can expand to
  // many copies of a long datablock name. Charge every copy, not only one.
  auto units=resource_sum(resource_sum(pattern.size(),mesh.size()),resource_sum(texture_set.size(),channel.size()));
  for(const auto &[token,length]:{std::pair{QString("$textureSet"),std::uint64_t(texture_set.size())},std::pair{QString("$mesh"),std::uint64_t(mesh.size())},std::pair{QString("$channel"),std::uint64_t(channel.size())},std::pair{QString("$udim"),std::uint64_t(16)}})units=resource_sum(units,resource_product(pattern.count(token),length));
  const auto bytes=resource_product(units,12);if(budget)budget->require(bytes,0,"Export filename expansion");else require_texture_ram(bytes,"Export filename expansion");pattern=canonical_export_pattern(pattern,openpbr);channel=canonical_export_suffix(channel,openpbr);
  QString marker(QChar(0xe002));while(pattern.contains(marker))marker+=QChar(0xe003);
  pattern.replace("$textureSet",export_token_component(texture_set));pattern.replace("$mesh",export_token_component(QFileInfo(mesh).completeBaseName()));pattern.replace("$channel",export_token_component(channel));pattern.replace("$udim",udim_asset?marker:QString::number(tile));
  if(pattern.contains('$'))throw std::invalid_argument("Unknown output naming token");
  if(pattern.isEmpty()||pattern.startsWith('/')||pattern.contains('\\')||pattern.contains(':'))throw std::invalid_argument("Output name must be a relative path without drive letters");
  const auto components=pattern.split('/');for(const auto &part:components){if(part.isEmpty()||part=="."||part==".."||part.trimmed()!=part||part.endsWith('.')||part.contains(QRegularExpression("[\\x00-\\x1f<>\\\"|?*]")))throw std::invalid_argument("Invalid output path component");}
  if(udim_asset)pattern.replace(marker,"<UDIM>");return pattern+"."+extension;
}
}
