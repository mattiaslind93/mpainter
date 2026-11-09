// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialImageUi.hh"
#include "NativeMaterialImageRefresh.hh"
#include "NativeMaterialImagePanel.hh"
#include "BlenderHost.hh"
#include "FileDialogs.hh"
#include <QFileDialog>
#include <QFileInfo>
namespace bts {
NativeMaterialImageUi::NativeMaterialImageUi(QWidget *owner,NativeMaterialImagePanel *panel,BlenderHost &host,NativeImageUiHooks hooks):QObject(owner),owner_(owner),panel_(panel),hooks_(std::move(hooks)){
 if(!panel)throw std::logic_error("Native image replacement panel was not constructed");
 NativeImageJobHooks jobs;
 jobs.prepare=[&host](const auto &id,const auto &hash,auto graph){host.prepare_native_material_gpu(id,hash,std::move(graph));};
 jobs.status=[&host](const auto &id){return host.native_material_status(id);};jobs.discard=[&host](const auto &id){host.discard_native_material_candidate(id);};
 jobs.redraw=hooks_.redraw;
 jobs.progress=[this](const auto &text){if(owner_)owner_->setProperty("nativeImageLoading",true);if(panel_)panel_->set_busy(true,text);if(hooks_.status)hooks_.status(text);};
 jobs.finished=[this]{if(owner_)owner_->setProperty("nativeImageLoading",false);if(panel_&&!panel_->property("nativeImageInspecting").toBool()&&!restrict_native_image_refresh(*panel_,NativeHeightPolicy(panel_->property("nativeImageHeightPolicy").toInt())))panel_->set_busy(false);};
 jobs.error=[this](const auto &text){active_.reset();error(text);};
 jobs.publish=[this](auto result,const auto &staging){if(!active_||!active_->current())throw std::runtime_error("Native image target changed during preparation");const auto target=*active_;const bool reset=reset_all_;active_.reset();hooks_.publish(std::move(result),staging,target,reset);refresh();if(hooks_.status)hooks_.status(reset?"Original source images restored":"Native image replaced");};
 jobs_=std::make_unique<NativeMaterialImageJobs>(nullptr,std::move(jobs));
 panel->source_changed=[this]{refresh();};panel->target_changed=[this]{if(active_||shelf_)cancel();else ++serial_;};panel->requested=[this](const auto &binding,bool file,const auto &policy){try{choose(binding,file,policy);}catch(const std::exception &e){error(QString::fromUtf8(e.what()));}};
 panel->reset_requested=[this]{try{reset();}catch(const std::exception &e){error(QString::fromUtf8(e.what()));}};panel->cancel_requested=[this]{cancel();};refresh();
}
NativeMaterialImageUi::~NativeMaterialImageUi(){if(panel_){panel_->source_changed={};panel_->target_changed={};panel_->requested={};panel_->reset_requested={};panel_->cancel_requested={};}jobs_.reset();}
void NativeMaterialImageUi::error(const QString &text){if(panel_)panel_->error(text);if(hooks_.error)hooks_.error(text);}
void NativeMaterialImageUi::refresh(){
 try{
  if(active_&&!active_->current())cancel();if(shelf_&&!shelf_->target.current())cancel();const auto target=hooks_.target();if(!target)return;
  if(restrict_native_image_refresh(*panel_,target->instance.height_policy))return;
  const auto id=target->source.id;const auto hash=target->source.hash();const bool reset=bool(native_image_variant_provenance(target->source));panel_->loading(target->source.name);
  jobs_->inspect(target->source,[this,id,hash,reset](auto result){if(!panel_)return;const auto target=hooks_.target();if(!target||target->instance.height_policy!=NativeHeightPolicy::None||target->source.id!=id||target->source.hash()!=hash)return;panel_->synchronize(std::move(result),reset);if(jobs_->pending()||shelf_)panel_->set_busy(true,"Preparing native image replacement…");},[this,id,hash](const auto &text){const auto target=hooks_.target();if(target&&target->instance.height_policy==NativeHeightPolicy::None&&target->source.id==id&&target->source.hash()==hash)error(text);});
 }catch(const std::exception &e){error(QString::fromUtf8(e.what()));}
}
void NativeMaterialImageUi::cancel(){++serial_;shelf_cancelled_|=bool(shelf_);shelf_.reset();active_.reset();jobs_->cancel();if(owner_)owner_->setProperty("nativeImageLoading",false);if(panel_)panel_->set_busy(false,"Image replacement cancelled");}
void NativeMaterialImageUi::begin(NativeImageChangeRequest request,NativeImageUiTarget target){
 if(!target.current())throw std::runtime_error("Native image target changed; choose the image slot again");
 require_native_height_image_replacement(target.instance.height_policy);
 const auto serial=++serial_;shelf_.reset();shelf_cancelled_=false;active_=target;reset_all_=request.reset_all;
 jobs_->change(std::move(request),[this,serial,current=target.current]{return serial_==serial&&current();});
}
void NativeMaterialImageUi::choose(const NativeImageBinding &binding,bool file,const NativeImageReplacementPolicy &policy){
 const auto target=hooks_.target();if(!target)throw std::runtime_error("Select an editable native material fill");
 require_native_height_image_replacement(target->instance.height_policy);
 NativeImageChangeRequest request;request.source=target->source;request.resources=target->resources;request.instance=target->instance;request.binding=binding;request.policy=policy;
 if(!file){cancel();shelf_cancelled_=false;shelf_=Pending{std::move(request),*target};if(owner_)owner_->setProperty("nativeImageLoading",true);panel_->set_busy(true,"Choose a texture from the Shelf");if(hooks_.shelf)hooks_.shelf();return;}
 const auto serial=serial_;
 auto *dialog=FileDialog::borrow(owner_,"Replace native image",{},"Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp *.webp);;All files (*)");dialog->setObjectName("nativeImageFileDialog");dialog->setFileMode(QFileDialog::ExistingFile);
 if(dialog->exec()!=QDialog::Accepted||dialog->selectedFiles().isEmpty())return;if(serial_!=serial)throw std::runtime_error("Native image selection changed while choosing a file");request.file=dialog->selectedFiles().first();FileDialog::remember_folder_async(QFileInfo(request.file).absolutePath());begin(std::move(request),*target);
}
bool NativeMaterialImageUi::consume_shelf(const StudioAsset &asset){
 if(!shelf_){if(shelf_cancelled_&&asset.usages.contains("texture")){shelf_cancelled_=false;error("Native image request was cancelled; choose an image slot again");return true;}return false;}
 try{if(!shelf_->target.current())throw std::runtime_error("Native image target changed; choose the image slot again");if(!asset.usages.contains("texture"))throw std::invalid_argument("Choose an image texture from the Shelf");auto pending=std::move(*shelf_);shelf_.reset();pending.request.replacement=asset;begin(std::move(pending.request),std::move(pending.target));}
 catch(const std::exception &e){cancel();error(QString::fromUtf8(e.what()));}return true;
}
void NativeMaterialImageUi::reset(){
 const auto target=hooks_.target();if(!target)throw std::runtime_error("Select an editable native material fill");
 require_native_height_image_replacement(target->instance.height_policy);NativeImageChangeRequest request;request.source=target->source;request.resources=target->resources;request.instance=target->instance;request.reset_all=true;begin(std::move(request),*target);
}
}
