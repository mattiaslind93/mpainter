#include "WarpDemo.hh"
#include "WarpIO.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include "ViewportWidget.hh"
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class WarpDemo final:public QObject {
 QWidget *w,*v;QString dir,mesh,image;int resolution,stage=0;QTimer timer;QJsonArray captures;std::function<QJsonObject()> state;
 void action(const char *name){auto *a=w->findChild<QAction *>(name);if(!a)qFatal("Warp action missing: %s",name);a->trigger();}
 void number(const char *name,double value){auto *c=w->findChild<QDoubleSpinBox *>(name);if(!c)qFatal("Warp number missing: %s",name);c->setValue(value);QMetaObject::invokeMethod(c,"editingFinished",Qt::DirectConnection);}
 void combo(const char *name,int value){auto *c=w->findChild<QComboBox *>(name);if(!c)qFatal("Warp combo missing: %s",name);c->setCurrentIndex(value);}
 void button(const char *name){auto *c=w->findChild<QPushButton *>(name);if(!c)qFatal("Warp button missing: %s",name);c->click();}
 void key(int key,Qt::KeyboardModifiers mods={}){QKeyEvent event(QEvent::KeyPress,key,mods);QApplication::sendEvent(v,&event);}
 QPointF point(int id){const auto a=state()["warp_screen"].toArray().at(id).toArray();return {a[0].toDouble(),a[1].toDouble()};}
 QPointF axis(int id){const auto a=state()["warp_axes"].toArray().at(id).toArray();return {a[0].toDouble(),a[1].toDouble()};}
 void mouse(QEvent::Type type,QPointF p,Qt::MouseButton b,Qt::MouseButtons buttons,Qt::KeyboardModifiers mods={}){QMouseEvent event(type,p,v->mapToGlobal(p.toPoint()),b,buttons,mods);QApplication::sendEvent(v,&event);}
 void navigation(Qt::MouseButton button,QPointF delta){const QPointF start(v->width()*.25,v->height()*.5);mouse(QEvent::MouseButtonPress,start,button,button,Qt::AltModifier);mouse(QEvent::MouseMove,start+delta,Qt::NoButton,button,Qt::AltModifier);mouse(QEvent::MouseButtonRelease,start+delta,button,Qt::NoButton,Qt::AltModifier);}
 void click(QPointF p,Qt::KeyboardModifiers mods={}){mouse(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,mods);mouse(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,mods);}
 void drag(QPointF p,QPointF delta,Qt::KeyboardModifiers mods={},bool cancel=false){mouse(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,mods);for(int i=1;i<=8;++i)mouse(QEvent::MouseMove,p+delta*(i/8.),Qt::NoButton,Qt::LeftButton,mods);if(cancel)key(Qt::Key_Escape);mouse(QEvent::MouseButtonRelease,p+delta,Qt::LeftButton,Qt::NoButton,mods);}
 void capture(const char *name){if(qEnvironmentVariableIsSet("BTS_GPU_DENSE_ORACLE"))action("checkGuiGpuStack");auto data=state();data["stage"]=name;std::printf("BTS_WARP_STAGE %s property_pending=%d dragging=%d idle=%d\n",name,data["property_pending"].toBool(),data["property_dragging"].toBool(),data["property_idle_ms"].toInt());data["columns_enabled"]=w->findChild<QSpinBox *>("warpColumns")->isEnabled();data["selected_point"]=w->findChild<QComboBox *>("warpSelectedPoint")->currentData().toInt();captures.append(data);w->grab().save(dir+"/"+name+".png");w->setProperty("guiCapturePath",dir+"/"+name+"-viewport.png");action("guiCaptureViewport");}
 void advance(){if(stage>2){const auto data=state();if(data["composite_pending"].toBool()||data["property_pending"].toBool())return;}switch(stage++){
 case 0:action("newProject");break;
 case 1:{auto *d=w->findChild<QDialog *>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));d->findChild<QPushButton *>("createProject")->click();break;}
 case 2:action("guiPlanarPrepare");w->findChild<QToolButton *>("addFillLayer")->click();break;
 case 3:{timer.stop();QTimer::singleShot(100,this,[this]{auto *picker=w->findChild<QFileDialog *>("fillTextureFileDialog");if(!picker)qFatal("Warp texture picker missing");picker->setDirectory(QFileInfo(image).absolutePath());picker->selectFile(image);if(auto *field=picker->findChild<QLineEdit *>("fileNameEdit"))field->setText(QFileInfo(image).fileName());QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);});combo("fillBaseColorResource",2);timer.start();break;}
 case 4:{const auto view=state()["view"].toObject();navigation(Qt::LeftButton,{view["scene_yaw"].toDouble()/.35,-view["scene_pitch"].toDouble()/.35});navigation(Qt::RightButton,{0,30});combo("fillProjectionMode",5);break;}
 case 5:capture("flat");key(Qt::Key_V,Qt::ShiftModifier);break;
 case 6:capture("vertices");drag(point(4),{38,-24},Qt::ControlModifier);break;
 case 7:capture("deformed");action("undoDocument");break;
 case 8:capture("undo-deform");action("redoDocument");break;
 case 9:capture("redo-deform");drag(point(4),{-30,20},Qt::ControlModifier,true);break;
 case 10:capture("cancelled");click(point(3));click(point(4),Qt::ShiftModifier);click(point(5),Qt::ShiftModifier);break;
 case 11:capture("multi");click(point(3),Qt::ControlModifier);break;
 case 12:capture("removed");w->findChild<QCheckBox *>("warpAutomaticTangents")->setChecked(false);number("warpVector1_2",.35);break;
 case 13:capture("tangents");combo("warpSplitMode",1);button("warpSplitAtCoordinates");break;
 case 14:capture("split");key(Qt::Key_Escape);break;
 case 15:capture("split-cancelled");button("warpReset2");break;
 case 16:capture("reset-selected");action("undoDocument");break;
 case 17:capture("undo-reset");number("warpProjectionDepth",.1);break;
 case 18:capture("short-depth");number("warpProjectionDepth",1);number("fill3d0_0",.1);break;
 case 19:capture("global-offset");number("fill3d1_1",15);break;
 case 20:capture("global-rotation");number("fill3d2_0",.8);break;
 case 21:capture("global-scale");combo("fillShapeCrop",1);break;
 case 22:capture("cropped");combo("fillWrap",1);break;
 case 23:capture("no-wrap");action("checkGuiComposite");break;
 case 24:capture("reference");action("guiPlanarSave");break;
 case 25:capture("saved");if(!QFile::rename(image,image+".removed"))qFatal("Warp source move failed");action("guiPlanarOpen");break;
 case 26:capture("opened");combo("warpSelectedPoint",11);number("warpVector0_0",.25);break;
 case 27:capture("edited-open");action("undoDocument");break;
 case 28:capture("undo-open");button("warpReset1");break;
 case 29:capture("reset-all");w->findChild<QSpinBox *>("warpRows")->setValue(5);break;
 case 30:capture("resized");button("warpReset0");break;
 case 31:capture("reset-global");key(Qt::Key_V,Qt::ShiftModifier);break;
 case 32:capture("transform-mode");drag(axis(0),{35,-4});break;
 case 33:capture("gizmo-moved");action("undoDocument");break;
 case 34:capture("undo-gizmo");action("redoDocument");break;
 case 35:capture("redo-gizmo");key(Qt::Key_V,Qt::ShiftModifier);break;
 case 36:click(point(10));key(Qt::Key_E);break;
 case 37:capture("rotate-tangent-tool");drag(axis(0),{22,12});break;
 case 38:capture("rotated-tangents");key(Qt::Key_R);break;
 case 39:drag(axis(2),{16,-23});break;
 case 40:capture("scaled-tangents");key(Qt::Key_W);break;
 case 41:drag(point(10),{-25,18});break;
 case 42:capture("surface-drag");button("warpMirror0");break;
 case 43:capture("mirrored");action("undoDocument");break;
 case 44:capture("undo-mirror");break;
 case 45:button("warpReset1");button("warpReset0");break;
 case 46:capture("split-click-before");combo("warpSplitMode",3);mouse(QEvent::MouseMove,{v->width()*.25+20,v->height()*.5-30},Qt::NoButton,Qt::NoButton);click({v->width()*.25+20,v->height()*.5-30});key(Qt::Key_Escape);break;
 case 47:capture("split-click");combo("fillFiltering",2);number("fillScaleU",2);number("fillUvRotation",15);number("fillOffset0",.1);break;
 case 48:capture("uv-filter");combo("fillScaleMode",1);break;
 case 49:capture("physical");combo("fillScaleMode",0);break;
 case 50:capture("tiling-restored");action("checkGuiComposite");break;
 case 51:{capture("final");timer.stop();QFile file(dir+"/warp.json");if(!file.open(QIODevice::WriteOnly))qFatal("Warp proof unavailable");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());file.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:WarpDemo(QWidget *workspace,QWidget *viewport,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),v(viewport),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir);image=dir+"/warp-source.png";QImage source(64,64,QImage::Format_RGBA8888);const QColor colors[]{Qt::red,Qt::green,Qt::blue,Qt::yellow};for(int y=0;y<64;++y)for(int x=0;x<64;++x)source.setPixelColor(x,y,colors[(x/8+y/8)%4]);if(!source.save(image))qFatal("Warp source write failed");timer.setInterval(750);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_warp_demo(QWidget *w,QWidget *v,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new WarpDemo(w,v,d,m,r,std::move(state));}
}
