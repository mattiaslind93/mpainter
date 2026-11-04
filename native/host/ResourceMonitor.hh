#pragma once
#include "ResourceScan.hh"
#include "FillTexture.hh"
#include <QObject>
#include <QTimer>
#include <future>
#include <functional>
class QWidget;
namespace bts {
struct PreparedResourceChange {
 ResourceChange change;
 std::shared_ptr<const FillTexture> color,raw;
};
struct ResourceMonitorHooks {
 std::function<std::vector<StudioAsset>(bool,bool)> sources;
 std::function<QString()> project;
 std::function<bool()> busy;
 std::function<void(const PreparedResourceChange &)> apply;
 std::function<void(const QString &)> log;
};
class ResourceMonitor final:public QObject {
 public:
 ResourceMonitor(QWidget *,ResourceMonitorHooks);
 ~ResourceMonitor();
 void refresh(bool library,bool project);
 protected:
 bool eventFilter(QObject *,QEvent *)override;
 private:
 struct Batch {QString project;std::vector<PreparedResourceChange> updates;QStringList messages;};
 ResourceMonitorHooks hooks_;QTimer timer_,poll_;std::future<Batch> job_;
 std::shared_ptr<std::atomic<bool>> cancel_;bool pending_library_=false,pending_project_=false;
 void configure();void poll();
};
}
