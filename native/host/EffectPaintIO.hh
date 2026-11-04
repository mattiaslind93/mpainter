#include "TextureExtent.hh"
#include "ResourceAvailability.hh"
#pragma once
#include "EffectPaint.hh"
#include <QDataStream>
namespace bts {
inline void write_effect_paint(QDataStream &stream,const EffectPaint &paint,std::uint64_t &decoded,std::uint64_t budget){
  if(!paint.loaded)throw std::invalid_argument("Unloaded effect paint");
  for(int i=0;i<11;++i){stream<<quint32(paint.plane(i).tiles().size());for(const auto &[number,pixels]:paint.plane(i).tiles()){quint32 pages=0;pixels.visit_pages([&](int,std::uint32_t,std::span<const PaintPixels::Pixel>){++pages;});decoded+=std::uint64_t(pages)*64*64*16;stream<<qint32(number)<<quint32(pages);pixels.visit_pages([&](int channel,std::uint32_t key,std::span<const PaintPixels::Pixel> values){stream<<quint8(channel)<<quint32(key);for(const auto &pixel:values)for(float v:pixel)stream<<v;});}}
}
inline void read_effect_paint(QDataStream &stream,EffectPaint &destination,const std::set<int> &allowed,std::uint64_t &decoded,std::uint64_t budget,bool mask=false){
  const auto fail=[](){throw std::invalid_argument("Invalid packed effect paint");};auto expected=effect_paint_json(destination);const int plane_count=expected["planes"].toArray().size();if(plane_count==10){auto planes=expected["planes"].toArray();planes.append(QJsonArray{});expected["planes"]=planes;}if(expected.contains("strokes")&&!expected.contains("displacement_strokes"))expected["displacement_strokes"]=QJsonValue{};EffectPaint result;result.resolution=destination.resolution;result.strokes=destination.strokes;result.user_strokes=destination.user_strokes;result.displacement_strokes=destination.displacement_strokes;
  for(int i=0;i<plane_count;++i){quint32 tiles;stream>>tiles;if(tiles>allowed.size())fail();std::set<int> seen;for(quint32 t=0;t<tiles;++t){qint32 number;quint32 pages;stream>>number>>pages;if(!allowed.contains(number)||!seen.insert(number).second||!result.resolution||std::uint64_t(pages)>resource_product(5,sparse_texture_page_count(result.resolution)))fail();decoded+=std::uint64_t(pages)*64*64*16;if(decoded>budget)fail();for(quint32 p=0;p<pages;++p){quint8 channel;quint32 key;stream>>channel>>key;if(channel>4||(i>0&&channel!=2)||(mask&&(i!=0||channel!=2)))fail();std::array<PaintPixels::Pixel,64*64> values;for(auto &pixel:values){for(float &v:pixel)stream>>v;if((i>0||mask)&&(pixel[1]!=0||pixel[2]!=0))fail();if(mask&&(pixel[0]<0||pixel[0]>pixel[3]))fail();}if(stream.status()!=QDataStream::Ok)fail();result.plane(i).assign_page(result.resolution,channel,key,values,number);}}}
  // Old descriptors can contain path-v2 brush recipes; compare their canonical
  // metadata only after validating the loaded raster against every declared hash.
  const auto history_json=[&](const auto &history,const StrokeRaster &flat){if(history&&!history->matches(flat))fail();return stroke_history_json(history,flat);};
  if(expected.contains("strokes")){
    expected["strokes"]=history_json(result.strokes,{result.pixels,result.ao});
    QJsonArray users;for(int slot=0;slot<8;++slot)users.append(history_json(result.user_strokes[slot],{result.users[slot],{}}));expected["user_strokes"]=users;
    expected["displacement_strokes"]=history_json(result.displacement_strokes,{result.displacement,{}});
  }
  if(effect_paint_json(result)!=expected)fail();destination=std::move(result);
}
}
