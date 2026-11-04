#pragma once
#include "Performance.hh"
#include <QApplication>
#include <QEvent>
namespace bts {
// Opt-in event timing includes Qt composition/presentation outside BlenderHost::draw.
class ProfileApplication final:public QApplication {
 public:
 using QApplication::QApplication;
 bool notify(QObject *receiver,QEvent *event)override{
  if(!performance::enabled())return QApplication::notify(receiver,event);
  static thread_local int depth=0,events=0;static thread_local double accounted_cpu=0;const bool ui=QThread::currentThread()==thread();if(ui&&depth==0)performance::start_ui_event();const int previous=++events;++depth;
  const double nested_cpu_before=accounted_cpu;const auto start=performance::Clock::now();const double cpu=performance::thread_ms();
  const int type=int(event->type());const auto name=receiver?receiver->objectName():QString{};
  const char *kind=receiver?receiver->metaObject()->className():"null";
  const bool result=QApplication::notify(receiver,event);const double wall=performance::ms(start);
  --depth;const double cpu_ms=performance::thread_ms()-cpu;const std::string event_name="qt_event:"+std::to_string(type)+":"+kind+":"+name.toStdString();
  const double self_cpu=std::max(0.,cpu_ms-(accounted_cpu-nested_cpu_before));accounted_cpu+=self_cpu;
  if(ui&&wall>100&&(events-previous<=1||self_cpu>100))performance::warn_stall(event_name,wall,cpu_ms,events-previous);
  if(wall>=20||(ui&&depth==0&&qEnvironmentVariableIsSet("BTS_AA_PERFORMANCE")))performance::record({performance::flow(),"qt_event:"+std::to_string(type)+":"+kind+":"+name.toStdString(),wall,performance::thread_ms()-cpu,0,true});
  return result;
 }
};
}
