// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialImageJobs.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialImageDependencies.hh"
#include "ImageSequence.hh"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <chrono>
namespace bts {
namespace {
void check(const std::atomic_bool &cancel){if(cancel.load())throw std::runtime_error("Native image replacement cancelled");}
StudioAsset file_source(const QString &path,const std::atomic_bool &cancel){
 const auto source=reference_image_resource(path,false,&cancel);const auto &file=source.files.front();

 StudioAsset asset;asset.name=source.name;asset.filename=source.filename;asset.usages={"texture"};asset.path=source.path;
 QFile input(path);if(!input.open(QIODevice::ReadOnly))throw std::runtime_error(input.errorString().toStdString());
 while(!input.atEnd()){check(cancel);auto bytes=input.read(4*1024*1024);if(bytes.isEmpty()&&input.error()!=QFileDevice::NoError)throw std::runtime_error(input.errorString().toStdString());if(asset.bytes.size()>file.size||bytes.size()>file.size-asset.bytes.size())throw std::runtime_error("Native image source changed during loading");asset.bytes.append(bytes);}
 if(asset.bytes.size()!=file.size||asset.hash()!=file.sha256)throw std::runtime_error("Native image source changed during loading");
 return asset;
}
NativeImageChange prepare_change(NativeImageChangeRequest request,const std::atomic_bool &cancel){
 check(cancel);
 if(request.reset_all){
  const auto *original=native_material_original_source(request.source,request.resources);if(!original)throw std::invalid_argument("This source has no image replacements to reset");
  auto graph=hydrate_native_material_resource(*original,cancel,request.resources);
  return {{*original,std::move(graph),{},{}},retain_native_image_instance(request.instance,*original->fill_preset->native_material)};
 }
 auto replacement=request.file.isEmpty()?std::move(request.replacement):file_source(request.file,cancel);
 auto result=prepare_native_image_variant(request.source,request.resources,request.instance,request.binding,replacement,request.policy,cancel);
 return {std::move(result.prepared),std::move(result.instance)};
}
}
NativeMaterialImageJobs::NativeMaterialImageJobs(QObject *parent,NativeImageJobHooks hooks):QObject(parent),hooks_(std::move(hooks)){
 timer_.setInterval(16);connect(&timer_,&QTimer::timeout,this,[this]{
  try{poll();}catch(const std::exception &error){try{if(hooks_.error)hooks_.error(QString::fromUtf8(error.what()));}catch(...){std::fprintf(stderr,"BTS_NATIVE_IMAGE_NOTIFICATION_ERROR\n");}}
  catch(...){std::fprintf(stderr,"BTS_NATIVE_IMAGE_POLL_ERROR\n");}
 });
 worker_=std::thread([this]{for(;;){std::function<void()> job;{std::unique_lock lock(mutex_);cv_.wait(lock,[this]{return stop_||!queue_.empty();});if(stop_&&queue_.empty())return;job=std::move(queue_.front());queue_.pop_front();}job();}});
}
NativeMaterialImageJobs::~NativeMaterialImageJobs(){
 timer_.stop();hooks_.finished={};hooks_.error={};cancel();for(auto &[key,job]:inspections_)job.cancel->store(true);for(auto &job:retired_)job.cancel->store(true);
 {std::lock_guard lock(mutex_);stop_=true;}cv_.notify_one();if(worker_.joinable())worker_.join();
}
void NativeMaterialImageJobs::inspect(const StudioAsset &asset,std::function<void(Availability)> ready,std::function<void(const QString &)> failed){
 if(!asset.fill_preset||!asset.fill_preset->native_material)throw std::invalid_argument("Native source interface is missing");
 const auto metadata=QJsonDocument(native_material_description_json(*asset.fill_preset->native_material)).toJson(QJsonDocument::Compact);
 const auto key=asset.hash()+":"+QString::fromLatin1(QCryptographicHash::hash(metadata,QCryptographicHash::Sha256).toHex())+":exact-image-edge-v1";
 auto found=inspections_.find(key);if(found!=inspections_.end()){
  if(found->second.result){ready(found->second.result);return;}if(!found->second.error.isEmpty()){failed(found->second.error);return;}
 }else{Inspection job;auto cancel=job.cancel;job.worker=submit([asset,cancel]{return std::make_shared<const NativeImageBindingAvailability>(native_material_image_availability(asset,*cancel));});try{found=inspections_.emplace(key,std::move(job)).first;}catch(...){cancel->store(true);throw;}}

 found->second.callbacks.emplace_back(std::move(ready),std::move(failed));timer_.start();
}
void NativeMaterialImageJobs::change(NativeImageChangeRequest request,std::function<bool()> current){
 cancel();if(!current||!current())return;
 // Every active change reserves its retirement slot before worker submission.
 // Cancellation must not allocate while releasing an unpublished GPU alias.
 static_assert(std::is_nothrow_move_constructible_v<Change>);
 if(retired_.size()==retired_.capacity()){const auto count=resource_sum(retired_.size(),1);retired_.reserve(count);}
 Change next;next.current=std::move(current);auto cancel=next.cancel;
 next.worker=submit([request=std::move(request),cancel]()mutable{return prepare_change(std::move(request),*cancel);});change_.emplace(std::move(next));timer_.start();if(hooks_.progress)hooks_.progress("Preparing native image source…");
}
void NativeMaterialImageJobs::cancel(){
 if(!change_)return;change_->cancel->store(true);const auto staging=change_->staging;const bool queued=change_->queued;
 retired_.push_back(std::move(*change_));change_.reset();
 if(queued&&hooks_.discard)try{hooks_.discard(staging);}catch(const std::exception &error){if(hooks_.error)hooks_.error(QString::fromUtf8(error.what()));}
 if(hooks_.finished)hooks_.finished();
}
void NativeMaterialImageJobs::poll(){
 using namespace std::chrono_literals;
 struct Finished{Availability result;QString error;decltype(Inspection::callbacks) callbacks;};std::vector<Finished> finished;
 for(auto &[key,job]:inspections_)if(job.worker.valid()&&job.worker.wait_for(0s)==std::future_status::ready){
  try{job.result=job.worker.get();}catch(const std::exception &error){job.error=QString::fromUtf8(error.what());}finished.push_back({job.result,job.error,std::move(job.callbacks)});
 }
 // Owner notifications can allocate or discover a stale/missing source. Never
 // let these exceptions escape QTimer dispatch or skip retirement of workers.
 const auto report=[&](const QString &message)noexcept{try{if(hooks_.error)hooks_.error(message);}catch(...){std::fprintf(stderr,"BTS_NATIVE_IMAGE_NOTIFICATION_ERROR\n");}};
 for(auto &job:finished)for(auto &[ready,failed]:job.callbacks){
  try{if(job.error.isEmpty()){if(ready)ready(job.result);}else if(failed)failed(job.error);}
  catch(const std::exception &error){report(QString::fromUtf8(error.what()));}
  catch(...){report(QStringLiteral("Native image inspection notification failed"));}
 }
 std::erase_if(retired_,[](Change &job){if(!job.worker.valid())return true;if(job.worker.wait_for(0s)!=std::future_status::ready)return false;try{job.worker.get();}catch(...){}return true;});
 if(change_)try{
  if(!change_->current()){cancel();return;}
  if(!change_->result&&change_->worker.valid()&&change_->worker.wait_for(0s)==std::future_status::ready)change_->result=change_->worker.get();
  if(change_->result&&!change_->queued){auto &result=*change_->result;change_->queued=true;hooks_.prepare(change_->staging,result.prepared.asset.hash(),std::move(result.prepared.graph));}
  if(change_->queued){const auto status=hooks_.status(change_->staging);if(!status.error.isEmpty())throw std::runtime_error(status.error.toStdString());if(status.ready){auto result=std::move(*change_->result);const auto staging=change_->staging;change_.reset();try{hooks_.publish(std::move(result),staging);}catch(...){hooks_.discard(staging);throw;}hooks_.discard(staging);if(hooks_.finished)hooks_.finished();}else{if(!status.pending)throw std::runtime_error("Project changed during native image GPU preparation");if(hooks_.progress)hooks_.progress(status.message);if(hooks_.redraw)hooks_.redraw();}}
 }catch(const std::exception &error){cancel();if(hooks_.finished)hooks_.finished();if(hooks_.error)hooks_.error(QString::fromUtf8(error.what()));}
 const bool inspecting=std::any_of(inspections_.begin(),inspections_.end(),[](const auto &entry){return entry.second.worker.valid();});if(!change_&&!inspecting&&retired_.empty())timer_.stop();
}
}
