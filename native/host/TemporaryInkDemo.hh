#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
namespace bts {
class TemporaryInkDemo final:public QObject{
 QWidget *w,*v;QString output,mesh;QTimer timer,dialogs;int stage=0,ticks=0,frame=0;bool busy=false;QJsonArray captures;std::function<QJsonObject()> state;
 QPointingDevice pen{"BTS temporary ink pen",0x425449,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1};
 template<class T>T *get(const char *name){auto *x=w->findChild<T *>(name);if(!x)qFatal("Missing temporary ink control %s",name);return x;}
 void action(const char *name){get<QAction>(name)->trigger();}
 void capture(const char *name){w->setProperty("temporarySample",true);auto d=state();w->setProperty("temporarySample",false);d["stage"]=name;captures.append(d);capture_demo_window(w,v).save(output+"/"+name+".png");}
 void key(bool on){QKeyEvent e(on?QEvent::KeyPress:QEvent::KeyRelease,Qt::Key_Control,on?Qt::ControlModifier:Qt::NoModifier);QCoreApplication::sendEvent(v,&e);}
 void stroke(bool tablet){const auto points=state()["points"].toArray();const QPointF a(points[0].toArray()[0].toDouble(),points[0].toArray()[1].toDouble()),b(points[3].toArray()[0].toDouble(),points[3].toArray()[1].toDouble());for(int i=0;i<=60;++i){if(i==20)key(true);if(i==40)key(false);const auto mods=i>=20&&i<40?Qt::ControlModifier:Qt::NoModifier;const auto p=a+(b-a)*(i/60.);if(tablet){QTabletEvent e(i==0?QEvent::TabletPress:QEvent::TabletMove,&pen,p,v->mapToGlobal(p.toPoint()),.2+.8*i/60.,0,0,0,0,0,mods,i==0?Qt::LeftButton:Qt::NoButton,Qt::LeftButton);QCoreApplication::sendEvent(v,&e);}else{QMouseEvent e(i==0?QEvent::MouseButtonPress:QEvent::MouseMove,p,v->mapToGlobal(p.toPoint()),i==0?Qt::LeftButton:Qt::NoButton,Qt::LeftButton,mods);QCoreApplication::sendEvent(v,&e);}}if(tablet){QTabletEvent e(QEvent::TabletRelease,&pen,b,v->mapToGlobal(b.toPoint()),0,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::NoButton);QCoreApplication::sendEvent(v,&e);}else{QMouseEvent e(QEvent::MouseButtonRelease,b,v->mapToGlobal(b.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(v,&e);}}
 void advance(){if(busy)return;if(++ticks>1600)qFatal("Temporary ink timeout stage %d",stage);const auto d=state();if(stage>1&&(d["composite_pending"].toBool()||d["render_frames"].toInt()<frame))return;switch(stage++){
 case 0:action("newProject");break;
 case 1:get<QComboBox>("projectTemplate")->setCurrentIndex(0);get<QLineEdit>("meshPath")->setText(mesh);get<QComboBox>("documentResolution")->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));busy=true;get<QPushButton>("createProject")->click();busy=false;break;
 case 2:action("guiTemporaryInkPrepare");break;
 case 3:capture("initial");stroke(false);break;
 case 4:capture("color");action("undoDocument");break;
 case 5:capture("undo");action("redoDocument");break;
 case 6:capture("redo");action("addBlackMask");break;
 case 7:capture("black-default");stroke(false);break;
 case 8:capture("black-mask");action("addWhiteMask");break;
 case 9:capture("white-default");if(!get<QToolButton>("brushPressureSize")->isChecked())get<QToolButton>("brushPressureSize")->click();stroke(true);break;
 case 10:capture("white-mask-tablet");timer.stop();{QFile f(output+"/temporary-ink.json");if(!f.open(QIODevice::WriteOnly))qFatal("Ink proof write failed");f.write(QJsonDocument(QJsonObject{{"captures",captures}}).toJson());}QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
 }frame=state()["render_frames"].toInt()+3;}
 public:TemporaryInkDemo(QWidget *window,QWidget *viewport,QString directory,QString source,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),output(directory),mesh(source),state(std::move(snapshot)){connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start(100);connect(&dialogs,&QTimer::timeout,this,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(widget);box&&box->isVisible()){if(box->icon()==QMessageBox::Critical)qFatal("Ink GUI error: %s",qPrintable(box->text()));if(auto *button=box->button(QMessageBox::Discard))button->click();}});dialogs.start(40);}
};
}
