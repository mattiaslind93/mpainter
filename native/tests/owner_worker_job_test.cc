// SPDX-License-Identifier: GPL-3.0-or-later
#include "OwnerWorkerJob.hh"
#include "LongOperationCancel.hh"
#include "TextureDocument.hh"
#include <QLineEdit>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cassert>
#include <thread>
using namespace bts;
struct Owned {std::thread::id *retired;~Owned(){*retired=std::this_thread::get_id();}};
int main(int argc,char **argv){
 QApplication app(argc,argv);const auto owner=std::this_thread::get_id();
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

 // Escape from a focused editor cancels a detached load even while delivery is
 // gated. No candidate may replace the current document; UI heartbeats continue.
 {
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Original";material.uniform_editable=true;scene.materials={material};ImportSummary imported;imported.materials={{1,"Original"}};TextureDocument document({},imported,scene);const auto original=document.to_json();QLineEdit editor;int cancels=0;
  LongOperationCancel escape([&]{return job.pending();},[&]{++cancels;job.cancel();});
  retired={};job.start(work(),[]{return true;},[&](auto){TextureDocument::add_fill(document.set(1));},error,{},done,[]{return false;});
  QKeyEvent override(QEvent::ShortcutOverride,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&editor,&override);assert(override.isAccepted()&&cancels==0);
  const int start=beats;QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&editor,&key);
  assert(cancels==1&&job.pending());wait();assert(document.to_json()==original&&retired==owner&&beats>start);
  QApplication::sendEvent(&editor,&key);assert(cancels==1);
 }
 // The bake progress dialog keeps pumping events until cancellation completes.
 {
  CancellableOperationDialog progress;bool running=true;int cancelled=0;
  progress.pending=[&]{return running;};progress.cancel=[&]{++cancelled;};
  QLineEdit editor(&progress);LongOperationCancel escape(progress.pending,[&]{progress.reject();});const int start=beats;
  QTimer::singleShot(0,&progress,[&]{QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&editor,&key);assert(cancelled==1&&progress.isVisible());});
  QTimer::singleShot(30,&progress,[&]{assert(progress.isVisible()&&beats>start);running=false;progress.reject();});
  assert(progress.exec()==QDialog::Rejected&&cancelled==1);
 }
 std::puts("OWNER_WORKER_JOB PASS timer/cancel/stale/failure/save-gate/shutdown ownership");
}
