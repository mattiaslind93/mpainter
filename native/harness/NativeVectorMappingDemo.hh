// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <array>
#include <utility>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
namespace bts {
// Diagnostic actor only. Every mutation uses actual visible production controls.
class NativeVectorMappingDemo final:public QObject {
 QWidget *w_,*v_;QString out_,mesh_,operation_,project_;std::function<QJsonObject()> state_;QTimer timer_;QElapsedTimer elapsed_;
 struct Step{QString name;std::function<bool()> run;bool modal=false;};std::vector<Step> steps_;int step_=0,frame_=0;bool busy_=false,failed_=false,cold_=false,file_wait_=false;QElapsedTimer new_admission_;QString new_control_;QJsonObject new_controls_;QElapsedTimer number_admission_;QString number_control_;QJsonObject number_controls_;
 QJsonArray checks_,pickers_,checkpoints_;QJsonObject baseline_,before_,edited_,saved_,expected_,picker_;QString parameter_;double offset_=0,scale_=1;
 static void require(bool ok,const QString &why){if(!ok)throw std::runtime_error(why.toStdString());}
 void write(const QString &name,const QJsonObject &value){QFile f(out_+"/"+name);require(f.open(QIODevice::WriteOnly),"Cannot write owned proof");f.write(QJsonDocument(value).toJson());}
 void fail(const QString &why){if(failed_)return;failed_=true;timer_.stop();QJsonObject s;try{s=state_();}catch(...){}try{write("failure.json",{{"error",why},{"step",step_},{"stage",step_<int(steps_.size())?steps_[step_].name:QString()},{"state",s},{"before",before_},{"edited",edited_},{"picker",picker_},{"new_controls",new_controls_},{"number_controls",number_controls_},{"pickers",pickers_},{"checks",checks_}});capture_demo_window(w_,v_).save(out_+"/failure.png");}catch(...){}std::fprintf(stderr,"BTS_AW_VECTOR_UI_FAIL step=%d %s\n",step_,qPrintable(why));QCoreApplication::exit(1);}
 template<class F>void protect(F f){try{f();}catch(const std::exception &e){fail(QString::fromUtf8(e.what()));}catch(...){fail("Nonstandard actor exception");}}
 template<class F>void later(int ms,F f){QTimer::singleShot(ms,this,[this,f]{protect(f);});}
 template<class T>T *get(const QString &name){auto *p=w_->findChild<T *>(name);require(p,"Missing actual control "+name);return p;}
 static void key(QWidget *widget,int value,Qt::KeyboardModifiers mods=Qt::NoModifier){for(auto type:{QEvent::KeyPress,QEvent::KeyRelease}){QKeyEvent e(type,value,mods);QApplication::sendEvent(widget,&e);}}
 static void pointer(QWidget *widget,QEvent::Type type,QPoint point){QMouseEvent e(type,point,widget->mapToGlobal(point),Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(widget,&e);}
 static void click(QWidget *widget,QPoint point){pointer(widget,QEvent::MouseButtonPress,point);pointer(widget,QEvent::MouseButtonRelease,point);}
 void reveal(QWidget *widget){for(auto *p=widget->parentWidget();p;p=p->parentWidget())if(auto *scroll=qobject_cast<QScrollArea *>(p))scroll->ensureWidgetVisible(widget);require(widget->isVisible()&&widget->isEnabled()&&!widget->visibleRegion().isEmpty(),"Actual control not visible/enabled: "+widget->objectName());}
 void text(QLineEdit *edit,const QString &value){reveal(edit);require(!edit->isReadOnly(),"Actual text control is read-only: "+edit->objectName());edit->setFocus(Qt::OtherFocusReason);require(edit->hasFocus(),"Actual text control lacks keyboard focus: "+edit->objectName());QApplication::clipboard()->setText(value);key(edit,Qt::Key_A,Qt::ControlModifier);key(edit,Qt::Key_V,Qt::ControlModifier);require(edit->text()==value,"Natural full-path/value paste failed");}
 // New is a real WindowModal dialog; combo closure/file-picker return can
 // precede its WindowActivate. Poll actual ownership/focus, never force Qt's
 // active-window model or make its deliberately read-only mesh field editable.
 bool new_ready(const QString &control){
  auto *dialog=get<QDialog>("newProjectDialog");auto *target=get<QWidget>(control);
  if(new_control_!=control){new_control_=control;new_admission_.start();}
  const auto describe=[](QWidget *widget){return widget?QJsonObject{{"class",widget->metaObject()->className()},{"name",widget->objectName()},{"title",widget->windowTitle()}}:QJsonObject{};};
  new_controls_=QJsonObject{{"control",control},{"elapsed_ms",new_admission_.elapsed()},{"visible",dialog->isVisible()},{"active",dialog->isActiveWindow()},{"active_window",describe(QApplication::activeWindow())},{"active_modal",describe(QApplication::activeModalWidget())},{"active_popup",describe(QApplication::activePopupWidget())},{"focus_widget",describe(QApplication::focusWidget())},{"mesh_read_only",get<QLineEdit>("meshPath")->isReadOnly()}};
  require(new_admission_.elapsed()<5000,"Actual New dialog did not activate/focus within5s; see new_controls");
  if(!dialog->isVisible()||QApplication::activeModalWidget()!=dialog||QApplication::activePopupWidget())return false;
  dialog->activateWindow();
  if(!dialog->isActiveWindow()||QApplication::activeWindow()!=dialog)return false;
  reveal(target);target->setFocus(Qt::OtherFocusReason);new_controls_["control_focus"]=target->hasFocus();new_controls_["focus_widget"]=describe(QApplication::focusWidget());
  if(!target->hasFocus())return false;
  checks_.append(QJsonObject{{"stage","new-control-admission"},{"control",new_controls_}});return true;
 }
 void action(const QString &name){auto *a=get<QAction>(name);require(a->isEnabled(),"Disabled action "+name);a->trigger();}
 void combo(QComboBox *box,int index){reveal(box);require(index>=0&&index<box->count(),"Combo target absent");box->setFocus();key(box,Qt::Key_F4);auto *view=box->view();const auto model=box->model()->index(index,box->modelColumn(),box->rootModelIndex());view->scrollTo(model);click(view->viewport(),view->visualRect(model).center());require(box->currentIndex()==index,"Actual combo pointer selection failed");}
 void tab(int index){auto *tabs=get<QTabWidget>("layerSettingsTabs");auto *bar=tabs->tabBar();reveal(bar);click(bar,bar->tabRect(index).center());require(tabs->currentIndex()==index,"Actual settings tab click failed");}
 void precision(int channel,int index){const int previous=get<QTabWidget>("layerSettingsTabs")->currentIndex();tab(1);combo(get<QComboBox>(QString("channelPrecision%1").arg(channel)),index);tab(previous);}
 // QAbstractSpinBox owns keyboard focus through its editor's focus proxy.
 // Admit the actual active owner before any before-state capture or typing;
 // the displayed editor retains its XYZ prefix while cleanText is numeric.
 bool number_ready(const QString &suffix){
  const auto name="nativeMapping_"+parameter_+"_"+suffix;QPointer<QDoubleSpinBox> spin=get<QDoubleSpinBox>(name);QPointer<QLineEdit> edit=spin->findChild<QLineEdit *>();require(edit,"Actual numeric line editor absent");reveal(spin);reveal(edit);
  if(number_control_!=name){number_control_=name;number_admission_.start();}
  const auto describe=[](QWidget *p){return p?QJsonObject{{"class",p->metaObject()->className()},{"name",p->objectName()},{"title",p->windowTitle()}}:QJsonObject{};};
  auto record=[&]{number_controls_=QJsonObject{{"control",name},{"elapsed_ms",number_admission_.elapsed()},{"owner",describe(spin?spin->window():nullptr)},{"owner_active",spin&&spin->window()->isActiveWindow()},{"active_window",describe(QApplication::activeWindow())},{"active_modal",describe(QApplication::activeModalWidget())},{"active_popup",describe(QApplication::activePopupWidget())},{"focus_widget",describe(QApplication::focusWidget())},{"spin_alive",bool(spin)},{"editor_alive",bool(edit)},{"spin_focus",spin&&spin->hasFocus()},{"editor_focus",edit&&edit->hasFocus()},{"editor_focus_proxy",describe(edit?edit->focusProxy():nullptr)},{"spin_focus_proxy",describe(spin?spin->focusProxy():nullptr)},{"read_only",!edit||edit->isReadOnly()},{"value",spin?spin->value():0},{"text",edit?edit->text():QString()}};};
  record();require(number_admission_.elapsed()<5000,"Actual numeric owner/focus admission exceeded5s; see number_controls");require(!edit->isReadOnly(),"Actual numeric editor is read-only");
  if(QApplication::activeModalWidget()||QApplication::activePopupWidget())return false;
  auto *owner=spin->window();owner->activateWindow();if(!owner->isActiveWindow()||QApplication::activeWindow()!=owner)return false;
  spin->setFocus(Qt::OtherFocusReason);record();require(spin&&edit,"Numeric controls were rebuilt during focus admission");return spin->hasFocus()&&edit->hasFocus();
 }
 void number(const QString &suffix,double value){
  const auto name="nativeMapping_"+parameter_+"_"+suffix;QPointer<QDoubleSpinBox> spin=get<QDoubleSpinBox>(name);QPointer<QLineEdit> edit=spin->findChild<QLineEdit *>();require(edit&&spin->hasFocus()&&edit->hasFocus(),"Actual numeric keyboard owner lost focus before typing");const auto numeric=QString::number(value,'f',spin->decimals());
  QApplication::clipboard()->setText(numeric);for(const auto &gesture:std::array<std::pair<int,Qt::KeyboardModifiers>,3>{{{Qt::Key_A,Qt::ControlModifier},{Qt::Key_V,Qt::ControlModifier},{Qt::Key_Return,Qt::NoModifier}}}){for(auto type:{QEvent::KeyPress,QEvent::KeyRelease}){require(spin&&edit,"Numeric controls rebuilt during keyboard input");QKeyEvent event(type,gesture.first,gesture.second);QApplication::sendEvent(spin,&event);}if(gesture.first==Qt::Key_V){require(spin&&edit&&spin->cleanText()==numeric&&edit->text()==spin->prefix()+numeric+spin->suffix(),"Natural numeric paste changed prefix/value text");}}
  require(get<QDoubleSpinBox>(name)->value()==value,"Actual numeric edit was not committed");number_control_.clear();
 }
 void press(int value){v_->setFocus(Qt::OtherFocusReason);key(v_,value);}
 static QString hash(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot hash actual checkpoint");QCryptographicHash h(QCryptographicHash::Sha256);require(h.addData(&f),"Cannot stream checkpoint hash");return QString::fromLatin1(h.result().toHex());}
 // Proven BM picker route: actual WindowActivate, focus, whole-path paste and
 // focused Space. No accept(), selectFile(), model mutation or direct slot.
 void picker_input(QPointer<QFileDialog> owner,bool save,const QString &path,QElapsedTimer clock,int phase=0,std::shared_ptr<QJsonObject> record={}){
  require(owner&&owner->isVisible()&&QApplication::activeModalWidget()==owner.data(),"Actual picker lost modal ownership");require(clock.elapsed()<5000,"Actual picker activation/input exceeded5s");
  if(!owner->isActiveWindow()){owner->activateWindow();later(10,[=,this]{picker_input(owner,save,path,clock,phase,record);});return;}
  auto *field=owner->findChild<QLineEdit *>("fileNameEdit");require(field&&field->isVisible(),"Actual filename editor absent");
  if(phase==0){text(field,path);later(0,[=,this]{picker_input(owner,save,path,clock,1);});return;}
  require(field->text()==path,"Actual picker filename changed");if(auto *c=field->completer();c&&c->popup()->isVisible())key(c->popup(),Qt::Key_Escape);require(!field->completer()||!field->completer()->popup()->isVisible(),"Actual completer did not close");
  require(owner->selectedFiles()==QStringList{path},"Actual selectedFiles is not the exact expected file");require(owner->fileMode()==(save?QFileDialog::AnyFile:QFileDialog::ExistingFile)&&owner->acceptMode()==(save?QFileDialog::AcceptSave:QFileDialog::AcceptOpen),"Actual picker mode differs");
  auto *box=owner->findChild<QDialogButtonBox *>();require(box,"Actual picker button box absent");QPointer<QPushButton> button=box->button(save?QDialogButtonBox::Save:QDialogButtonBox::Open);require(button&&button->isVisible()&&button->isEnabled(),"Actual picker button unavailable");
  record=std::make_shared<QJsonObject>(QJsonObject{{"save",save},{"path",path},{"selected_files",QJsonArray::fromStringList(owner->selectedFiles())},{"active",owner->isActiveWindow()},{"field_focus",field->hasFocus()},{"pressed",0},{"released",0},{"clicked",0},{"accepted",0}});
  for(const auto *signal:{"pressed","released","clicked","accepted"})(*record)[signal]=0;
  auto count=[record](const char *name){(*record)[name]=(*record)[name].toInt()+1;};connect(button.data(),&QPushButton::pressed,this,[count]{count("pressed");});connect(button.data(),&QPushButton::released,this,[count]{count("released");});connect(button.data(),&QPushButton::clicked,this,[count]{count("clicked");});connect(owner.data(),&QDialog::accepted,this,[count]{count("accepted");});
  button->setFocus(Qt::OtherFocusReason);require(button->hasFocus()&&!button->isDown(),"Actual picker action lacks idle focus");key(button,Qt::Key_Space);picker_=*record;
  auto *poll=new QTimer(this);connect(poll,&QTimer::timeout,this,[=,this]{protect([&]{picker_=*record;if(owner&&owner->isVisible()){require(clock.elapsed()<5000,"Actual picker did not close after Space");return;}for(const auto *signal:{"pressed","released","clicked","accepted"})require((*record)[signal].toInt()==1,"Actual picker must emit exactly one "+QString(signal));pickers_.append(*record);file_wait_=false;poll->stop();poll->deleteLater();});});poll->start(10);
 }
 void file_dialog(bool save,const QString &path,const std::function<void()> &open){require(!file_wait_,"A normal picker is already pending");file_wait_=true;later(30,[=,this]{auto *dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget());require(dialog,"Actual file chooser absent");QElapsedTimer clock;clock.start();dialog->activateWindow();picker_input(dialog,save,path,clock);});open();}
 void project_file(bool save){file_dialog(save,project_,[=,this]{if(!save){action("openProject");return;}QAction *selected=nullptr;for(auto *a:get<QMenu>("projectFileMenu")->actions()){auto label=a->text();label.remove('&');if(label=="Save As…")selected=a;}require(selected&&selected->isEnabled(),"Actual Save As unavailable");selected->trigger();});}
 static QJsonObject native(const QJsonObject &s){return s["layer"].toObject()["fill_projection"].toObject()["native_material"].toObject();}
 QJsonObject parameter(const QJsonObject &s){for(auto p:native(s)["parameters"].toArray())if(p.toObject()["name"]=="Mapping B")return p.toObject();throw std::runtime_error("Retained Mapping B parameter absent");}
 QJsonObject probe(){w_->setProperty("awVectorProbe",true);QJsonObject s;try{s=state_();}catch(...){w_->setProperty("awVectorProbe",false);throw;}w_->setProperty("awVectorProbe",false);require(s["probe_ready"].toBool()&&s["gpu"].toArray().size()==2,"Actual Color/Roughness GPU bindings not ready");return s;}
 void invariant(const QJsonObject &s){for(const auto *name:{"source_id","source_sha256","source_description","lower","cpu_materializations","cpu_layer_compositions","compiled_shaders"})require(s[name]==baseline_[name],"Instance edit changed immutable source/lower/CPU/shader state: "+QString(name));const auto a=s["queues"].toObject()["bj_native_gpu"].toObject(),b=baseline_["queues"].toObject()["bj_native_gpu"].toObject();require(!a.isEmpty()&&a["sources"]==b["sources"]&&a["programs"]==b["programs"],"Instance edit changed shared native source/program cache");}
 void oracle(const QJsonObject &s,double offset,double scale){const auto packets=s["gpu"].toArray();require(packets.size()==2,"Missing GPU packets");const auto values=packets[1].toObject()["values"].toArray();require(values.size()==3,"Missing roughness samples");double error=0;for(int i=0;i<3;++i){const double u=(32+32*i+.5)/128.;const double original=operation_=="Add"?u-.125:u*.5;const double wanted=original*scale+offset;auto rgba=values[i].toArray();require(rgba.size()==4,"GPU RGBA absent");for(int c=0;c<4;++c){const double actual=rgba[c].toDouble(NAN),expected=c==3?1:wanted;require(std::isfinite(actual),"Nonfinite GPU value");error=std::max(error,std::abs(actual-expected));} }require(error<3e-5,"Independent retained VectorMath then POINT oracle failed");checks_.append(QJsonObject{{"stage","analytic"},{"operation",operation_},{"offset",offset},{"scale",scale},{"max_error",error},{"bound",3e-5}});}
 void capture(const QString &name,const QJsonObject &s){checks_.append(QJsonObject{{"stage",name},{"state",s}});require(capture_demo_window(w_,v_).save(out_+"/"+operation_+"-"+name+".png"),"Cannot save owned UI capture");}
 bool edit(const QString &suffix,double value){if(!number_ready(suffix))return false;before_=probe();number(suffix,value);return true;}
 void edited(double offset,double scale){edited_=probe();invariant(edited_);oracle(edited_,offset,scale);require(edited_["undo_count"].toInt()==before_["undo_count"].toInt()+1&&native(edited_)!=native(before_),"Actual field edit must change selected mapping with exactly one Undo");require(edited_["gpu"].toArray()[0]==before_["gpu"].toArray()[0]&&edited_["gpu"].toArray()[1]!=before_["gpu"].toArray()[1],"Mapping B edit must change only Roughness, retaining Mapping A fanout");capture("edited",edited_);action("undoDocument");}
 void undone(){const auto s=probe();invariant(s);require(s["layer"]==before_["layer"]&&s["gpu"]==before_["gpu"]&&s["undo_count"]==before_["undo_count"],"One Undo did not restore exact authored mapping/GPU");action("redoDocument");}
 void redone(){const auto s=probe();invariant(s);require(s["layer"]==edited_["layer"]&&s["gpu"]==edited_["gpu"],"Redo did not restore exact selected mapping/GPU");}
 void open_check(const QJsonObject &saved){const auto s=probe();for(const auto *name:{"set","layer_id","layer","lower","source_id","source_sha256","source_description","gpu","tiles","width","height"})require(s[name]==saved[name],"Actual packed Open changed "+QString(name));oracle(s,.125,.75);capture(cold_?"cold-opened":"reopened",s);write("witness-"+operation_+".json",{{"operation",operation_},{"project",project_},{"project_sha256",hash(project_)},{"state",s},{"scope","Real Open; persistent IDs/source bytes/instance/lower metadata checked before explicit settled GPU probes. No pre-render data-first claim."}});checkpoints_.append(QJsonObject{{"operation",operation_},{"project",project_},{"project_sha256",hash(project_)},{"state",s}});}
 void add(const QString &name,std::function<void()> f,bool modal=false){steps_.push_back({name,[f]{f();return true;},modal});}
 void bind_steps(){add("inspect-roughness",[this]{press(Qt::Key_M);press(Qt::Key_C);press(Qt::Key_C);press(Qt::Key_C);require(state_()["inspection"].toInt(-1)==2,"Real Roughness inspection failed");});add("inspect-color",[this]{press(Qt::Key_M);press(Qt::Key_C);require(state_()["inspection"].toInt(-1)==0,"Real Color inspection failed");});}
 void shelf(){auto *list=get<QListWidget>("shelfAssets");QListWidgetItem *found=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->text()=="AW Vector "+operation_)found=list->item(i);require(found,"Imported native source absent from actual Shelf");list->scrollToItem(found);const auto p=list->visualItemRect(found).center();require(list->viewport()->rect().contains(p),"Shelf asset is not visible");click(list->viewport(),p);pointer(list->viewport(),QEvent::MouseButtonDblClick,p);pointer(list->viewport(),QEvent::MouseButtonRelease,p);}
 void warm_steps(const QString &operation){add("operation-"+operation,[=,this]{operation_=operation;project_=out_+"/"+operation+".blend";baseline_={};new_control_.clear();new_controls_={};action("newProject");});
  steps_.push_back({"new-template",[this]{if(!new_ready("projectTemplate"))return false;combo(get<QComboBox>("projectTemplate"),get<QComboBox>("projectTemplate")->findData(":pbr"));return true;},true});
  steps_.push_back({"new-mesh-picker",[this]{if(!new_ready("selectMesh"))return false;require(get<QLineEdit>("meshPath")->isReadOnly(),"Actual New mesh path must remain read-only");file_dialog(false,mesh_,[this]{auto *button=get<QPushButton>("selectMesh");click(button,button->rect().center());});return true;},true});
  steps_.push_back({"new-resolution",[this]{if(!new_ready("documentResolution"))return false;require(get<QLineEdit>("meshPath")->isReadOnly()&&get<QLineEdit>("meshPath")->text()==mesh_,"Real Select mesh picker did not publish the exact read-only path");combo(get<QComboBox>("documentResolution"),get<QComboBox>("documentResolution")->findData(128));return true;},true});
  steps_.push_back({"new-create",[this]{if(!new_ready("createProject"))return false;require(get<QComboBox>("projectTemplate")->currentData().toString()==":pbr"&&get<QComboBox>("documentResolution")->currentData().toInt()==128&&get<QLineEdit>("meshPath")->text()==mesh_,"Actual New controls changed before Create");auto *button=get<QPushButton>("createProject");click(button,button->rect().center());return true;},true});
  add("precision-color",[this]{precision(0,8);});add("precision-roughness",[this]{precision(2,7);});
  add("import-picker",[this]{file_dialog(false,qEnvironmentVariable("BTS_AW_VECTOR_SOURCE"),[this]{action("importNativeBlendMaterialAction");});});
  steps_.push_back({"select-import",[this]{auto *list=w_->findChild<QListWidget *>("blendMaterialList");if(!list||!list->isEnabled())return false;QListWidgetItem *item=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toString()=="AW Vector "+operation_)item=list->item(i);require(item,"Own material not listed");list->scrollToItem(item);click(list->viewport(),list->visualItemRect(item).center());auto *button=get<QPushButton>("importBlendMaterial");require(button->isEnabled(),"Import actual selection not enabled");click(button,button->rect().center());return true;},true});
  add("apply-lower",[this]{shelf();});add("apply-upper",[this]{shelf();});bind_steps();
  add("identity",[this]{baseline_=probe();require(baseline_["tiles"].toArray()==QJsonArray{1001}&&baseline_["width"].toInt()==128&&baseline_["height"].toInt()==128,"Actual fixture dimensions/tiles differ");const auto p=parameter(baseline_);require(p["mapping"].toBool()&&p["editable"].toBool()&&p["source_node"]=="ShaderNodeVectorMath"&&p["source_socket"]=="Vector"&&!p.contains("mapping_transform"),"Actual retained VectorMath identity capability absent");parameter_=p["id"].toString();int siblings=0;for(const auto &x:baseline_["lower"].toArray())if(x.toObject()["fill_projection"].toObject()["material_resource"]==baseline_["source_id"])++siblings;require(siblings==1,"Two actual native Fill instances must share the retained source");oracle(baseline_,0,1);capture("identity",baseline_);});
  steps_.push_back({"offset-input",[this]{return edit("Offset_0",.125);}});add("offset-check",[this]{edited(.125,1);});add("offset-undo",[this]{undone();});add("offset-redo",[this]{redone();});
  steps_.push_back({"scale-input",[this]{return edit("Scale_0",.75);}});add("scale-check",[this]{edited(.125,.75);});add("scale-undo",[this]{undone();});add("scale-redo",[this]{redone();});
  add("reset-input",[this]{before_=probe();auto *button=get<QPushButton>("nativeMappingReset_"+parameter_);reveal(button);click(button,button->rect().center());});add("reset-check",[this]{const auto s=probe();invariant(s);oracle(s,0,1);require(!parameter(s).contains("mapping_transform")&&s["gpu"]==baseline_["gpu"]&&s["undo_count"].toInt()==before_["undo_count"].toInt()+1,"Reset did not restore exact identity with one Undo");action("undoDocument");});add("reset-undo",[this]{const auto s=probe();require(s["layer"]==before_["layer"]&&s["gpu"]==before_["gpu"],"Reset Undo did not restore authored transform/GPU");});
  add("save-as",[this]{saved_=probe();project_file(true);});add("saved",[this]{require(QFileInfo::exists(project_)&&state_()["document_path"].toString()==project_,"Actual Save As failed");project_file(false);});bind_steps();add("reopened",[this]{open_check(saved_);});
 }
 void advance(){if(busy_||failed_||file_wait_)return;busy_=true;protect([&]{require(elapsed_.elapsed()<240000,"AW UI deadline240s exceeded");const auto s=state_();for(const auto *name:{"shader_failures","open_error","save_error","material_error","workspace_error"})require(s[name].toString().isEmpty(),"Production error "+QString(name)+": "+s[name].toString());if(step_>=int(steps_.size())){timer_.stop();write("native-vector-ui.json",{{"complete",true},{"cold",cold_},{"pid",QCoreApplication::applicationPid()},{"checks",checks_},{"pickers",pickers_},{"checkpoints",checkpoints_},{"scope","Actual visible native extra-POINT controls; Add/Multiply, independent Roughness oracle, unchanged Color/lower/source/cache, one Undo/Redo, Reset, normal packed Save/Open. Explicit GPU probes only after settling."}});std::puts("BTS_AW_VECTOR_UI_PASS");QCoreApplication::quit();return;}const auto &step=steps_[step_];if(!step.modal&&(QApplication::activeModalWidget()||QApplication::activePopupWidget()))return;if(!step.modal&&step.name!="cold-open"&&!step.name.startsWith("operation-")&&!s["has_document"].toBool())return;if(!step.modal&&(s["composite_pending"].toBool()||s["property_pending"].toBool()||s["native_pending"].toBool()||s["open_pending"].toBool()||s["save_pending"].toBool()||s["import_pending"].toBool()||s["texture_loading"].toBool()||s["layer_thumbnails_pending"].toBool()||s["render_frames"].toInt()<frame_))return;std::printf("BTS_AW_VECTOR_UI step=%d %s\n",step_,qPrintable(step.name));std::fflush(stdout);if(step.run()){++step_;frame_=state_()["render_frames"].toInt()+3;}});busy_=false;}
 public:NativeVectorMappingDemo(QWidget *w,QWidget *v,const QString &out,const QString &mesh,std::function<QJsonObject()> state):QObject(w),w_(w),v_(v),out_(out),mesh_(mesh),state_(std::move(state)){
  QDir().mkpath(out_);w_->resize(1450,950);w_->activateWindow();w_->raise();cold_=qEnvironmentVariableIsSet("BTS_AW_VECTOR_COLD");
  if(cold_){QFile f(qEnvironmentVariable("BTS_AW_VECTOR_EXPECTED"));require(f.open(QIODevice::ReadOnly),"Cold witness unavailable");expected_=QJsonDocument::fromJson(f.readAll()).object();operation_=expected_["operation"].toString();project_=expected_["project"].toString();require(hash(project_)==expected_["project_sha256"].toString(),"Packed cold input changed");add("cold-open",[this]{project_file(false);});bind_steps();add("cold-check",[this]{open_check(expected_["state"].toObject());});}
  else{warm_steps("Add");warm_steps("Multiply");}elapsed_.start();connect(&timer_,&QTimer::timeout,this,[this]{advance();});timer_.start(40);
 }
};
}
