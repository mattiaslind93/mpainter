// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialLayeringPanelTest.hh"
#include "MaterialLayeringPanel.hh"
#include "MaterialLayeringUi.hh"
#include "MaterialLayeringIO.hh"
#include "ColorPicker.hh"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <cassert>
#include <memory>
namespace bts {
namespace {
ColorPicker &picker(MaterialLayeringPanel &panel){
 auto *widget=panel.findChild<QWidget *>("uniformMaterialColor");assert(widget);
 return *static_cast<ColorPicker *>(widget); // Owned exact widget; no Q_OBJECT subclass cast.
}
QDoubleSpinBox &scalar(MaterialLayeringPanel &panel){auto *field=panel.findChild<QDoubleSpinBox *>("uniformMaterialScalar0");assert(field);return *field;}
void pending(QDoubleSpinBox &spin){
 QApplication::processEvents();assert(spin.isVisible()&&spin.isEnabled()&&!spin.keyboardTracking());
 auto *line=spin.findChild<QLineEdit *>();assert(line);const double before=spin.value();assert(before!=.5);
 spin.setFocus();
 const auto key=[&](int code,Qt::KeyboardModifiers modifiers,const QString &text){QKeyEvent down(QEvent::KeyPress,code,modifiers,text),up(QEvent::KeyRelease,code,modifiers,text);QApplication::sendEvent(&spin,&down);QApplication::sendEvent(&spin,&up);};
 key(Qt::Key_A,Qt::ControlModifier,"a");key(Qt::Key_0,Qt::NoModifier,"0");key(Qt::Key_Period,Qt::NoModifier,".");key(Qt::Key_5,Qt::NoModifier,"5");
 assert(line->text().toDouble()==.5&&spin.value()==before);
}
void show(MaterialLayeringPanel &panel){panel.show();QApplication::processEvents();assert(panel.isVisible()&&panel.width()<=400&&panel.height()<=720);}
void reveal_linear(ColorPicker &color){
 color.findChild<QPushButton *>("colorSwatch")->click();
 auto *popup=color.findChild<QDialog *>("colorPickerDialog");assert(popup&&popup->isVisible());
 auto *advanced=color.findChild<QCheckBox *>("colorAdvanced");assert(advanced&&!advanced->isChecked());advanced->setChecked(true);QApplication::processEvents();
}
}
QJsonObject test_material_layering_panel(){
 // Host initialized native IMB transforms: no replacement color conversion stubs.
 TextureSet set;set.uniform_editable=true;int callbacks=0;std::array<MaterialValues,4> applied;
 {MaterialLayeringPanel panel;panel.load(set,"Linear Rec.709");show(panel);
  auto &color=picker(panel);auto *advanced=color.findChild<QCheckBox *>("colorAdvanced");assert(advanced&&!advanced->isChecked());
  auto *popup=color.findChild<QDialog *>("colorPickerDialog");assert(popup&&popup->windowTitle().contains("Linear Rec.709"));
  assert(!color.linear_fields()[0]->isVisible());
  bool linear_label=false;for(auto *label:color.findChildren<QLabel *>())linear_label|=label->text()=="Linear Linear Rec.709 / HDR";assert(linear_label);
  panel.apply_requested=[&](bool enabled,const auto &values){assert(enabled);++callbacks;applied=values;};
  panel.findChild<QCheckBox *>("uniformMaterialLayeringEnabled")->setChecked(true);
  color.accept_sample({.125f,.25f,.375f,1});assert(callbacks==0);
  panel.findChild<QComboBox *>("uniformMaterialInput")->setCurrentIndex(2);color.accept_sample({.875f,.5f,.25f,1});
  panel.findChild<QComboBox *>("uniformMaterialInput")->setCurrentIndex(0);assert(color.color()[0]==.125f&&callbacks==0);
  panel.findChild<QPushButton *>("uniformMaterialLayeringApply")->click();
  assert(callbacks==1&&applied[0].base_color[0]==.125f&&applied[2].base_color[0]==.875f&&applied[1].base_color==set.material_layering.materials[1].values.base_color);
 }
 {MaterialLayeringPanel panel;panel.load(set,"ACEScg");panel.apply_requested=[&](bool,const auto&){++callbacks;};show(panel);picker(panel).accept_sample({.3f,.4f,.5f,1});panel.reject();assert(callbacks==1);}
 {MaterialLayeringPanel panel;panel.load(set,"ACEScg");panel.apply_requested=[](bool,const auto&){throw std::runtime_error("stale original target");};show(panel);scalar(panel).setValue(.2);panel.findChild<QPushButton *>("uniformMaterialLayeringApply")->click();assert(panel.isVisible()&&panel.result()!=QDialog::Accepted&&panel.findChild<QLabel *>("uniformMaterialLayeringMessage")->text().contains("stale"));}
 // Positive unchanged Qt hide control; the dirty text really commits through its own context.
 {MaterialLayeringPanel panel;panel.load(set,"ACEScg");show(panel);auto &field=scalar(panel);int edits=0;QObject::connect(&field,&QDoubleSpinBox::valueChanged,&panel,[&]{++edits;});pending(field);panel.hide();assert(edits==1&&field.value()==.5&&callbacks==1);}
 // Retire before hide, direct destructor, and nested picker pending text. No Apply/model write.
 for(int mode=0;mode<3;++mode){int edits=0;auto panel=std::make_unique<MaterialLayeringPanel>();panel->load(set,"ACEScg");panel->apply_requested=[&](bool,const auto&){++callbacks;};show(*panel);
  auto &field=mode==2?*picker(*panel).linear_fields()[0]:scalar(*panel);if(mode==2)reveal_linear(picker(*panel));
  QObject::connect(&field,&QDoubleSpinBox::valueChanged,panel.get(),[&]{++edits;});pending(field);
  if(mode==0){panel->retire_callbacks();panel->retire_callbacks();panel->hide();}panel.reset();assert(edits==0&&callbacks==1&&!set.material_layering.enabled);
 }
 // Standalone picker fallback also retires child connections before member lifetime ends.
 {int edits=0;auto color=std::make_unique<ColorPicker>();assert(color->findChild<QDialog *>("colorPickerDialog")->windowTitle().contains("ACEScg"));color->set_color({0,0,0,1});color->changed=[&](const auto&){++edits;};color->show();reveal_linear(*color);pending(*color->linear_fields()[0]);color.reset();assert(edits==0);}
 // Maps and per-input SRT are only panel data until explicit Apply.
 auto color_map=std::make_shared<FillTexture>();color_map->asset=std::string(36,'a');color_map->hash=std::string(64,'b');color_map->name="Buffered linear source";color_map->width=color_map->height=2;color_map->input_space="ACEScg";color_map->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{.25f,.5f,.75f,0});auto raw_map=std::make_shared<FillTexture>(*color_map);raw_map->raw=true;raw_map->input_space="Non-Color/raw";
 for(bool commit:{false,true}){MaterialLayeringPanel panel;panel.load(set,"ACEScg");show(panel);int calls=0;panel.apply_inputs_requested=[&](bool,const auto &inputs){++calls;assert(inputs[0].textures[0]==color_map&&inputs[2].textures[2]==raw_map&&inputs[0].tiling[0]==2&&inputs[2].offset[1]==.125&&inputs[0].offset[1]==0);return true;};panel.install_texture(0,0,color_map);panel.install_texture(2,2,raw_map);panel.findChild<QDoubleSpinBox *>("materialInputMapping0")->setValue(2);panel.findChild<QComboBox *>("uniformMaterialInput")->setCurrentIndex(2);panel.findChild<QDoubleSpinBox *>("materialInputMapping3")->setValue(.125);assert(calls==0&&!set.material_layering.materials[0].textures[0]);if(commit){panel.findChild<QPushButton *>("uniformMaterialLayeringApply")->click();assert(calls==1);}else{panel.reject();assert(calls==0);}assert(!set.material_layering.materials[2].textures[2]);}
 // Raw Bump shares the real File/Shelf route, stays buffered, and has no Normal choice.
 for(bool commit:{false,true}){MaterialLayeringPanel panel;panel.load(set,"ACEScg");show(panel);auto *channels=panel.findChild<QComboBox *>("materialInputSourceChannel");assert(channels&&channels->findData(4)>=0&&channels->findData(3)<0);channels->setCurrentIndex(channels->findData(4));assert(!panel.findChild<QComboBox *>("materialInputColorSpace")->isVisible());int requests=0,calls=0;
  panel.source_requested=[&](int input,int channel,bool file,const std::string &space){assert(input==0&&channel==4&&space.empty());assert(file==(requests==0));++requests;};
  panel.findChild<QPushButton *>("materialInputFile")->click();panel.findChild<QPushButton *>("materialInputShelf")->click();assert(requests==2);
  panel.apply_inputs_requested=[&](bool,const auto &inputs){++calls;assert(inputs[0].textures[4]==raw_map&&inputs[0].values.bump==.5f&&inputs[0].values.normal==(std::array<float,3>{0,0,1}));return true;};panel.install_texture(0,4,raw_map);assert(!set.material_layering.materials[0].textures[4]&&calls==0);
  if(commit){panel.findChild<QPushButton *>("uniformMaterialLayeringApply")->click();assert(calls==1);}else{panel.reject();assert(calls==0);}
 }
 QWidget owner;QVBoxLayout layout(&owner);TextureSet current;current.uniform_editable=true;configure_material_layering(current,true);std::uint64_t revision=1;const auto project=QUuid::createUuid();int applies=0,selections=0;QString error;
 MaterialLayeringHooks hooks;hooks.current=[&]() -> std::optional<MaterialLayeringTarget>{return MaterialLayeringTarget{project,current.id,1,revision,&current,"ACEScg",{}};};hooks.validate=[](const auto&){};hooks.begin_parameter_export=[](const auto&,const auto&){};hooks.poll_parameter_export=[](bool){return true;};hooks.cancel_parameter_export=[]{};hooks.error=[&](const QString &e){error=e;};hooks.select=[&](const auto&,int which){++selections;select_material_substack(current,which);};hooks.apply=[&](const auto&,bool enabled,const auto &values){++applies;current=prepare_uniform_material_layering(current,enabled,values);++revision;};
 auto ui=std::make_unique<MaterialLayeringUi>(owner,layout,std::move(hooks));auto *selector=owner.findChild<QComboBox *>("materialOpacitySubstack");assert(selector&&selector->count()==4&&selector->isEnabled());selector->setCurrentIndex(0);assert(selections==1&&current.material_layering.active_mask==-1&&owner.findChild<QLabel *>("materialLayeringScope")->text().contains("read-only"));
 const auto roots=current.material_layering.mask_roots;owner.findChild<QToolButton *>("editUniformMaterialLayering")->click();auto *buffer=static_cast<MaterialLayeringPanel *>(owner.findChild<QDialog *>("uniformMaterialLayeringDialog"));assert(buffer&&buffer->windowModality()==Qt::NonModal&&!buffer->isModal()&&QApplication::activeModalWidget()!=buffer);picker(*buffer).accept_sample({.345f,.1f,.2f,1});assert(applies==0&&current.material_layering.mask_roots==roots);++revision;buffer->findChild<QPushButton *>("uniformMaterialLayeringApply")->click();assert(applies==0&&buffer->findChild<QLabel *>("uniformMaterialLayeringMessage")->text().contains("changed"));buffer->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);assert(owner.findChild<QToolButton *>("exportMaterialLayerParameters")->isEnabled());
 owner.findChild<QToolButton *>("editUniformMaterialLayering")->click();buffer=static_cast<MaterialLayeringPanel *>(owner.findChild<QDialog *>("uniformMaterialLayeringDialog"));assert(buffer);buffer->findChild<QCheckBox *>("uniformMaterialLayeringEnabled")->setChecked(false);buffer->findChild<QPushButton *>("uniformMaterialLayeringApply")->click();assert(applies==1&&!current.material_layering.enabled&&current.material_layering.mask_roots==roots);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);ui->synchronize();assert(!selector->isEnabled());
 // Ui-owned early destruction while a real scalar editor is pending.
 const auto retained=material_layering_json(current.material_layering);owner.findChild<QToolButton *>("editUniformMaterialLayering")->click();buffer=static_cast<MaterialLayeringPanel *>(owner.findChild<QDialog *>("uniformMaterialLayeringDialog"));assert(buffer);pending(scalar(*buffer));ui.reset();assert(applies==1&&material_layering_json(current.material_layering)==retained&&!current.material_layering.enabled);
 // Return commits a real numeric field; it must never choose a File/Shelf/Clear
 // button as QDialog's implicit default action.
 {MaterialLayeringPanel panel;panel.load(set,"ACEScg");show(panel);int requested=0,cleared=0;panel.source_requested=[&](int,int,bool,const auto&){++requested;};panel.source_cleared=[&](int,int){++cleared;};auto *field=panel.findChild<QDoubleSpinBox *>("materialInputMapping0");assert(field);for(const char *name:{"materialInputFile","materialInputShelf","materialInputClear"}){auto *button=panel.findChild<QPushButton *>(name);assert(button&&!button->autoDefault()&&!button->isDefault());}panel.findChild<QScrollArea *>()->ensureWidgetVisible(field);QApplication::processEvents();assert(field->isVisible()&&!field->visibleRegion().isEmpty());field->setFocus();QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);assert(requested==0&&cleared==0&&panel.isVisible());}
 // Cancel must clean owner state before the panel blocks finished(), including
 // pending worker slots and an Apply that has started asynchronous GPU prepare.
 {QWidget cancel_owner;QVBoxLayout cancel_layout(&cancel_owner);int cancelled=0,released=0,prepared=0;TextureLoadTarget loading;
  MaterialLayeringHooks cancel_hooks;cancel_hooks.current=[&]() -> std::optional<MaterialLayeringTarget>{return MaterialLayeringTarget{project,current.id,1,revision,&current,"ACEScg",{}};};cancel_hooks.validate=[](const auto&){};cancel_hooks.cancel_load=[&](const auto&){++cancelled;};cancel_hooks.release=[&]{++released;};cancel_hooks.apply_inputs=[&](const auto&,bool,const auto&,const auto&){++prepared;return false;};cancel_hooks.load_asset=[&](auto target,const auto&,const auto&){loading=target;};
  MaterialLayeringUi cancel_ui(cancel_owner,cancel_layout,std::move(cancel_hooks));cancel_owner.findChild<QToolButton *>("editUniformMaterialLayering")->click();auto *cancel_panel=static_cast<MaterialLayeringPanel *>(cancel_owner.findChild<QDialog *>("uniformMaterialLayeringDialog"));assert(cancel_panel);cancel_panel->source_requested(0,0,false,"ACEScg");StudioAsset asset;asset.usages={"texture"};assert(cancel_ui.consume_shelf(asset)&&cancel_ui.load_target_current(loading));cancel_panel->findChild<QCheckBox *>("uniformMaterialLayeringEnabled")->setChecked(true);cancel_panel->findChild<QPushButton *>("uniformMaterialLayeringApply")->click();assert(prepared==1&&released==0);const int before_cancel=cancelled;cancel_panel->reject();assert(cancelled-before_cancel==20&&released==1&&!cancel_ui.load_target_current(loading));cancel_panel->retire_callbacks();assert(released==1);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
 }
 // A nested FileDialog event may replace the document and invalidate the old
 // owner-call-only TextureSet pointer. Neither case may begin capture or publish.
 for(int changed=0;changed<2;++changed){QWidget export_owner;QVBoxLayout export_layout(&export_owner);QTemporaryDir directory;assert(directory.isValid());auto active=std::make_unique<TextureSet>();active->uniform_editable=true;configure_material_layering(*active,true);auto project_id=QUuid::createUuid();std::uint64_t export_revision=1;int begins=0;QString message;
  MaterialLayeringHooks export_hooks;export_hooks.current=[&]() -> std::optional<MaterialLayeringTarget>{return MaterialLayeringTarget{project_id,active->id,1,export_revision,active.get(),"ACEScg",{}};};export_hooks.begin_parameter_export=[&](const auto&,const auto&){++begins;};export_hooks.poll_parameter_export=[](bool){return true;};export_hooks.cancel_parameter_export=[]{};export_hooks.error=[&](const auto &error){message=error;};MaterialLayeringUi export_ui(export_owner,export_layout,std::move(export_hooks));
  const auto destination=directory.filePath("not-created.btsparams");QTimer::singleShot(0,&export_owner,[&]{auto *dialog=export_owner.findChild<QFileDialog *>("materialParameterExportDialog");assert(dialog&&dialog->isVisible());dialog->selectFile(destination);if(changed==0)++export_revision;else{project_id=QUuid::createUuid();auto next=std::make_unique<TextureSet>(*active);active=std::move(next);}QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
  auto *button=export_owner.findChild<QToolButton *>("exportMaterialLayerParameters");assert(button&&button->isEnabled());button->click();assert(begins==0&&message.contains("target changed")&&!QFileInfo::exists(destination)&&!export_owner.findChild<QProgressDialog *>("materialParameterExportProgress"));
 }
 // Import chooses the package DIRECTORY. Neither a nested revision change nor
 // a document replacement may forward its previous owner-only TextureSet pointer.
 for(int changed=0;changed<3;++changed){QWidget import_owner;QVBoxLayout import_layout(&import_owner);QTemporaryDir directory;assert(directory.isValid());auto active=std::make_unique<TextureSet>();active->uniform_editable=true;auto project_id=QUuid::createUuid();std::uint64_t import_revision=1;int begins=0,cancels=0;QString message;
  MaterialLayeringHooks import_hooks;import_hooks.current=[&]() -> std::optional<MaterialLayeringTarget>{return MaterialLayeringTarget{project_id,active->id,1,import_revision,active.get(),"ACEScg",{}};};import_hooks.begin_parameter_import=[&](const auto&,const auto&){++begins;};import_hooks.parameter_import_status=[]{return MaterialParameterImportStatus{};};import_hooks.cancel_parameter_import=[&]{++cancels;};import_hooks.error=[&](const auto &error){message=error;};MaterialLayeringUi import_ui(import_owner,import_layout,std::move(import_hooks));
  QTimer::singleShot(0,&import_owner,[&]{auto *dialog=import_owner.findChild<QFileDialog *>("materialParameterImportDialog");assert(dialog&&dialog->isVisible()&&dialog->fileMode()==QFileDialog::Directory);dialog->setDirectory(directory.path());dialog->selectFile(directory.path());if(changed==0)++import_revision;else if(changed==1){project_id=QUuid::createUuid();active=std::make_unique<TextureSet>(*active);}if(changed==2)dialog->reject();else QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});
  auto *button=import_owner.findChild<QToolButton *>("importMaterialLayerParameters");assert(button&&button->isEnabled());button->click();assert(begins==0&&cancels==0&&(changed==2||message.contains("target changed"))&&!import_owner.findChild<QProgressDialog *>("materialParameterImportProgress"));
 }
 {QWidget import_owner;QVBoxLayout import_layout(&import_owner);QTemporaryDir directory;assert(directory.isValid());TextureSet active;active.uniform_editable=true;MaterialParameterImportStatus state;int begins=0,cancels=0;QString selected;
  MaterialLayeringHooks hooks;hooks.current=[&]() -> std::optional<MaterialLayeringTarget>{return MaterialLayeringTarget{project,active.id,1,1,&active,"ACEScg",{}};};hooks.begin_parameter_import=[&](const auto &target,const auto &folder){assert(target.texture==&active);selected=folder;++begins;state={true,false,"Reading package…",{}};};hooks.parameter_import_status=[&]{return state;};hooks.cancel_parameter_import=[&]{++cancels;state.cancelled=true;state.text="Cancelling…";};MaterialLayeringUi ui(import_owner,import_layout,std::move(hooks));
  QTimer::singleShot(0,&import_owner,[&]{auto *dialog=import_owner.findChild<QFileDialog *>("materialParameterImportDialog");assert(dialog);dialog->setDirectory(directory.path());dialog->selectFile(directory.path());QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});auto *button=import_owner.findChild<QToolButton *>("importMaterialLayerParameters");button->click();assert(begins==1&&selected==directory.path()&&!button->isEnabled());auto *progress=import_owner.findChild<QProgressDialog *>("materialParameterImportProgress");assert(progress&&progress->windowModality()==Qt::NonModal);QMetaObject::invokeMethod(progress,"canceled",Qt::DirectConnection);assert(cancels==1&&state.pending);ui.synchronize();assert(!button->isEnabled());state.pending=false;state.text="Parameter import cancelled; project unchanged.";QElapsedTimer deadline;deadline.start();while(import_owner.findChild<QProgressDialog *>("materialParameterImportProgress")&&deadline.elapsed()<1000){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}assert(!import_owner.findChild<QProgressDialog *>("materialParameterImportProgress")&&button->isEnabled()&&begins==1);
 }
 return {{"parameter_import_directory_dialog_reacquires_target",true},{"parameter_import_cancel_retires_before_reenable",true},{"raw_bump_buffered_file_shelf_no_Normal_UI",true},{"parameter_export_nested_dialog_reacquires_target",true},{"parameter_export_reenabled_after_panel_delete",true},{"numeric_enter_never_chooses_source_action",true},{"cancel_cleans_worker_and_gpu_preparation",true},{"buffered_maps_and_independent_srt",true},{"map_cancel_keeps_document",true},{"buffered_four_inputs",true},{"real_color_picker",true},{"recorded_space_label",true},{"pending_hide_positive_control",true},{"pending_retirement_destructor_and_ui_owner",true},{"stale_apply_rejected",true},{"disable_retains_roots",true}};
}
}
