// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
namespace bts {
class HeightNeutralDemo final : public QObject {
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
      qFatal("Height neutral control missing %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    captures.append(data);
    {
      QFile partial(out + "/height-neutral-partial.json");
      if (partial.open(QIODevice::WriteOnly))
        partial.write(QJsonDocument(QJsonObject{{"captures", captures}}).toJson());
    }
    capture_viewport_framebuffer(v).save(out + "/" + name + ".png");
    w->grab().save(out + "/" + name + "-ui.png");
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 1600)
      qFatal("Height neutral demo timeout %d", stage);
    auto data = state();
    if (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
        (frame && data["render_frames"].toInt() < frame))
      return;
    frame = 0;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 2:
        capture("created");
        get<QToolButton>("addFillLayer")->click();
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 3:
        capture("fill-neutral");
        if (get<QDoubleSpinBox>("materialBump")->value() != .5)
          qFatal("Bump neutral is not .5");
        get<QDoubleSpinBox>("materialBump")->setValue(.8);
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 4:
        capture("bump-raised");
        get<QDoubleSpinBox>("materialBump")->setValue(.5);
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 5:
        capture("bump-flat");
        get<QAbstractButton>("fillChannelEnabled14")->click();
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 6:
        capture("disp-zero");
        get<QDoubleSpinBox>("displacementMidlevel")->setValue(.5);
        get<QDoubleSpinBox>("materialDisplacement")->setValue(.5);
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 7:
        capture("disp-gray");
        action("undoDocument");
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 8:
        capture("undone");
        action("redoDocument");
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 9:
        capture("redone");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 10:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 11:
        capture("opened");
        timer.stop();
        {
          QFile file(out + "/height-neutral.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Height neutral proof unavailable");
          file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                               {"pid", QCoreApplication::applicationPid()}})
                         .toJson());
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
  }

 public:
  HeightNeutralDemo(QWidget *window,
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
    timer.setInterval(30);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
