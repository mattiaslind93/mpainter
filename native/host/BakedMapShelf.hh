#pragma once
#include "FillTexture.hh"
#include "ResourceBudget.hh"
#include "FillTexturePool.hh"
#include "MeshMaps.hh"
#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <QUuid>
#include <cstring>
#include <stdexcept>
namespace bts {
// Raw float tiles. Shelf assignment must not pass baked maps through IMB.
inline constexpr char baked_map_magic[]="BTSMAP1";
inline bool is_btsmap(const QByteArray &bytes){return bytes.size()>=8&&std::memcmp(bytes.constData(),baked_map_magic,7)==0;}
inline QString baked_shelf_id(const QUuid &set,int role){return "baked:"+set.toString(QUuid::WithoutBraces)+":"+QString::number(role);}
inline QByteArray encode_btsmap(const MeshMap &map){
  if(map.tiles.empty())throw std::invalid_argument("Baked map has no tiles");
  if(map.tiles.size()>std::numeric_limits<std::uint32_t>::max())throw std::overflow_error("Baked map count exceeds uint32 format");
  std::uint64_t needed=12;for(const auto &[tile,plane]:map.tiles){if(!plane.pixels)throw std::invalid_argument("Baked map pixels are missing");needed=resource_sum(needed,resource_sum(12,resource_product(plane.pixels->rgba.size(),sizeof(float))));}
  if(needed>std::uint64_t(std::numeric_limits<qsizetype>::max()))throw std::overflow_error("Baked map archive exceeds QByteArray representation");require_texture_ram(needed,"Encoded baked map archive");
  QByteArray bytes(baked_map_magic,8);bytes.reserve(qsizetype(needed));const auto append=[&](const void *data,qsizetype size){bytes.append(static_cast<const char *>(data),size);};
  const std::uint32_t count=std::uint32_t(map.tiles.size());append(&count,4);
  for(const auto &[tile,plane]:map.tiles){const std::int32_t header[3]={tile,plane.pixels->resolution,plane.pixels->height};append(header,12);append(plane.pixels->rgba.data(),qsizetype(resource_product(plane.pixels->rgba.size(),sizeof(float))));}
  return bytes;
}
inline std::shared_ptr<const FillTexture> decode_btsmap(const QString &asset_id,const QByteArray &bytes,const QString &name){
  if(!is_btsmap(bytes))throw std::invalid_argument("Baked map payload is not BTSMAP1");
  const char *cursor=bytes.constData()+8,*end=bytes.constData()+bytes.size();const auto take=[&](std::uint64_t size){if(size>std::uint64_t(end-cursor))throw std::invalid_argument("Truncated baked map");const char *p=cursor;cursor+=std::ptrdiff_t(size);return p;};
  std::uint32_t count=0;std::memcpy(&count,take(4),4);if(count<1||std::uint64_t(count)>std::uint64_t(end-cursor)/28)throw std::invalid_argument("Invalid baked map tile count for encoded payload");const ResourceBudget budget;std::uint64_t decoded=resource_product(count,sizeof(FillTexture)+sizeof(std::pair<int,std::shared_ptr<const FillTexture>>));budget.require(decoded,0,"Baked map descriptors");
  auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();
  const auto hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();
  for(std::uint32_t i=0;i<count;++i){std::int32_t header[3];std::memcpy(header,take(12),12);const int tile=header[0],width=header[1],height=header[2];if(tile<1001||tile>9999||width<1||height<1)throw std::invalid_argument("Invalid baked map tile");const auto count_pixels=resource_product(width,height),pixel_bytes=resource_product(count_pixels,sizeof(FillTexture::Pixel));const char *source=take(pixel_bytes);decoded=resource_sum(decoded,pixel_bytes);budget.require(decoded,0,"Decoded baked map tiles");auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(count_pixels));std::memcpy(pixels->data(),source,std::size_t(pixel_bytes));float low=0,high=0;bool range=false;for(const auto &pixel:*pixels)for(int c=0;c<3;++c){if(!range){low=high=pixel[c];range=true;}low=std::min(low,pixel[c]);high=std::max(high,pixel[c]);}auto texture=std::make_shared<FillTexture>();texture->asset=asset_id.toStdString();texture->hash=hash;if(count>1)texture->storage_key=hash+":tile:"+std::to_string(tile);texture->name=name.toStdString();texture->width=width;texture->height=height;texture->raw=true;texture->floating_point=true;texture->range_valid=range;texture->minimum=low;texture->maximum=high;texture->pixels=intern_fill_texture_pixels(*texture,std::move(pixels));texture->validate();if(!members->emplace(tile,std::move(texture)).second)throw std::invalid_argument("Duplicate baked map tile identity");}
  if(cursor!=end)throw std::invalid_argument("Baked map payload has trailing bytes");
  auto result=std::make_shared<FillTexture>(*members->begin()->second);if(members->size()>1){result->tiles=members;for(const auto &[tile,member]:*members){result->minimum=std::min(result->minimum,member->minimum);result->maximum=std::max(result->maximum,member->maximum);}result->validate();}
  return result;
}
}
