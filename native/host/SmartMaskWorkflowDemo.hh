#pragma once
#include "ViewportDemoCapture.hh"
#include "LayerRowLayout.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QWindow>
#include <functional>
#include <array>
#include <cstdio>
#include <stdexcept>
namespace bts {
// Explicit private UI diagnostic. Target fixture creation is separate from the
// real Shelf-drop, typed Properties, Undo/Redo, Bake and packed Save/Open paths.
class SmartMaskWorkflowDemo final:public QObject {
 QWidget *w,*v;QString output,mesh;std::function<QJsonObject()> state;QTimer timer,dialogs;QElapsedTimer application_clock;double application_handler_ms=0;
 std::vector<std::function<void()>> steps;QJsonArray checks,captures,edits;QJsonObject before,after,saved;
 int step=0,ticks=0,frame=0,undo_before=0;QString failure;
 inline static constexpr std::array<const char *,8> names{"Rust","Edge Wear","Crease Dirt","Moss","Ground Dirt","Scratches","Leaks & Stains","Surface Dust"};
 static void require(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
 template<class T>T *get(const char *name)const{auto *value=w->findChild<T *>(name);require(value,name);return value;}
 void action(const char *name){auto *value=get<QAction>(name);require(value->isEnabled(),name);value->trigger();}
 QJsonObject snapshot(){action("guiBnSnapshot");return w->property("bnState").toJsonObject();}
 void write(){QFile file(output+"/smart-masks.json");require(file.open(QIODevice::WriteOnly),"BN UI receipt write");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"completed_steps",step},{"failure",failure},{"checks",checks},{"captures",captures},{"property_latency",edits}}).toJson());}
 void capture(const char *stage){auto data=snapshot();data["stage"]=stage;captures.append(data);require(capture_demo_window(w,v).save(output+"/"+stage+".png"),"BN UI screenshot write");write();}
 void select_parent_mask(){
  const auto id=snapshot()["selected_layer"].toString();auto *list=get<QListWidget>("layerStack");QPoint point;bool found=false;
  {for(int i=0;i<list->count();++i){auto *item=list->item(i);if(item->data(Qt::UserRole).toString()!=id||!item->data(Qt::UserRole+10).toString().isEmpty())continue;list->scrollToItem(item);point=layer_mask_rect(list->indexFromItem(item),list->visualItemRect(item)).center();found=true;break;}}
  require(found,"BN parent mask thumbnail missing");
  // The press can rebuild the rows. Retain only the point and viewport, never
  // an item or model index across either real selection callback.
  for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(type,QPointF(point),QPointF(list->viewport()->mapToGlobal(point)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(list->viewport(),&event);}
 }
 void select_effect(const char *kind){
  select_parent_mask();const auto s=snapshot();QString id;for(const auto &value:s["effects"].toArray()){const auto effect=value.toObject();if(effect["type"].toString()==kind){id=effect["id"].toString();break;}}require(!id.isEmpty(),"BN typed effect absent");
  auto *list=get<QListWidget>("layerStack");for(int i=0;i<list->count();++i){auto *item=list->item(i);if(QUuid(item->data(Qt::UserRole+10).toString())==QUuid(id)){list->scrollToItem(item);list->setCurrentItem(item,QItemSelectionModel::ClearAndSelect);return;}}throw std::runtime_error("BN effect row absent");
 }
 void drop(const char *name){
  auto *shelf=get<QListWidget>("shelfAssets");QString asset_id;for(int i=0;i<shelf->count();++i){const auto *item=shelf->item(i);if(item->data(Qt::UserRole+1).toString()==name||item->text()==name){asset_id=item->data(Qt::UserRole).toString();break;}}require(!asset_id.isEmpty(),"BN exact preset Shelf card absent");
  const auto id=snapshot()["selected_layer"].toString();auto *layers=get<QListWidget>("layerStack");QPoint point;bool found=false;
  {for(int i=0;i<layers->count();++i){auto *item=layers->item(i);if(item->data(Qt::UserRole).toString()==id&&item->data(Qt::UserRole+10).toString().isEmpty()){layers->scrollToItem(item);point=layers->visualItemRect(item).center();found=true;break;}}}require(found,"BN target row absent");
  QMimeData mime;mime.setData("application/x-bts-asset",asset_id.toUtf8());QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(layers->viewport(),&enter);require(enter.isAccepted(),"BN Shelf drag rejected");QDropEvent event(QPointF(point),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(layers->viewport(),&event);require(event.isAccepted(),"BN Shelf drop rejected");
 }
 template<class F>void property(const char *name,bool folder,const char *field,F edit){QElapsedTimer timer;timer.start();edit();edits.append(QJsonObject{{"preset",name},{"folder",folder},{"field",field},{"handler_ms",timer.nsecsElapsed()/1e6}});}
 void fail(const char *why){failure=QString::fromUtf8(why);timer.stop();dialogs.stop();try{write();}catch(...){}std::fprintf(stderr,"BTS_BN_SMART_MASK_FAIL step=%d %s\n",step,why);QCoreApplication::exit(1);}
 void advance(){try{if(++ticks>12000)throw std::runtime_error("BN smart-mask UI timeout");v->update();const auto s=state();if(s["texture_loading"].toBool()||s["open_pending"].toBool()||s["save_pending"].toBool()||s["composite_pending"].toBool()||s["property_pending"].toBool()||s["layer_thumbnails_pending"].toBool()||(step!=1&&s["render_frames"].toInt()<frame))return;
  for(const char *key:{"material_error","open_error","save_error"})require(s[key].toString().isEmpty(),qPrintable(s[key].toString()));
  if((QApplication::activeModalWidget()&&step!=1)||QApplication::activePopupWidget())return;
  if(step!=1&&QApplication::activeWindow()!=w){if(w->windowHandle())w->windowHandle()->requestActivate();return;}
  if(w->property("bnWaitingThumbnails").toBool()){action("guiBnCheckThumbnails");if(!w->property("bnThumbnailsValid").toBool())return;}
  if(step==int(steps.size())){timer.stop();dialogs.stop();write();std::puts("BTS_BN_SMART_MASK_UI_PASS presets=8 applications=16 typed_edits=48 undo_redo=16 packed_save_open=1");QTimer::singleShot(100,qApp,&QCoreApplication::quit);return;}
  steps[step++]();frame=state()["render_frames"].toInt()+3;write();
 }catch(const std::exception &error){fail(error.what());}}
 void build_steps(){
  steps.push_back([this]{action("newProject");});
  steps.push_back([this]{get<QComboBox>("projectTemplate")->setCurrentIndex(0);get<QLineEdit>("meshPath")->setText(mesh);auto *resolution=get<QComboBox>("documentResolution");resolution->setCurrentIndex(resolution->findData(128));get<QPushButton>("createProject")->click();});
  steps.push_back([this]{auto *categories=get<QListWidget>("shelfCategories");const auto found=categories->findItems("Mask generators",Qt::MatchExactly);require(!found.empty(),"BN Mask generators category absent");categories->setCurrentItem(found.front());get<QLineEdit>("shelfSearch")->clear();});
  for(const char *name:names)for(bool folder:{false,true}){
   steps.push_back([this,name,folder]{w->setProperty("bnTargetName",name);w->setProperty("bnTargetFolder",folder);action("guiBnTarget");});
   steps.push_back([this,name]{application_clock.start();drop(name);application_handler_ms=application_clock.nsecsElapsed()/1e6;});
   steps.push_back([this,name,folder]{const auto s=snapshot();require(s["has_mask"].toBool()&&s["triplanar_bitmap"].toBool()&&s["generator_surface_flags"].toBool(),"BN applied stack lost editable generator/true triplanar bitmap");require(s["folder"].toBool()==folder,"BN wrong target kind");checks.append(QJsonObject{{"preset",name},{"folder",folder},{"native_generator_triplanar_bitmap",true},{"activation","row_drop"},{"handler_return_ms",application_handler_ms},{"drop_to_settled_observation_ms",application_clock.nsecsElapsed()/1e6},{"settled_observation_cadence_ms",100}});select_effect("generator");before=snapshot()["mask_state"].toObject();undo_before=state()["undo_count"].toInt();});
   steps.push_back([this,name,folder]{property(name,folder,"generator",[&]{auto *level=get<QDoubleSpinBox>("generatorLevel");level->setValue(level->value()>.6?.43:.67);});});
   steps.push_back([this]{select_effect("fill");});
   steps.push_back([this,name,folder]{property(name,folder,"fill",[&]{get<QDoubleSpinBox>("maskEffectFillOpacity")->setValue(61);});});
   steps.push_back([this]{select_effect("levels");});
   steps.push_back([this,name,folder]{property(name,folder,"levels",[&]{get<QDoubleSpinBox>("levelsGamma")->setValue(1.37);});});
   steps.push_back([this]{after=snapshot()["mask_state"].toObject();require(after!=before&&state()["undo_count"].toInt()==undo_before+3,"BN typed edits missing expected Undo boundaries");action("undoDocument");action("undoDocument");action("undoDocument");});
   steps.push_back([this]{require(snapshot()["mask_state"].toObject()==before,"BN typed edit Undo changed tree");action("redoDocument");action("redoDocument");action("redoDocument");});
   steps.push_back([this]{require(snapshot()["mask_state"].toObject()==after,"BN typed edit Redo changed tree");});
  }
  steps.push_back([this]{select_effect("generator");require(get<QPushButton>("generatorBakeMaps")->isVisible(),"BN missing-map Bake button hidden");capture("fallback-generator");get<QPushButton>("generatorBakeMaps")->click();});
  steps.push_back([this]{require(snapshot()["baked_roles"].toInt()>=5,"BN baked map roles not installed");capture("baked-generator");});
  steps.push_back([this]{action("guiBnSave");saved=snapshot()["document"].toObject();});
  steps.push_back([this]{action("guiBnOpen");});
  steps.push_back([this]{require(snapshot()["document"].toObject()==saved,"BN packed Open changed mask parameters");action("guiBnValidateOpen");});
  steps.push_back([this]{action("guiBnPrimeThumbnails");action("guiBnCheckThumbnails");});
  steps.push_back([this]{require(w->property("bnThumbnailsValid").toBool(),"BN evaluated native sphere thumbnails not complete/distinct");w->resize(1024,768);});
  steps.push_back([this]{capture("smart-mask-shelf-1024");select_effect("fill");});
  steps.push_back([this]{capture("triplanar-fill-1024");w->resize(1920,1080);});
  steps.push_back([this]{capture("triplanar-fill-1920");select_effect("generator");});
  steps.push_back([this]{capture("generator-1920");});
 }
 void handle_dialogs(){try{for(auto *widget:QApplication::topLevelWidgets()){
  if(auto *dialog=qobject_cast<QDialog *>(widget);dialog&&dialog->isVisible()&&dialog->objectName()=="meshBakeDialog"&&!dialog->property("bnPrepared").toBool()){dialog->setProperty("bnPrepared",true);require(dialog->grab().save(output+"/bake-mesh-maps.png"),"BN Bake dialog screenshot");auto *maps=get<QListWidget>("bakeMapTypes");for(int i=0;i<maps->count();++i)maps->item(i)->setCheckState(i==1||i>=3?Qt::Checked:Qt::Unchecked);get<QCheckBox>("bakeUseProjectMesh")->setChecked(true);auto *resolution=get<QComboBox>("bakeResolution");resolution->setCurrentIndex(resolution->findData(128));get<QSpinBox>("bakeSecondaryRays")->setValue(8);get<QPushButton>("startMeshBake")->click();}
  if(auto *dialog=qobject_cast<QDialog *>(widget);dialog&&dialog->isVisible()&&dialog->objectName()=="meshBakeProgress"){auto *finish=dialog->findChild<QPushButton *>("finishMeshBake");if(finish&&finish->text()=="OK")finish->click();else if(finish&&finish->text()=="Close")throw std::runtime_error("BN actual native mesh bake failed");}
  if(auto *box=qobject_cast<QMessageBox *>(widget);box&&box->isVisible()){if(box->icon()==QMessageBox::Critical)throw std::runtime_error(box->text().toStdString());if(auto *discard=box->button(QMessageBox::Discard))discard->click();}
 }}catch(const std::exception &error){fail(error.what());}}
 public:SmartMaskWorkflowDemo(QWidget *window,QWidget *viewport,QString directory,QString source,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),output(std::move(directory)),mesh(std::move(source)),state(std::move(snapshot)){QDir().mkpath(output);build_steps();timer.setInterval(100);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();dialogs.setInterval(40);connect(&dialogs,&QTimer::timeout,this,[this]{handle_dialogs();});dialogs.start();}
};
}
