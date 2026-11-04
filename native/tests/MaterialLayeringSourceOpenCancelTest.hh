#pragma once
#include <QtCore>
#include "BlenderHost.hh"
#include "ProjectOpenJob.hh"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace bts {
// Invoke on an idle Host owner, after the positive cold Open callback returns.
// The probe must only fingerprint live Main/document/alias IDs and GPU handles;
// it must not upload, render, composite, read pixels or mutate the image cache.
// Both cases use the packed project directly; no original input file is needed.
inline QJsonObject test_material_layering_source_open_cancel(
    BlenderHost &host,const QString &packed_project,
    const std::function<QJsonObject()> &probe)
{
 const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 require(bool(probe)&&QFileInfo(packed_project).isFile(),"Source cancellation needs a probe and packed project");
 require(host.project_gpu_available(),"Source cancellation requires an idle GPU owner");
 QJsonArray cases;
 for(bool cpu_barrier:{true,false}){
  const auto before=probe();require(!before.isEmpty(),"Source cancellation fingerprint must not be empty");
  struct State {
   std::mutex mutex;std::condition_variable wake;
   bool released=false;
   std::atomic_bool prepared=false,barrier_seen=false,cancel_seen=false,barrier_timeout=false;
   std::atomic<PendingProject *> pending=nullptr;
   void release()noexcept{{std::lock_guard lock(mutex);released=true;}wake.notify_all();}
  };
  auto state=std::make_shared<State>();
  ProjectOpenJob job(host);QEventLoop loop;QTimer observe,deadline;
  observe.setInterval(1);deadline.setSingleShot(true);deadline.setInterval(30000);
  int ready_count=0,error_count=0,finished_count=0,stage_progress_count=0;
  bool cancelled=false,timed_out=false,pending_at_cancel=false,missing_stage_rejected=false;
  QString error,callback_failure;
  // Release the test barrier BEFORE any terminal worker drain, including on
  // exception/timeout. No worker wait occurs in either normal Cancel callback.
  const auto cleanup=qScopeGuard([&]{observe.stop();deadline.stop();state->release();job.shutdown();});
  const auto cancel=[&]{pending_at_cancel=job.pending();job.cancel();cancelled=true;state->release();};
  QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{timed_out=true;cancel();loop.quit();});
  QObject::connect(&observe,&QTimer::timeout,&loop,[&]{
   if(cpu_barrier&&state->prepared.load(std::memory_order_acquire)&&!cancelled)cancel();
  });
  job.start(
   [state,packed_project,cpu_barrier](auto token,auto progress){
    auto pending=PendingProject::prepare(packed_project.toStdString(),token,progress);
    pending->validate();
    if(!pending->layer_sources)throw std::runtime_error("Cancellation project has no staged original-backed sources");
    state->pending.store(pending.get(),std::memory_order_release);
    state->prepared.store(true,std::memory_order_release);
    if(cpu_barrier){
     // Deterministic result boundary: real preparation has completed, but the
     // future cannot become ready/publicable until the owner cancels it.
     state->barrier_seen.store(true,std::memory_order_release);
     std::unique_lock lock(state->mutex);
     if(!state->wake.wait_for(lock,std::chrono::seconds(30),[&]{return state->released||token->load();}))
      state->barrier_timeout.store(true,std::memory_order_release);
     state->cancel_seen.store(token->load(),std::memory_order_release);
    }
    return pending;
   },[]{return true;},
   [&](ProjectOpenResult result){++ready_count;result.clear();},
   [&](const QString &message){++error_count;error=message;},
   [&](const QString &message){
    if(cpu_barrier||message!=QStringLiteral("Preparing isolated project source GPU resources…"))return;
    ++stage_progress_count;
    // ProjectOpenJob emits this only after begin_project_gpu has returned and
    // before its first timer poll. No GPU-stage upload/dispatch is claimed.
    auto *pending=state->pending.load(std::memory_order_acquire);
    if(!pending){callback_failure="GPU-stage cancellation lost its prepared result";cancel();return;}
    try{host.validate_project_adoption(*pending,nullptr);}
    catch(const std::logic_error &message){
     missing_stage_rejected=QString::fromUtf8(message.what())==QStringLiteral("Source Open requires complete isolated owner GPU staging before adoption");
    }
    catch(const std::exception &message){callback_failure=QString::fromUtf8(message.what());}
    cancel();
   },
   [&]{++finished_count;loop.quit();});
  observe.start();deadline.start();loop.exec();observe.stop();deadline.stop();
  // A timeout may still have a CPU reader inside native preparation; release
  // first and perform the existing terminal owner drain, never detach it or
  // let pending BFD/worker state outlive the Host. The outer runner bounds OS
  // lifetime; this does not claim preemption of arbitrary native reader code.
  if(timed_out){state->release();job.shutdown();}
  require(!timed_out&&!state->barrier_timeout.load(),"Source cancellation timed out (barrier released and owner drained)");
  require(callback_failure.isEmpty(),qPrintable(callback_failure));
  require(error_count==0&&error.isEmpty(),"Cancelled source Open delivered an error callback");
  require(cancelled&&pending_at_cancel,"Source Open was not pending at its cancellation boundary");
  require(ready_count==0&&finished_count==1&&!job.pending(),"Cancelled source Open published or failed to finish exactly once");
  if(cpu_barrier)require(state->barrier_seen.load()&&state->cancel_seen.load(),"CPU result barrier did not observe the real Cancel token");
  else require(stage_progress_count==1&&missing_stage_rejected,"Source-stage/missing-stage admission boundary was not exercised");
  // finish() clears ProjectOpenResult (GPU holder before Pending BFD) before
  // calling finished. The owner guard and full caller probe then demonstrate
  // that the same live owner remains usable; no cleanup counters are invented.
  require(host.project_gpu_available(),"Source cancellation left its owner unavailable");
  const auto after=probe();require(after==before,"Cancelled source Open changed the live fingerprint");
  cases.append(QJsonObject{
   {"boundary",cpu_barrier?"worker_after_real_prepare_before_future_publication":"source_gpu_after_begin_before_first_poll"},
   {"ready_callbacks",ready_count},{"adopt_calls",0},{"finished_callbacks",finished_count},{"error_callbacks",error_count},
   {"pending_at_cancel",pending_at_cancel},{"pending_after",job.pending()},
   {"real_prepare_completed",state->prepared.load()},{"cancel_token_seen_at_barrier",cpu_barrier&&state->cancel_seen.load()},
   {"source_stage_progress_callbacks",stage_progress_count},{"missing_gpu_stage_rejected",missing_stage_rejected},
   {"before",before},{"after",after},{"owner_available_after",true},
   {"cleanup_contract","ProjectOpenJob::finish clears GPU holder before Pending BFD, then invokes finished; no allocator/context-loss instrumentation"}});
 }
 return QJsonObject{{"cases",cases},{"cancel_cases",2},{"uses_packed_project_only",true},
  {"scope","CPU-result barrier cancellation and source-stage cancellation before first poll; no physical OOM/context-loss or post-upload cancellation claim"}};
}
}
