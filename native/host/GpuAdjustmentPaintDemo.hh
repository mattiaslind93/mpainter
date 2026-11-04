#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QAbstractItemView>
#include <QCompleter>
#include <QClipboard>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QRegularExpression>
#include <map>
#include <memory>
#include <set>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <vector>
namespace bts {
// Only real authoring controls and pointer events. Read-only diagnostics are
// gated after the measured interaction; backend numerical/export proof is separate.
class GpuAdjustmentPaintDemo final:public QObject {
 QWidget *w_,*v_;QString out_,mesh_;std::function<QJsonObject()> state_;QTimer timer_;
 int step_=0,ticks_=0,frame_=0,drag_step_=-1;bool busy_=false,refusal_=false;QPointF first_,last_;QString drag_name_;QJsonArray captures_,strokes_,checks_;bool flatten_wait_=false;QJsonObject flatten_before_;
 bool cycles_wait_=false,cycles_published_=false;int cycles_done_=-1;QString cycles_sample_;QJsonObject cycles_expected_,cycles_before_,cycles_after_,cycles_entry_;QJsonArray cycles_packets_;QImage cycles_previous_,cycles_first_;QJsonObject cycles_previous_state_;std::map<std::pair<int,int>,qulonglong> cycles_revisions_;
 QString clone_packed_sha_;QJsonArray picker_events_;
 QJsonObject picker_activation_,picker_diagnostic_,last_probe_,before_,painted_,pointer_transfers_,saved_,refused_,add_before_,toggle_before_;QUuid adjustment_;std::vector<std::function<void()>> steps_;
 [[noreturn]]void fail(const QString &message){QFile file(out_+"/failure.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"step",step_},{"drag_step",drag_step_},{"error",message},{"state",state_()},{"last_probe",last_probe_},{"before",before_},{"painted",painted_},{"project_picker",picker_diagnostic_},{"project_pickers",picker_events_},{"captures",captures_},{"checks",checks_}}).toJson());qFatal("BM UI step%d: %s",step_,qPrintable(message));}
 void require(bool ok,const QString &message){if(!ok)fail(message);}
 template<class T>T *get(const char *name){auto *p=w_->findChild<T *>(name);require(p,QString("Missing actual control ")+name);return p;}
 void action(const char *name){auto *a=get<QAction>(name);require(a->isEnabled(),QString("Disabled action ")+name);a->trigger();}
 static void mouse(QWidget *target,QEvent::Type kind,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(kind,point,target->mapToGlobal(point.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(target,&event);}
 static void click(QWidget *target,QPoint point){mouse(target,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);mouse(target,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);}
 static void key(QWidget *target,int value,bool down=true,Qt::KeyboardModifiers mods=Qt::NoModifier,const QString &text={}){QKeyEvent event(down?QEvent::KeyPress:QEvent::KeyRelease,value,mods,text);QApplication::sendEvent(target,&event);}
 void press(int value){v_->setFocus(Qt::OtherFocusReason);key(v_,value);key(v_,value,false);}
 void reveal(QWidget *widget){for(auto *parent=widget->parentWidget();parent;parent=parent->parentWidget())if(auto *scroll=qobject_cast<QScrollArea *>(parent))scroll->ensureWidgetVisible(widget);require(widget->isVisible()&&widget->isEnabled(),"Actual control is not visible/enabled: "+widget->objectName());}
 void tool(const char *name){auto *button=get<QToolButton>(name);reveal(button);click(button,QPoint(button->width()/3,button->height()/2));}
 void number(const char *name,int value){auto *spin=get<QDoubleSpinBox>(name);reveal(spin);auto *edit=spin->findChild<QLineEdit *>();require(edit,"Actual numeric QLineEdit absent");reveal(edit);require(!edit->isReadOnly(),"Actual numeric editor is read-only");edit->setFocus();key(edit,Qt::Key_A,true,Qt::ControlModifier);key(edit,Qt::Key_A,false,Qt::ControlModifier);const auto text=QString::number(value,'f',spin->decimals());key(edit,Qt::Key_unknown,true,Qt::NoModifier,text);key(edit,Qt::Key_unknown,false,Qt::NoModifier,text);key(edit,Qt::Key_Return);key(edit,Qt::Key_Return,false);const auto actual=get<QDoubleSpinBox>(name)->value();require(actual==value,QString("Brush UI numeric edit was not accepted: %1 expected=%2 actual=%3").arg(name).arg(value).arg(actual));}
 void brush(){number("brushSize",20);number("brushFlow",100);if(get<QToolButton>("brushPressureSize")->isChecked())tool("brushPressureSize");}
 QPointF uv(int tile,double u){const auto points=state_()["bm_points"].toArray();require(points.size()==6,"BM actor requires actual two-tile UV endpoints");const auto a=points[(tile-1001)*3].toArray(),b=points[(tile-1001)*3+2].toArray();require(a.size()==2&&b.size()==2,"Missing UV endpoint");const QPointF first(a[0].toDouble(),a[1].toDouble()),last(b[0].toDouble(),b[1].toDouble());const auto point=first+(last-first)*((u-.25)/.5);require(v_->rect().contains(point.toPoint()),"Actual UV hit point is outside the viewport");return point;}
 QJsonObject probe(){w_->setProperty("bmProbe",true);QJsonObject d;try{d=state_();}catch(const std::exception &error){w_->setProperty("bmProbe",false);fail("Actual GPU probe failed: "+QString::fromUtf8(error.what()));}catch(...){w_->setProperty("bmProbe",false);fail("Actual GPU probe failed with a nonstandard exception");}w_->setProperty("bmProbe",false);last_probe_=d;require(d["bm_probe_ready"].toBool(),"Actual Color/Roughness bindings are not ready for strict GPU probes");const auto rows=d["bm_probes"].toArray();require(rows.size()==4,"Missing channel/tile GPU diagnostic rows");for(const auto &r:rows){const auto values=r.toObject()["values"].toArray();require(values.size()==4,"Missing actual GPU probe values");for(const auto &sample:values){require(sample.toArray().size()==4,"Incomplete GPU RGBA");for(auto x:sample.toArray())require(x.isDouble()&&std::isfinite(x.toDouble()),"Nonfinite GPU diagnostic");}}return d;}
 void capture(const QString &name){auto d=probe();d["stage"]=name;d["project_picker"]=picker_diagnostic_;d["project_pickers"]=picker_events_;captures_.append(d);require(capture_demo_window(w_,v_).save(out_+"/"+name+".png"),"Cannot save BM owned-window image");}
 bool adjustment(const QJsonObject &d){const auto modes=d["bm_layer"].toObject()["blending"].toArray();return modes.size()==5&&std::all_of(modes.begin(),modes.end(),[](auto x){return x.toString()=="Passthrough";});}
 void layer_guard(const QJsonObject &d){require(QUuid(d["bm_layer_id"].toString())==adjustment_&&!d["bm_selected_mask"].toBool()&&QUuid(d["bm_selected_effect"].toString()).isNull(),"BM stroke target is not the selected adjustment Paint layer");require(d["bm_layer"].toObject()["kind"]=="paint"&&adjustment(d),"Actual adjustment Paint mode missing");}
 void menu(const char *name,const std::function<void()> &open){
  QTimer::singleShot(20,this,[this,target=QString::fromLatin1(name)]{auto *popup=qobject_cast<QMenu *>(QApplication::activePopupWidget());require(popup,"Actual menu did not become active");QAction *choice=nullptr;for(auto *a:popup->actions())if(a->objectName()==target)choice=a;require(choice&&choice->isEnabled(),"Actual menu action unavailable: "+target);const auto rect=popup->actionGeometry(choice);require(!rect.isEmpty(),"Actual menu action has no hit rectangle");click(popup,rect.center());});
  open();
 }
 void add_adjustment(){auto *button=get<QToolButton>("addPaintLayer");require(button->menu(),"Production Add Paint split menu missing");menu("addAdjustmentPaintLayer",[=]{click(button,QPoint(button->width()-4,button->height()/2));});}
 void context_toggle(){auto *list=get<QListWidget>("layerStack");QListWidgetItem *item=nullptr;for(int i=0;i<list->count();++i){auto *row=list->item(i);if(QUuid(row->data(Qt::UserRole).toString())==adjustment_&&QUuid(row->data(Qt::UserRole+10).toString()).isNull()){item=row;break;}}require(item,"Adjustment row missing");list->scrollToItem(item);const QPoint point=list->visualItemRect(item).center();menu("layerAdjustmentMode",[=]{QContextMenuEvent event(QContextMenuEvent::Mouse,point,list->viewport()->mapToGlobal(point));QApplication::sendEvent(list->viewport(),&event);});}
 QJsonObject transfer_delta(const QJsonObject &start,const QJsonObject &end){QJsonObject out;for(const auto &name:{"upload","readback","finish","mipmap"}){QJsonObject row;const auto a=start[name].toObject(),b=end[name].toObject();for(const auto &key:{"calls","bytes","cpu_wall_ms"})row[key]=b[key].toDouble()-a[key].toDouble();out[name]=row;}return out;}
 void begin(const QString &name,int first_tile,double start,int last_tile,double end){before_=probe();if(name!="lower-paint")layer_guard(before_);first_=uv(first_tile,start);last_=uv(last_tile,end);drag_name_=name;drag_step_=0;pointer_transfers_=state_()["bm_transfers"].toObject();v_->setFocus(Qt::OtherFocusReason);}
 void drag(){const int i=drag_step_++;const auto point=first_+(last_-first_)*(std::min(i,20)/20.);mouse(v_,i==0?QEvent::MouseButtonPress:i<=20?QEvent::MouseMove:QEvent::MouseButtonRelease,point,i==0||i>20?Qt::LeftButton:Qt::NoButton,i>20?Qt::NoButton:Qt::LeftButton);if(i>20){const auto d=state_();pointer_transfers_=transfer_delta(pointer_transfers_,d["bm_transfers"].toObject());drag_step_=-1;frame_=d["render_frames"].toInt()+3;}}
 void stroke_done(){painted_=probe();require(painted_["undo_count"].toInt()==before_["undo_count"].toInt()+1,"Expected exactly one Undo entry per actual drag");require(painted_["cpu_materializations"]==before_["cpu_materializations"]&&painted_["cpu_layer_compositions"]==before_["cpu_layer_compositions"],"Interactive stroke caused CPU materialization/composition");require(pointer_transfers_["finish"].toObject()["calls"].toDouble()==0,"Interactive stroke called GPU_finish");if(drag_name_!="lower-paint"){layer_guard(painted_);require(painted_["bm_lower"]==before_["bm_lower"],"Adjustment stroke modified lower layer metadata/pages");require(painted_["bm_layer"]!=before_["bm_layer"]&&painted_["bm_probes"]!=before_["bm_probes"],"Adjustment stroke did not change authored pages and actual GPU output");}else require(painted_["bm_probes"]!=before_["bm_probes"],"Real lower stroke did not create visible contrast");strokes_.append(QJsonObject{{"tool",drag_name_},{"before",before_},{"after",painted_},{"pointer_transfers",pointer_transfers_}});capture(drag_name_+"-painted");}
 void undone(){const auto d=probe();require(d["bm_layer"]==before_["bm_layer"]&&d["bm_lower"]==before_["bm_lower"]&&d["bm_probes"]==before_["bm_probes"],"Undo did not restore exact layer metadata and GPU samples");capture(drag_name_+"-undo");}
 void redone(){const auto d=probe();require(d["bm_layer"]==painted_["bm_layer"]&&d["bm_lower"]==painted_["bm_lower"]&&d["bm_probes"]==painted_["bm_probes"],"Redo did not restore exact painted metadata and GPU samples");capture(drag_name_+"-redo");}
 void refused(){const auto d=probe(),r=d["bm_refusal"].toObject();layer_guard(d);require(r["fired"].toBool()&&r["terminal"].toBool()&&r["stroke_cancelled"].toBool()&&r["needed_bytes"].toInteger()==131072&&r["available_bytes"].toInteger()==0&&r["reason"].toString().contains("MiB"),"Expected real actionable GPU page-budget refusal was not reported");require(d["undo_count"]==before_["undo_count"]&&d["bm_layer"]==before_["bm_layer"]&&d["bm_lower"]==before_["bm_lower"]&&d["bm_probes"]==before_["bm_probes"],"Refused gesture changed document/GPU or created Undo");require(d["cpu_materializations"]==before_["cpu_materializations"]&&d["cpu_layer_compositions"]==before_["cpu_layer_compositions"],"Refusal performed CPU composition/materialization");refused_=QJsonObject{{"before",before_},{"after",d},{"pointer_transfers",pointer_transfers_}};capture("adjustment-refused");}
 static QJsonValue logical(QJsonValue value){if(value.isArray()){QJsonArray result;for(const auto &x:value.toArray())result.append(logical(x));return result;}if(value.isObject()){auto result=value.toObject();result.remove("paint_hashes");result.remove("hashes");result.remove("immutable_storage");for(auto i=result.begin();i!=result.end();++i)i.value()=logical(i.value());return result;}return value;}
 static QJsonObject widget_description(QWidget *widget){if(!widget)return {};return QJsonObject{{"class",widget->metaObject()->className()},{"object_name",widget->objectName()},{"title",widget->windowTitle()}};}
 void project_file_input(bool save,QPointer<QFileDialog> picker,QElapsedTimer admission){
  picker_activation_["active_window_after"]=widget_description(QApplication::activeWindow());picker_activation_["active_modal_after"]=widget_description(QApplication::activeModalWidget());picker_activation_["focus_widget_after"]=widget_description(QApplication::focusWidget());picker_activation_["owner_active"]=picker&&picker->isActiveWindow();picker_activation_["wait_ms"]=admission.elapsed();picker_diagnostic_=QJsonObject{{"operation",save?"Save As":"Open"},{"activation",picker_activation_}};
  require(picker&&picker->isVisible()&&QApplication::activeModalWidget()==picker.data(),"Actual project picker lost visible modal ownership before activation");
  if(!picker->isActiveWindow()){require(admission.elapsed()<5000,"Actual project picker did not activate within5s");picker_activation_["polls"]=picker_activation_["polls"].toInt()+1;QTimer::singleShot(10,this,[this,save,picker,admission]{project_file_input(save,picker,admission);});return;}
  auto *field=picker->findChild<QLineEdit *>("fileNameEdit");picker_activation_["filename_focus_proxy"]=widget_description(field?field->focusProxy():nullptr);require(field&&field->isVisible()&&!field->isReadOnly(),"Actual editable fileNameEdit absent");const auto path=out_+"/bm-adjustment-ui.blend";field->setFocus(Qt::OtherFocusReason);const bool focus_before=field->hasFocus();picker_diagnostic_=QJsonObject{{"activation",picker_activation_},{"operation",save?"Save As":"Open"},{"expected_filename",path},{"field_focus_before",focus_before},{"input_method","private clipboard full-path Ctrl+V"}};require(focus_before,"Actual project filename field did not acquire keyboard focus");auto *clipboard=QApplication::clipboard();require(clipboard,"Private Qt clipboard unavailable");clipboard->setText(path);key(field,Qt::Key_A,true,Qt::ControlModifier);key(field,Qt::Key_A,false,Qt::ControlModifier);require(field->hasFocus(),"Actual filename focus lost before paste");key(field,Qt::Key_V,true,Qt::ControlModifier);key(field,Qt::Key_V,false,Qt::ControlModifier);const bool focus_after=field->hasFocus();picker_diagnostic_["field_focus_after"]=focus_after;picker_diagnostic_["filename"]=field->text();require(field->text()==path,"Actual project filename paste differs");
   QPointer<QFileDialog> owner(picker);QPointer<QLineEdit> editor(field);const auto activation=picker_activation_;
   QTimer::singleShot(0,this,[this,save,owner,editor,path,focus_before,focus_after,activation]{
    require(owner&&editor&&QApplication::activeModalWidget()==owner.data(),"Project picker lost actual modal ownership");auto *completion=editor->completer();QPointer<QAbstractItemView> popup=completion?completion->popup():nullptr;const bool visible=popup&&popup->isVisible();picker_diagnostic_=QJsonObject{{"activation",activation},{"operation",save?"Save As":"Open"},{"field_focus_before",focus_before},{"field_focus_after",focus_after},{"input_method","private clipboard full-path Ctrl+V"},{"title",owner->windowTitle()},{"filename",editor->text()},{"expected_filename",path},{"completion_was_visible",visible},{"closed",false}};
    if(visible){key(popup,Qt::Key_Escape);if(popup)key(popup,Qt::Key_Escape,false);}require(!popup||!popup->isVisible(),"Actual filename completer popup did not close on Escape");require(owner&&owner->isVisible()&&QApplication::activeModalWidget()==owner.data(),"Dismissing filename completion closed the actual picker");require(editor&&editor->text()==path,"Dismissing completion changed exact project filename");
    auto *buttons=owner->findChild<QDialogButtonBox *>();require(buttons,"Actual file chooser buttons missing");QPointer<QPushButton> button=buttons->button(save?QDialogButtonBox::Save:QDialogButtonBox::Open);require(button&&button->isVisible()&&button->isEnabled(),"Actual project file action unavailable");
    // Observe the normal button/dialog signals. Never call click(), accept() or a slot.
    auto record=std::make_shared<QJsonObject>(picker_diagnostic_);auto finished=std::make_shared<bool>(false);
    const auto publish=[this,record]{picker_diagnostic_=*record;};
    for(const auto *name:{"pressed","released","clicked","accepted"})(*record)[name]=0;
    const auto observed=[record,publish](const char *name){(*record)[name]=(*record)[name].toInt()+1;publish();};
    connect(button.data(),&QPushButton::pressed,this,[observed]{observed("pressed");});
    connect(button.data(),&QPushButton::released,this,[observed]{observed("released");});
    connect(button.data(),&QPushButton::clicked,this,[observed]{observed("clicked");});
    connect(owner.data(),&QDialog::accepted,this,[observed]{observed("accepted");});
    button->setFocus(Qt::OtherFocusReason);
    const auto selected=owner->selectedFiles();const auto rect=button->geometry();const auto parent=QFileInfo(path).absoluteDir();
    (*record)["selected_files"]=QJsonArray::fromStringList(selected);(*record)["file_mode"]=int(owner->fileMode());(*record)["accept_mode"]=int(owner->acceptMode());(*record)["current_directory"]=owner->directory().absolutePath();(*record)["parent_directory"]=parent.absolutePath();(*record)["parent_exists"]=parent.exists();(*record)["expected_file_exists"]=QFileInfo::exists(path);(*record)["button_text"]=button->text();(*record)["button_geometry"]=QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()};(*record)["button_enabled"]=button->isEnabled();(*record)["button_visible"]=button->isVisible();(*record)["button_focus"]=button->hasFocus();(*record)["button_down_before"]=button->isDown();(*record)["input"]="focused Space press/release";publish();
    require(selected==QStringList{path},"Actual project picker selectedFiles differs from exact expected path");require(parent.exists(),"Actual project picker destination parent directory absent");require(owner->fileMode()==(save?QFileDialog::AnyFile:QFileDialog::ExistingFile)&&owner->acceptMode()==(save?QFileDialog::AcceptSave:QFileDialog::AcceptOpen),"Actual project picker mode differs");require(button->hasFocus()&&!button->isDown()&&!rect.isEmpty(),"Actual project action did not acquire keyboard focus with a valid idle button");
    const auto closed=[this,record,finished,publish]{if(*finished)return;(*record)["closed"]=true;publish();for(const auto *name:{"pressed","released","clicked","accepted"})require((*record)[name].toInt()==1,QString("Project picker did not emit exactly one normal signal: ")+name);*finished=true;picker_events_.append(*record);};
    auto *poll=new QTimer(this);QElapsedTimer elapsed;elapsed.start();connect(poll,&QTimer::timeout,this,[this,poll,owner,editor,record,publish,closed,elapsed]{if(!owner||!owner->isVisible()){closed();poll->stop();poll->deleteLater();return;}(*record)["filename"]=editor?editor->text():QString();(*record)["selected_files_after"]=QJsonArray::fromStringList(owner->selectedFiles());(*record)["active_modal"]=QApplication::activeModalWidget()?QApplication::activeModalWidget()->windowTitle():QString();(*record)["completion_visible"]=editor&&editor->completer()&&editor->completer()->popup()->isVisible();publish();require(elapsed.elapsed()<5000,"Actual project picker did not close within5s after button input; see project_picker metadata");});poll->start(50);
    key(button.data(),Qt::Key_Space);(*record)["button_down_after_press"]=button&&button->isDown();publish();require(button&&button->isDown()&&(*record)["pressed"].toInt()==1,"Actual focused project button did not receive Space press");key(button.data(),Qt::Key_Space,false);(*record)["button_down_after_release"]=button&&button->isDown();publish();if(!owner||!owner->isVisible())closed();

   });
 }
 void project_file(bool save){
  QTimer::singleShot(30,this,[this,save]{
   auto *picker=qobject_cast<QFileDialog *>(QApplication::activeModalWidget());require(picker,"Actual project file chooser absent");require(picker->windowTitle()==(save?"Save project":"Open project"),"Wrong actual project chooser");picker_activation_=QJsonObject{{"active_window_before",widget_description(QApplication::activeWindow())},{"active_modal_before",widget_description(QApplication::activeModalWidget())},{"focus_widget_before",widget_description(QApplication::focusWidget())},{"owner_active_before",picker->isActiveWindow()},{"polls",0}};picker->activateWindow();QElapsedTimer admission;admission.start();project_file_input(save,picker,admission);
  });
  if(!save){action("openProject");return;}QAction *chosen=nullptr;for(auto *action:get<QMenu>("projectFileMenu")->actions()){auto text=action->text();text.remove('&');if(text=="Save As…")chosen=action;}require(chosen&&chosen->isEnabled(),"Actual Save As action missing");chosen->trigger();
 }
 QJsonArray parameters(const QJsonArray &layers){QJsonArray result;for(const auto &entry:layers){auto layer=entry.toObject();for(const auto *key:{"paint_pages","paint_hashes","paint_tiles","strokes"})layer.remove(key);auto ao=layer["ao"].toObject();ao.remove("tiles");layer["ao"]=ao;result.append(logical(layer));}return result;}
 QJsonArray layers(const QJsonObject &d){auto result=d["bm_lower"].toArray();result.append(d["bm_layer"]);return result;}
 void save_existing(){const auto d=probe();require(d["bm_document_path"].toString()==out_+"/bm-adjustment-ui.blend"&&!d["render_ui"].toBool(),"Final Smudge Save requires the existing project in painting mode");QAction *chosen=nullptr;for(auto *candidate:get<QMenu>("projectFileMenu")->actions()){auto text=candidate->text();text.remove('&');if(text=="Save")chosen=candidate;}require(chosen&&chosen->isEnabled()&&chosen->shortcut()==QKeySequence(QKeySequence::Save),"Actual normal Save QAction is unavailable");chosen->trigger();}
 void save_checkpoint(const QString &stage="adjustment-saved"){saved_=probe();require(QFileInfo::exists(out_+"/bm-adjustment-ui.blend")&&saved_["bm_document_path"].toString()==out_+"/bm-adjustment-ui.blend","Actual GUI Save As did not publish owned project");capture(stage);}
 void opened(const QString &stage="adjustment-reopened",bool final_smudge=false){const auto d=probe();layer_guard(d);require(d["bm_set"]==saved_["bm_set"]&&d["bm_layer_id"]==saved_["bm_layer_id"]&&d["bm_tiles"]==saved_["bm_tiles"],"GUI Open changed persistent target identity/tiles");require(parameters(layers(d))==parameters(layers(saved_)),"GUI Open changed layer parameter metadata");require(d["bm_layer"].toObject()["coverage"]=="painted"&&d["bm_layer"].toObject()["paint_pages"].toArray()[0].toInteger()>0,"GUI Open lost nonempty adjustment Color ink");bool lower_ink=false;for(const auto &entry:d["bm_lower"].toArray()){const auto layer=entry.toObject();lower_ink|=layer["kind"]=="paint"&&layer["paint_pages"].toArray()[0].toInteger()>0;}require(lower_ink,"GUI Open lost lower Paint ink");require(d["bm_probes"]==saved_["bm_probes"],"GUI Open changed canonical GPU packets");QFile packed(out_+"/bm-adjustment-ui.blend");require(packed.open(QIODevice::ReadOnly),"Cannot hash actual saved project");QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&packed),"Cannot stream saved project hash");const auto packed_sha=QString::fromLatin1(hash.result().toHex());if(final_smudge){require(!clone_packed_sha_.isEmpty()&&packed_sha!=clone_packed_sha_,"Normal Save did not replace the earlier Clone-only packed checkpoint");require(d["bm_probes"]==cycles_after_["bm_probes"]&&d["bm_probes"]!=cycles_before_["bm_probes"],"Final packed Open did not retain the actual post-Clone Smudge change");checks_.append(QJsonObject{{"stage","smudge-packed-persistence"},{"project_pickers",picker_events_},{"clone_packed_sha256",clone_packed_sha_},{"final_packed_sha256",packed_sha},{"gpu",d["bm_probes"]},{"same_as_cycles_redo",true},{"different_from_clone",true},{"scope","Actual normal Save after completed Cycles Undo/Redo, then actual Open; immutable cold witness now includes Clone and Smudge"}});}else clone_packed_sha_=packed_sha;
  QFile witness(out_+"/bm-adjustment-witness.json");require(witness.open(QIODevice::WriteOnly),"Cannot write canonical BM packed witness");witness.write(QJsonDocument(QJsonObject{{"document_path",d["bm_document_path"]},{"packed_file_sha256",packed_sha},{"set",d["bm_set"]},{"selected_layer",d["bm_layer_id"]},{"tiles",d["bm_tiles"]},{"layers",logical(layers(d))},{"gpu",d["bm_probes"]},{"project_pickers",picker_events_},{"tools",final_smudge?QJsonArray{"clone","smudge"}:QJsonArray{"clone"}},{"scope",final_smudge?"Actual canonical metadata and GPU packets after final Smudge normal Save and GUI Open; prior Clone retained. Fresh-process/data-first admission remains separate.":"Intermediate Clone-only canonical metadata and GPU packets after GUI Open; final post-Smudge Save/Open overwrites this witness before completion."}}).toJson());capture(stage);}
 double gpu_difference(const QJsonArray &a,const QJsonArray &b){require(a.size()==4&&b.size()==4,"GPU comparison packets absent");double error=0;for(int i=0;i<4;++i){const auto x=a[i].toObject(),y=b[i].toObject();require(x["tile"]==y["tile"]&&x["channel"]==y["channel"],"GPU comparison identity differs");const auto u=x["values"].toArray(),v=y["values"].toArray();require(u.size()==4&&v.size()==4,"GPU comparison points absent");for(int j=0;j<4;++j){const auto m=u[j].toArray(),n=v[j].toArray();require(m.size()==4&&n.size()==4,"GPU comparison RGBA absent");for(int c=0;c<4;++c)error=std::max(error,std::abs(m[c].toDouble()-n[c].toDouble()));}}return error;}
 void arm_cycles(const QJsonObject &expected){
  require(!cycles_wait_,"BM Cycles previous observation is still pending");cycles_expected_=expected;require(expected["bm_probes"].toArray().size()==4,"BM Cycles canonical packets absent");cycles_revisions_.clear();
  for(const auto &value:state_()["bm_cycles_mirrors"].toObject()["packets"].toArray()){const auto row=value.toObject();if(row["material"]==expected["bm_runtime_uid"]&&(row["role"].toInt()==0||row["role"].toInt()==2))cycles_revisions_[{row["tile"].toInt(),row["role"].toInt()}]=row["published"].toString().toULongLong();}
  cycles_wait_=true;cycles_published_=false;cycles_done_=-1;cycles_sample_.clear();cycles_packets_={};
 }
 bool cycles_ready(const QJsonObject &d){
  if(!cycles_wait_)return true;if(!d["render_ui"].toBool())return false;
  const auto render=d["render"].toObject();const auto status=render["status"].toString();require(!render["image_mirror_paused"].toBool()&&!render["image_mirror_error"].toBool()&&!status.contains("error",Qt::CaseInsensitive)&&!status.contains("failed",Qt::CaseInsensitive),"BM Cycles renderer/publication failed");
  const auto mirrors=d["bm_cycles_mirrors"].toObject();std::set<std::pair<int,int>> matched;QJsonArray packets;
  for(const auto &entry:mirrors["packets"].toArray()){
   const auto row=entry.toObject();const int tile=row["tile"].toInt(),role=row["role"].toInt();const auto key=std::pair{tile,role};
   if(row["material"]!=cycles_expected_["bm_runtime_uid"]||(tile!=1001&&tile!=1002)||(role!=0&&role!=2)||!row["current"].toBool())continue;
   const auto revision=row["published"].toString().toULongLong();if(revision<=cycles_revisions_[key])continue;
   require(row["wanted"]==row["published"]&&row["packet_revision"]==row["published"]&&row["packet_epoch"]==mirrors["current_entry_epoch"],"BM Cycles packet is not the current successful generation");
   require(row["width"].toInt()==2048&&row["height"].toInt()==2048,"BM Cycles packet dimensions differ");
   QJsonObject canonical;for(const auto &value:cycles_expected_["bm_probes"].toArray()){const auto p=value.toObject();if(p["tile"].toInt()==tile&&p["channel"].toInt()==role)canonical=p;}
   require(!canonical.isEmpty(),"BM Cycles canonical channel/tile not found");const auto samples=row["samples"].toArray();require(samples.size()==3,"BM Cycles successful packet needs three samples");
   std::set<std::pair<int,int>> coordinates;
   for(const auto &value:samples){const auto point=value.toObject();const auto xy=point["xy"].toArray(),rgba=point["rgba"].toArray();require(xy.size()==2&&rgba.size()==4,"BM Cycles packet sample shape differs");QJsonArray expected;
    for(const auto &candidate:canonical["points"].toArray()){const auto p=candidate.toObject();if(p["xy"].toArray()==xy)expected=p["rgba"].toArray();}
    require(expected.size()==4&&coordinates.emplace(xy[0].toInt(),xy[1].toInt()).second,"BM Cycles sample does not match a unique canonical integer coordinate");
    for(int c=0;c<4;++c)require(std::isfinite(rgba[c].toDouble())&&std::abs(rgba[c].toDouble()-expected[c].toDouble())<3e-6,"BM current successful Cycles publication differs from canonical GPU values");
   }
   require(matched.insert(key).second,"BM Cycles duplicate current channel/tile packet");packets.append(row);
  }
  if(mirrors["active"].toBool()&&matched.size()==4){cycles_published_=true;cycles_packets_=packets;}
  if(cycles_published_&&QRegularExpression("\\bSample [0-9]+/[0-9]+").match(status).hasMatch())cycles_sample_=status;
  if(!cycles_published_||cycles_sample_.isEmpty()||!status.contains("Rendering Done"))return false;
  if(cycles_done_<0){cycles_done_=d["render_frames"].toInt();v_->update();return false;}return d["render_frames"].toInt()>cycles_done_;
 }
 QJsonObject image_delta(const QImage &a,const QImage &b){
  require(!a.isNull()&&a.size()==b.size(),"BM Cycles image size changed");const QRect roi=a.rect();double total=0;int changed=0;
  for(int y=roi.top();y<=roi.bottom();++y)for(int x=roi.left();x<=roi.right();++x){int sum=0;for(int c=0;c<3;++c)sum+=std::abs(int(a.constScanLine(y)[x*4+c])-int(b.constScanLine(y)[x*4+c]));total+=sum/(3.*255);changed+=sum>3;}
  return {{"roi",QJsonArray{roi.x(),roi.y(),roi.width(),roi.height()}},{"changed_pixels",changed},{"mean_absolute_rgb",total/(roi.width()*roi.height())}};
 }
 QJsonObject signed_emission_change(const QJsonObject &current,const QImage &image){
  require(current["bm_inspection"].toInt(-1)==0&&cycles_previous_state_["bm_inspection"].toInt(-1)==0,"BM visible-frame witness requires real BaseColor emission inspection");
  require(current["bm_cycles_surface_points"]==cycles_previous_state_["bm_cycles_surface_points"],"BM Cycles surface/camera correspondence changed across Undo/Redo");
  double strongest=0;QJsonObject selected;QJsonArray selected_rgba,previous_rgba;
  const auto previous_packets=cycles_previous_state_["bm_probes"].toArray();
  const auto luminance=[](const QJsonArray &p){return .2126*p[0].toDouble()+.7152*p[1].toDouble()+.0722*p[2].toDouble();};
  for(const auto &value:current["bm_probes"].toArray()){
   const auto packet=value.toObject();if(packet["channel"].toInt()!=0)continue;QJsonObject before;
   for(const auto &candidate:previous_packets){const auto row=candidate.toObject();if(row["channel"].toInt()==0&&row["tile"]==packet["tile"])before=row;}
   for(const auto &entry:packet["points"].toArray()){const auto point=entry.toObject();QJsonArray prior;
    for(const auto &p:before["points"].toArray())if(p.toObject()["xy"]==point["xy"])prior=p.toObject()["rgba"].toArray();
    const auto rgba=point["rgba"].toArray();require(prior.size()==4&&rgba.size()==4,"BM emission canonical correspondence absent");const double difference=luminance(rgba)-luminance(prior);
    if(std::abs(difference)>std::abs(strongest)){strongest=difference;selected=point;selected["tile"]=packet["tile"];selected_rgba=rgba;previous_rgba=prior;}
   }
  }
  require(std::abs(strongest)>.02,"BM canonical Color change is too weak for an independent visible-frame witness");
  for(const auto &rgba:{selected_rgba,previous_rgba})require(std::abs(rgba[0].toDouble()-rgba[1].toDouble())<1e-5&&std::abs(rgba[1].toDouble()-rgba[2].toDouble())<1e-5,"BM emission sign oracle requires the actual grayscale fixture");
  QJsonObject surface;for(const auto &value:current["bm_cycles_surface_points"].toArray()){const auto p=value.toObject();if(p["tile"]==selected["tile"]&&p["xy"]==selected["xy"])surface=p;}require(!surface.isEmpty(),"BM actual surface projection absent");const auto screen=surface["screen"].toArray();require(screen.size()==2,"BM actual surface screen coordinate absent");
  const int px=qRound(screen[0].toDouble()*image.width()/v_->width()),py=qRound(screen[1].toDouble()*image.height()/v_->height());require(image.rect().adjusted(3,3,-3,-3).contains(px,py),"BM emission witness point lies outside the completed frame");
  const auto patch=[&](const QImage &frame){double sum=0;for(int y=py-2;y<=py+2;++y)for(int x=px-2;x<=px+2;++x){const auto *p=frame.constScanLine(y)+x*4;sum+=(.2126*p[0]+.7152*p[1]+.0722*p[2])/255.;}return sum/25;};
  const double old_display=patch(cycles_previous_),new_display=patch(image),observed=new_display-old_display;
  require(observed*strongest>0&&std::abs(observed)>.01,"BM completed emission frame did not follow the predicted canonical Color brightness direction");
  return {{"tile",selected["tile"]},{"canonical_xy",selected["xy"]},{"surface",surface},{"frame_xy",QJsonArray{px,py}},{"canonical_before",previous_rgba},{"canonical_after",selected_rgba},{"predicted_linear_luminance_change",strongest},{"displayed_luminance_before",old_display},{"displayed_luminance_after",new_display},{"observed_display_luminance_change",observed},{"patch_size",5},{"inspection",0},{"scope","Known grayscale GPU sample determines brightness direction. Real BaseColor inspection uses direct emission; no lighting/Monte Carlo noise difference can substitute for signed material response."}};
 }
 void cycles_capture(const QString &stage){
  const auto d=probe();require(cycles_wait_&&cycles_published_&&!cycles_sample_.isEmpty()&&cycles_done_>=0&&d["render_frames"].toInt()>cycles_done_,"BM Cycles publication -> Sample -> Done -> later frame was not observed");
  require(d["bm_layer"]==cycles_expected_["bm_layer"]&&d["bm_lower"]==cycles_expected_["bm_lower"]&&d["bm_probes"]==cycles_expected_["bm_probes"],"BM Cycles Undo/Redo did not preserve exact canonical document/GPU state");
  require(d["cpu_materializations"]==cycles_entry_["cpu_materializations"]&&d["cpu_layer_compositions"]==cycles_entry_["cpu_layer_compositions"],"BM Cycles transition performed CPU layer composition/materialization");
  require(d["bm_inspection"].toInt(-1)==0,"BM Cycles visible witness lost real BaseColor emission inspection");
  auto image=capture_viewport_framebuffer(v_).convertToFormat(QImage::Format_RGBA8888);require(!image.isNull()&&image.save(out_+"/"+stage+".png"),"BM completed Cycles viewport capture failed");
  QJsonObject proof{{"stage",stage},{"packets",cycles_packets_},{"observed_sample_status",cycles_sample_},{"done_frame",cycles_done_},{"captured_frame",d["render_frames"]},{"render",d["render"]},{"image_sha256",QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex())},{"canonical_state",d},{"scope","Actual new successful Color/Roughness publication on both UDIMs -> renderer Sample -> Done -> later displayed frame; canonical GPU equality, no cross-engine beauty equality"}};
  if(!cycles_previous_.isNull()){const auto delta=image_delta(image,cycles_previous_);require(delta["changed_pixels"].toInt()>64&&delta["mean_absolute_rgb"].toDouble()>.002,"BM adjustment Undo/Redo did not change a completed Cycles frame");proof["previous_frame_delta"]=delta;proof["signed_emission_change"]=signed_emission_change(d,image);}
  if(cycles_first_.isNull())cycles_first_=image;else if(stage=="cycles-adjustment-redo")proof["redo_vs_baseline_observation"]=image_delta(image,cycles_first_);
  cycles_previous_=image;cycles_previous_state_=d;checks_.append(proof);cycles_wait_=false;
 }
 void export_files(){
  const auto directory=out_+"/normal-export";require(QDir().mkpath(directory),"Cannot create owned Export directory");
  QTimer::singleShot(30,this,[this,directory]{auto *dialog=get<QDialog>("textureExportDialog");auto *field=get<QLineEdit>("exportDirectory");field->setFocus();key(field,Qt::Key_A,true,Qt::ControlModifier);key(field,Qt::Key_A,false,Qt::ControlModifier);for(auto c:directory){key(field,Qt::Key_unknown,true,Qt::NoModifier,QString(c));key(field,Qt::Key_unknown,false,Qt::NoModifier,QString(c));}key(field,Qt::Key_Tab);key(field,Qt::Key_Tab,false);require(field->text()==directory,"Actual Export directory edit failed");
   auto *sets=get<QListWidget>("exportTextureSets");require(sets->count()==1,"Export actual set count differs");if(sets->item(0)->checkState()!=Qt::Checked)click(sets->viewport(),QPoint(sets->visualItemRect(sets->item(0)).left()+9,sets->visualItemRect(sets->item(0)).center().y()));require(sets->item(0)->checkState()==Qt::Checked,"Actual Export set not selected");
   auto *preset=get<QComboBox>("exportPreset");preset->setFocus();key(preset,Qt::Key_Home);key(preset,Qt::Key_Home,false);key(preset,Qt::Key_Tab);key(preset,Qt::Key_Tab,false);require(preset->currentIndex()==0,"Actual Document channels template not selected");
   auto *format=get<QComboBox>("exportFormat");format->setFocus();key(format,Qt::Key_Home);key(format,Qt::Key_Home,false);key(format,Qt::Key_Down);key(format,Qt::Key_Down,false);key(format,Qt::Key_Tab);key(format,Qt::Key_Tab,false);require(format->currentText()=="exr","Actual EXR format not selected");auto *precision=get<QComboBox>("exportPrecision");precision->setFocus();key(precision,Qt::Key_End);key(precision,Qt::Key_End,false);key(precision,Qt::Key_Tab);key(precision,Qt::Key_Tab,false);require(precision->currentText().startsWith("32f"),"Actual EXR32 precision not selected");
   auto *poll=new QTimer(this);QPointer<QDialog> owner(dialog);QElapsedTimer elapsed;elapsed.start();connect(poll,&QTimer::timeout,this,[this,poll,owner,elapsed,directory]{require(owner,"Actual Export dialog disappeared before completion");require(elapsed.elapsed()<120000,"Normal async GPU Export exceeded120s");const auto error=owner->property("exportError").toString();require(error.isEmpty(),"Actual normal Export failed: "+error);if(!owner->property("exportFinished").toBool())return;poll->stop();QJsonArray files;bool first=false,second=false;QDirIterator it(directory,QDir::Files,QDirIterator::Subdirectories);while(it.hasNext()){const auto path=it.next();require(QFileInfo(path).size()>0,"Actual Export produced empty file");files.append(path);first|=path.contains("1001");second|=path.contains("1002");}const auto stats=w_->property("bmExportGpuStats").toJsonObject();require(files.size()>0&&first&&second,"Normal Export did not write both actual tiles");require(stats["tiles"].toInt()==2&&stats["packets"].toInt()>0,"Normal Export did not use actual GPU capture packets");checks_.append(QJsonObject{{"stage","normal-export"},{"files",files},{"gpu_capture",stats},{"scope","Real Export dialog async GPU bridge. Owner assembly timings are not total Export latency."}});require(capture_demo_window(w_,v_).save(out_+"/normal-export.png"),"Export screenshot failed");auto *close=get<QPushButton>("cancelTextureExport");require(close->text()=="Close"&&close->isEnabled(),"Actual completed Export Close control absent");poll->deleteLater();click(close,close->rect().center());});poll->start(50);auto *run=get<QPushButton>("runTextureExport");require(run->isEnabled(),"Actual normal Export button disabled");click(run,run->rect().center());
  });action("exportTextures");
 }
 void flattened(){const auto d=probe();require(QUuid(d["bm_layer_id"].toString())!=QUuid(flatten_before_["bm_layer_id"].toString())&&layers(d).size()==layers(flatten_before_).size()+1&&d["undo_count"].toInt()==flatten_before_["undo_count"].toInt()+1,"Flatten did not install actual replacement plus exactly one Undo");const auto error=gpu_difference(d["bm_probes"].toArray(),flatten_before_["bm_probes"].toArray());require(error<3e-6,"Actual GPU Flatten changed output");checks_.append(QJsonObject{{"stage","flatten-installation"},{"before",flatten_before_},{"after",d},{"maximum_gpu_error",error}});capture("adjustment-flattened");action("undoDocument");}
 void flatten_undone(){const auto d=probe();require(d["bm_layer_id"]==flatten_before_["bm_layer_id"]&&d["bm_layer"]==flatten_before_["bm_layer"]&&d["bm_lower"]==flatten_before_["bm_lower"]&&d["bm_probes"]==flatten_before_["bm_probes"]&&d["undo_count"]==flatten_before_["undo_count"],"Flatten Undo did not restore exact source metadata/GPU/history");checks_.append(QJsonObject{{"stage","flatten-undone"},{"exact_source_GPU_history",true}});capture("adjustment-flatten-undone");}
 void advance(){if(busy_)return;busy_=true;const auto done=[&]{busy_=false;};if(++ticks_>6000)fail("Timed out");const auto d=state_();for(const auto *name:{"open_error","save_error","material_error","workspace_error","shader_failures"}){const auto text=d[name].toString();const auto r=d["bm_refusal"].toObject();const bool expected=refusal_&&r["fired"].toBool()&&r["terminal"].toBool()&&text==r["reason"].toString()&&(QString::fromLatin1(name)=="material_error"||QString::fromLatin1(name)=="workspace_error");require(text.isEmpty()||expected,QString("Production error ")+name+": "+text);}if(!cycles_ready(d)){done();return;}if(drag_step_>=0){drag();done();return;}if(flatten_wait_){if(QUuid(d["bm_layer_id"].toString())==QUuid(flatten_before_["bm_layer_id"].toString())){done();return;}flatten_wait_=false;}if(step_>1&&(QApplication::activeModalWidget()||QApplication::activePopupWidget())){done();return;}if(step_>1&&(!d["has_document"].toBool()||d["composite_pending"].toBool()||d["property_pending"].toBool()||d["open_pending"].toBool()||d["save_pending"].toBool()||d["layer_thumbnails_pending"].toBool()||d["render_frames"].toInt()<frame_)){done();return;}if(step_>=int(steps_.size())){timer_.stop();QFile file(out_+"/adjustment-paint-ui.json");require(file.open(QIODevice::WriteOnly),"Cannot write BM UI proof");file.write(QJsonDocument(QJsonObject{{"captures",captures_},{"strokes",strokes_},{"refusal",refused_},{"checks",checks_},{"project_pickers",picker_events_},{"scope","Real UI menu/context/Smudge/Clone/one-Undo routes; backend numerical/export/packed cold tests are separate"}}).toJson());file.close();std::puts("BTS_BM_ADJUSTMENT_UI_PASS strokes=4 adjustment_strokes=3 refusal=1 tiles=2");QTimer::singleShot(100,qApp,&QCoreApplication::quit);done();return;}steps_[step_++]();if(drag_step_<0)frame_=state_()["render_frames"].toInt()+3;done();}
 public:
 GpuAdjustmentPaintDemo(QWidget *w,QWidget *v,const QString &directory,const QString &mesh,std::function<QJsonObject()> state):QObject(w),w_(w),v_(v),out_(directory),mesh_(mesh),state_(std::move(state)){
  QDir().mkpath(out_);w_->resize(1280,850);w_->activateWindow();w_->raise();
  steps_.push_back([this]{action("newProject");});
  steps_.push_back([this]{get<QComboBox>("projectTemplate")->setCurrentIndex(0);auto *field=get<QLineEdit>("meshPath");field->setText(mesh_);get<QCheckBox>("preserveUvTiles")->setChecked(true);auto *size=get<QComboBox>("documentResolution");require(size->findData(2048)>=0,"Actual New resolution2048 option absent");size->setCurrentIndex(size->findData(2048));require(size->currentData().toInt()==2048,"Actual New control did not retain2048");click(get<QPushButton>("createProject"),get<QPushButton>("createProject")->rect().center());});
  steps_.push_back([this]{require(state_()["bm_tiles"].toArray()==QJsonArray{1001,1002},"Actual fixture did not retain two tiles");const auto dimensions=state_()["bm_dimensions"].toArray();require(dimensions.size()==2,"Actual tile dimensions unavailable");for(const auto &entry:dimensions){const auto d=entry.toObject();require(d["width"].toInt()==2048&&d["height"].toInt()==2048,"Actual fixture did not create2048 tiles");}press(Qt::Key_F3);press(Qt::Key_F);press(Qt::Key_C);tool("addFillLayer");});
  steps_.push_back([this]{tool("addPaintLayer");tool("paintTool");brush();});
  steps_.push_back([this]{const auto ink=state_()["bm_brush"].toObject()["ink"].toArray();require(ink.size()==4&&ink[0].toDouble()==0&&ink[1].toDouble()==0&&ink[2].toDouble()==0,"Real default Paint ink must supply black contrast");begin("lower-paint",1001,.25,1001,.5);});
  steps_.push_back([this]{stroke_done();add_before_=state_();add_adjustment();});
  steps_.push_back([this]{const auto d=state_();adjustment_=QUuid(d["bm_layer_id"].toString());layer_guard(d);auto lower=add_before_["bm_lower"].toArray();lower.append(add_before_["bm_layer"]);require(d["bm_lower"].toArray()==lower&&d["undo_count"].toInt()==add_before_["undo_count"].toInt()+1,"Add adjustment changed lower layers or added more than one layer/Undo");require(d["bm_layer"].toObject()["coverage"]=="empty","New adjustment layer is not transparent");toggle_before_=d;context_toggle();});
  steps_.push_back([this]{const auto d=state_();require(!adjustment(d)&&d["bm_lower"]==toggle_before_["bm_lower"]&&d["undo_count"].toInt()==toggle_before_["undo_count"].toInt()+1,"Context toggle off did not publish exactly one nondestructive change");toggle_before_=d;context_toggle();});
  steps_.push_back([this]{const auto d=state_();layer_guard(d);require(d["bm_lower"]==toggle_before_["bm_lower"]&&d["undo_count"].toInt()==toggle_before_["undo_count"].toInt()+1,"Context toggle on did not publish exactly one nondestructive change");capture("adjustment-empty");tool("smudgeTool");brush();});
  steps_.push_back([this]{require(state_()["bm_brush"].toObject()["smudge"].toBool(),"Actual Smudge tool not active");begin("smudge",1001,.45,1001,.75);});
  steps_.push_back([this]{stroke_done();action("undoDocument");});
  steps_.push_back([this]{undone();action("redoDocument");});
  steps_.push_back([this]{redone();action("undoDocument");});
  steps_.push_back([this]{undone();tool("cloneTool");brush();});
  steps_.push_back([this]{require(state_()["bm_brush"].toObject()["clone"].toBool(),"Actual Clone tool not active");const int undo=state_()["undo_count"].toInt();const auto point=uv(1001,.3);v_->setFocus();key(v_,Qt::Key_V);mouse(v_,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);mouse(v_,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);key(v_,Qt::Key_V,false);require(state_()["bm_clone_source"].toBool()&&state_()["undo_count"].toInt()==undo,"Actual V+click failed or created an Undo entry");});
  steps_.push_back([this]{begin("clone",1002,.65,1002,.85);});
  steps_.push_back([this]{stroke_done();action("undoDocument");});
  steps_.push_back([this]{undone();action("redoDocument");});
  steps_.push_back([this]{redone();capture("adjustment-final");});
  steps_.push_back([this]{project_file(true);});
  steps_.push_back([this]{save_checkpoint();project_file(false);});
  steps_.push_back([this]{press(Qt::Key_M);press(Qt::Key_C);press(Qt::Key_C);press(Qt::Key_C);require(state_()["bm_inspection"].toInt(-1)==2,"Actual postOpen Roughness inspection was not selected");});
  steps_.push_back([this]{require(state_()["bm_inspection"].toInt(-1)==2,"Actual postOpen Roughness inspection was lost");press(Qt::Key_M);press(Qt::Key_C);require(state_()["bm_inspection"].toInt(-1)==0,"Actual postOpen Color inspection was not restored");});
  steps_.push_back([this]{opened();});
  steps_.push_back([this]{tool("smudgeTool");brush();});
  steps_.push_back([this]{action("guiBmRefuseNextPage");require(state_()["bm_refusal"].toObject()["armed"].toBool(),"Owner refusal diagnostic did not arm");refusal_=true;begin("refused-smudge",1002,.7,1002,.45);});
  steps_.push_back([this]{refused();action("guiBmAcknowledgeRefusal");refusal_=false;});
  steps_.push_back([this]{require(state_()["bm_refusal"].toObject()["acknowledged"].toBool(),"Exact refusal was not acknowledged");begin("recovery-smudge",1002,.7,1002,.45);});
  steps_.push_back([this]{stroke_done();capture("adjustment-recovered");});
  steps_.push_back([this]{cycles_before_=before_;cycles_after_=painted_;cycles_entry_=probe();press(Qt::Key_M);press(Qt::Key_C);press(Qt::Key_F2);});
  steps_.push_back([this]{require(state_()["view"].toObject()["layout"]=="3D only"&&state_()["bm_inspection"].toInt(-1)==0,"BM Cycles requires actual 3D BaseColor emission viewport");press(Qt::Key_F);});
  steps_.push_back([this]{arm_cycles(cycles_after_);action("renderingMode");get<QSpinBox>("renderSamples")->setValue(1024);get<QCheckBox>("renderDenoise")->setChecked(false);});
  steps_.push_back([this]{cycles_capture("cycles-adjustment-painted");arm_cycles(cycles_before_);action("undoDocument");});
  steps_.push_back([this]{cycles_capture("cycles-adjustment-undo");arm_cycles(cycles_after_);action("redoDocument");});
  steps_.push_back([this]{cycles_capture("cycles-adjustment-redo");action("paintingMode");});
  steps_.push_back([this]{const auto d=probe();require(!d["render_ui"].toBool()&&d["bm_layer"]==cycles_after_["bm_layer"]&&d["bm_lower"]==cycles_after_["bm_lower"]&&d["bm_probes"]==cycles_after_["bm_probes"],"BM return from Cycles lost latest adjustment");});
  steps_.push_back([this]{save_existing();});
  steps_.push_back([this]{save_checkpoint("smudge-adjustment-saved");project_file(false);});
  steps_.push_back([this]{press(Qt::Key_M);press(Qt::Key_C);press(Qt::Key_C);press(Qt::Key_C);require(state_()["bm_inspection"].toInt(-1)==2,"Actual postOpen Roughness inspection was not selected");});
  steps_.push_back([this]{require(state_()["bm_inspection"].toInt(-1)==2,"Actual postOpen Roughness inspection was lost");press(Qt::Key_M);press(Qt::Key_C);require(state_()["bm_inspection"].toInt(-1)==0,"Actual postOpen Color inspection was not restored");});
  steps_.push_back([this]{opened("smudge-adjustment-reopened",true);});
  steps_.push_back([this]{export_files();});
  steps_.push_back([this]{flatten_before_=probe();flatten_wait_=true;action("flattenLayer");});
  steps_.push_back([this]{flattened();});
  steps_.push_back([this]{flatten_undone();});
  connect(&timer_,&QTimer::timeout,this,[this]{advance();});timer_.setInterval(33);timer_.setTimerType(Qt::PreciseTimer);timer_.start();
 }
};
}
