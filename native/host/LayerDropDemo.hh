#pragma once
#include "ViewportDemoCapture.hh"
#include "LayerStackList.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class LayerDropDemo final:public QObject{
 QWidget *w,*v;QString output,mesh;QTimer timer,dialogs;int stage=0,ticks=0,frame=0;bool busy=false;QJsonArray captures;std::function<QJsonObject()> state;QUuid paint,a,b,second;
 template<class T>T *get(const char *name){auto *x=w->findChild<T *>(name);if(!x)qFatal("Missing drop control %s",name);return x;}
 LayerStackList *list(){return dynamic_cast<LayerStackList *>(get<QListWidget>("layerStack"));}
 QListWidgetItem *item(QUuid id){for(int i=0;i<list()->count();++i)if(QUuid(list()->item(i)->data(Qt::UserRole).toString())==id&&list()->item(i)->data(Qt::UserRole+10).toString().isEmpty())return list()->item(i);qFatal("Drop row missing");}
 QUuid first(){return QUuid(list()->item(0)->data(Qt::UserRole).toString());}
 void action(const char *name){get<QAction>(name)->trigger();}
 void capture(const char *name){auto d=state();d["stage"]=name;captures.append(d);capture_demo_window(w,v).save(output+"/"+name+".png");}
 void drop(std::vector<QUuid> sources,QUuid target){auto *view=list();view->clearSelection();for(const auto &id:sources)item(id)->setSelected(true);view->setCurrentItem(item(sources.front()),QItemSelectionModel::NoUpdate);const auto mime=view->drag_data(sources.front());const QPoint point=target.isNull()?QPoint(8,view->visualItemRect(view->item(view->count()-1)).center().y()):view->visualItemRect(item(target)).center();QDragEnterEvent enter(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(view->viewport(),&enter);if(!enter.isAccepted())qFatal("Layer drag enter rejected");QDragMoveEvent move(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(view->viewport(),&move);if(stage==6)capture("folder-drop-indicator");QDropEvent event(point,Qt::MoveAction,mime.get(),Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(view->viewport(),&event);if(!event.isAccepted())qFatal("Layer drop rejected");}
 void advance(){if(busy)return;if(++ticks>1600)qFatal("Drop demo timeout stage %d",stage);const auto d=state();if(stage>1&&(d["composite_pending"].toBool()||d["render_frames"].toInt()<frame))return;switch(stage++){
 case 0:action("newProject");break;
 case 1:get<QComboBox>("projectTemplate")->setCurrentIndex(0);get<QLineEdit>("meshPath")->setText(mesh);get<QComboBox>("documentResolution")->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));busy=true;get<QPushButton>("createProject")->click();busy=false;break;
 case 2:paint=first();get<QToolButton>("addLayerFolder")->click();break;
 case 3:a=first();get<QToolButton>("addPaintLayer")->click();break;
 case 4:second=first();get<QToolButton>("addLayerFolder")->click();break;
 case 5:b=first();capture("setup");drop({paint},a);break;
 case 6:capture("inside-a");drop({paint},b);break;
 case 7:capture("inside-b");list()->setCurrentItem(item(b));action("toggleLayerFolder");drop({second},b);break;
 case 8:capture("collapsed-received");list()->setCurrentItem(item(b));action("toggleLayerFolder");drop({paint},{});break;
 case 9:capture("root");drop({paint,second},a);break;
 case 10:capture("multiple-in-a");drop({a},b);break;
 case 11:capture("nested");action("undoDocument");break;
 case 12:capture("undo");action("redoDocument");break;
 case 13:capture("redo");busy=true;action("guiSkewSave");busy=false;break;
 case 14:busy=true;action("guiSkewOpen");busy=false;break;
 case 15:capture("reopened");timer.stop();{QFile f(output+"/layer-drop.json");if(!f.open(QIODevice::WriteOnly))qFatal("Drop proof write failed");f.write(QJsonDocument(QJsonObject{{"captures",captures},{"paint",paint.toString(QUuid::WithoutBraces)},{"second",second.toString(QUuid::WithoutBraces)},{"a",a.toString(QUuid::WithoutBraces)},{"b",b.toString(QUuid::WithoutBraces)}}).toJson());}QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
 }frame=state()["render_frames"].toInt()+3;}
 public:LayerDropDemo(QWidget *window,QWidget *viewport,QString directory,QString source,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),output(directory),mesh(source),state(std::move(snapshot)){connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start(100);connect(&dialogs,&QTimer::timeout,this,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(widget);box&&box->isVisible()){if(box->icon()==QMessageBox::Critical)qFatal("Drop GUI error: %s",qPrintable(box->text()));if(auto *button=box->button(QMessageBox::Discard))button->click();}});dialogs.start(40);}
};
}
