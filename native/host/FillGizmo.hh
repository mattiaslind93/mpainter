#pragma once
#include "FillProjection.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QRectF>
#include <QVector3D>
#include <QVector4D>
#include <functional>
#include <optional>

namespace bts {
struct FillGizmoHandle {
 int axis=0;QPointF start,end;QPolygonF path;QVector3D direction;
 double world_length=1,rotation_sign=1;
};
struct FillGizmoGeometry {
 int mode=0,tool=0;QRectF pane;QPointF center;double extent=1;
 QMatrix4x4 matrix;QPolygonF uv_frame;std::vector<FillGizmoHandle> handles;
};
// The viewport supplies its real camera/lens/UV projection. The same returned
// paths are drawn, hit-tested and exposed read-only to the private UI actor.
inline FillGizmoGeometry fill_gizmo_geometry(const FillProjection &p,
 const std::array<float,3> &minimum,const std::array<float,3> &maximum,QRectF pane,
 const std::function<QPointF(QVector3D)> &scene,const std::function<QPointF(QPointF)> &uv){
 FillGizmoGeometry g;g.mode=p.mode;g.tool=p.gizmo;g.pane=pane;
 if(!p.manipulator||p.mode==6||pane.isEmpty()||p.mode==5)return g;
 const auto ring=[&](int axis,const std::function<QPointF(double)> &point){FillGizmoHandle h;h.axis=axis;h.start=g.center;for(int step=0;step<=64;++step)h.path<<point(step*2*std::numbers::pi/64);h.end=h.path[8];double area=0;for(int i=1;i<h.path.size();++i)area+=h.path[i-1].x()*h.path[i].y()-h.path[i].x()*h.path[i-1].y();h.rotation_sign=area>=0?1:-1;g.handles.push_back(std::move(h));};
 if(p.mode==0){
  const auto point=[&](double x,double y){const auto q=fill_uv_frame(p,x,y);return uv({q[0],q[1]});};
  const double arm=.35*p.gizmo_size/.4;g.center=point(.5,.5);for(const auto q:{QPointF(0,0),QPointF(1,0),QPointF(1,1),QPointF(0,1)})g.uv_frame<<point(q.x(),q.y());
  if(p.gizmo==1){const double radius=std::max(12.,QLineF(g.center,point(.5+arm,.5)).length());ring(2,[&](double a){return g.center+QPointF(std::cos(a),std::sin(a))*radius;});}
  else for(int axis=0;axis<2;++axis){const auto q=fill_uv_frame(p,axis==0?.5+arm:.5,axis==1?.5+arm:.5),c=fill_uv_frame(p,.5,.5);QVector3D direction(q[0]-c[0],q[1]-c[1],0);FillGizmoHandle h;h.axis=axis;h.start=g.center;h.end=uv({q[0],q[1]});h.world_length=direction.length();h.direction=direction.normalized();h.path<<h.start<<h.end;g.handles.push_back(std::move(h));}
  return g;
 }
 g.extent=0;for(int c=0;c<3;++c)g.extent=std::max(g.extent,double(maximum[c]-minimum[c]));if(g.extent<1e-6)g.extent=1;
 if(p.mode>=2)g.matrix.translate((minimum[0]+maximum[0])*.5+p.offset3[0]*g.extent,(minimum[1]+maximum[1])*.5+p.offset3[1]*g.extent,(minimum[2]+maximum[2])*.5+p.offset3[2]*g.extent);else g.matrix.translate(p.offset3[0],p.offset3[1],p.offset3[2]);
 g.matrix.rotate(p.rotation3[2],0,0,1);g.matrix.rotate(p.rotation3[1],0,1,0);g.matrix.rotate(p.rotation3[0],1,0,0);const auto size=fill_physical_size(p);
 g.matrix.scale(p.scale3[0]*(p.physical?size[0]/(p.cm_per_native*g.extent):1),p.scale3[1]*(p.physical?size[p.mode>=2?2:1]/(p.cm_per_native*g.extent):1),p.scale3[2]*(p.physical?size[p.mode>=2?1:2]/(p.cm_per_native*g.extent):1));
 const auto point=[&](QVector3D v){return scene((g.matrix*QVector4D(v,1)).toVector3DAffine());};g.center=point({});
 for(int axis=0;axis<3;++axis){
  if(p.gizmo==1)ring(axis,[&](double a){QVector3D v;v[(axis+1)%3]=std::cos(a)*g.extent*p.gizmo_size;v[(axis+2)%3]=std::sin(a)*g.extent*p.gizmo_size;return point(v);});
  else{QVector3D v;v[axis]=g.extent*p.gizmo_size;const auto direction=g.matrix.mapVector(v);FillGizmoHandle h;h.axis=axis;h.start=g.center;h.end=point(v);h.world_length=direction.length();h.direction=direction.normalized();h.path<<h.start<<h.end;g.handles.push_back(std::move(h));}
 }return g;
}
inline double fill_gizmo_segment_distance(QPointF p,QPointF a,QPointF b){const auto d=b-a;const double square=d.x()*d.x()+d.y()*d.y(),t=square<1e-12?0:std::clamp(((p-a).x()*d.x()+(p-a).y()*d.y())/square,0.,1.);return QLineF(p,a+t*d).length();}
inline const FillGizmoHandle *fill_gizmo_hit(const FillGizmoGeometry &g,QPointF p){
 if(!g.pane.contains(p))return nullptr;const FillGizmoHandle *best=nullptr;double distance=7;
 for(const auto &h:g.handles){if(QLineF(h.start,h.end).length()<8&&g.tool!=1)continue;for(int i=1;i<h.path.size();++i){const auto a=h.path[i-1],b=h.path[i];const double d=fill_gizmo_segment_distance(p,a,b);if(d<distance&&(g.tool==1||QLineF(p,h.start).length()>10)){distance=d;best=&h;}}}return best;
}
inline void paint_fill_gizmo(QPainter &painter,const FillGizmoGeometry &g){
 if(!g.uv_frame.isEmpty()){painter.setPen(QPen(QColor(200,200,200,200),1,Qt::DotLine));painter.setBrush(Qt::NoBrush);painter.drawPolygon(g.uv_frame);}
 const QColor colors[3]={QColor("#dc6060"),QColor("#70cc80"),QColor("#649ce4")};painter.setBrush(Qt::NoBrush);
 for(const auto &h:g.handles){painter.setPen(QPen(colors[h.axis],2));painter.drawPolyline(h.path);if(g.tool==2)painter.drawRect(QRectF(h.end-QPointF(3,3),QSizeF(6,6)));else if(g.tool==0){const auto delta=h.end-h.start;const double length=QLineF(h.start,h.end).length();if(length>1){const auto d=delta/length;const QPointF side(-d.y(),d.x());painter.drawLine(h.end,h.end-d*8+side*4);painter.drawLine(h.end,h.end-d*8-side*4);}}}
}
class FillGizmo {
 struct Drag {FillProjection before;FillGizmoGeometry geometry;FillGizmoHandle handle;QPointF press;};
 std::optional<Drag> drag_;QString target_;
 public:
 std::function<void()> started;std::function<void(const FillProjection &)> changed;std::function<void(bool)> finished;std::function<void(const QString &)> error;
 bool dragging()const{return bool(drag_);}
 void synchronize(const QString &target){if(target_!=target){finish(true);target_=target;}}
 void finish(bool cancel){if(!drag_)return;drag_.reset();if(finished)finished(cancel);}
 bool press(QMouseEvent *event,const FillProjection &p,const FillGizmoGeometry &g){if(event->button()!=Qt::LeftButton||event->modifiers()!=Qt::NoModifier||!changed)return false;const auto *h=fill_gizmo_hit(g,event->position());if(!h)return false;drag_=Drag{p,g,*h,event->position()};if(started)started();return true;}
 bool move(QMouseEvent *event){
 if(!drag_)return false;try{if(!event->buttons().testFlag(Qt::LeftButton)){finish(true);return true;}
 const auto &d=*drag_;auto p=d.before;const auto screen=d.handle.end-d.handle.start,delta=event->position()-d.press;const double square=screen.x()*screen.x()+screen.y()*screen.y(),fraction=square<1e-12?0:(delta.x()*screen.x()+delta.y()*screen.y())/square;
 if(d.geometry.tool==0){const auto distance=fraction*d.handle.world_length;if(p.mode==0){for(int c=0;c<2;++c)p.offset[c]=std::clamp(p.offset[c]+d.handle.direction[c]*distance,-1000.,1000.);}else for(int c=0;c<3;++c)p.offset3[c]=std::clamp(p.offset3[c]+d.handle.direction[c]*distance/(p.mode>=2?d.geometry.extent:1),-1e6,1e6);}
 else if(d.geometry.tool==1){const auto a=d.press-d.geometry.center,b=event->position()-d.geometry.center;const double angle=std::remainder((std::atan2(b.y(),b.x())-std::atan2(a.y(),a.x()))*180/std::numbers::pi,360.);if(p.mode==0)p.rotation=std::remainder(p.rotation-angle,360.);else p.rotation3[d.handle.axis]=std::remainder(p.rotation3[d.handle.axis]+angle*d.handle.rotation_sign,360.);}
 else{const double factor=std::exp(std::clamp(fraction,-10.,10.)*(p.mode==0?-1 : 1));if(p.mode==0)for(int c=0;c<2;++c){if(p.linked||c==d.handle.axis)p.scale[c]=std::copysign(std::clamp(std::abs(p.scale[c]*factor),.001,1000.),p.scale[c]);}else for(int c=0;c<3;++c)if(p.linked3||c==d.handle.axis)p.scale3[c]=std::copysign(std::clamp(std::abs(p.scale3[c]*factor),.001,1000.),p.scale3[c]);}
 validate_fill_projection(p);if(changed)changed(p);return true;}catch(const std::exception &exception){if(error)error(QString::fromUtf8(exception.what()));finish(true);return true;}
 }
 bool release(QMouseEvent *event){if(!drag_||event->button()!=Qt::LeftButton)return false;finish(false);return true;}
 QJsonObject diagnostic(const FillGizmoGeometry &g)const{
 // Pick an actually visible/hittable handle from the current drawn paths.
 for(const auto &h:g.handles){QPointF from,to;if(g.tool==1){if(h.path.size()<17)continue;from=h.path[8];to=h.path[12];}else{from=h.start+(h.end-h.start)*.8;to=from+(h.end-h.start)*.2;}const auto *hit=fill_gizmo_hit(g,from);if(hit&&hit->axis==h.axis&&g.pane.adjusted(8,8,-8,-8).contains(to))return {{"domain",g.mode==0?"uv":g.mode==5?"warp":"spatial"},{"tool",g.tool},{"axis",h.axis},{"from",QJsonArray{from.x(),from.y()}},{"to",QJsonArray{to.x(),to.y()}}};}
 return {};
 }
};
}
