#pragma once
#include "ViewportDemoCapture.hh"
#include "Performance.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class PolygonBucketDemo final : public QObject {
  QWidget *w, *v;
  QString out, mesh;
  int resolution, stage = 0, ticks = 0, frame = 0;
  bool busy = false, awaiting = false;
  QTimer timer, ping;
  QElapsedTimer heartbeat, latency;
  double max_ping = 0;
  QJsonArray captures, calls;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("Bucket control missing %s", name);
    return p;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    const double elapsed = awaiting ? latency.elapsed() : 0;
    awaiting = false;
    performance::begin("");
    action("guiPolygonSample");
    auto s = state();
    s["stage"] = name;
    s["samples"] = w->property("polygonSamples").toJsonArray();
    s["settled_ms"] = elapsed;
    captures.append(s);
    capture_demo_window(w, v).save(out + "/" + name + ".png");
  }
  void click(int point, bool ctrl = false)
  {
    const auto a = state()["polygon_points"].toArray()[point].toArray();
    const QPointF p(a[0].toDouble(), a[1].toDouble());
    heartbeat.restart();
    latency.restart();
    awaiting = true;
    performance::begin("polygon_bucket_" + std::to_string(stage));
    const auto start = performance::Clock::now();
    for (auto t : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
      QMouseEvent e(t,
                    p,
                    v->mapToGlobal(p.toPoint()),
                    Qt::LeftButton,
                    t == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                    ctrl ? Qt::ControlModifier : Qt::NoModifier);
      QCoreApplication::sendEvent(v, &e);
    }
    calls.append(
        QJsonObject{{"point", point}, {"ctrl", ctrl}, {"handler_ms", performance::ms(start)}});
    frame = state()["render_frames"].toInt() + 1;
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 3000)
      qFatal("Bucket timeout stage %d", stage);
    auto s = state();
    if (stage > 1 && (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
                      s["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QCheckBox>("preserveUvTiles")->setChecked(true);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(resolution));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      case 2:
        get<QToolButton>("addPaintLayer")->click();
        action("guiPolygonPrepare");
        get<QDoubleSpinBox>("secondaryColor0")->setValue(0);
        get<QDoubleSpinBox>("secondaryColor1")->setValue(0);
        get<QDoubleSpinBox>("secondaryColor2")->setValue(1);
        break;
      case 3:
        capture("initial");
        if (!get<QToolButton>("polygonFillTool")->isEnabled())
          qFatal("Bucket still disabled for Paint");
        action("selectPolygonFill");
        click(0);
        break;
      case 4:
        capture("object-red");
        action("undoDocument");
        break;
      case 5:
        capture("undo-object");
        action("redoDocument");
        break;
      case 6:
        capture("redo-object");
        action("undoDocument");
        break;
      case 7:
        click(1);
        break;
      case 8:
        capture("uv-red");
        click(2, true);
        break;
      case 9:
        capture("uv-blue-1002");
        action("addBlackMask");
        break;
      case 10:
        capture("black-mask");
        action("selectPolygonFill");
        click(1);
        break;
      case 11:
        capture("black-white-ink");
        click(1, true);
        break;
      case 12:
        capture("black-ctrl");
        action("undoDocument");
        break;
      case 13:
        capture("undo-mask");
        action("redoDocument");
        break;
      case 14:
        capture("redo-mask");
        action("addWhiteMask");
        break;
      case 15:
        capture("white-default");
        action("selectPolygonFill");
        click(1);
        break;
      case 16:
        capture("white-black-ink");
        click(1, true);
        break;
      case 17:
        capture("white-ctrl");
        busy = true;
        action("guiPolygonSave");
        busy = false;
        break;
      case 18:
        busy = true;
        action("guiPolygonOpen");
        busy = false;
        break;
      case 19:
        capture("reopened");
        timer.stop();
        ping.stop();
        {
          QFile f(out + "/polygon-bucket.json");
          if (!f.open(QIODevice::WriteOnly))
            qFatal("Bucket proof unavailable");
          f.write(QJsonDocument(QJsonObject{{"captures", captures},
                                            {"calls", calls},
                                            {"max_interaction_ping_ms", max_ping},
                                            {"resolution", resolution},
                                            {"phases", performance::snapshot()}})
                      .toJson());
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
    if (!awaiting)
      frame = state()["render_frames"].toInt() + 2;
  }

 public:
  PolygonBucketDemo(
      QWidget *a, QWidget *b, QString c, QString d, int r, std::function<QJsonObject()> f)
      : QObject(a), w(a), v(b), out(c), mesh(d), resolution(r), state(std::move(f))
  {
    timer.setInterval(16);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    ping.setInterval(10);
    connect(&ping, &QTimer::timeout, this, [this] {
      if (awaiting)
        max_ping = std::max(max_ping, double(heartbeat.nsecsElapsed()) / 1e6);
      heartbeat.restart();
    });
    heartbeat.start();
    ping.start();
    timer.start();
  }
};
}  // namespace bts
