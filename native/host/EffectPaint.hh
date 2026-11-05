#pragma once
#include "TilePaintPixels.hh"
#include "TextureExtent.hh"
#include "StrokeMetadata.hh"
#include "ChannelPrecision.hh"
#include "LayerBlend.hh"
#include <QJsonArray>
#include <QJsonObject>
namespace bts {
// Sparse UV authoring owned by an effect, with the same COW page semantics as layers.
struct EffectPaint {
  TilePaintPixels pixels,ao,displacement;
  std::shared_ptr<const StrokeHistory> strokes,displacement_strokes;
  std::array<std::shared_ptr<const StrokeHistory>,8> user_strokes;
  std::array<TilePaintPixels,8> users;
  int resolution=0;
  bool loaded=true;
  QJsonObject declared;
  TilePaintPixels &plane(int i){return i==0?pixels:i==1?ao:i==10?displacement:users.at(i-2);}
  const TilePaintPixels &plane(int i)const{return i==0?pixels:i==1?ao:i==10?displacement:users.at(i-2);}
};
inline QJsonObject effect_paint_json(const EffectPaint &paint){
  if(!paint.loaded)return paint.declared;QJsonArray planes;
  for(int i=0;i<11;++i){QJsonArray tiles;for(const auto &[number,pixels]:paint.plane(i).tiles()){QJsonArray pages,hashes;for(int c=0;c<5;++c){pages.append(qint64(pixels.page_count(c)));hashes.append(QString::number(pixels.hash(c),16));}tiles.append(QJsonObject{{"number",number},{"pages",pages},{"hashes",hashes}});}planes.append(tiles);}
  QJsonObject result{{"resolution",paint.resolution},{"planes",planes}};const auto primary=stroke_history_json(paint.strokes,{paint.pixels,paint.ao});QJsonArray users;bool retained=!primary.isNull();for(int slot=0;slot<8;++slot){const auto history=stroke_history_json(paint.user_strokes[slot],{paint.users[slot],{}});retained|=!history.isNull();users.append(history);}retained|=!stroke_history_json(paint.displacement_strokes,{paint.displacement,{}}).isNull();if(retained){result["displacement_strokes"]=stroke_history_json(paint.displacement_strokes,{paint.displacement,{}});result["strokes"]=primary;result["user_strokes"]=users;}return result;
}
inline EffectPaint read_effect_paint(const QJsonValue &value){
  const auto fail=[](){throw std::invalid_argument("Invalid effect paint metadata");};if(!value.isObject())fail();const auto o=value.toObject();const auto size=o["resolution"];if(!size.isDouble()||size.toDouble()!=size.toInt(-1))fail();require_texture_resolution_limit(size.toDouble(),"Effect paint");const int resolution=size.toInt();if(resolution&&!valid_sparse_texture_extent(resolution,128,true))fail();if(!o["planes"].isArray()||(o["planes"].toArray().size()!=10&&o["planes"].toArray().size()!=11))fail();
  for(int i=0;i<o["planes"].toArray().size();++i){const auto plane=o["planes"].toArray()[i];if(!plane.isArray()||plane.toArray().size()>1000)fail();std::set<int> seen;for(const auto &entry:plane.toArray()){if(!entry.isObject())fail();const auto tile=entry.toObject();const auto number=tile["number"];if(!number.isDouble()||number.toDouble()!=number.toInt()||number.toInt()<1001||number.toInt()>2000||!seen.insert(number.toInt()).second||!resolution)fail();const auto pages=tile["pages"].toArray(),hashes=tile["hashes"].toArray();if(pages.size()!=5||hashes.size()!=5)fail();std::uint64_t total=0;for(int c=0;c<5;++c){const auto n=pages[c];const auto count=n.toDouble(-1);if(!n.isDouble()||!std::isfinite(count)||count<0||count!=std::floor(count)||count>double(sparse_texture_page_count(resolution))||(i>0&&c!=2&&count!=0))fail();total=resource_sum(total,std::uint64_t(count));bool valid=false;hashes[c].toString().toULongLong(&valid,16);if(!hashes[c].isString()||hashes[c].toString().isEmpty()||hashes[c].toString().size()>16||!valid)fail();}if(!total)fail();}}
  EffectPaint result;if(o.contains("strokes")){result.strokes=read_stroke_history_json(o["strokes"]);if(!o["user_strokes"].isArray()||o["user_strokes"].toArray().size()!=8)fail();const auto users=o["user_strokes"].toArray();for(int slot=0;slot<8;++slot)result.user_strokes[slot]=read_stroke_history_json(users[slot]);}if(o.contains("displacement_strokes")){if(!o.contains("strokes"))fail();result.displacement_strokes=read_stroke_history_json(o["displacement_strokes"]);}result.resolution=resolution;result.loaded=false;result.declared=o;return result;
}
inline std::array<float,3> blend_effect_paint(const EffectPaint &paint,int channel,std::array<float,3> value,float &coverage,int x,int y,int tile,int width,int height,ChannelPrecision precision,float opacity=1,LayerBlend mode=LayerBlend::Normal){
  if(x<0||y<0||mode==LayerBlend::Disabled||opacity<=0)return value;const auto pixel=(channel<5?paint.pixels:channel==5?paint.ao:channel==14?paint.displacement:paint.users.at(channel-6)).sample_resampled(channel<5?channel:2,x,y,width,tile,height);const float alpha=pixel[3]*opacity;if(alpha<=0&&mode!=LayerBlend::Replace)return value;std::array<float,3> source{};if(pixel[3]>0)for(int c=0;c<3;++c)source[c]=(channel==0||channel==3?pixel[c]:pixel[0])/pixel[3];if(channel==3)for(float &v:source)v=v*.5f+.5f;source=precision_rgb(source,precision);
  if(mode==LayerBlend::Replace){const float result=alpha+coverage*(1-opacity);for(int c=0;c<3;++c)value[c]=result>0?(source[c]*alpha+value[c]*coverage*(1-opacity))/result:0;coverage=result;return value;}
  auto blended=channel==0?blend_rgb(mode,source,value):std::array<float,3>{};if(channel==3&&normal_blend_mode(mode)){const auto normal=blend_normal(mode,{source[0]*2-1,source[1]*2-1,source[2]*2-1},{value[0]*2-1,value[1]*2-1,value[2]*2-1});for(int c=0;c<3;++c)blended[c]=normal[c]*.5f+.5f;}else if(channel!=0)for(int c=0;c<3;++c)blended[c]=blend_value(mode,source[c],value[c]);const float result=alpha+coverage*(1-alpha);for(int c=0;c<3;++c)value[c]=((1-alpha)*value[c]*coverage+alpha*((1-coverage)*source[c]+coverage*blended[c]))/result;coverage=result;return value;
}
}
