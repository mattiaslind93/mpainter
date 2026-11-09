#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class BakeSourceDemo final : public QObject {
  QWidget *w, *v;
  QString out, mesh;
  std::function<QJsonObject()> state;
  QTimer timer;
  QJsonArray captures;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Bake source demo missing %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  QString source(const char *name)
  {
    return out + "/fixtures/" + name;
  }
  void high(const char *name)
  {
    auto *line = get<QLineEdit>("skewHighFiles");
    line->setText(source(name));
    QMetaObject::invokeMethod(line, "editingFinished", Qt::DirectConnection);
  }
  void bake()
  {
    get<QPushButton>("bakeSelectedMaps")->click();
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    captures.append(data);
    QFile partial(out + "/bake-source-gui-partial.json");
    if (partial.open(QIODevice::WriteOnly))
      partial.write(QJsonDocument(QJsonObject{{"captures", captures}}).toJson());
    auto *panel = get<QWidget>("bakingDock");
    if (panel->isVisible())
      panel->grab().save(out + "/" + name + "-panel.png");
    capture_viewport_framebuffer(v).save(out + "/" + name + ".png");
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 2000)
      qFatal("Bake source GUI timeout %d", stage);
    auto data = state();
    if (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
        data["skew"].toObject()["pending"].toBool())
      return;
    frame = 0;
    std::printf("BTS_BAKE_SOURCE_GUI stage=%d\n", stage);
    std::fflush(stdout);
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QCheckBox>("preserveUvTiles")->setChecked(true);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      case 2:
        capture("new-source");
        action("enterBakingMode");
        break;
      case 3:
        high("high.abc");
        get<QCheckBox>("skewUseProjectMesh")->setChecked(false);
        for (int role = 0; role < 7; ++role)
          get<QCheckBox>(qPrintable(QString("bakeSelectedMap%1").arg(role)))
              ->setChecked(role == 0 || role == 3 || role == 4);
        get<QComboBox>("autoBakeWidth")
            ->setCurrentIndex(get<QComboBox>("autoBakeWidth")->findData(64));
        get<QDoubleSpinBox>("skewFrontalDistance")->setValue(.5);
        get<QDoubleSpinBox>("skewRearDistance")->setValue(.5);
        get<QCheckBox>("skewRelativeDistances")->setChecked(false);
        bake();
        break;
      case 4:
        capture("baked-abc");
        high("high.usdc");
        bake();
        break;
      case 5:
        capture("baked-usdc");
        high("animated.usdc");
        get<QCheckBox>("skewSourceFrameEnabled")->setChecked(true);
        get<QDoubleSpinBox>("skewSourceFrame")->setValue(2);
        QMetaObject::invokeMethod(
            get<QDoubleSpinBox>("skewSourceFrame"), "editingFinished", Qt::DirectConnection);
        bake();
        break;
      case 6:
        capture("baked-frame2");
        get<QCheckBox>("skewSourceFrameEnabled")->setChecked(false);
        high("high.usdc");
        get<QLineEdit>("skewCageFile")->setText(source("cage.abc"));
        QMetaObject::invokeMethod(
            get<QLineEdit>("skewCageFile"), "editingFinished", Qt::DirectConnection);
        get<QCheckBox>("skewUseCage")->setChecked(true);
        bake();
        break;
      case 7:
        capture("baked-abc-cage");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 8:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 9:
        capture("opened");
        timer.stop();
        {
          QFile file(out + "/bake-source-gui.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Cannot write bake source GUI proof");
          file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                               {"pid", QCoreApplication::applicationPid()}})
                         .toJson());
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
  }

 public:
  BakeSourceDemo(QWidget *window,
                 QWidget *viewport,
                 QString directory,
                 QString source,
                 std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        out(directory),
        mesh(source),
        state(std::move(snapshot))
  {
    timer.setInterval(80);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
