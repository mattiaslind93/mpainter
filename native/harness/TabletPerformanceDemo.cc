// SPDX-License-Identifier: GPL-3.0-or-later
#include "TabletPerformanceDemo.hh"
#include "SyntheticInputGuard.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPointingDevice>
#include <QPushButton>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <cmath>

namespace bts {
namespace {
class TabletDemo final : public QObject {
  QWidget *workspace_, *viewport_;
  SyntheticInputGuard input_guard_;
  QString directory_, mesh_;
  int resolution_, stage_ = 0, run_ = 0, sample_ = 0;
  std::function<QJsonObject()> state_;
  QTimer timer_, ping_;
  QElapsedTimer ping_clock_, burst_clock_;
  double max_ping_ = 0;
  QJsonObject before_;
  QJsonArray runs_;
  QJsonObject hover_;
  QPointingDevice device_{"BTS queued pressure test",
                          0x425454,
                          QInputDevice::DeviceType::Stylus,
                          QPointingDevice::PointerType::Pen,
                          QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                          1,
                          1};
  // All runs follow precisely the same polyline. Tablet pressure rises from .15 to 1.
  QPointF point(int index) const
  {
    const double t = double(index) / 400;
    return {viewport_->width() * .43 + 24 * std::sin(t * 12),
            viewport_->height() * .48 + 20 * (1 - std::cos(t * 12))};
  }
  bool tablet() const
  {
    return run_ < 2;
  }
  bool oracle() const
  {
    return run_ % 2 == 1;
  }
  QAction *action(const char *name)
  {
    auto *value = workspace_->findChild<QAction *>(name);
    if (!value)
      qFatal("Missing tablet demo action %s", name);
    return value;
  }
  void event(QEvent::Type type, int index, bool post)
  {
    const bool press = type == QEvent::TabletPress, release = type == QEvent::TabletRelease;
    QInputEvent *event = nullptr;
    if (tablet())
      event = new QTabletEvent(type,
                               &device_,
                               point(index),
                               viewport_->mapToGlobal(point(index).toPoint()),
                               release ? 0 : .15 + .85 * index / 400.,
                               0,
                               0,
                               0,
                               0,
                               0,
                               Qt::NoModifier,
                               press || release ? Qt::LeftButton : Qt::NoButton,
                               release ? Qt::NoButton : Qt::LeftButton);
    else
      event = new QMouseEvent(press   ? QEvent::MouseButtonPress :
                              release ? QEvent::MouseButtonRelease :
                                        QEvent::MouseMove,
                              point(index),
                              viewport_->mapToGlobal(point(index).toPoint()),
                              press || release ? Qt::LeftButton : Qt::NoButton,
                              release ? Qt::NoButton : Qt::LeftButton,
                              Qt::NoModifier);
    event->setTimestamp(index * 5);
    input_guard_.own(event);
    if (post)
      QCoreApplication::postEvent(viewport_, event);
    else {
      QCoreApplication::sendEvent(viewport_, event);
      delete event;
      action("flushPointerInput")->trigger();
    }
  }
  void begin()
  {
    before_ = state_();
    sample_ = 0;
    max_ping_ = 0;
    ping_clock_.start();
    burst_clock_.start();
    if (!oracle())
      ping_.start();
    event(QEvent::TabletPress, 0, !oracle());
    timer_.setInterval(oracle() ? 1 : 5);
    stage_ = 3;
  }
  void advance()
  {
    switch (stage_) {
      case 0: {
        QAction *new_action = nullptr;
        for (auto *candidate : workspace_->findChildren<QAction *>())
          if (candidate->text() == "&New…")
            new_action = candidate;
        if (!new_action)
          qFatal("Missing File New");
        new_action->trigger();
        stage_ = 1;
        break;
      }
      case 1: {
        QDialog *dialog = nullptr;
        for (auto *candidate : workspace_->findChildren<QDialog *>())
          if (candidate->findChild<QLineEdit *>("meshPath"))
            dialog = candidate;
        if (!dialog)
          qFatal("Missing New dialog");
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh_);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution_));
        dialog->findChild<QPushButton *>("createProject")->click();
        stage_ = 2;
        break;
      }
      case 2: {
        workspace_->findChild<QToolButton *>("addPaintLayer")->click();
        workspace_->findChild<QDoubleSpinBox *>("materialBump")->setValue(.08);
        hover_["before"] = state_()["hover_picks"];
        for (int i = 0; i < 400; ++i) {
          auto *tablet_event = new QTabletEvent(QEvent::TabletMove,
                                                &device_,
                                                point(i),
                                                viewport_->mapToGlobal(point(i).toPoint()),
                                                0,
                                                0,
                                                0,
                                                0,
                                                0,
                                                0,
                                                Qt::NoModifier,
                                                Qt::NoButton,
                                                Qt::NoButton);
          input_guard_.own(tablet_event);
          QCoreApplication::postEvent(viewport_, tablet_event);
          auto *mouse_event = new QMouseEvent(QEvent::MouseMove,
                                              point(i),
                                              point(i),
                                              viewport_->mapToGlobal(point(i).toPoint()),
                                              Qt::NoButton,
                                              Qt::NoButton,
                                              Qt::NoModifier,
                                              Qt::MouseEventSynthesizedByQt);
          input_guard_.own(mouse_event);
          QCoreApplication::postEvent(viewport_, mouse_event);
        }
        stage_ = 7;
        timer_.setInterval(50);
        break;
      }
      case 3: {
        if (oracle()) {
          for (int i = 1; i <= 400; ++i)
            event(QEvent::TabletMove, i, false);
          sample_ = 400;
        }
        else {
          const int due = std::min(400, int(burst_clock_.elapsed() / 5));
          while (sample_ < due)
            event(QEvent::TabletMove, ++sample_, true);
        }
        if (sample_ == 400) {
          event(QEvent::TabletRelease, 400, !oracle());
          stage_ = 4;
          timer_.setInterval(20);
        }
        break;
      }
      case 4: {
        if (state_()["composite_pending"].toBool())
          return;
        // One additional timer turn ensures the posted release has reached the viewport.
        stage_ = 5;
        break;
      }
      case 5: {
        ping_.stop();
        auto result = state_();
        result["before"] = before_;
        result["input"] = tablet() ? "tablet" : "mouse";
        result["oracle"] = oracle();
        result["moves"] = sample_;
        result["max_ping_ms"] = max_ping_;
        result["duration_ms"] = burst_clock_.elapsed();
        runs_.append(result);
        if (!oracle())
          workspace_->grab().save(directory_ + (tablet() ? "/tablet.png" : "/mouse.png"));
        if (++run_ == 4) {
          timer_.stop();
          QFile file(directory_ + "/tablet-performance.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Tablet proof output failed");
          file.write(
              QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                        {"resolution", resolution_},
                                        {"runs", runs_},
                                        {"queued_events", true},
                                        {"ignored_native_input", qint64(input_guard_.ignored)},
                                        {"hover", hover_}})
                  .toJson());
          file.close();
          QTimer::singleShot(100, qApp, &QCoreApplication::quit);
          return;
        }
        action("undoDocument")->trigger();
        stage_ = 6;
        timer_.setInterval(200);
        break;
      }
      case 6:
        if (!state_()["composite_pending"].toBool())
          begin();
        break;
      case 7:
        hover_["after"] = state_()["hover_picks"];
        hover_["tablet_moves"] = 400;
        hover_["synthesized_mouse_moves"] = 400;
        begin();
        break;
    }
  }

 public:
  TabletDemo(QWidget *w,
             QWidget *v,
             QString directory,
             QString mesh,
             int resolution,
             std::function<QJsonObject()> state)
      : QObject(w),
        workspace_(w),
        viewport_(v),
        input_guard_(v),
        directory_(directory),
        mesh_(mesh),
        resolution_(resolution),
        state_(std::move(state))
  {
    input_guard_.enabled(true);
    QDir().mkpath(directory_);
    timer_.setInterval(1000);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    ping_.setInterval(10);
    ping_.setTimerType(Qt::PreciseTimer);
    connect(&ping_, &QTimer::timeout, this, [this] {
      max_ping_ = std::max(max_ping_, ping_clock_.nsecsElapsed() / 1e6);
      ping_clock_.restart();
    });
    timer_.start();
  }
};
}  // namespace
void start_tablet_performance_demo(QWidget *w,
                                   QWidget *v,
                                   const QString &d,
                                   const QString &m,
                                   int r,
                                   std::function<QJsonObject()> state)
{
  new TabletDemo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
