// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QToolButton>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
namespace bts {
class BakingLayoutDemo final : public QObject {
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
      qFatal("Missing baking layout control: %s", name);
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
    data["window_width"] = w->width();
    data["window_height"] = w->height();
    data["viewport_width"] = v->width();
    data["viewport_height"] = v->height();
    auto *panel = get<QWidget>("bakingDock");
    data["baking_visible"] = panel->isVisible();
    data["baking_separate_window"] = panel->isWindow();
    data["baking_width"] = panel->width();
    data["baking_height"] = panel->height();
    data["undo_shortcut_available"] = panel->actions().contains(get<QAction>("undoDocument"));
    data["redo_shortcut_available"] = panel->actions().contains(get<QAction>("redoDocument"));
    if (auto *button = w->findChild<QPushButton *>("bakeSelectedMaps")) {
      data["bake_enabled"] = button->isEnabled();
      data["bake_tooltip"] = button->toolTip();
    }
    captures.append(data);
    capture_demo_window(w, v).save(output + "/" + name + ".png");
    if (panel->isVisible())
      panel->grab().save(output + "/" + name + "-panel.png");
  }
  void finish()
  {
    timer.stop();
    QFile file(output + "/baking-layout.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("Cannot write baking layout proof");
    file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                         {"pid", QCoreApplication::applicationPid()}})
                   .toJson());
    file.close();
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 1200)
      qFatal("Baking layout demo timeout stage=%d", stage);
    const auto data = state();
    if (stage > 1 && (data["composite_pending"].toBool() || data["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        w->resize(1024, 768);
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
        break;
      case 2:
        capture("painting-1024");
        action("enterBakingMode");
        break;
      case 3:
        capture("baking-1024");
        get<QPushButton>("leaveBakingMode")->click();
        w->resize(1920, 1080);
        break;
      case 4:
        capture("painting-1920");
        action("enterBakingMode");
        break;
      case 5:
        capture("baking-1920");
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 6:
        capture("returned");
        if (qEnvironmentVariableIsSet("BTS_BAKING_LAYOUT_BEFORE"))
          finish();
        else
          action("enterBakingMode");
        break;
      case 7: {
        for (int role = 0; role < 7; ++role) {
          auto *map = get<QCheckBox>(qPrintable(QString("bakeSelectedMap%1").arg(role)));
          if (map->isChecked())
            map->click();
        }
        capture("none-selected");
        get<QCheckBox>("bakeSelectedMap5")->click();
        get<QCheckBox>("bakeSelectedMap4")->click();
        auto *width = get<QComboBox>("autoBakeWidth");
        width->setCurrentIndex(width->findData(128));
        capture("selected-maps");
        get<QPushButton>("bakeSelectedMaps")->click();
        break;
      }
      case 8:
        if (data["skew"].toObject()["pending"].toBool()) {
          --stage;
          return;
        }
        capture("baked-selected");
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 9:
        capture("returned-after-bake");
        action("enterBakingMode");
        break;
      case 10:
        get<QWidget>("bakingDock")->close();
        break;
      case 11:
        capture("closed-to-painting");
        finish();
        break;
    }
    frame = state()["render_frames"].toInt() + 3;
  }

 public:
  BakingLayoutDemo(QWidget *window,
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
    connect(&dialogs, &QTimer::timeout, this, [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical)
            qFatal("Baking GUI error: %s", qPrintable(box->text()));
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
    });
    dialogs.start(40);
  }
};
}  // namespace bts
