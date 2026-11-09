// SPDX-License-Identifier: GPL-3.0-or-later
#include "FillChannelsDemo.hh"
#include "GuiFileSelection.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *w;QString dir,mesh,gray,normal;int resolution,stage=0,channel=0,phase=0;QTimer timer;QJsonArray captures;std::function<QJsonObject()> state;
 void action(const QString &name){auto *a=w->findChild<QAction *>(name);if(!a)qFatal("Channel texture action missing: %s",qPrintable(name));a->trigger();}
 QComboBox *resource(int c){return w->findChild<QComboBox *>(c==0?QString("fillBaseColorResource"):QString("fillChannelResource%1").arg(c));}
 void capture(const QString &name){auto data=state();data["stage"]=name;QJsonArray labels;for(int c=0;c<15;++c)labels.append(QJsonObject{{"text",resource(c)->currentText()},{"visible",resource(c)->isVisible()}});data["resource_labels"]=labels;captures.append(data);w->grab().save(dir+"/"+name+".png");}
 void file(int c){timer.stop();const auto path=c==3?normal:gray;select_gui_test_file(w,"fillTextureFileDialog",path);resource(c)->setCurrentIndex(c==0?2:1);timer.start();}
 void shelf(int c){resource(c)->setCurrentIndex(c==0?3:2);auto *list=w->findChild<QListWidget *>("shelfAssets");for(int row=0;row<list->count();++row)if(list->item(row)->text()=="channel-pair"){QMetaObject::invokeMethod(list,"itemDoubleClicked",Qt::DirectConnection,Q_ARG(QListWidgetItem *,list->item(row)));return;}qFatal("Channel texture Shelf item missing");}
 void advance(){if(stage>2){const auto status=state();if(status["composite_pending"].toBool()||status["property_pending"].toBool())return;}switch(stage++){
 case 0:action("newProject");break;
 case 1:{auto *d=w->findChild<QDialog *>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));d->findChild<QPushButton *>("createProject")->click();break;}
 case 2:action("guiFillTexturePrepare");action("addChannel5");for(int slot=0;slot<8;++slot)action(QString("addUserChannel%1").arg(slot));w->findChild<QToolButton *>("shaderSettingsButton")->click();w->findChild<QCheckBox *>("displacementChannel")->setChecked(true);w->findChild<QDoubleSpinBox *>("displacementStrength")->setValue(.15);w->findChild<QSpinBox *>("displacementSubdivisionCount")->setValue(2);w->findChild<QToolButton *>("addFillLayer")->click();break;
 case 3:if(phase++==0){capture("uniform");file(0);--stage;}else{capture(QString("channel-%1").arg(channel));if(++channel<15){auto *context=w->findChild<QComboBox *>("layerChannelContext");context->setCurrentIndex(context->findData(channel));if(channel==2)shelf(channel);else file(channel);--stage;}}break;
 case 4:capture("all-bound");resource(2)->setCurrentIndex(0);break;
 case 5:capture("roughness-uniform");action("undoDocument");break;
 case 6:capture("undo-uniform");action("redoDocument");break;
 case 7:capture("redo-uniform");action("undoDocument");break;
 case 8:capture("textures-restored");w->findChild<QDoubleSpinBox *>("fillScaleU")->setValue(2);break;
 case 9:capture("tiling");w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(1);break;
 case 10:capture("triplanar");w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(2);break;
 case 11:capture("planar");w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(3);break;
 case 12:capture("spherical");w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(4);break;
 case 13:capture("cylindrical");action("checkGuiComposite");break;
 case 14:capture("reference");action("guiFillTextureSave");break;
 case 15:capture("saved");if(!QFile::rename(gray,gray+".removed")||!QFile::rename(normal,normal+".removed"))qFatal("Could not remove original source files for packed proof");action("guiFillTextureOpen");break;
 case 16:capture("opened-packed");if(!qEnvironmentVariable("BTS_FILL_CHANNELS_LEGACY").isEmpty())action("guiFillChannelsLegacy");break;
 case 17:{if(!qEnvironmentVariable("BTS_FILL_CHANNELS_LEGACY").isEmpty())capture("legacy");timer.stop();QFile f(dir+"/fill-channels.json");if(!f.open(QIODevice::WriteOnly))qFatal("Channel texture proof write failed");f.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());f.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:Demo(QWidget *workspace,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir);gray=dir+"/channel-pair.png";normal=dir+"/channel-normal.png";QImage image(64,32,QImage::Format_RGBA8888);for(int y=0;y<32;++y)for(int x=0;x<64;++x)image.setPixelColor(x,y,x<32?QColor(64,32,192):QColor(192,224,32));if(!image.save(gray))qFatal("Gray raw/color fixture write failed");image.fill(QColor(128,128,255));if(!image.save(normal))qFatal("Normal fixture write failed");timer.setInterval(600);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_fill_channels_demo(QWidget *w,QWidget *,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new Demo(w,d,m,r,std::move(state));}
}
