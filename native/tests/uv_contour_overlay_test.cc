// SPDX-License-Identifier: GPL-3.0-or-later
#include "UvContourOverlay.hh"
#include <QGuiApplication>
#include <cassert>
#include <iostream>
using namespace bts;
int main(int argc,char **argv){
 QGuiApplication app(argc,argv);UvContourOverlay cache;
 auto source=std::make_shared<const UvContourOverlay::Edges>(UvContourOverlay::Edges{{.1f,.2f,.9f,.8f},{.1f,.8f,.9f,.2f},{1.1f,.2f,1.9f,.8f}});
 for(double dpr:{1.,1.5,2.}){
  const QSizeF size(256,192);const QPointF center(.75,.5);const double extent=1.5;
  const auto &image=cache.image(source,size,dpr,center,extent,{});const auto key=image.cacheKey();const auto builds=cache.builds();
  for(int i=0;i<100;++i)assert(cache.image(source,size,dpr,center,extent,{}).cacheKey()==key);
  assert(cache.builds()==builds);
  QImage direct(image.size(),image.format()),cached(image.size(),image.format());direct.setDevicePixelRatio(dpr);cached.setDevicePixelRatio(dpr);direct.fill(QColor(130,90,55));cached.fill(QColor(130,90,55));
  {QPainter painter(&direct);UvContourOverlay::draw(painter,{QPointF{},size},*source,center,extent,{});}
  {QPainter painter(&cached);painter.drawImage(QPointF{},image);}
  int ink=0;for(int y=0;y<direct.height();++y)for(int x=0;x<direct.width();++x){const auto a=direct.pixelColor(x,y),b=cached.pixelColor(x,y);for(auto values:{std::pair(a.red(),b.red()),std::pair(a.green(),b.green()),std::pair(a.blue(),b.blue())})assert(std::abs(values.first-values.second)<=2);ink+=a!=QColor(130,90,55);}
  assert(ink>100);
  const auto nearby_alpha=[&](const QImage &value){const int x=qRound((size.width()*.5+(1.5-center.x())*size.height()/extent)*dpr),y=qRound(size.height()*.5*dpr);int alpha=0;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)alpha=std::max(alpha,value.pixelColor(x+dx,y+dy).alpha());return alpha;};
  assert(nearby_alpha(image)>0);
  const auto before=cache.builds();const auto &hidden=cache.image(source,size,dpr,center,extent,{1002});assert(cache.builds()==before+1);assert(nearby_alpha(hidden)==0);
  cache.image(source,size,dpr,center+QPointF(.1,0),extent,{1002});assert(cache.builds()==before+2);
  cache.image(source,size,dpr,center+QPointF(.1,0),extent*.5,{1002});assert(cache.builds()==before+3);
  cache.image(source,size+QSizeF(1,0),dpr,center,extent,{});assert(cache.builds()==before+4);
 }
 auto changed=std::make_shared<const UvContourOverlay::Edges>(UvContourOverlay::Edges{{.2f,.1f,.8f,.9f}});const auto before=cache.builds();cache.image(changed,{256,192},1,{.5,.5},1,{});assert(cache.builds()==before+1);
 std::cout<<"UV contour cache: repeated edits retain image, camera/geometry/tile/size/DPR invalidation, direct/cached pixels within2RGB levels\n";
}
