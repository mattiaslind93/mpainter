// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <functional>
#include "ViewportDemoCapture.hh"
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
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class UvTextureDemo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int size, step = 0, frame = 0, ticks = 0;
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *object = w->findChild<T *>(name);
    if (!object)
      qFatal("UV texture control missing: %s", name);
    return object;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void key(int code)
  {
    v->setFocus();
    QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier),
        release(QEvent::KeyRelease, code, Qt::NoModifier);
    QApplication::sendEvent(v, &press);
    QApplication::sendEvent(v, &release);
  }
  void color(std::array<double, 3> rgb)
  {
    for (int c = 0; c < 3; ++c)
      get<QDoubleSpinBox>((QString("baseColor") + QString::number(c)).toUtf8().constData())
          ->setValue(rgb[c]);
  }
  void capture(const char *name, int channel = 0, bool uv = true)
  {
    QMouseEvent move(QEvent::MouseMove,
                     QPointF(-500, -500),
                     v->mapToGlobal(QPoint(-500, -500)),
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(v, &move);
    w->setProperty("uvProofChannel", channel);
    w->setProperty("uvProofVisible", uv);
    action("guiUvTextureProbe");
    auto sample = state();
    QJsonObject result{{"stage", name},
                       {"view", sample["view"]},
                       {"environment", sample["environment"]},
                       {"shader_failures", sample["shader_failures"]},
                       {"contours", w->property("uvContourCount").toInt()},
                       {"probes", uv ? w->property("uvTextureProbe").toJsonArray() : QJsonArray{}},
                       {"document", w->property("uvDocumentState").toJsonObject()}};
    captures.append(result);
    if (!capture_viewport_framebuffer(v).save(dir + "/" + name + "-viewport.png"))
      qFatal("UV framebuffer capture failed");
    if (QString(name) == "pattern" || QString(name) == "split")
      capture_demo_window(w, v).save(dir + "/" + name + ".png");
  }
  void advance()
  {
    if (++ticks > 1000)
      qFatal("UV texture demo timeout");
    const auto s = state();
    if (step > 1 && (s["property_pending"].toBool() || s["composite_pending"].toBool() ||
                     s["render_frames"].toInt() < frame))
      return;
    switch (step++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *r = d->findChild<QComboBox *>("documentResolution");
        r->setCurrentIndex(r->findData(size));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        get<QDoubleSpinBox>("environmentRotation")->setValue(0);
        color({.18, .18, .18});
        get<QDoubleSpinBox>("metallicControl")->setValue(1);
        get<QDoubleSpinBox>("roughnessControl")->setValue(.2);
        for (const char *name : {"fillShowManipulator", "fillManipulator"})
          if (auto *button = w->findChild<QAbstractButton *>(name))
            button->setChecked(false);
        key(Qt::Key_F3);
        break;
      case 4:
        capture("gray");
        get<QDoubleSpinBox>("environmentRotation")->setValue(90);
        break;
      case 5:
        capture("gray-light90");
        color({.05, .2, .5});
        break;
      case 6:
        capture("color");
        key(Qt::Key_C);
        break;
      case 7:
        capture("color-solo");
        key(Qt::Key_C);
        key(Qt::Key_C);
        break;
      case 8:
        capture("roughness", 2);
        key(Qt::Key_M);
        get<QComboBox>("fillBaseColorResource")->setCurrentIndex(1);
        break;
      case 9:
        capture("pattern");
        key(Qt::Key_F1);
        break;
      case 10:
        capture("split");
        key(Qt::Key_F4);
        break;
      case 11:
        capture("swapped");
        key(Qt::Key_F2);
        break;
      case 12:
        capture("3d-only", 0, false);
        key(Qt::Key_F4);
        break;
      case 13:
        capture("3d-swap", 0, false);
        key(Qt::Key_F3);
        break;
      case 14:
        capture("2d-restored");
        key(Qt::Key_F4);
        break;
      case 15:
        capture("2d-swap");
        key(Qt::Key_F1);
        break;
      case 16: {
        capture("split-restored");
        timer.stop();
        QFile file(dir + "/uv-texture.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("UV proof cannot be written");
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
  UvTextureDemo(QWidget *window,
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
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
