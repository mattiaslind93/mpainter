#pragma once
#include "NativeMaterialImageVariant.hh"
#include "NativeMaterialPreparationStatus.hh"
#include <QObject>
#include <QTimer>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <thread>
namespace bts {
struct NativeImageChange {
 PreparedNativeMaterialResource prepared;
 NativeMaterialDescription instance;
};
struct NativeImageChangeRequest {
 StudioAsset source;
 std::vector<StudioAsset> resources;
 NativeMaterialDescription instance;
 NativeImageBinding binding;
 StudioAsset replacement;
 QString file;
 NativeImageReplacementPolicy policy;
 bool reset_all=false;
};
struct NativeImageJobHooks {
 std::function<void(const QUuid &,const QString &,std::shared_ptr<NativeMaterialGraph>)> prepare;
 std::function<NativeMaterialPreparationStatus(const QUuid &)> status;
 std::function<void(const QUuid &)> discard;
 // Called once after worker + frame-stepped GPU readiness and final validity.
 // Only the private staging alias is passed; it is never a catalog asset UUID.
 std::function<void(NativeImageChange,const QUuid &)> publish;
 std::function<void()> redraw;
 std::function<void()> finished;
 std::function<void(const QString &)> progress,error;
};
class NativeMaterialImageJobs final:public QObject {
 public:
 using Availability=std::shared_ptr<const NativeImageBindingAvailability>;
 NativeMaterialImageJobs(QObject *,NativeImageJobHooks);
 ~NativeMaterialImageJobs();
 // Coalesces and caches immutable results. Refreshes do not repeat probes.
 void inspect(const StudioAsset &,std::function<void(Availability)>,std::function<void(const QString &)>);
 void change(NativeImageChangeRequest,std::function<bool()> still_current);
 void cancel();
 bool pending()const{return bool(change_);}
 private:
 template<class F>auto submit(F function){
  using T=std::invoke_result_t<F>;auto task=std::make_shared<std::packaged_task<T()>>(std::move(function));auto future=task->get_future();
  {std::lock_guard lock(mutex_);queue_.emplace_back([task]{(*task)();});}cv_.notify_one();return future;
 }
 struct Inspection {
  std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);
  std::future<Availability> worker;
  Availability result;QString error;
  std::vector<std::pair<std::function<void(Availability)>,std::function<void(const QString &)>>> callbacks;
 };
 struct Change {
  std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);
  std::future<NativeImageChange> worker;
  std::optional<NativeImageChange> result;
  std::function<bool()> current;
  QUuid staging=QUuid::createUuid();bool queued=false;
 };
 void poll();
 NativeImageJobHooks hooks_;QTimer timer_;
 std::map<QString,Inspection> inspections_;
 std::optional<Change> change_;std::vector<Change> retired_;
 std::mutex mutex_;std::condition_variable cv_;std::deque<std::function<void()>> queue_;bool stop_=false;std::thread worker_;
};
}
