#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <functional>
namespace bts {
class EdgeLengthDemo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int resolution, stage = 0, frame = 0, ticks = 0;
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *widget = w->findChild<T *>(name);
    if (!widget)
      qFatal("Edge Length control missing: %s", name);
    return widget;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void key(int code)
  {
    v->setFocus();
    QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
    QApplication::sendEvent(v, &event);
  }
  void number(const char *name, double value)
  {
    get<QDoubleSpinBox>(name)->setValue(value);
  }
  void mode(int value)
  {
    auto *choice = get<QComboBox>("displacementSubdivisionMode");
    const int index = choice->findData(value);
    if (index < 0 || !static_cast<QStandardItemModel *>(choice->model())->item(index)->isEnabled())
      qFatal("Subdivision mode cannot be selected");
    choice->setCurrentIndex(index);
  }
  void capture(const char *name)
  {
    w->setProperty("displacementObj", dir + "/" + name + ".obj");
    action("guiDisplacementExportOriginal");
    auto data = state();
    data["stage"] = name;
    data["count_enabled"] = get<QSpinBox>("displacementSubdivisionCount")->isEnabled();
    data["max_length_enabled"] = get<QDoubleSpinBox>("displacementMaxLength")->isEnabled();
    captures.append(data);
    if (!capture_viewport_framebuffer(v).save(dir + "/" + name + ".png"))
      qFatal("Edge Length framebuffer capture failed");
    if (QString(name) == "edge-quarter")
      capture_demo_window(w, v).save(dir + "/workspace.png");
  }
  void advance()
  {
    if (++ticks > 1500)
      qFatal("Edge Length demo timeout");
    const auto s = state();
    if (stage > 1 && (s["property_pending"].toBool() || s["composite_pending"].toBool() ||
                      s["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *r = d->findChild<QComboBox *>("documentResolution");
        r->setCurrentIndex(r->findData(resolution));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        get<QToolButton>("shaderSettingsButton")->click();
        get<QCheckBox>("displacementChannel")->setChecked(true);
        get<QSpinBox>("displacementSubdivisionCount")->setValue(0);
        number("displacementStrength", .25);
        for (const char *name : {"fillShowManipulator", "fillManipulator"})
          if (auto *button = w->findChild<QAbstractButton *>(name))
            button->setChecked(false);
        break;
      case 4:
        get<QCheckBox>("fillChannelEnabled14")->setChecked(true);
        number("materialDisplacement", .75);
        break;
      case 5:
        capture("level0");
        get<QSpinBox>("displacementSubdivisionCount")->setValue(2);
        break;
      case 6:
        capture("uniform");
        key(Qt::Key_F3);
        break;
      case 7:
        capture("uv-uniform");
        key(Qt::Key_F2);
        mode(2);
        break;
      case 8:
        capture("catmull");
        key(Qt::Key_F3);
        break;
      case 9:
        capture("uv-catmull");
        key(Qt::Key_F2);
        mode(1);
        break;
      case 10:
        capture("edge-default");
        key(Qt::Key_F3);
        break;
      case 11:
        capture("uv-edge");
        key(Qt::Key_F2);
        number("displacementMaxLength", .5);
        break;
      case 12:
        capture("edge-half");
        number("displacementMaxLength", .25);
        break;
      case 13:
        capture("edge-quarter");
        action("undoDocument");
        break;
      case 14:
        capture("undo-edge");
        action("redoDocument");
        break;
      case 15:
        capture("redo-edge");
        get<QCheckBox>("displacementEnabled")->setChecked(false);
        break;
      case 16:
        capture("disabled");
        get<QCheckBox>("displacementEnabled")->setChecked(true);
        break;
      case 17:
        capture("enabled");
        action("guiDisplacementSave");
        break;
      case 18:
        capture("saved");
        mode(0);
        break;
      case 19:
        capture("changed");
        action("guiDisplacementOpen");
        break;
      case 20:
        capture("opened");
        get<QPushButton>("displacementRestoreDefaults")->click();
        break;
      case 21:
        capture("defaults");
        action("undoDocument");
        break;
      case 22: {
        capture("undo-defaults");
        timer.stop();
        QFile file(dir + "/edge-length.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Edge Length proof cannot be saved");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures}})
                       .toJson());
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 20;
  }

 public:
  EdgeLengthDemo(QWidget *window,
                 QWidget *viewport,
                 QString directory,
                 QString fixture,
                 int size,
                 std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        dir(directory),
        mesh(fixture),
        resolution(size),
        state(std::move(snapshot))
  {
    QDir().mkpath(dir);
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
