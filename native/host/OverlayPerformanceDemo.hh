#pragma once
#include "GpuPerformance.hh"
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
namespace bts {
class OverlayPerformanceDemo final:public QObject {
 QWidget *w,*v;QString out;std::function<QJsonObject()> state;QTimer timer,ping;QElapsedTimer heartbeat;QJsonArray captures,pings;int phase=-1,step=0,ticks=0;bool paint_added=false;QPointF origin;QJsonObject transfers;
 QStringList names{"scene-fill","scene-bucket","uv-paint","uv-bucket"};
 bool scene()const{return phase<2||phase==4;}
 bool wire()const{return phase==1||phase==3||phase==4||phase==5;}
 template<class T>T *get(const char *name){auto *value=w->findChild<T *>(name);if(!value)qFatal("Overlay profile missing %s",name);return value;}
 void action(const char *name){get<QAction>(name)->trigger();}
 void pointer(QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,point,v->mapToGlobal(point),button,buttons,Qt::AltModifier);QCoreApplication::sendEvent(v,&event);}
 void capture(){performance::begin("");QJsonObject data{{"phase",names[phase]},{"transfers_before",transfers},{"transfers_after",gpu_transfer_snapshot()}};for(const char *key:{"overlayTriangles","polygonBuilds0","polygonBuilds1","polygonLines0","polygonLines1","uvContourEdges","uvContourBuilds","native_wire_vbos","native_wire_cache_changes","uv_vbo_builds","uv_vbo_upload_bytes","triangle_vbo_builds","triangle_upload_bytes","triangle_draws"})data[key]=QJsonValue::fromVariant(v->property(key));captures.append(data);capture_viewport_framebuffer(v).save(out+"/"+names[phase]+".png");}
 void begin(){++phase;step=0;if(phase==names.size()){timer.stop();ping.stop();QFile file(out+"/overlay-performance.json");if(!file.open(QIODevice::WriteOnly))qFatal("Overlay profile output failed");file.write(QJsonDocument(QJsonObject{{"captures",captures},{"pings",pings},{"phases",performance::snapshot()},{"pid",QCoreApplication::applicationPid()}}).toJson());QTimer::singleShot(100,qApp,&QCoreApplication::quit);return;}
  performance::begin("av-warmup");auto *layout=get<QComboBox>("viewLayoutControl");const int desired=scene()?0:1;if(layout->currentIndex()==desired)layout->setCurrentIndex(desired==0?2:0);layout->setCurrentIndex(desired);
  if(wire()){if(!paint_added){get<QToolButton>("addPaintLayer")->click();paint_added=true;}action("selectPolygonFill");}else action("selectPaintTool");
  if(phase>=4)get<QToolButton>("polygonMode0_0")->click();
  origin=v->rect().center();pointer(QEvent::MouseButtonPress,origin,scene()?Qt::LeftButton:Qt::MiddleButton,scene()?Qt::LeftButton:Qt::MiddleButton);
 }
 void advance(){if(++ticks>3000)qFatal("Overlay profile timeout");const auto s=state();if(s["composite_pending"].toBool()||s["property_pending"].toBool())return;if(phase<0){if(s["render_frames"].toInt()<120)return;begin();return;}if(phase>=names.size())return;
  if(step==12){transfers=gpu_transfer_snapshot();performance::begin("av-"+names[phase].toStdString());}
  pointer(QEvent::MouseMove,origin+QPointF(step*2,12*std::sin(step*.13)),Qt::NoButton,scene()?Qt::LeftButton:Qt::MiddleButton);v->update();
  if(++step==76){pointer(QEvent::MouseButtonRelease,origin+QPointF(step*2,0),scene()?Qt::LeftButton:Qt::MiddleButton,Qt::NoButton);capture();begin();}
 }
 public:OverlayPerformanceDemo(QWidget *window,QWidget *viewport,QString directory,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),out(directory),state(std::move(snapshot)){
  if(qEnvironmentVariableIsSet("BTS_OVERLAY_TRIANGLE_PROFILE"))names.append({"scene-triangles","uv-triangles"});
  w->setProperty("guiProbeOnly",true);qputenv("BTS_TEST_CONTINUOUS_FRAMES","1");timer.setInterval(20);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();heartbeat.start();ping.setInterval(10);connect(&ping,&QTimer::timeout,this,[this]{const double elapsed=heartbeat.nsecsElapsed()/1e6;heartbeat.restart();if(phase>=0&&phase<names.size()&&step>=12)pings.append(QJsonObject{{"phase",names[phase]},{"ms",elapsed}});});ping.start();
 }
};
}
