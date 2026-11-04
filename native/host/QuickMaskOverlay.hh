#pragma once
#include "QuickMask.hh"
#include <memory>
namespace bts {
// Session-only source. No raster copy of the temporary field is made, and this
// source is never put into the persistent document or resource library.
struct QuickMaskOverlay {
 std::shared_ptr<const QuickMaskField> field;
 bool editing=false;
 static constexpr int resolution=256;
 PaintPixels::Pixel texel(int x,int y,int tile)const{
  const float allowed=field?field->sample_uv((tile-1001)%10+(x+.5)/resolution,(tile-1001)/10+(y+.5)/resolution,resolution):1;
  const float checker=editing?.14f:0,gray=((x/8+y/8)%2)?.3f:.12f;
  const float alpha=1-allowed+checker*allowed,rgb=gray*checker*allowed;return {rgb,rgb,rgb,alpha};
 }
 PaintPixels::Pixel sample(int x,int y,int width,int height,int tile)const{
  if(x<0||y<0)return {};
  struct Axis{int first,count;double fraction;bool area;};
  const auto axis=[](int p,int size){if(resolution>=size&&resolution%size==0)return Axis{p*(resolution/size),resolution/size,0,true};const double location=(p+.5)*resolution/size-.5;const int first=int(std::floor(location));return Axis{first,2,location-first,false};};
  const auto u=axis(x,width),v=axis(y,height);std::array<double,4> sum{};
  for(int dy=0;dy<v.count;++dy)for(int dx=0;dx<u.count;++dx){const auto pixel=texel(std::clamp(u.first+dx,0,resolution-1),std::clamp(v.first+dy,0,resolution-1),tile);const double weight=(u.area?1./u.count:dx?u.fraction:1-u.fraction)*(v.area?1./v.count:dy?v.fraction:1-v.fraction);for(int c=0;c<4;++c)sum[c]+=pixel[c]*weight;}
  PaintPixels::Pixel result;for(int c=0;c<4;++c)result[c]=float(sum[c]);return result;
 }
};
}
