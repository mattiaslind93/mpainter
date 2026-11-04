#include "TextureExtent.hh"
#include "ResourceAvailability.hh"
#include "SkewArchive.hh"
#include "SkewJSON.hh"
#include <QDataStream>
#include <QIODevice>
namespace bts {namespace {

[[noreturn]] void fail(){throw std::invalid_argument("Invalid packed skew correction data");}
void configure(QDataStream &stream){stream.setVersion(QDataStream::Qt_6_8);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);}
}
QByteArray write_skew_archive(const TextureDocument &document){
 QByteArray raw;QDataStream stream(&raw,QIODevice::WriteOnly);configure(stream);stream<<quint32(0x4254534b)<<quint32(1)<<quint32(document.texture_sets().size());std::uint64_t decoded=0;
 for(const auto &set:document.texture_sets()){
  const auto &field=set.skew;if(!field.loaded)fail();const auto metadata=skew_field_json(field);read_skew_field_json(metadata,set.tiles);stream<<set.id<<qint32(field.resolution)<<quint32(metadata["tiles"].toArray().size());
  for(const auto &[number,pixels]:field.pixels.tiles()){
   for(int c:{0,1,3,4})if(pixels.page_count(c))fail();if(!pixels.page_count(2))continue;if(!set.tiles.contains(number)||pixels.resolution()!=field.resolution)fail();const auto pages=pixels.page_count(2);decoded+=pages*64*64*16;stream<<qint32(number)<<quint32(pages);
   pixels.visit_pages([&](int c,std::uint32_t key,std::span<const PaintPixels::Pixel> values){if(c!=2)fail();stream<<quint32(key);for(const auto &p:values){if(p[1]!=0||p[2]!=0||p[0]<0||p[0]>p[3])fail();for(float v:p)stream<<v;}});
  }
 }
 if(stream.status()!=QDataStream::Ok)fail();return raw;
}
void read_skew_archive(TextureDocument &document,const QByteArray &packed){
 if(packed.size()<12)fail();QDataStream stream(packed);configure(stream);quint32 magic,version,count;stream>>magic>>version>>count;if(magic!=0x4254534b||version!=1||count!=document.texture_sets().size())fail();auto candidate=document;std::set<QUuid> seen;std::uint64_t decoded=0;
 for(quint32 i=0;i<count;++i){
  QUuid id;qint32 resolution;quint32 tiles;stream>>id>>resolution>>tiles;const auto found=std::find_if(candidate.texture_sets().begin(),candidate.texture_sets().end(),[&](const auto &s){return s.id==id;});if(found==candidate.texture_sets().end()||!seen.insert(id).second)fail();auto &set=candidate.set(found->material_session_uid);auto &field=set.skew;const auto expected=skew_field_json(field);
  if(resolution!=field.resolution||tiles!=quint32(expected["tiles"].toArray().size()))fail();field.pixels={};std::set<int> tile_ids;
  for(quint32 t=0;t<tiles;++t){qint32 number;quint32 pages;stream>>number>>pages;if(!set.tiles.contains(number)||!tile_ids.insert(number).second||!pages||std::uint64_t(pages)>sparse_texture_page_count(resolution))fail();decoded+=std::uint64_t(pages)*64*64*16;
   for(quint32 p=0;p<pages;++p){quint32 key;stream>>key;std::array<PaintPixels::Pixel,64*64> values;for(auto &pixel:values){for(float &v:pixel)stream>>v;if(pixel[1]!=0||pixel[2]!=0||pixel[0]<0||pixel[0]>pixel[3])fail();}if(stream.status()!=QDataStream::Ok)fail();field.pixels.assign_page(resolution,2,key,values,number);}
  }
  field.loaded=true;field.pending_tiles.clear();if(skew_field_json(field)!=expected)fail();
 }
 if(!stream.atEnd()||stream.status()!=QDataStream::Ok)fail();document=std::move(candidate);
}
}
