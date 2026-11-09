// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
namespace bts {
class LayerChannelDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh;
  QTimer timer, dialogs;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Missing channel control: %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    w->setProperty("channelSample", true);
    auto data = state();
    w->setProperty("channelSample", false);
    data["stage"] = name;
    captures.append(data);
    if (auto *scroll = w->findChild<QScrollArea *>("propertiesScroll"))
      scroll->ensureWidgetVisible(
          get<QToolButton>("brushChannel0")->isVisible() ?
              static_cast<QWidget *>(get<QToolButton>("brushChannel0")) :
              static_cast<QWidget *>(get<QCheckBox>("fillChannelEnabled0")));
    capture_demo_window(w, v).save(output + "/" + name + ".png");
  }
  void stroke(const QJsonArray &point)
  {
    const QPointF p(point[0].toDouble(), point[1].toDouble());
    for (const auto type :
         {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease})
    {
      const bool release = type == QEvent::MouseButtonRelease;
      QMouseEvent event(type,
                        p + (type == QEvent::MouseMove ? QPointF(4, 0) : QPointF()),
                        v->mapToGlobal(p.toPoint()),
                        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                        release ? Qt::NoButton : Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(v, &event);
    }
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 1800)
      qFatal("Channel demo timeout stage=%d", stage);
    const auto data = state();
    if (stage > 1 && (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
                      data["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        get<QCheckBox>("preserveUvTiles")->setChecked(true);
        auto *res = get<QComboBox>("documentResolution");
        res->setCurrentIndex(res->findData(512));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      }
      case 2:
        action("guiChannelPrepare");
        break;
      case 3:
        capture("defaults");
        for (const auto &p : data["channel_points"].toArray())
          stroke(p.toArray());
        break;
      case 4:
        capture("painted");
        get<QToolButton>("brushChannel0")->click();
        break;
      case 5:
        capture("color-off");
        action("undoDocument");
        break;
      case 6:
        capture("color-undo");
        action("redoDocument");
        break;
      case 7:
        capture("color-redo");
        get<QToolButton>("brushChannel0")->click();
        break;
      case 8:
        capture("color-empty-on");
        get<QToolButton>("brushDisplacement")->click();
        break;
      case 9:
        capture("disp-on");
        get<QCheckBox>("displacementChannel")->click();
        break;
      case 10:
        capture("disp-off");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 11:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 12:
        capture("reopened");
        get<QToolButton>("addFillLayer")->click();
        break;
      case 13:
        get<QToolButton>("fillMaterialTab")->click();
        capture("fill-defaults");
        get<QCheckBox>("fillChannelEnabled2")->click();
        break;
      case 14:
        capture("fill-rough-off");
        action("undoDocument");
        break;
      case 15:
        capture("fill-rough-undo");
        action("redoDocument");
        break;
      case 16:
        capture("fill-rough-redo");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 17:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 18: {
        capture("fill-reopened");
        timer.stop();
        QFile file(output + "/layer-channels.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Cannot write channel proof");
        file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                             {"pid", QCoreApplication::applicationPid()}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 5;
  }

 public:
  LayerChannelDemo(QWidget *window,
                   QWidget *viewport,
                   QString directory,
                   QString source,
                   std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        output(directory),
        mesh(source),
        state(std::move(snapshot))
  {
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(100);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical)
            qFatal("Channel GUI error: %s", qPrintable(box->text()));
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
    });
    dialogs.start(40);
  }
};
}  // namespace bts
