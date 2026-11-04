#include "TextureExtent.hh"
#include "ResourceBudget.hh"
#pragma once
#include "LayerMask.hh"
#include "MaskPresetTextures.hh"
#include "TextureMemory.hh"
#include "EffectPaintIO.hh"
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QtEndian>
#include <limits>
namespace bts {
inline std::uint64_t mask_preset_memory_budget(){
 const auto ram=texture_available_ram_bytes();
 // Raw qCompress archives have a 32-bit decoded-length header. This is a file
 // representation limit; actual admission comes from currently available RAM.
 return std::min<std::uint64_t>(ram/8,std::numeric_limits<quint32>::max()-1ULL);
}
inline QByteArray write_mask_preset(const LayerMask &mask){
  const auto mask_preset_budget=mask_preset_memory_budget();const MaskPresetTextures textures(mask);
  if(!std::isfinite(mask.background)||mask.background<0||mask.background>1)throw std::invalid_argument("Invalid smart-mask background");
  std::uint64_t pages=0;for(const auto &[number,pixels]:mask.pixels.tiles())pages+=pixels.page_count(2);
  const auto metadata=QJsonDocument(QJsonObject{{"version",6},{"background",mask.background},{"effects",mask_effects_json(mask.effects)},{"texture_resources",textures.descriptors},{"texture_bindings",textures.bindings}}).toJson(QJsonDocument::Compact);
  if(std::uint64_t(metadata.size())>std::numeric_limits<quint32>::max()||pages*PaintPixels::page_side*PaintPixels::page_side*16+metadata.size()+40960>std::uint64_t(mask_preset_budget))throw std::length_error("Mask generator exceeds the available safe RAM budget");
  QByteArray raw;QDataStream stream(&raw,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_8);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  stream<<quint32(0x42544d31)<<quint32(metadata.size());if(stream.writeRawData(metadata.constData(),metadata.size())!=metadata.size())throw std::runtime_error("Cannot encode smart-mask header");stream<<quint32(mask.pixels.tiles().size());
  for(const auto &[number,pixels]:mask.pixels.tiles()){stream<<qint32(number)<<qint32(pixels.resolution())<<quint32(pixels.page_count(2));pixels.visit_pages([&](int channel,std::uint32_t key,std::span<const PaintPixels::Pixel> values){if(channel!=2)return;stream<<quint32(key);for(const auto &pixel:values)for(float v:pixel)stream<<v;});}
  std::uint64_t decoded=pages*64*64*16+metadata.size()+40960;for(const auto &entry:mask_effect_entries(mask.effects))if(entry.effect->kind==MaskEffectKind::Paint)write_effect_paint(stream,entry.effect->paint,decoded,mask_preset_budget);
  textures.write(stream,decoded,mask_preset_budget);
  if(stream.status()!=QDataStream::Ok||raw.size()>mask_preset_budget)throw std::runtime_error("Cannot encode mask generator");return qCompress(raw,1);
}
inline LayerMask read_mask_preset(const QByteArray &bytes){
  const auto mask_preset_budget=mask_preset_memory_budget();
  const auto fail=[](){throw std::invalid_argument("Invalid smart-mask preset");};if(bytes.size()<4||bytes.size()>mask_preset_budget||qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()))>mask_preset_budget)fail();const auto raw=qUncompress(bytes);if(raw.isEmpty()||raw.size()>mask_preset_budget)fail();QDataStream stream(raw);stream.setVersion(QDataStream::Qt_6_8);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  quint32 magic,metadata_size,tiles;stream>>magic>>metadata_size;if(magic!=0x42544d31||metadata_size>quint32(raw.size()))fail();QByteArray metadata(qsizetype(metadata_size),Qt::Uninitialized);if(stream.readRawData(metadata.data(),metadata_size)!=metadata_size)fail();stream>>tiles;if(tiles>1000)fail();QJsonParseError error;const auto root=QJsonDocument::fromJson(metadata,&error).object();if(error.error!=QJsonParseError::NoError||(root["version"].toInt()<1||root["version"].toInt()>6))fail();LayerMask mask;mask.background=0;if(root["version"].toInt()>=3){const auto background=root["background"];if(!background.isDouble()||!std::isfinite(background.toDouble())||background.toDouble()<0||background.toDouble()>1)fail();mask.background=float(background.toDouble());}mask.effects=read_mask_effects(root["effects"]);std::set<int> numbers;std::uint64_t decoded=0;
  for(quint32 t=0;t<tiles;++t){qint32 tile,resolution;quint32 pages;stream>>tile>>resolution>>pages;if(tile<1001||tile>2000||!numbers.insert(tile).second||!valid_sparse_texture_extent(resolution,128,true)||std::uint64_t(pages)>sparse_texture_page_count(resolution))fail();if(mask.author_resolution&&mask.author_resolution!=resolution)fail();mask.author_resolution=resolution;decoded+=std::uint64_t(pages)*PaintPixels::page_side*PaintPixels::page_side*16;if(decoded>std::uint64_t(mask_preset_budget))fail();for(quint32 p=0;p<pages;++p){quint32 key;stream>>key;std::array<PaintPixels::Pixel,PaintPixels::page_side*PaintPixels::page_side> values;for(auto &pixel:values)for(auto &v:pixel)stream>>v;if(stream.status()!=QDataStream::Ok)fail();mask.pixels.assign_page(resolution,2,key,values,tile);}}
  for(auto *entry:edit_mask_effects_of_kind(mask.effects,MaskEffectKind::Paint)){auto &effect=*entry;if(root["version"].toInt()<2)fail();std::set<int> allowed;for(const auto &plane:effect_paint_json(effect.paint)["planes"].toArray())for(const auto &entry:plane.toArray())allowed.insert(entry.toObject()["number"].toInt());read_effect_paint(stream,effect.paint,allowed,decoded,mask_preset_budget,true);}
  if(root["version"].toInt()>=4)MaskPresetTextures::read(stream,mask,root,decoded,mask_preset_budget);
  if(!stream.atEnd()||stream.status()!=QDataStream::Ok)fail();return mask;
}
}
