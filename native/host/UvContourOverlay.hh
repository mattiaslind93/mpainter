#include "Theme.hh"
#pragma once
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QSizeF>
#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>
namespace bts {
// UI decoration, independent of painted channel pixels. Retain the rasterized
// contours while geometry and the UV camera are unchanged. Qt then reuses one
// image texture instead of stroking thousands of GL paths after every edit.
class UvContourOverlay {
 public:
 using Edges=std::vector<std::array<float,4>>;
 using Source=std::shared_ptr<const Edges>;
 static void draw(QPainter &painter,const QRectF &rect,const Edges &edges,QPointF center,double extent,const std::set<int> &hidden){
  const double pixels=rect.height()/extent;
  const auto point=[&](float u,float v){return QPointF(rect.center().x()+(u-center.x())*pixels,rect.center().y()-(v-center.y())*pixels);};
  QVector<QLineF> lines;lines.reserve(qsizetype(edges.size()));
  for(const auto &e:edges)lines.append(QLineF(point(e[0],e[1]),point(e[2],e[3])));
  painter.save();painter.setClipRect(rect,Qt::IntersectClip);
  if(!hidden.empty()){QPainterPath clip;clip.addRect(rect);for(int tile:hidden){const int number=tile-1001;QPainterPath hole;hole.addRect(QRectF(point(number%10,number/10),point(number%10+1,number/10+1)).normalized());clip=clip.subtracted(hole);}painter.setClipPath(clip,Qt::IntersectClip);}
  painter.setPen(QPen(bts::theme::alpha(bts::theme::Surface, 200),1));painter.drawLines(lines);painter.restore();
 }
 const QImage &image(const Source &source,QSizeF size,double dpr,QPointF center,double extent,const std::set<int> &hidden){
  if(source_==source&&size_==size&&dpr_==dpr&&center_==center&&extent_==extent&&hidden_==hidden&&!image_.isNull())return image_;
  source_=source;size_=size;dpr_=dpr;center_=center;extent_=extent;hidden_=hidden;
  const QSize physical(qRound(size.width()*dpr),qRound(size.height()*dpr));
  image_=QImage(physical,QImage::Format_ARGB32_Premultiplied);image_.setDevicePixelRatio(dpr);image_.fill(Qt::transparent);
  if(source&&!image_.isNull()){QPainter painter(&image_);draw(painter,QRectF(QPointF{},size),*source,center,extent,hidden);}
  ++builds_;return image_;
 }
 std::uint64_t builds()const{return builds_;}
 private:
 Source source_;QSizeF size_;double dpr_=0,extent_=0;QPointF center_;std::set<int> hidden_;QImage image_;std::uint64_t builds_=0;
};
}
