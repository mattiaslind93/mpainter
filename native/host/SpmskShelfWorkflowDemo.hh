#pragma once
#include "ViewportDemoCapture.hh"
#include "LayerRowLayout.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QWindow>
#include <array>
#include <functional>
#include <cstdio>
#include <stdexcept>
namespace bts {
// Separate explicit Resource Import/User Shelf diagnostic. It never calls the
// preset application callback, parses source archives or clears/prewarms caches.
class SpmskShelfWorkflowDemo final:public QObject {
 QWidget *w,*v;QString output,mesh,phase;std::function<QJsonObject()> state;QTimer timer,dialogs;
 std::vector<std::function<void()>> steps;QJsonArray checks,captures,observations;QJsonObject before,applied,edited;QJsonArray content_before;
 int step=0,ticks=0,frame=0,undo_before=0,layer_count_before=0;QString failure,pending_import,target_id;
 static void require(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
 template<class T>T *get(const char *name)const{auto *value=w->findChild<T *>(name);require(value,name);return value;}
 void action(const char *name){auto *value=get<QAction>(name);require(value->isEnabled(),name);value->trigger();}
 QJsonObject snapshot(){action("guiSpmskSnapshot");return w->property("spmskState").toJsonObject();}
 QJsonObject source(const QJsonObject &s,const QString &name){for(const auto &value:s["user_sources"].toArray()){const auto row=value.toObject();if(row["name"].toString()=="BN Imported "+name)return row;}throw std::runtime_error("Imported SPMSK User Shelf source missing");}
 void write(){QFile file(output+"/spmsk-shelf.json");require(file.open(QIODevice::WriteOnly),"SPMSK proof write");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"phase",phase},{"completed_steps",step},{"failure",failure},{"checks",checks},{"captures",captures},{"source_observations",observations},{"persistence",w->property("spmskOpenProof").toJsonObject()}}).toJson());}
 void observe(const char *stage){observations.append(QJsonObject{{"stage",stage},{"state",state()},{"catalog",snapshot()}});}
 void capture(const char *stage){auto row=state();row["stage"]=stage;row["metadata"]=snapshot();captures.append(row);require(capture_demo_window(w,v).save(output+"/"+stage+".png"),"SPMSK screenshot write");write();}
 void select_generator(){
  const auto s=snapshot();const auto parent=s["selected_layer"].toString();auto *list=get<QListWidget>("layerStack");QPoint point;bool found=false;
  {for(int i=0;i<list->count();++i){auto *item=list->item(i);if(item->data(Qt::UserRole).toString()==parent&&item->data(Qt::UserRole+10).toString().isEmpty()){list->scrollToItem(item);point=layer_mask_rect(list->indexFromItem(item),list->visualItemRect(item)).center();found=true;break;}}}
  require(found,"SPMSK actual mask thumbnail absent");
  for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(type,QPointF(point),QPointF(list->viewport()->mapToGlobal(point)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(list->viewport(),&event);}
  QString id;std::function<void(QJsonArray)> find=[&](QJsonArray effects){for(const auto &value:effects){const auto effect=value.toObject();if(effect["type"].toString()=="generator"&&id.isEmpty())id=effect["id"].toString();if(effect["type"].toString()=="group")find(effect["effects"].toArray());}};find(snapshot()["effects"].toArray());require(!id.isEmpty(),"SPMSK recognized editable Generator absent");
  // Clicking the parent can rebuild every row. Resolve again by effect UUID.
  for(int i=0;i<list->count();++i){auto *item=list->item(i);if(QUuid(item->data(Qt::UserRole+10).toString())==QUuid(id)){list->scrollToItem(item);list->setCurrentItem(item,QItemSelectionModel::ClearAndSelect);return;}}throw std::runtime_error("SPMSK Generator row absent");
 }
 void select_material_thumbnail(){
  const auto parent=snapshot()["selected_layer"].toString();auto *list=get<QListWidget>("layerStack");QPoint point;bool found=false;
  for(int i=0;i<list->count();++i){auto *item=list->item(i);if(item->data(Qt::UserRole).toString()==parent&&item->data(Qt::UserRole+10).toString().isEmpty()){list->scrollToItem(item);const auto index=list->indexFromItem(item);const auto box=list->visualItemRect(item);point={layer_thumbnail_x(index,box)+12,box.center().y()};found=true;break;}}
  require(found,"Fresh Generator material thumbnail missing");for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(type,QPointF(point),QPointF(list->viewport()->mapToGlobal(point)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(list->viewport(),&event);}
 }
 void require_new_generator(const QJsonArray &effects){require(!effects.isEmpty(),"Fresh Generator missing");const auto e=effects.last().toObject();require(e["surface_fallback"].toBool()&&e["grunge_projection"].toString()=="triplanar"&&get<QCheckBox>("generatorSurfaceFallback")->isChecked(),"Fresh Generator UI/native defaults lost");}
 void activate(const QString &name,bool folder,bool drop,bool first){
  auto *shelf=get<QListWidget>("shelfAssets");require(shelf->isVisible(),"SPMSK User Shelf is hidden");const auto asset=source(snapshot(),name);const auto id=asset["id"].toString();QPoint point;bool found=false;
  if(drop){const auto target=snapshot()["selected_layer"].toString();auto *list=get<QListWidget>("layerStack");for(int i=0;i<list->count();++i){auto *item=list->item(i);if(item->data(Qt::UserRole).toString()==target&&item->data(Qt::UserRole+10).toString().isEmpty()){list->scrollToItem(item);point=list->visualItemRect(item).center();found=true;break;}}}
  else for(int i=0;i<shelf->count();++i){auto *item=shelf->item(i);if(item->data(Qt::UserRole).toString()==id){require(item->flags()&Qt::ItemIsEnabled,"SPMSK User Shelf card is disabled");shelf->scrollToItem(item);const auto box=shelf->visualItemRect(item);point={box.center().x(),box.top()+24};found=true;break;}}
  require(found,"SPMSK actual Shelf/target row absent");
  const auto observed=source(snapshot(),name);if(phase=="shelf-cold"&&first)require(!observed["smart_mask_present"].toBool()&&observed["smart_mask_pointer"].toString()=="0","Fresh User Shelf source unexpectedly has a smart_mask pointer");
  QJsonObject record{{"preset",name},{"folder",folder},{"route",drop?"row-drop":"double-click"},{"first_source_application",first},{"before_activation",observed},{"source_cache_note","Cache-only observation. Natural thumbnail jobs may already be preparing/holding this source; absence does not establish a new HDF read after activation."},{"state_before",state()}};
  if(drop){auto *list=get<QListWidget>("layerStack");QMimeData mime;mime.setData("application/x-bts-asset",id.toUtf8());QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(list->viewport(),&enter);require(enter.isAccepted(),"SPMSK Shelf drag rejected");QDropEvent event(QPointF(point),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(list->viewport(),&event);require(event.isAccepted(),"SPMSK Shelf drop rejected");}
  else for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}){QMouseEvent event(type,QPointF(point),QPointF(shelf->viewport()->mapToGlobal(point)),Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(shelf->viewport(),&event);}
  const auto after=state();require(after["spsm_application_polls"].toInt()>0,"Actual SPMSK worker application poll was not scheduled");record["state_after_activation"]=after;observations.append(record);
 }
 void fail(const char *why){failure=QString::fromUtf8(why);timer.stop();dialogs.stop();try{write();}catch(...){}std::fprintf(stderr,"BTS_SPMSK_SHELF_FAIL step=%d %s\n",step,why);QCoreApplication::exit(1);}
 void advance(){try{
  if(++ticks>9000)throw std::runtime_error("SPMSK UI diagnostic timeout");v->update();const auto s=state();const bool new_dialog_step=phase!="packed-cold"&&step==1;
  if(s["texture_loading"].toBool()||s["open_pending"].toBool()||s["save_pending"].toBool()||s["resource_importing"].toBool()||s["spsm_application_polls"].toInt()>0||s["composite_pending"].toBool()||s["property_pending"].toBool()||s["layer_thumbnails_pending"].toBool()||(!new_dialog_step&&s["render_frames"].toInt()<frame))return;
  for(const char *key:{"material_error","open_error","save_error","workspace_error"})require(s[key].toString().isEmpty(),qPrintable(s[key].toString()));
  // CLI Open is scheduled after three real frames; wait for its actual document.
  if(phase=="packed-cold"&&step==0&&!s["has_document"].toBool())return;
  if((QApplication::activeModalWidget()&&!new_dialog_step)||QApplication::activePopupWidget())return;
  if(!new_dialog_step&&QApplication::activeWindow()!=w){if(w->windowHandle())w->windowHandle()->requestActivate();return;}
  if(step==int(steps.size())){timer.stop();dialogs.stop();write();std::printf("BTS_SPMSK_SHELF_PASS phase=%s applications=%lld persistence=1\n",qPrintable(phase),static_cast<long long>(checks.size()));QTimer::singleShot(100,qApp,&QCoreApplication::quit);return;}
  steps[step++]();frame=state()["render_frames"].toInt()+3;write();
 }catch(const std::exception &error){fail(error.what());}}
 void build_steps(){
  if(phase=="packed-cold"){
   steps.push_back([this]{require(state()["has_document"].toBool(),"Packed SPMSK Open has no document");require(snapshot()["user_sources"].toArray().isEmpty(),"Packed cold process unexpectedly has imported User Shelf sources");action("guiSpmskValidateOpen");capture("packed-cold-open");});return;
  }
  steps.push_back([this]{action("newProject");});
  steps.push_back([this]{get<QComboBox>("projectTemplate")->setCurrentIndex(0);get<QLineEdit>("meshPath")->setText(mesh);auto *resolution=get<QComboBox>("documentResolution");resolution->setCurrentIndex(resolution->findData(128));get<QPushButton>("createProject")->click();});
  steps.push_back([this]{auto *shelf=get<QAction>("toggleShelf");if(!shelf->isChecked())shelf->trigger();});
  if(phase=="import")for(const auto *name:{"Rust","Edge Rust"}){
   steps.push_back([this,name]{pending_import=name;auto *button=get<QToolButton>("importResources");require(button->isVisible()&&button->isEnabled(),"Resource Import button is not visible/enabled");button->click();});
   steps.push_back([this,name]{const auto row=source(snapshot(),name);require(row["smart_mask_present"].toBool()&&row["prepared_source_cached"].toBool(),"Resource Import did not publish a prepared User Shelf SPMSK source");observe("resource-import-published");});
  }
  steps.push_back([this]{require(snapshot()["user_sources"].toArray().size()==2,"SPMSK test needs exactly two imported User Shelf sources");auto *categories=get<QListWidget>("shelfCategories");const auto items=categories->findItems("Mask generators",Qt::MatchExactly);require(!items.empty(),"Mask generators category absent");categories->setCurrentItem(items.front());get<QComboBox>("shelfLibraryScope")->setCurrentIndex(4);get<QLineEdit>("shelfSearch")->clear();});
  struct Case{const char *name;bool folder,drop,first;};
  for(const auto test:std::array{Case{"Rust",false,false,true},Case{"Edge Rust",true,true,true},Case{"Rust",true,true,false},Case{"Edge Rust",false,false,false}}){
   steps.push_back([this,test]{w->setProperty("spmskTargetName",test.name);w->setProperty("spmskTargetFolder",test.folder);action("guiSpmskTarget");const auto fixture=snapshot();before=fixture["mask_state"].toObject();target_id=fixture["selected_layer"].toString();layer_count_before=fixture["layer_count"].toInt();require(!target_id.isEmpty()&&before.isEmpty(),"SPMSK fixture unexpectedly masked");undo_before=state()["undo_count"].toInt();});
   steps.push_back([this,test]{activate(test.name,test.folder,test.drop,test.first);});
   steps.push_back([this,test]{const auto s=snapshot();require(s["selected_layer"].toString()==target_id&&s["layer_count"].toInt()==layer_count_before&&s["has_mask"].toBool()&&s["folder"].toBool()==test.folder&&!s["effects"].toArray().isEmpty(),"SPMSK did not apply to the requested target");require(state()["undo_count"].toInt()==undo_before+1,"SPMSK apply must publish exactly one Undo");applied=s["mask_state"].toObject();action("undoDocument");});
   steps.push_back([this]{require(!snapshot()["has_mask"].toBool()&&state()["undo_count"].toInt()==undo_before,"SPMSK apply Undo failed");action("redoDocument");});
   if(phase=="import"&&!test.folder&&test.first){
    steps.push_back([this]{require(snapshot()["mask_state"].toObject()==applied,"Fresh Generator setup changed imported mask");select_generator();action("addMaskGenerator");});
    steps.push_back([this]{require_new_generator(snapshot()["effects"].toArray());action("undoDocument");});
    steps.push_back([this]{require(snapshot()["mask_state"].toObject()==applied,"Fresh mask Generator Undo changed imported stack");select_material_thumbnail();content_before=snapshot()["content_effects"].toArray();action("addMaskGenerator");});
    steps.push_back([this]{require_new_generator(snapshot()["content_effects"].toArray());action("undoDocument");});
    steps.push_back([this]{require(snapshot()["content_effects"].toArray()==content_before,"Fresh content Generator Undo changed authored effects");observe("new-ui-generator-defaults-mask-and-content");});
   }
   steps.push_back([this]{require(snapshot()["mask_state"].toObject()==applied,"SPMSK apply Redo changed effect tree");select_generator();undo_before=state()["undo_count"].toInt();});
   steps.push_back([this]{auto *level=get<QDoubleSpinBox>("generatorLevel");require(level->isVisible()&&level->isEnabled(),"Imported Generator Properties are not editable");const double value=level->value();level->setValue(value>.6?.39:.71);});
   steps.push_back([this]{edited=snapshot()["mask_state"].toObject();require(edited!=applied&&state()["undo_count"].toInt()==undo_before+1,"Imported Generator edit missing one Undo");action("undoDocument");});
   steps.push_back([this]{require(snapshot()["mask_state"].toObject()==applied,"Imported Generator Undo changed tree");action("redoDocument");});
   steps.push_back([this,test]{require(snapshot()["mask_state"].toObject()==edited,"Imported Generator Redo changed tree");checks.append(QJsonObject{{"preset",test.name},{"folder",test.folder},{"route",test.drop?"row-drop":"double-click"},{"apply_one_undo",true},{"apply_undo_redo_exact_tree",true},{"typed_generator_edit_one_undo",true},{"typed_edit_undo_redo_exact_tree",true}});});
  }
  steps.push_back([this]{capture("imported-spmsk-edited");action("guiSpmskSave");});
  steps.push_back([this]{action("guiSpmskOpen");});
  steps.push_back([this]{action("guiSpmskValidateOpen");capture("imported-spmsk-reopened");});
 }
 void handle_dialogs(){try{for(auto *widget:QApplication::topLevelWidgets()){
  if(auto *dialog=qobject_cast<QDialog *>(widget);dialog&&dialog->isVisible()&&dialog->objectName()=="resourceImportDialog"&&!dialog->property("spmskProofPrepared").toBool()){
   require(!pending_import.isEmpty(),"Unexpected Resource Import dialog");dialog->setProperty("spmskProofPrepared",true);const auto path=qEnvironmentVariable(pending_import=="Rust"?"BTS_BN_SPMSK_RUST":"BTS_BN_SPMSK_EDGE_RUST");require(!path.isEmpty(),"Private SPMSK fixture path missing");dialog->findChild<QLineEdit *>("resourceImportPath")->setText(path);dialog->findChild<QLineEdit *>("resourceImportName")->setText("BN Imported "+pending_import);auto *usage=dialog->findChild<QComboBox *>("resourceImportUsage");require(usage&&usage->currentIndex()==4,"SPMSK automatic Mask generator usage missing");dialog->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(2);
   auto *options=dialog->findChild<QGroupBox *>("spsmBitmapOptions");require(options,"SPMSK bitmap interpretation controls missing");options->setChecked(true);for(const char *name:{"spsmBitmapBytes","spsmBitmapRows","spsmBitmapAlpha","spsmBitmapUv","spsmBitmapSampling"}){auto *choice=options->findChild<QComboBox *>(name);require(choice&&choice->count()>1,"SPMSK explicit interpretation choice missing");choice->setCurrentIndex(1);}
   dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
  }
  if(auto *box=qobject_cast<QMessageBox *>(widget);box&&box->isVisible()){if(auto *discard=box->button(QMessageBox::Discard))discard->click();else throw std::runtime_error(box->text().toStdString());}
 }}catch(const std::exception &error){fail(error.what());}}
 public:SpmskShelfWorkflowDemo(QWidget *window,QWidget *viewport,QString directory,QString source,std::function<QJsonObject()> snapshot_state):QObject(window),w(window),v(viewport),output(std::move(directory)),mesh(std::move(source)),phase(qEnvironmentVariable("BTS_BN_SPMSK_PHASE")),state(std::move(snapshot_state)){
  require(phase=="import"||phase=="shelf-cold"||phase=="packed-cold","Invalid SPMSK diagnostic phase");QDir().mkpath(output);observe("initial-process-catalog");build_steps();timer.setInterval(100);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();dialogs.setInterval(40);connect(&dialogs,&QTimer::timeout,this,[this]{handle_dialogs();});dialogs.start();
 }
};
}
