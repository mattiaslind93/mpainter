#include "OwnerWorkerJob.hh"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cassert>
#include <thread>
using namespace bts;
struct Owned {std::thread::id *retired;~Owned(){*retired=std::this_thread::get_id();}};
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);const auto owner=std::this_thread::get_id();
 OwnerWorkerJob<std::unique_ptr<Owned>> job;std::thread::id retired;int beats=0,ready=0,errors=0,finished=0,release_after=0;
 std::shared_ptr<std::atomic_bool> release;
 QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,[&]{++beats;if(release&&beats>=release_after)release->store(true);});heartbeat.start();
 const auto wait=[&]{QElapsedTimer timeout;timeout.start();while(job.pending()){assert(timeout.elapsed()<5000);QCoreApplication::processEvents();QThread::msleep(1);}};
 const auto work=[&]{release=std::make_shared<std::atomic_bool>(false);release_after=beats+20;return [&,gate=release](auto,auto report){assert(std::this_thread::get_id()!=owner);report("Worker progress");while(!gate->load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return std::make_unique<Owned>(&retired);};};
 const auto error=[&](auto){assert(std::this_thread::get_id()==owner);++errors;};
 const auto done=[&]{assert(std::this_thread::get_id()==owner);++finished;};
 job.start(work(),[]{return true;},[&](auto){assert(std::this_thread::get_id()==owner);++ready;},error,{},done);wait();
 assert(ready==1&&errors==0&&finished==1&&beats>=20&&retired==owner);
 retired={};job.start(work(),[]{return true;},[&](auto){++ready;},error,{},done);job.cancel();assert(!release->load());wait();assert(ready==1&&retired==owner&&finished==2);
 retired={};job.start(work(),[]{return false;},[&](auto){++ready;},error,{},done);wait();assert(errors==1&&ready==1&&retired==owner);
 retired={};job.start(work(),[]{return true;},[&](auto){throw std::runtime_error("Rejected document");},error,{},done);wait();assert(errors==2&&retired==owner);
 // A completed read may await an accepted Save, while owner events keep firing.
 const int deliver_after=beats+35;job.start(work(),[]{return true;},[&](auto){assert(beats>=deliver_after);++ready;},error,{},done,[&]{return beats>=deliver_after;});wait();assert(ready==2&&retired==owner);
 job.start([](auto,auto)->std::unique_ptr<Owned>{throw 42;},[]{return true;},[&](auto){++ready;},error);wait();assert(errors==3&&ready==2);
 retired={};job.start(work(),[]{return true;},[&](auto){++ready;},error);release->store(true);job.shutdown();assert(retired==owner&&!job.pending()&&ready==2);
 std::puts("OWNER_WORKER_JOB PASS timer/cancel/stale/failure/save-gate/shutdown ownership");
}
