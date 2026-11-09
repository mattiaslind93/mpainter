// SPDX-License-Identifier: GPL-3.0-or-later
#include "ViewportDemoCapture.hh"
#include "InspectionDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QSpinBox>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int size, stage = 0, after_frame = 0;
  QTimer timer;
  QJsonArray captures, menus;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Inspection control missing: %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void key(int code, bool reverse = false)
  {
    v->setFocus();
    QKeyEvent press(QEvent::KeyPress, code, reverse ? Qt::ShiftModifier : Qt::NoModifier);
    QApplication::sendEvent(v, &press);
    QKeyEvent release(QEvent::KeyRelease, code, reverse ? Qt::ShiftModifier : Qt::NoModifier);
    QApplication::sendEvent(v, &release);
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    captures.append(data);
    capture_viewport_framebuffer(v).save(dir + "/" + name + "-viewport.png");
    if (QString(name) == "ao" || QString(name) == "color")
      capture_demo_window(w, v).save(dir + "/" + name + ".png");
  }
  void menu(const char *pane, const char *choice)
  {
    timer.stop();
    QPointF point;
    const auto panes = state()["view"].toObject()["panes"].toArray();
    for (auto p : panes) {
      const auto o = p.toObject();
      if (o["view"].toString() != pane)
        continue;
      const auto r = o["rect"].toArray();
      const double scale = v->devicePixelRatioF();
      point = {(r[0].toDouble() + r[2].toDouble()) / scale - 52, r[1].toDouble() / scale + 18};
    }
    QTimer::singleShot(100, w, [this, choice] {
      auto *popup = get<QMenu>("viewportInspectionMenu");
      QJsonArray entries;
      for (auto *a : popup->actions())
        entries.append(QJsonObject{{"name", a->objectName()},
                                   {"text", a->text()},
                                   {"checked", a->isChecked()},
                                   {"section", a->isSeparator()}});
      menus.append(QJsonObject{
          {"pane", popup->property("pane").toString()}, {"choice", choice}, {"entries", entries}});
      if (QString(choice) == "inspectMeshMap2")
        popup->grab().save(dir + "/inspection-menu.png");
      auto *a = popup->findChild<QAction *>(choice);
      if (!a)
        qFatal("Inspection menu action missing: %s", choice);
      a->trigger();
      popup->close();
    });
    const auto global = v->mapToGlobal(point.toPoint());
    QMouseEvent press(
        QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(v, &press);
    QMouseEvent release(
        QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(v, &release);
    timer.start();
  }
  void advance()
  {
    const auto s = state();
    if (stage > 1 &&
        (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
         s["skew"].toObject()["pending"].toBool() || s["render_frames"].toInt() < after_frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *resolution = d->findChild<QComboBox *>("documentResolution");
        resolution->setCurrentIndex(resolution->findData(size));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        action("guiSkewPrepare");
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        for (int c = 0; c < 3; ++c)
          get<QDoubleSpinBox>((QString("baseColor") + QString::number(c)).toUtf8().constData())
              ->setValue(.2);
        get<QDoubleSpinBox>("metallicControl")->setValue(1);
        get<QDoubleSpinBox>("roughnessControl")->setValue(.2);
        get<QComboBox>("fillBaseColorResource")->setCurrentIndex(1);
        break;
      case 4:
        action("enterBakingMode");
        get<QComboBox>("bakeQualityRole")->setCurrentIndex(3);
        get<QSpinBox>("bakeMapRays")->setValue(8);
        get<QSpinBox>("bakeMapDilation")->setValue(2);
        get<QToolButton>("bakeNow2")->click();
        break;
      case 5:
        get<QToolButton>("bakeNow3")->click();
        break;
      case 6:
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 7:
        capture("material");
        key(Qt::Key_C);
        break;
      case 8:
        capture("color");
        key(Qt::Key_C);
        break;
      case 9:
        capture("metallic");
        key(Qt::Key_C, true);
        break;
      case 10:
        capture("color-reverse");
        get<QDoubleSpinBox>("environmentRotation")->setValue(0);
        break;
      case 11:
        capture("color-light0");
        get<QDoubleSpinBox>("environmentRotation")->setValue(90);
        break;
      case 12:
        capture("color-light90");
        key(Qt::Key_B);
        break;
      case 13:
        capture("id");
        key(Qt::Key_B);
        break;
      case 14:
        capture("ao");
        menu("2D", "inspectMeshMap2");
        break;
      case 15:
        capture("id-menu-uv");
        menu("3D", "inspectChannel4");
        break;
      case 16:
        capture("normal-menu-3d");
        menu("2D", "inspectChannel2");
        break;
      case 17:
        capture("roughness-menu-uv");
        key(Qt::Key_M);
        break;
      case 18:
        capture("material-restored");
        get<QDoubleSpinBox>("environmentRotation")->setValue(0);
        break;
      case 19:
        capture("material-light0");
        get<QDoubleSpinBox>("environmentRotation")->setValue(90);
        break;
      case 20:
        capture("material-light90");
        menu("3D", "inspectMeshMap3");
        break;
      case 21:
        capture("ao-menu-3d");
        key(Qt::Key_B, true);
        break;
      case 22:
        capture("id-reverse");
        menu("2D", "inspectMaterial");
        break;
      case 23: {
        capture("material-menu-uv");
        timer.stop();
        QFile f(dir + "/inspection.json");
        if (!f.open(QIODevice::WriteOnly))
          qFatal("Inspection proof unavailable");
        f.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                          {"captures", captures},
                                          {"menus", menus}})
                    .toJson());
        f.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    after_frame = state()["render_frames"].toInt() + 40;
  }

 public:
  Demo(QWidget *window,
       QWidget *viewport,
       QString directory,
       QString fixture,
       int resolution,
       std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        dir(directory),
        mesh(fixture),
        size(resolution),
        state(std::move(snapshot))
  {
    QDir().mkpath(dir);
    timer.setInterval(200);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_inspection_demo(QWidget *w,
                           QWidget *v,
                           const QString &dir,
                           const QString &mesh,
                           int size,
                           std::function<QJsonObject()> state)
{
  new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
