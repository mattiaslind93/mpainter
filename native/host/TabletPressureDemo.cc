#include "ViewportDemoCapture.hh"
#include "TabletPressureDemo.hh"
#include "SyntheticInputGuard.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include "ViewportWidget.hh"
#include <QScreen>
#include <QElapsedTimer>
#include <memory>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *w,*v;SyntheticInputGuard input_guard;QString dir,mesh;int resolution,stage=0,run=0,resume_frame=0;QTimer timer;QJsonObject before;QJsonArray runs;std::function<QJsonObject()> state;
 std::unique_ptr<QPointingDevice> pen;
 QPointingDevice foreign_pen{"BTS unrelated pen",0x4254ff,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1};
 bool xi2=qEnvironmentVariableIsSet("BTS_TABLET_XI2");QElapsedTimer external_clock;
 void external_ready(){auto a=v->mapToGlobal(point(0).toPoint()),b=v->mapToGlobal(point(40).toPoint());QRect screens;for(auto *screen:QGuiApplication::screens())screens|=screen->geometry();QFile f(dir+QString("/xi2-ready-%1.json").arg(run));if(!f.open(QIODevice::WriteOnly))qFatal("XI2 ready file failed");f.write(QJsonDocument(QJsonObject{{"start",QJsonArray{a.x(),a.y()}},{"end",QJsonArray{b.x(),b.y()}},{"screen",QJsonArray{screens.x(),screens.y(),screens.width(),screens.height()}},{"pid",QCoreApplication::applicationPid()}}).toJson());f.close();external_clock.start();}
 QPointF point(int i)const{return {v->width()*.32+i*4.,v->height()*.5};}
 bool oracle()const{return run%2;}
 bool mouse()const{return run>=8;}
 void action(const char *name){auto *a=w->findChild<QAction *>(name);if(!a)qFatal("Pressure action missing: %s",name);a->trigger();}
 void mouse_event(QEvent::Type type,int i,const QPointingDevice *device=nullptr){const auto p=point(i);QMouseEvent *e=new QMouseEvent(type,p,v->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier,device?device:QPointingDevice::primaryPointingDevice());input_guard.own(e);QCoreApplication::sendEvent(v,e);delete e;}
 void event(QEvent::Type type,int i){
  if(mouse()){mouse_event(type==QEvent::TabletPress?QEvent::MouseButtonPress:type==QEvent::TabletRelease?QEvent::MouseButtonRelease:QEvent::MouseMove,i);if(oracle())action("flushPointerInput");return;}
  const bool press=type==QEvent::TabletPress,release=type==QEvent::TabletRelease;
  // Alternate runs omit the button bit on contact moves, reproducing V.
  const bool missing_buttons=run<4&&!press&&!release;
  const auto p=point(i);auto *e=new QTabletEvent(type,pen.get(),p,v->mapToGlobal(p.toPoint()),release?0:.1+.9*i/40.,i-20,20-i,0,0,0,Qt::NoModifier,press||release?Qt::LeftButton:Qt::NoButton,release||missing_buttons?Qt::NoButton:Qt::LeftButton);e->setTimestamp(100+i*5);input_guard.own(e);
  if(oracle()){QCoreApplication::sendEvent(v,e);delete e;action("flushPointerInput");}
  else QCoreApplication::postEvent(v,e);
 }
 void advance(){switch(stage){
  case 0:w->findChild<QComboBox *>("viewLayoutControl")->setCurrentIndex(1);action("newProject");++stage;break;
  case 1:{auto *d=w->findChild<QDialog *>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));d->findChild<QPushButton *>("createProject")->click();++stage;break;}
  case 2:w->findChild<QToolButton *>("addPaintLayer")->click();w->findChild<QComboBox *>("viewLayoutControl")->setCurrentIndex(1);w->findChild<QDoubleSpinBox *>("brushSize")->setValue(4);w->findChild<QDoubleSpinBox *>("brushSpacing")->setValue(20);w->findChild<QToolButton *>("brushPressureSize")->setChecked(true);w->findChild<QToolButton *>("brushPressureFlow")->setChecked(true);if(!w->findChild<QAction *>("showPenPressure")->isChecked())action("showPenPressure");++stage;break;
  case 3:{if(state()["composite_pending"].toBool())return;before=state();
   // New object/ID for every stroke, including late appearance after app start.
   pen=std::make_unique<QPointingDevice>(QString("BTS late pen %1").arg(run),0x425400+run,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure|QInputDevice::Capability::XTilt|QInputDevice::Capability::YTilt,1,1);
   v->setProperty("tabletTestLinearInput",run<2||run==4||run==5);
   // Known-stylus mouse events cannot create a full-pressure provisional stroke.
   if(xi2){external_ready();stage=8;return;}
   if(!mouse()){mouse_event(QEvent::MouseButtonPress,0,pen.get());if(state()["painting"].toBool())qFatal("Known stylus mouse press started paint");}
   // Separate regression: unknown master-pointer press before TabletPress.
   if(run==6||run==7)mouse_event(QEvent::MouseButtonPress,0);
   event(QEvent::TabletPress,0);
   // A second tool hovering cannot take over pressure/contact ownership.
   {auto *hover=new QTabletEvent(QEvent::TabletMove,&foreign_pen,point(0),v->mapToGlobal(point(0).toPoint()),.8,0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::NoButton);input_guard.own(hover);if(oracle()){QCoreApplication::sendEvent(v,hover);delete hover;}else QCoreApplication::postEvent(v,hover);}
   // A different pen leaving proximity must not cancel the current stroke.
   {auto *leave=new QTabletEvent(QEvent::TabletLeaveProximity,&foreign_pen,point(0),v->mapToGlobal(point(0).toPoint()),0,0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::NoButton);if(oracle()){QCoreApplication::sendEvent(qApp,leave);delete leave;}else QCoreApplication::postEvent(qApp,leave);}
   for(int i=1;i<=40;++i){event(QEvent::TabletMove,i);if(!mouse()){
    // Both synthesized and unsynthesized companion events are delivered while
    // pen contact owns the stroke; neither can overwrite its pressure.
    const auto p=point(i);auto *duplicate=new QMouseEvent(QEvent::MouseMove,p,p,v->mapToGlobal(p.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier,Qt::MouseEventSynthesizedByQt);
    input_guard.own(duplicate);if(oracle()){QCoreApplication::sendEvent(v,duplicate);delete duplicate;mouse_event(QEvent::MouseMove,i);}else{QCoreApplication::postEvent(v,duplicate);auto *master=new QMouseEvent(QEvent::MouseMove,p,v->mapToGlobal(p.toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);input_guard.own(master);QCoreApplication::postEvent(v,master);}
   }}++stage;break;}
  case 8:{if(external_clock.elapsed()>30000)qFatal("XI2 injection timed out");auto snapshot=state();if(snapshot["painting"].toBool()||snapshot["hash"]==before["hash"]||snapshot["composite_pending"].toBool())return;
   action("checkGuiComposite");auto data=state();data["before"]=before;data["input"]="kernel-virtual-pen";runs.append(data);capture_viewport_framebuffer(v).save(dir+QString("/xi2-%1.png").arg(run));
   if(++run<2){stage=3;return;}timer.stop();QFile f(dir+"/tablet-pressure-xi2.json");if(!f.open(QIODevice::WriteOnly))qFatal("XI2 proof failed");f.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"runs",runs},{"physical_tablet_verified",false},{"kernel_virtual_device",true}}).toJson());f.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
  case 4:{auto contact=state();if(!contact["painting"].toBool())qFatal("Contact move canceled pressure stroke");w->setProperty("pressureContact",contact);event(QEvent::TabletRelease,40);++stage;break;}
  case 5:if(state()["painting"].toBool()||state()["composite_pending"].toBool())return;action("checkGuiGpuStack");{auto data=state();data["gpu_composite_checked"]=true;data["before"]=before;data["contact"]=w->property("pressureContact").toJsonObject();data["oracle"]=oracle();data["input"]=mouse()?"mouse":"pen";data["linear"]=run<2||run==4||run==5;data["missing_buttons"]=run<4;data["master_press_first"]=run==6||run==7;runs.append(data);if(run==2)capture_viewport_framebuffer(v).save(dir+"/pressure-overlay.png");}
   if(++run<10){action("undoDocument");stage=3;break;}
   resume_frame=state()["render_frames"].toInt()+2;w->findChild<QComboBox *>("viewLayoutControl")->setCurrentIndex(0);stage=6;break;
  case 6:if(state()["composite_pending"].toBool()||state()["render_frames"].toInt()<resume_frame)return;
   action("checkGuiGpuStack");std::printf("BTS_GPU_HIDDEN_CHANNELS_VERIFIED uv_to_scene=1 native_oracle=1\n");
   {timer.stop();QFile f(dir+"/tablet-pressure.json");if(!f.open(QIODevice::WriteOnly))qFatal("Pressure proof failed");f.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"runs",runs},{"physical_tablet_verified",false},{"ignored_native_input",qint64(input_guard.ignored)}}).toJson());f.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);}break;
 }}
 public:Demo(QWidget *workspace,QWidget *viewport,QString directory,QString source,int size,std::function<QJsonObject()> snapshot):QObject(workspace),w(workspace),v(viewport),input_guard(viewport),dir(directory),mesh(source),resolution(size),state(std::move(snapshot)){input_guard.enabled(!xi2);QDir().mkpath(dir);timer.setInterval(100);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_tablet_pressure_demo(QWidget *w,QWidget *v,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new Demo(w,v,d,m,r,std::move(state));}
}
