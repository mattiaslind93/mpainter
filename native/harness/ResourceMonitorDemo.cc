// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceMonitorDemo.hh"
#include "Performance.hh"
#include "GuiFileSelection.hh"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *w;QString dir,mesh,path,library;int size,stage=0,waits=0;QTimer timer,ping;QElapsedTimer heartbeat;double max_ping=0;QJsonArray captures,gaps;std::function<QJsonObject()> state;
 template<class T>T *get(const char *name){auto *value=w->findChild<T *>(name);if(!value)qFatal("Resource update control missing: %s",name);return value;}
 void action(const char *name){get<QAction>(name)->trigger();}
 void write(const QString &file,QColor color,int width=32){QImage image(width,32,QImage::Format_RGBA8888);image.fill(color);QByteArray bytes;QBuffer buffer(&bytes);if(!buffer.open(QIODevice::WriteOnly)||!image.save(&buffer,"PNG"))qFatal("Resource image encode failed");QSaveFile output(file);if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())qFatal("Resource atomic overwrite failed");}
 void capture(const char *name){performance::Scope timing("resource_test_oracle");auto data=state();data["stage"]=name;auto *monitor=get<QObject>("resourceMonitor");data["monitor_completed"]=monitor->property("completed").toInt();data["monitor_messages"]=QJsonArray::fromStringList(monitor->property("messages").toStringList());data["library_auto"]=get<QAction>("autoUpdateAssets")->isChecked();data["project_auto"]=get<QAction>("autoUpdateProject")->isChecked();data["skip_mismatch"]=get<QAction>("skipResourceMismatch")->isChecked();captures.append(data);if(QString(name)=="automatic")w->grab().save(dir+"/resource-updates.png");}
 void import_user(){timer.stop();QTimer::singleShot(100,w,[this]{auto *dialog=get<QDialog>("resourceImportDialog");dialog->findChild<QLineEdit *>("resourceImportPath")->setText(library);dialog->findChild<QComboBox *>("resourceImportUsage")->setCurrentIndex(1);dialog->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(2);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});get<QToolButton>("importResources")->click();timer.start();}
 void start_ping(){performance::begin("resource-refresh-stage-"+std::to_string(stage));heartbeat.start();ping.start();}
 void advance(){if(stage>2){w->setProperty("resourceProbeOnly",true);const auto s=state();w->setProperty("resourceProbeOnly",false);if(s["composite_pending"].toBool()||s["property_pending"].toBool()||get<QObject>("resourceMonitor")->property("pending").toBool())return;}
 switch(stage++){
 case 0:capture("defaults");action("newProject");break;
 case 1:{auto *dialog=get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *resolution=dialog->findChild<QComboBox *>("documentResolution");resolution->setCurrentIndex(resolution->findData(size));dialog->findChild<QPushButton *>("createProject")->click();break;}
 case 2:action("guiFillTexturePrepare");get<QToolButton>("addFillLayer")->click();break;
 case 3:timer.stop();select_gui_test_file(w,"fillTextureFileDialog",path);get<QComboBox>("fillBaseColorResource")->setCurrentIndex(2);timer.start();break;
 case 4:{get<QComboBox>("layerChannelContext")->setCurrentIndex(get<QComboBox>("layerChannelContext")->findData(2));get<QComboBox>("fillChannelResource2")->setCurrentIndex(2);auto *list=get<QListWidget>("shelfAssets");bool assigned=false;for(int i=0;i<list->count();++i)if(list->item(i)->text()=="monitored"){QMetaObject::invokeMethod(list,"itemDoubleClicked",Qt::DirectConnection,Q_ARG(QListWidgetItem *,list->item(i)));assigned=true;break;}if(!assigned)qFatal("Project texture Shelf item missing");break;}
 case 5:capture("assigned");import_user();break;
 case 6:capture("imported");write(path,QColor(192,224,32));write(library,QColor(192,224,32));start_ping();action("refreshAssetsNow");break;
 case 7:ping.stop();capture("library-only");start_ping();for(int i=0;i<100;++i)action("refreshProjectResourcesNow");break;
 case 8:ping.stop();capture("project-updated");action("undoDocument");break;
 case 9:capture("undo");action("redoDocument");break;
 case 10:capture("redo");action("undoDocument");break;
 case 11:action("guiFillTextureSave");break;
 case 12:capture("saved-undo");action("guiFillTextureOpen");break;
 case 13:capture("opened-undo");get<QSpinBox>("resourceUpdateInterval")->setValue(0);get<QAction>("autoUpdateProject")->setChecked(true);write(path,QColor(224,16,16));waits=0;start_ping();break;
 case 14:if(++waits<32){--stage;break;}ping.stop();capture("automatic");get<QAction>("autoUpdateProject")->setChecked(false);if(!QFile::rename(path,path+".removed"))qFatal("Resource missing-source setup failed");action("refreshProjectResourcesNow");break;
 case 15:capture("missing");write(path,QColor(16,192,224),64);action("refreshProjectResourcesNow");break;
 case 16:capture("recovered-resized");action("checkGuiComposite");break;
 case 17:capture("reference");action("guiFillTextureSave");break;
 case 18:capture("saved");if(!QFile::rename(path,path+".final"))qFatal("Resource packed-only setup failed");action("guiFillTextureOpen");break;
 case 19:capture("opened-packed");path=dir+"/sequence.1001.png";write(path,QColor(64,32,192));timer.stop();select_gui_test_file(w,"fillTextureFileDialog",path);get<QComboBox>("fillBaseColorResource")->setCurrentIndex(2);timer.start();break;
 case 20:capture("sequence-one");write(dir+"/sequence.1002.png",QColor(192,224,32));action("refreshProjectResourcesNow");break;
 case 21:capture("sequence-skipped");get<QAction>("skipResourceMismatch")->setChecked(false);action("refreshProjectResourcesNow");break;
 case 22:capture("sequence-accepted");action("guiFillTextureSave");break;
 case 23:capture("sequence-saved");if(!QFile::rename(path,path+".removed")||!QFile::rename(dir+"/sequence.1002.png",dir+"/sequence.1002.png.removed"))qFatal("Sequence packed-only setup failed");action("guiFillTextureOpen");break;
 case 24:{capture("sequence-opened");timer.stop();QFile file(dir+"/resource-monitor.json");if(!file.open(QIODevice::WriteOnly))qFatal("Resource update proof unavailable");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures},{"max_ping_ms",max_ping},{"gaps",gaps},{"timings",performance::snapshot()}}).toJson());file.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:Demo(QWidget *owner,QString directory,QString fixture,int resolution,std::function<QJsonObject()> status):QObject(owner),w(owner),dir(directory),mesh(fixture),size(resolution),state(std::move(status)){QDir().mkpath(dir);path=dir+"/monitored.png";library=dir+"/library.png";write(path,QColor(64,32,192));write(library,QColor(64,32,192));timer.setObjectName("resourceDemoAdvance");ping.setObjectName("resourceDemoPing");timer.setInterval(200);connect(&timer,&QTimer::timeout,this,[this]{advance();});ping.setInterval(10);ping.setTimerType(Qt::PreciseTimer);connect(&ping,&QTimer::timeout,this,[this]{const double gap=heartbeat.restart();max_ping=std::max(max_ping,gap);if(gap>=50){gaps.append(QJsonObject{{"stage",stage},{"gap_ms",gap}});std::fprintf(stderr,"BTS_RESOURCE_PING stage=%d gap_ms=%.1f\n",stage,gap);}});timer.start();}
};}
void start_resource_monitor_demo(QWidget *w,QWidget *,const QString &dir,const QString &mesh,int size,std::function<QJsonObject()> state){new Demo(w,dir,mesh,size,std::move(state));}
}
