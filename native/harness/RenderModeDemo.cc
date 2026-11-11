// SPDX-License-Identifier: GPL-3.0-or-later
#include "ViewportCapture.hh"
#include "RenderModeDemo.hh"
#include <QAction>
#include <QToolButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include "ViewportWidget.hh"
#include <QPushButton>
#include <QTimer>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QSplitter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QScrollArea>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int resolution, step = 0, phase = 0, ticks = 0, settled = 0;
  bool workspace_mode = qEnvironmentVariableIsSet("BTS_RENDER_WORKSPACE_DEMO");
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("Render mode control missing: %s", name);
    return p;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    data["render_panel_visible"] = get<QWidget>("renderSettings")->isVisible();
    data["layers_visible"] = get<QWidget>("inspectorSplitter")->isVisible();
    data["shelf_visible"] = get<QWidget>("assetShelf")->isVisible();
    data["display_floating"] = get<QDialog>("displaySettings")->isVisible();
    data["shader_floating"] = get<QDialog>("shaderSettings")->isVisible();
    data["render_tab"] = get<QTabWidget>("renderSettingsTabs")->currentIndex();
    data["render_tab_count"] = get<QTabWidget>("renderSettingsTabs")->count();
    data["shader_name_visible"] = get<QWidget>("shaderInstanceName")->isVisible();
    data["display_scroll_visible"] = get<QScrollArea>("displaySettingsScroll")->isVisible();
    data["f9"] = get<QAction>("paintingMode")->shortcut().toString();
    data["f10"] = get<QAction>("renderingMode")->shortcut().toString();
    captures.append(data);
    if (workspace_mode) {
      QFile checkpoint(dir + "/render-workspace.json");
      if (checkpoint.open(QIODevice::WriteOnly))
        checkpoint.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                                   {"captures", captures}})
                             .toJson());
    }
    capture_viewport_framebuffer(v).save(dir + "/" + name + "-viewport.png");
    if (QString(name) == "rendered" || QString(name) == "shader-tab")
      w->grab().save(dir + "/" + name + "-workspace.png");
  }
  void orbit()
  {
    const auto pane =
        state()["view"].toObject()["panes"].toArray()[0].toObject()["rect"].toArray();
    const QPointF from(pane[0].toDouble() + pane[2].toDouble() * .5,
                       pane[1].toDouble() + pane[3].toDouble() * .5);
    const auto send =
        [&](QEvent::Type type, QPointF point, Qt::MouseButton b, Qt::MouseButtons bs) {
          QMouseEvent e(type, point, v->mapToGlobal(point.toPoint()), b, bs, Qt::AltModifier);
          QCoreApplication::sendEvent(v, &e);
        };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 8; ++i)
      send(QEvent::MouseMove, from + QPointF(i * 5, i * 2), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, from + QPointF(40, 16), Qt::LeftButton, Qt::NoButton);
  }
  void paint()
  {
    const auto point = state()["lens_probe_screen"].toArray();
    if (point.size() != 2)
      qFatal("Return to painting needs a visible surface");
    const QPointF from(point[0].toDouble(), point[1].toDouble());
    const auto send =
        [&](QEvent::Type type, QPointF point, Qt::MouseButton b, Qt::MouseButtons bs) {
          QMouseEvent e(type, point, v->mapToGlobal(point.toPoint()), b, bs, Qt::NoModifier);
          QCoreApplication::sendEvent(v, &e);
        };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 6; ++i)
      send(QEvent::MouseMove, from + QPointF(i, 0), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, from + QPointF(6, 0), Qt::LeftButton, Qt::NoButton);
  }
  void workspace_step()
  {
    if (++ticks > 600)
      qFatal("Render workspace demo timed out");
    const auto current = state();
    if (current["composite_pending"].toBool() || current["property_pending"].toBool())
      return;
    if (current["render_ui"].toBool()) {
      const auto text = current["render"].toObject()["status"].toString();
      if (text.contains("error", Qt::CaseInsensitive) ||
          text.contains("failed", Qt::CaseInsensitive))
        qFatal("Cycles failed: %s", qPrintable(text));
      if (!text.contains("Rendering Done")) {
        settled = 0;
        return;
      }
      if (++settled < 4)
        return;
    }
    settled = 0;
    switch (phase++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        auto *create = d->findChild<QPushButton *>("createProject");
        if (!create->isEnabled())
          qFatal("Render workspace fixture must be valid");
        create->click();
        break;
      }
      case 2:
        get<QToolButton>("addPaintLayer")->click();
        action("guiSkewPrepare");
        get<QToolButton>("displaySettingsButton")->click();
        break;
      case 3:
        get<QDoubleSpinBox>("environmentRotation")->setValue(32);
        get<QCheckBox>("postEffectsEnabled")->setChecked(true);
        break;
      case 4:
        capture("painting");
        action("renderingMode");
        break;
      case 5:
        capture("rendered");
        get<QDoubleSpinBox>("environmentRotation")->setValue(80);
        break;
      case 6:
        capture("light-rotated");
        get<QSpinBox>("renderSamples")->setValue(32);
        get<QCheckBox>("renderDenoise")->setChecked(false);
        break;
      case 7:
        capture("raw32");
        get<QCheckBox>("postColorEnabled")->setChecked(true);
        get<QDoubleSpinBox>("postColorSaturation")->setValue(0);
        break;
      case 8:
        capture("post-color");
        get<QCheckBox>("postColorEnabled")->setChecked(false);
        break;
      case 9:
        capture("post-restored");
        get<QTabWidget>("renderSettingsTabs")->setCurrentIndex(1);
        break;
      case 10:
        capture("shader-tab");
        get<QTabWidget>("renderSettingsTabs")->setCurrentIndex(0);
        get<QComboBox>("renderDevice")->setCurrentIndex(1);
        break;
      case 11:
        capture("cpu");
        orbit();
        break;
      case 12: {
        capture("orbited");
        const auto rect = v->rect();
        const QPointF point = rect.center();
        QWheelEvent event(point,
                          v->mapToGlobal(point.toPoint()),
                          {},
                          QPoint(0, 120),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(v, &event);
        break;
      }
      case 13: {
        capture("zoomed");
        QKeyEvent event(QEvent::KeyPress, Qt::Key_F6, Qt::NoModifier);
        QCoreApplication::sendEvent(v, &event);
        break;
      }
      case 14:
        capture("orthographic");
        action("paintingMode");
        break;
      case 15:
        capture("painting-restored");
        paint();
        break;
      case 16:
        capture("painted");
        action("guiShaderPaintUndo");
        break;
      case 17:
        capture("paint-undone");
        action("renderingMode");
        break;
      case 18:
        capture("second-render");
        action("guiSkewSave");
        break;
      case 19:
        capture("saved");
        action("guiSkewOpen");
        break;
      case 20:
        capture("opened");
        action("renderingMode");
        break;
      case 21:
        capture("opened-render");
        action("paintingMode");
        break;
      case 22: {
        capture("final");
        timer.stop();
        QFile file(dir + "/render-workspace.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Render workspace proof unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }
  void advance()
  {
    if (workspace_mode) {
      workspace_step();
      return;
    }
    ++step;
    if (step == 1)
      get<QAction>("newProject")->trigger();
    if (step == 2) {
      auto *d = get<QDialog>("newProjectDialog");
      d->findChild<QLineEdit *>("meshPath")->setText(mesh);
      auto *size = d->findChild<QComboBox *>("documentResolution");
      size->setCurrentIndex(size->findData(resolution));
      d->findChild<QPushButton *>("createProject")->click();
    }
    if (step == 12 || step == 32 || step == 62) {
      auto data = state();
      data["stage"] = step;
      captures.append(data);
      if (workspace_mode) {
        QFile checkpoint(dir + "/render-workspace.json");
        if (checkpoint.open(QIODevice::WriteOnly))
          checkpoint.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                                     {"captures", captures}})
                               .toJson());
      }
      capture_viewport_framebuffer(v).save(dir + QString("/render-%1.png").arg(step));
    }
    if (step == 64) {
      timer.stop();
      QFile f(dir + "/render-mode.json");
      if (!f.open(QIODevice::WriteOnly))
        qFatal("Render mode proof unavailable");
      f.write(QJsonDocument(
                  QJsonObject{{"pid", QCoreApplication::applicationPid()}, {"captures", captures}})
                  .toJson());
      f.close();
      QTimer::singleShot(100, qApp, &QCoreApplication::quit);
    }
  }

 public:
  Demo(QWidget *window,
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
    timer.setInterval(500);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_render_mode_demo(QWidget *w,
                            QWidget *v,
                            const QString &dir,
                            const QString &mesh,
                            int size,
                            std::function<QJsonObject()> state)
{
  new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
