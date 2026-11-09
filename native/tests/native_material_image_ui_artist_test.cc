// SPDX-License-Identifier: GPL-3.0-or-later
// Linked in place of GuiFixDemo.cc; calls the real separately compiled main.
#include "GuiFixDemo.hh"
#include "NativeMaterialImagePanel.hh"
#include "FillPanel.hh"
#include "PaintPixels.hh"
#include "WorkspaceShell.hh"
#include "ChannelPrecision.hh"
#include "ViewportDemoCapture.hh"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <cmath>
#include <stdexcept>
namespace bts {namespace {
class NativeImageArtist final:public QObject {
 QWidget *window,*viewport;QString output,mesh,library,replacement,project;std::function<QJsonObject()> snapshot;
 QTimer timer,dialogs,heartbeat;QElapsedTimer request_age,heartbeat_age;QJsonArray request_timings;double maximum_ping_ms=0;int request_pings=0;bool measuring=false;int stage=0,ticks=0,after_frame=0,history=0,failures_checked=0;bool busy=false;QJsonArray captures,fixed_samples;QJsonObject oracle,baseline,authored,first_variant;
 QString original_id,variant_id;std::uint64_t recipes=0;double weight=.375;
 template<class T>T *control(const char *name){auto *widget=window->findChild<T *>(name);if(!widget)throw std::runtime_error(std::string("Missing native image control: ")+name);return widget;}
 static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
 void action(const char *name){control<QAction>(name)->trigger();}
 QJsonObject native_layer(const QJsonObject &state){for(const auto &set:state["document"].toObject()["texture_sets"].toArray())for(const auto &entry:set.toObject()["layers"].toArray()){const auto layer=entry.toObject();if(layer["fill_projection"].toObject()["native_material"].isObject())return layer;}throw std::runtime_error("Native image fixture has no native layer");}
 QJsonObject description(const QJsonObject &state){return native_layer(state)["fill_projection"].toObject()["native_material"].toObject();}
 QString source(const QJsonObject &state){return native_layer(state)["fill_projection"].toObject()["material_resource"].toString();}
 QJsonArray authored_fields(const QJsonObject &description){QJsonArray result;for(const auto &entry:description["parameters"].toArray()){const auto p=entry.toObject();result.append(QJsonObject{{"id",p["id"]},{"value",p["value"]},{"ramp",p["ramp"]},{"mapping_transform",p["mapping_transform"]}});}return result;}
 void numeric(QDoubleSpinBox *number,const QString &text){require(number,"Authored native editor missing");number->setFocus();auto *edit=number->findChild<QLineEdit *>();require(edit,"Native numeric line edit missing");QKeyEvent select(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier,"a"),type(QEvent::KeyPress,Qt::Key_0,Qt::NoModifier,text),enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&select);QApplication::sendEvent(edit,&type);QApplication::sendEvent(edit,&enter);}
 QString parameter(const QString &name){for(const auto &entry:description(snapshot())["parameters"].toArray()){const auto p=entry.toObject();if(p["name"]==name)return p["id"].toString();}throw std::runtime_error("Expected combined native source parameter missing");}
 void save(){for(auto *value:window->findChildren<QAction *>()){auto text=value->text();text.remove('&');if(text.startsWith("Save As")){busy=true;value->trigger();busy=false;return;}}throw std::runtime_error("Save As missing");}
 void select_slot(){auto *slots=control<QComboBox>("nativeImageSlot");const auto original=oracle["binding"].toObject();int chosen=-1;for(int i=0;i<slots->count();++i){const auto candidate=slots->itemData(i).toJsonObject();bool same=candidate["consumer"]==original["consumer"]&&candidate["input"]==original["input"]&&candidate["output"]==original["output"];const auto a=candidate["groups"].toArray(),b=original["groups"].toArray();same&=a.size()==b.size();for(int g=0;same&&g<a.size();++g)same=a[g].toObject()["node"]==b[g].toObject()["node"];if(same){chosen=i;break;}}require(chosen>=0,"Exact native Color slot missing");slots->setCurrentIndex(chosen);auto *space=control<QComboBox>("nativeImageSourceSpace");const int srgb=space->findText("sRGB");require(srgb>1,"Explicit named sRGB policy missing");space->setCurrentIndex(srgb);require(control<QPushButton>("nativeImageFile")->isEnabled(),"Supported exact slot unavailable");}
 void finish_measure(const char *name){if(!measuring)return;measuring=false;request_timings.append(QJsonObject{{"stage",name},{"wall_ms",request_age.nsecsElapsed()/1e6},{"ui_ping_max_ms",maximum_ping_ms},{"ui_ping_count",request_pings},{"includes_automated_modal_picker",true}});}
 void file(){select_slot();measuring=true;maximum_ping_ms=0;request_pings=0;request_age.start();heartbeat_age.restart();++stage;QTimer::singleShot(0,this,[this]{busy=true;control<QPushButton>("nativeImageFile")->click();busy=false;});}
 void capture(const char *name,bool replaced){
  auto state=snapshot();require(!state["native_image_loading"].toBool(),"Native candidate still pending");const auto samples=state["native_image_samples"].toArray();require(samples.size()==4&&samples[0].toArray().size()==4,"Real native GPU sample unavailable");const auto pixel=samples[0].toArray();require(samples[1].toArray().size()==4&&samples[2].toArray().size()==4,"Authored scalar/ramp GPU targets unavailable");if(fixed_samples.isEmpty())fixed_samples=samples;for(int channel=1;channel<4;++channel){const auto expected=fixed_samples[channel].toArray(),actual=samples[channel].toArray();require(expected.size()==actual.size(),"Authored input GPU target disappeared");for(int c=0;c<actual.size();++c)require(std::abs(expected[c].toDouble()-actual[c].toDouble())<2e-6,"Replacement changed authored scalar/ramp/Bump GPU output");}const auto colors=oracle["scene_colors"].toArray();double maximum=0;for(int c=0;c<3;++c){const double a=colors[0].toArray()[c].toDouble(),b=colors[1].toArray()[c].toDouble();maximum=std::max(maximum,std::abs(pixel[c].toDouble()-(replaced?a*(1-weight)+b*weight:a)));}require(maximum<3e-5,"Actual native replacement pixels differ from independent OCIO/asymmetric-edge oracle");const auto consumers=state["native_image_dependents"].toArray();require(consumers.size()==2,"Dependent instance fixture is missing");for(const auto &entry:consumers){const auto rgba=entry.toObject()["pixel"].toArray();require(rgba.size()==4,"Dependent instance GPU target not ready");for(int c=0;c<4;++c)require(std::abs(rgba[c].toDouble()-pixel[c].toDouble())<3e-5,"Dependent instance source queue is stale");}require(pixel[3].toDouble()>.999,"Replacement lost native layer coverage");require(authored_fields(description(state))==authored_fields(authored),"Image edit/Reset discarded authored scalar, ramp or Mapping");require(PaintPixels::recipe_materializations()==recipes,"Interactive replacement materialized a CPU recipe");state["stage"]=name;state["maximum_error"]=maximum;captures.append(state);capture_viewport_framebuffer(viewport).save(output+"/"+name+".png");
 }
 void unchanged(){const auto state=snapshot();for(const char *key:{"document","catalog","undo_count","redo_count","dirty","document_revision","native_image_samples","native_image_dependents","native_image_queues"})require(state[key]==baseline[key],"Failed native publication mutated document/catalog/history/dirty/GPU");++failures_checked;}
 void proof(){timer.stop();dialogs.stop();QFile file(output+"/native-image-artist.json");require(file.open(QIODevice::WriteOnly),"Cannot write native image UI proof");file.write(QJsonDocument(QJsonObject{{"request_timings",request_timings},{"production_main_callbacks",true},{"prepublication_failures_checked",failures_checked},{"handler_queue_and_gpu_handles_unchanged",failures_checked==3},{"cold_open",qEnvironmentVariableIsSet("BTS_NATIVE_IMAGE_REOPEN")},{"captures",captures},{"genuine_gpu_oracle",true},{"tolerance",3e-5},{"cpu_interactive_materializations",0}}).toJson());file.close();std::puts("NATIVE_IMAGE_ARTIST_PASS");QTimer::singleShot(0,qApp,&QCoreApplication::quit);}
 void advance(){
  if(busy)return;if(++ticks>2400)throw std::runtime_error("Native image artist deadline expired");const auto state=snapshot();
  if(stage>1&&(state["composite_pending"].toBool()||state["property_pending"].toBool()||(stage!=16&&state["native_image_loading"].toBool())||state["native_image_inspecting"].toBool()||state["render_frames"].toInt()<after_frame))return;
  require(state["open_error"].toString().isEmpty(),"Cold native project Open failed");require(state["save_error"].toString().isEmpty(),"Native project Save failed");if(state["save_pending"].toBool()||state["open_pending"].toBool()||window->property("blendMaterialImporting").toBool())return;
  std::fprintf(stderr,"NATIVE_IMAGE_ARTIST_STAGE %d\n",stage);
  switch(stage){
   case 0:++stage;action("newProject");return;
   case 1:{auto *dialog=control<QDialog>("newProjectDialog");dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(128));++stage;busy=true;control<QPushButton>("createProject")->click();busy=false;break;}
   case 2:control<QComboBox>("channelPrecision0")->setCurrentIndex(int(ChannelPrecision::RGB32F));++stage;break;
   case 3:++stage;QTimer::singleShot(0,this,[this]{action("importNativeBlendMaterialAction");});break;
   case 4:{auto *list=window->findChild<QListWidget *>("blendMaterialList");if(!list||!list->isEnabled())return;QListWidgetItem *chosen=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole)=="Replacement-source")chosen=list->item(i);require(chosen,"Native replacement fixture not listed");list->setCurrentItem(chosen);++stage;control<QPushButton>("importBlendMaterial")->click();break;}
   case 5:{auto *list=control<QListWidget>("shelfAssets");QListWidgetItem *chosen=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->text()=="Replacement-source")chosen=list->item(i);if(!chosen)return;list->setCurrentItem(chosen);list->itemDoubleClicked(chosen);++stage;break;}
   case 6:if(control<QComboBox>("nativeImageSlot")->count()!=5)return;original_id=source(state);numeric(window->findChild<QDoubleSpinBox *>("nativeValue_"+parameter("Metallic")+"_0"),"0.625");++stage;break;
   case 7:numeric(window->findChild<QDoubleSpinBox *>("nativeMapping_"+parameter("Mapping")+"_Offset_0"),"0.2");++stage;break;
   case 8:numeric(window->findChild<QDoubleSpinBox *>("nativeRampPosition_"+parameter("Roughness")+"_0"),"0.125");++stage;break;
   case 9:authored=description(state);action("copyLayerReference");static_cast<WorkspaceShell *>(window)->texture_sets()->setCurrentRow(1);stage=50;break;
   case 50:control<QComboBox>("channelPrecision0")->setCurrentIndex(int(ChannelPrecision::RGB32F));++stage;break;
   case 51:action("pasteLayerInstance");++stage;break;
   case 52:{bool instance=false;for(const auto &entry:state["document"].toObject()["texture_sets"].toArray()[1].toObject()["layers"].toArray())instance|=entry.toObject()["instance"].isObject();require(instance,"Actual cross-set paste did not create an instance");static_cast<WorkspaceShell *>(window)->texture_sets()->setCurrentRow(0);++stage;break;}
   case 53:recipes=PaintPixels::recipe_materializations();capture("authored-before",false);stage=10;save();break;
   case 10:require(!state["dirty"].toBool(),"Save did not establish clean failure baseline");baseline=state;history=state["undo_count"].toInt();replacement=QFileInfo(replacement).absolutePath()+"/invalid.png";file();break;
   case 11:finish_measure("invalid-file");require(!state["native_image_error"].toString().isEmpty(),"Invalid encoded image not reported");unchanged();replacement=qEnvironmentVariable("BTS_NATIVE_IMAGE_REPLACEMENT");window->setProperty("nativeImageFailure","before-reserve");stage=39;file();break;
   case 40:finish_measure("before-reserve");require(!state["native_image_error"].toString().isEmpty(),"Pre-reserve failure not reported");unchanged();window->setProperty("nativeImageFailure","queue-prepared");file();break;
   case 41:finish_measure("queue-prepared");require(!state["native_image_error"].toString().isEmpty(),"Prepared-queue failure not reported");unchanged();window->setProperty("nativeImageFailure","committed-refresh");stage=12;file();break;
   case 13:finish_measure("file-committed");capture("file-committed-refresh-warning",true);require(state["undo_count"].toInt()==history+1&&state["dirty"].toBool()&&state["document_revision"].toInteger()>baseline["document_revision"].toInteger(),"Committed refresh failure lost dirty/revision or one Undo");first_variant=state;variant_id=source(state);++stage;action("undoDocument");break;
   case 14:capture("file-undone",false);require(source(state)==original_id&&state["undo_count"].toInt()==history,"Undo lost original source binding");++stage;action("redoDocument");break;
   case 15:capture("file-redone",true);require(source(state)==variant_id,"Redo changed immutable variant identity");select_slot();control<QPushButton>("nativeImageShelf")->click();++stage;break;
   case 16:{auto *list=control<QListWidget>("shelfAssets");QString original_image;for(const auto &entry:state["catalog"].toArray()){const auto a=entry.toObject();if(a["sha256"]==oracle["source_sha256"])original_image=a["id"].toString();}require(!original_image.isEmpty(),"Original image asset not retained");QListWidgetItem *chosen=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toUuid()==QUuid(original_image))chosen=list->item(i);require(chosen,"Original image resource absent from Shelf");list->setCurrentItem(chosen);list->itemDoubleClicked(chosen);++stage;break;}
   case 17:capture("shelf-replacement",false);require(state["undo_count"].toInt()==history+2&&source(state)!=original_id,"Shelf did not create one immutable source edit");++stage;control<QPushButton>("nativeImageResetAll")->click();break;
   case 18:capture("reset-all-original",false);require(source(state)==original_id&&state["undo_count"].toInt()==history+3,"Reset did not restore first original with one Undo");++stage;action("undoDocument");break;
   case 19:capture("reset-undone",false);require(source(state)!=original_id,"Reset Undo lost variant source");++stage;action("undoDocument");break;
   case 20:capture("shelf-undone",true);require(source(state)==variant_id,"Shelf Undo lost first variant");++stage;save();break;
   case 21:{QFile expected(output+"/expected-cold.json");require(expected.open(QIODevice::WriteOnly),"Cannot write cold descriptor oracle");expected.write(QJsonDocument(QJsonObject{{"authored",authored},{"fixed_samples",fixed_samples},{"source",source(state)},{"original",original_id}}).toJson());require(QFileInfo(project).size()>0,"Packed Save did not finish");proof();return;}
   case 30:++stage;busy=true;action("openProject");busy=false;break;
   case 31:if(!state["has_document"].toBool()||control<QComboBox>("nativeImageSlot")->count()!=5)return;capture("cold-packed-open",true);require(source(state)==variant_id,"Cold packed Open changed variant identity");++stage;control<QPushButton>("nativeImageResetAll")->click();break;
   case 32:capture("cold-reset-original",false);require(source(state)==original_id,"Cold packed Reset lost FIRST original");++stage;action("undoDocument");break;
   case 33:capture("cold-reset-undone",true);proof();return;
  }
  after_frame=snapshot()["render_frames"].toInt()+8;
 }
 public:
 NativeImageArtist(QWidget *w,QWidget *v,QString directory,QString source,std::function<QJsonObject()> state):QObject(w),window(w),viewport(v),output(std::move(directory)),mesh(std::move(source)),library(qEnvironmentVariable("BTS_NATIVE_IMAGE_LIBRARY")),replacement(qEnvironmentVariable("BTS_NATIVE_IMAGE_REPLACEMENT")),project(qEnvironmentVariable("BTS_NATIVE_IMAGE_PROJECT")),snapshot(std::move(state)){
  // Match both production ownership changes: WorkspaceShell reparents the
  // channel subtree, while MaskFill prefixes every named descendant.
  { QWidget other;FillPanel fill;auto *original=fill.native_image_panel();
    require(original,"Constructed FillPanel has no image panel");
    fill.channel_panel()->setParent(&other);
    for(auto *child:fill.channel_panel()->findChildren<QObject *>())
      if(!child->objectName().isEmpty())child->setObjectName("maskSpatial_"+child->objectName());
    require(fill.native_image_panel()==original,"Reparent/name-prefix lost exact owned image panel");
    fill.set_color_spaces({"sRGB"});fill.channel_panel()->setParent(&fill);
  }
  QFile input(qEnvironmentVariable("BTS_NATIVE_IMAGE_ORACLE"));require(input.open(QIODevice::ReadOnly),"Native image fixture oracle missing");oracle=QJsonDocument::fromJson(input.readAll()).object();weight=oracle["binding"].toObject()["input"]=="Color2"?.125:.375;window->setProperty("nativeImageSample",true);
  if(qEnvironmentVariableIsSet("BTS_NATIVE_IMAGE_REOPEN")){stage=30;QFile saved(qEnvironmentVariable("BTS_NATIVE_IMAGE_EXPECTED"));require(saved.open(QIODevice::ReadOnly),"Cold UI oracle missing");const auto expected=QJsonDocument::fromJson(saved.readAll()).object();authored=expected["authored"].toObject();fixed_samples=expected["fixed_samples"].toArray();variant_id=expected["source"].toString();original_id=expected["original"].toString();recipes=PaintPixels::recipe_materializations();}
  heartbeat_age.start();connect(&heartbeat,&QTimer::timeout,this,[this]{const double elapsed=heartbeat_age.nsecsElapsed()/1e6;heartbeat_age.restart();if(measuring){maximum_ping_ms=std::max(maximum_ping_ms,elapsed);++request_pings;}});heartbeat.start(16);
  connect(&timer,&QTimer::timeout,this,[this]{try{advance();}catch(const std::exception &e){timer.stop();dialogs.stop();std::fprintf(stderr,"NATIVE_IMAGE_ARTIST_FAIL stage=%d %s\n",stage,e.what());QCoreApplication::exit(1);}});timer.start(100);
  connect(&dialogs,&QTimer::timeout,this,[this]{try{for(auto *widget:QApplication::topLevelWidgets()){
   if(auto *file=dynamic_cast<QFileDialog *>(widget);file&&file->isVisible()){
    const bool save=file->windowTitle().contains("Save project"),open=file->windowTitle().contains("Open project");QString path;if(save||open)path=project;else if(file->objectName()=="blendMaterialFileDialog")path=library;else if(file->objectName()=="nativeImageFileDialog")path=replacement;if(!path.isEmpty()){const auto key=file->windowTitle()+path;if(file->property("nativeImageTestSelection").toString()!=key){file->setDirectory(QFileInfo(path).absolutePath());file->setProperty("nativeImageTestSelection",key);}auto *name=file->findChild<QLineEdit *>("fileNameEdit");require(name,"Native chooser filename editor missing");if(name->text()!=path)name->setText(path);QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);}
   }
   if(auto *box=qobject_cast<QMessageBox *>(widget);box&&box->isVisible()){if(box->icon()==QMessageBox::Critical)throw std::runtime_error(box->text().toStdString());if(auto *discard=box->button(QMessageBox::Discard))discard->click();else if(auto *yes=box->button(QMessageBox::Yes))yes->click();}
  }}catch(const std::exception &e){std::fprintf(stderr,"NATIVE_IMAGE_CHOOSER_FAIL %s\n",e.what());QCoreApplication::exit(1);}});dialogs.start(30);
 }
};
}
void start_gui_fix_demo(QWidget *workspace,QWidget *viewport,const QString &directory,const QString &mesh,int,std::function<QJsonObject()> state){new NativeImageArtist(workspace,viewport,directory,mesh,std::move(state));}
}
