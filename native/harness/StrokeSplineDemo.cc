// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeSplineDemo.hh"
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
#include <QPointingDevice>
#include <QPushButton>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  SyntheticInputGuard input_guard;
  QString dir, mesh;
  int resolution, stage = 0, run = 0, index = 0;
  QTimer timer, ping;
  QElapsedTimer ping_clock, release_clock;
  double max_ping = 0;
  QJsonObject before, contact;
  QJsonArray runs;
  std::function<QJsonObject()> state;
  QPointingDevice pen{"BTS sparse spline test",
                      0x425453,
                      QInputDevice::DeviceType::Stylus,
                      QPointingDevice::PointerType::Pen,
                      QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
                          QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt,
                      1,
                      1};
  static constexpr std::array<std::array<double, 2>, 8> points{{{.28, .55},
                                                                {.33, .38},
                                                                {.42, .32},
                                                                {.51, .45},
                                                                {.54, .66},
                                                                {.62, .72},
                                                                {.70, .60},
                                                                {.74, .40}}};
  QPointF position(int i) const
  {
    return {v->width() * .5 + v->height() * (points[i][0] - .5), v->height() * points[i][1]};
  }
  void action(const char *name)
  {
    auto *a = w->findChild<QAction *>(name);
    if (!a)
      qFatal("Spline action missing: %s", name);
    a->trigger();
  }
  bool tablet() const
  {
    return run < 2;
  }
  bool oracle() const
  {
    return run % 2;
  }
  void event(QEvent::Type type, int i)
  {
    const bool press = type == QEvent::TabletPress, release = type == QEvent::TabletRelease;
    const auto p = position(i);
    QInputEvent *e;
    if (tablet())
      e = new QTabletEvent(type,
                           &pen,
                           p,
                           v->mapToGlobal(p.toPoint()),
                           release ? 0 : .15 + .85 * i / 7.,
                           -35 + i * 10,
                           -20 + i * 6,
                           0,
                           0,
                           0,
                           Qt::NoModifier,
                           press || release ? Qt::LeftButton : Qt::NoButton,
                           release ? Qt::NoButton : Qt::LeftButton);
    else
      e = new QMouseEvent(press   ? QEvent::MouseButtonPress :
                          release ? QEvent::MouseButtonRelease :
                                    QEvent::MouseMove,
                          p,
                          v->mapToGlobal(p.toPoint()),
                          press || release ? Qt::LeftButton : Qt::NoButton,
                          release ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
    e->setTimestamp(i * 40);
    input_guard.own(e);
    if (oracle()) {
      QCoreApplication::sendEvent(v, e);
      delete e;
      action("flushPointerInput");
    }
    else
      QCoreApplication::postEvent(v, e);
  }
  QJsonObject probe()
  {
    w->setProperty("guiProbeOnly", true);
    auto value = state();
    w->setProperty("guiProbeOnly", false);
    return value;
  }
  void advance()
  {
    switch (stage) {
      case 0:
        action("newProject");
        ++stage;
        break;
      case 1: {
        auto *dialog = w->findChild<QDialog *>("newProjectDialog");
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        dialog->findChild<QPushButton *>("createProject")->click();
        ++stage;
        break;
      }
      case 2:
        w->findChild<QToolButton *>("addPaintLayer")->click();
        w->findChild<QComboBox *>("viewLayoutControl")->setCurrentIndex(1);
        w->findChild<QDoubleSpinBox *>("brushSize")
            ->setValue(qEnvironmentVariable("BTS_SPLINE_BRUSH_SIZE", "1").toDouble());
        w->findChild<QDoubleSpinBox *>("brushSpacing")->setValue(20);
        w->findChild<QDoubleSpinBox *>("materialBump")->setValue(.08);
        w->findChild<QToolButton *>("brushPressureFlow")->setChecked(true);
        ++stage;
        break;
      case 3:
        if (state()["composite_pending"].toBool())
          return;
        before = state();
        max_ping = 0;
        index = 0;
        ping_clock.start();
        if (!oracle())
          ping.start();
        event(QEvent::TabletPress, 0);
        timer.setInterval(40);
        ++stage;
        break;
      case 4:
        if (++index < 8) {
          event(QEvent::TabletMove, index);
          break;
        }
        ++stage;
        break;
      case 5:
        contact = probe();
        release_clock.start();
        event(QEvent::TabletRelease, 7);
        ++stage;
        break;
      case 6:
        if (probe()["painting"].toBool())
          return;
        {
          const auto release_ms = release_clock.elapsed();
          ping.stop();
          auto data = state();
          data["input"] = tablet() ? "tablet" : "mouse";
          data["oracle"] = oracle();
          data["before"] = before;
          data["contact"] = contact;
          data["max_ping_ms"] = max_ping;
          data["penup_ms"] = release_ms;
          QJsonArray inputs;
          for (int i = 0; i < 8; ++i) {
            const auto p = position(i);
            inputs.append(QJsonArray{p.x(), p.y()});
          }
          data["inputs"] = inputs;
          runs.append(data);
          if (!oracle())
            w->grab().save(dir + (tablet() ? "/tablet.png" : "/mouse.png"));
          ++stage;
          timer.setInterval(100);
          break;
        }
      case 7:
        if (state()["composite_pending"].toBool())
          return;
        action("checkGuiComposite");
        v->repaint();
        {
          auto entry = runs.last().toObject();
          entry["reference"] = state();
          runs[runs.size() - 1] = entry;
        }
        if (++run < 4) {
          action("undoDocument");
          stage = 3;
          return;
        }
        {
          timer.stop();
          QFile file(dir + "/stroke-spline.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Spline proof write failed");
          file.write(
              QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                        {"runs", runs},
                                        {"ignored_native_input", qint64(input_guard.ignored)}})
                  .toJson());
          file.close();
          QTimer::singleShot(100, qApp, &QCoreApplication::quit);
          break;
        }
    }
  }

 public:
  Demo(QWidget *workspace,
       QWidget *viewport,
       QString directory,
       QString source,
       int size,
       std::function<QJsonObject()> snapshot)
      : QObject(workspace),
        w(workspace),
        v(viewport),
        input_guard(viewport),
        dir(directory),
        mesh(source),
        resolution(size),
        state(std::move(snapshot))
  {
    input_guard.enabled(true);
    QDir().mkpath(dir);
    timer.setInterval(100);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    ping.setInterval(10);
    ping.setTimerType(Qt::PreciseTimer);
    connect(&ping, &QTimer::timeout, this, [this] {
      max_ping = std::max(max_ping, double(ping_clock.restart()));
    });
    timer.start();
  }
};
}  // namespace
void start_stroke_spline_demo(QWidget *w,
                              QWidget *v,
                              const QString &d,
                              const QString &m,
                              int r,
                              std::function<QJsonObject()> state)
{
  new Demo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
