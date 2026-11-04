#pragma once
#include <epoxy/gl.h>
#include <QJsonObject>
#include <QJsonArray>
#include "ViewportDemoCapture.hh"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QStyleOptionViewItem>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <cmath>
namespace bts {
class CheckboxDemo final:public QObject {
 QWidget *window_,*viewport_;QString directory_,mesh_;std::function<QJsonObject()> state_;QTimer timer_;int stage_=0,ticks_=0;bool busy_=false;
 int body_=0,isolated_=1;
 QJsonObject before_,hidden_,reloaded_,camera_;double angle_=0;QJsonArray evidence_;
 template<class T>T *get(const char *name){auto *w=window_->findChild<T *>(name);if(!w)throw std::runtime_error(std::string("Missing control: ")+name);return w;}
 void action(const char *name){get<QAction>(name)->trigger();}
 static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
 QJsonObject probe(){action("checkboxProbe");return window_->property("checkboxProbe").toJsonObject();}
 void set_loaded(int row,bool enabled){auto *list=get<QListWidget>("textureSets");list->setCurrentRow(row);auto *check=list->itemWidget(list->item(row))->findChild<QCheckBox *>("textureSetLoaded");require(check,"Missing set checkbox");if(check->isChecked()==enabled)return;const QPointF point=check->rect().center();for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(type,point,check->mapToGlobal(point.toPoint()),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(check,&event);}require(check->isChecked()==enabled,"Set checkbox click failed");}
 void tile_loaded(bool enabled){auto *tree=get<QTreeWidget>("uvTiles");require(tree->topLevelItemCount()==1,"Missing tiles");auto *root=tree->topLevelItem(0);require(root->childCount()>=2,"Need two UV tiles");root->child(1)->setCheckState(0,enabled?Qt::Checked:Qt::Unchecked);}
 void drag(int dx,Qt::KeyboardModifiers modifiers){const QPointF from=viewport_->rect().center();for(auto type:{QEvent::MouseButtonPress,QEvent::MouseMove,QEvent::MouseButtonRelease}){const auto p=type==QEvent::MouseButtonPress?from:from+QPointF(dx,0);QMouseEvent event(type,p,viewport_->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::RightButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::RightButton,modifiers);QCoreApplication::sendEvent(viewport_,&event);}}
 void check_rotation_controls(){const double angle=state_()["environment"].toObject()["rotation"].toDouble();require(std::abs(get<QDoubleSpinBox>("environmentRotation")->value()-angle)<.11,"HDR spinbox is stale");require(std::abs(get<QSlider>("environmentRotationSlider")->value()/10.-angle)<.11,"HDR slider is stale");}
 void advance(){
  if(busy_)return;busy_=true;
  try{
   require(++ticks_<1800,"Checkbox regression timed out");const auto state=state_();
   if(stage_>1&&(state["composite_pending"].toBool()||state["texture_loading"].toBool()||state["open_pending"].toBool()||state["save_pending"].toBool())){busy_=false;return;}
   if(stage_>1)require(state["open_error"].toString().isEmpty()&&state["save_error"].toString().isEmpty(),"Project I/O failed");
   qInfo("CHECKBOX_STAGE %d",stage_);
   switch(stage_++){
   case 0:action("newProject");break;
   case 1:get<QComboBox>("projectTemplate")->setCurrentIndex(0);get<QLineEdit>("meshPath")->setText(mesh_);get<QComboBox>("documentResolution")->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));get<QCheckBox>("preserveUvTiles")->setChecked(true);get<QPushButton>("createProject")->click();break;
   case 2:for(int i=0;i<get<QListWidget>("textureSets")->count();++i){if(get<QListWidget>("textureSets")->item(i)->text().contains("Painted"))body_=i;else isolated_=i;}action("checkboxPrepare");get<QTabWidget>("layerSettingsTabs")->setCurrentIndex(1);break;
   case 3:before_=probe();set_loaded(isolated_,false);break;
   case 4:get<QListWidget>("textureSets")->setCurrentRow(body_);tile_loaded(false);break;
   case 5:hidden_=probe();require(hidden_["gpu_bytes"].toDouble()<before_["gpu_bytes"].toDouble(),"Unloading did not free GPU allocations");require(hidden_["hashes"]==before_["hashes"],"Unloading changed authoring data");capture_demo_window(window_,viewport_).save(directory_+"/texturesets-tab.png");tile_loaded(true);break;
   case 6:set_loaded(isolated_,true);break;
   case 7:reloaded_=probe();require(reloaded_["hashes"]==before_["hashes"],"Reload changed paint data");require(reloaded_["samples"]==before_["samples"],"Reload changed GPU pixels");{const auto environment=state_().value("environment");drag(20,Qt::NoModifier);require(state_()["environment"]==environment,"Ordinary RMB rotated the environment");}get<QListWidget>("textureSets")->setCurrentRow(body_);break;
   case 8:camera_=state_()["view"].toObject();angle_=state_()["environment"].toObject()["rotation"].toDouble();drag(80,Qt::ShiftModifier);break;
   case 9:require(state_()["view"].toObject()==camera_,"HDR drag moved the camera");require(std::abs(std::remainder(state_()["environment"].toObject()["rotation"].toDouble()-angle_-40,360.))<.01,"HDR drag did not rotate 40 degrees");check_rotation_controls();evidence_.append(state_());action("checkboxCycles");break;
   case 10:camera_=state_()["view"].toObject();angle_=state_()["environment"].toObject()["rotation"].toDouble();drag(-40,Qt::ShiftModifier);break;
   case 11:require(state_()["view"].toObject()==camera_,"Cycles HDR drag moved camera");require(std::abs(std::remainder(state_()["environment"].toObject()["rotation"].toDouble()-angle_+20,360.))<.01,"Cycles HDR drag failed");check_rotation_controls();evidence_.append(state_());action("checkboxEevee");set_loaded(isolated_,false);break;
   case 12:get<QListWidget>("textureSets")->setCurrentRow(body_);tile_loaded(false);break;
   case 13:angle_=state_()["environment"].toObject()["rotation"].toDouble();action("checkboxSave");break;
   case 14:action("checkboxOpen");break;
   case 15:{const auto restored=probe();require(restored["loaded"]==hidden_["loaded"],"Saved visibility choices were lost");require(restored["hashes"]==before_["hashes"],"Save/open lost unloaded paint");require(std::abs(state_()["environment"].toObject()["rotation"].toDouble()-angle_)<.01,"Saved HDR rotation changed");action("checkboxExport");get<QTabWidget>("layerSettingsTabs")->setCurrentIndex(1);capture_demo_window(window_,viewport_).save(directory_+"/texturesets-tab.png");QFile file(directory_+"/checkbox-regression.json");require(file.open(QIODevice::WriteOnly),"Cannot write proof");file.write(QJsonDocument(QJsonObject{{"before",before_},{"hidden",hidden_},{"reloaded",reloaded_},{"reopened",restored},{"hdr",evidence_},{"passed",true}}).toJson());timer_.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
   }
  }catch(const std::exception &e){qCritical("CHECKBOX_TEST_FAILED stage=%d: %s",stage_,e.what());timer_.stop();QCoreApplication::exit(1);}
  busy_=false;
 }
 public:
 CheckboxDemo(QWidget *window,QWidget *viewport,QString directory,QString mesh,std::function<QJsonObject()> state):QObject(window),window_(window),viewport_(viewport),directory_(std::move(directory)),mesh_(std::move(mesh)),state_(std::move(state)){connect(&timer_,&QTimer::timeout,this,[this]{advance();});timer_.start(350);}
};
}
