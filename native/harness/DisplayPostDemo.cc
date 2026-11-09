// SPDX-License-Identifier: GPL-3.0-or-later
#include "ViewportDemoCapture.hh"
#include "DisplayPostDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QOpenGLWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int size, stage = 0, after_frame = 0;
  bool lens_mode = qEnvironmentVariableIsSet("BTS_DISPLAY_LENS_DEMO"),
       color_mode = qEnvironmentVariableIsSet("BTS_DISPLAY_COLOR_DEMO"),
       optics_mode = qEnvironmentVariableIsSet("BTS_DISPLAY_OPTICS_DEMO");
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Display post control missing: %s", name);
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
    data["panel_visible"] = get<QDialog>("displaySettings")->isVisible();
    data["master"] = get<QCheckBox>("postEffectsEnabled")->isChecked();
    data["effect"] = get<QCheckBox>("postVignetteEnabled")->isChecked();
    data["strength"] = get<QDoubleSpinBox>("postVignetteStrength")->value();
    data["vignette_expanded"] = get<QToolButton>("postVignetteExpand")->isChecked();
    data["color_expanded"] = get<QToolButton>("postColorExpand")->isChecked();
    captures.append(data);
    capture_viewport_framebuffer(v).save(dir + "/" + name + "-viewport.png");
    if (QString(name) == "enabled" || QString(name) == "warm" || QString(name) == "defocused" ||
        QString(name) == "spectral" || QString(name) == "barrel")
    {
      get<QScrollArea>("displaySettingsScroll")
          ->ensureWidgetVisible(get<QWidget>(QString(name) == "barrel" ? "postLensSmoothness" :
                                             QString(name) == "warm"   ? "postColorTemperature" :
                                             QString(name) == "defocused" ? "postDofAperture" :
                                             QString(name) == "spectral"  ? "postGlareRemap" :
                                                                            "postVignetteStrength"),
                                0,
                                20);
      capture_demo_window(w, v).save(dir + "/post-effects.png");
      get<QDialog>("displaySettings")->grab().save(dir + "/post-panel.png");
    }
  }
  void lens_step()
  {
    switch (stage++) {
      case 3:
        capture("baseline");
        get<QCheckBox>("postEffectsEnabled")->setChecked(true);
        get<QToolButton>("postLensExpand")->click();
        get<QCheckBox>("postLensEnabled")->setChecked(true);
        get<QDoubleSpinBox>("postLensRoundness")->setValue(0);
        get<QDoubleSpinBox>("postLensSmoothness")->setValue(0);
        break;
      case 4:
        capture("neutral");
        get<QDoubleSpinBox>("postLensPower")->setValue(1.8);
        break;
      case 5:
        capture("barrel");
        get<QDoubleSpinBox>("postLensPower")->setValue(.2);
        break;
      case 6:
        capture("pincushion");
        get<QDoubleSpinBox>("postLensFov")->setValue(65);
        break;
      case 7:
        capture("wide");
        get<QDoubleSpinBox>("postLensPower")->setValue(1);
        get<QDoubleSpinBox>("postLensFov")->setValue(45);
        get<QDoubleSpinBox>("postLensRoundness")->setValue(.3);
        break;
      case 8:
        capture("round");
        get<QDoubleSpinBox>("postLensSmoothness")->setValue(2);
        break;
      case 9:
        capture("smooth");
        get<QCheckBox>("postLensEnabled")->setChecked(false);
        break;
      case 10:
        capture("disabled-retained");
        action("guiShaderPaintUndo");
        break;
      case 11:
        capture("undo-off");
        action("redoDocument");
        break;
      case 12:
        capture("redo-off");
        get<QCheckBox>("postLensEnabled")->setChecked(true);
        get<QDoubleSpinBox>("postLensPower")->setValue(1.6);
        break;
      case 13:
        capture("combined");
        action("guiSkewSave");
        break;
      case 14:
        capture("saved");
        action("guiSkewOpen");
        action("guiSkewPrepare");
        break;
      case 15:
        capture("opened");
        get<QPushButton>("postEffectsRestoreAll")->click();
        break;
      case 16:
        capture("restored");
        get<QCheckBox>("postEffectsEnabled")->setChecked(true);
        get<QCheckBox>("postLensEnabled")->setChecked(true);
        get<QDoubleSpinBox>("postLensPower")->setValue(1.8);
        break;
      case 17: {
        capture("paint-ready");
        const auto screen = state()["lens_probe_screen"].toArray();
        if (screen.size() != 2)
          qFatal("Lens painting needs a visible reference surface");
        const QPointF start(screen[0].toDouble(), screen[1].toDouble());
        const auto send = [&](QEvent::Type type,
                              QPointF point,
                              Qt::MouseButton button,
                              Qt::MouseButtons buttons) {
          QMouseEvent event(
              type, point, v->mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
          QCoreApplication::sendEvent(v, &event);
        };
        send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i)
          send(QEvent::MouseMove, start + QPointF(i, 0), Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, start + QPointF(8, 0), Qt::LeftButton, Qt::NoButton);
        break;
      }
      case 18:
        capture("painted");
        action("guiShaderPaintUndo");
        break;
      case 19: {
        capture("undo-paint");
        timer.stop();
        QFile file(dir + "/display-lens.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Display lens proof unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }
  void optics_step()
  {
    switch (stage++) {
      case 3:
        capture("baseline");
        get<QCheckBox>("postEffectsEnabled")->setChecked(true);
        get<QToolButton>("postDofExpand")->click();
        get<QCheckBox>("postDofEnabled")->setChecked(true);
        break;
      case 4:
        capture("aperture-zero");
        get<QDoubleSpinBox>("postDofAperture")->setValue(4);
        get<QDoubleSpinBox>("postDofFocus")->setValue(3.7);
        break;
      case 5:
        capture("near-focus");
        get<QDoubleSpinBox>("postDofFocus")->setValue(100);
        break;
      case 6:
        capture("defocused");
        get<QCheckBox>("postDofEnabled")->setChecked(false);
        break;
      case 7:
        capture("dof-off-retained");
        get<QCheckBox>("postDofEnabled")->setChecked(true);
        get<QDoubleSpinBox>("postDofAperture")->setValue(0);
        break;
      case 8:
        capture("zero-again");
        get<QCheckBox>("postDofEnabled")->setChecked(false);
        get<QToolButton>("postDofExpand")->click();
        get<QToolButton>("postGlareExpand")->click();
        get<QDoubleSpinBox>("postGlareThreshold")->setValue(.1);
        get<QCheckBox>("postGlareEnabled")->setChecked(true);
        break;
      case 9:
        capture("streaks");
        get<QDoubleSpinBox>("postGlareLuminance")->setValue(.2);
        break;
      case 10:
        capture("weak-streaks");
        get<QDoubleSpinBox>("postGlareLuminance")->setValue(1);
        get<QDoubleSpinBox>("postGlareRemap")->setValue(8);
        break;
      case 11:
        capture("spectral");
        get<QComboBox>("postGlareShape")->setCurrentIndex(1);
        break;
      case 12:
        capture("fog");
        get<QComboBox>("postGlareShape")->setCurrentIndex(2);
        break;
      case 13:
        capture("ghosts");
        get<QDoubleSpinBox>("postGlareLuminance")->setValue(0);
        break;
      case 14:
        capture("glare-zero");
        action("guiShaderPaintUndo");
        break;
      case 15:
        capture("undo-glare");
        action("redoDocument");
        break;
      case 16:
        capture("redo-zero");
        get<QComboBox>("postGlareShape")->setCurrentIndex(0);
        get<QDoubleSpinBox>("postGlareThreshold")->setValue(.2);
        get<QDoubleSpinBox>("postGlareRemap")->setValue(3);
        get<QDoubleSpinBox>("postGlareLuminance")->setValue(.5);
        get<QCheckBox>("postDofEnabled")->setChecked(true);
        get<QDoubleSpinBox>("postDofAperture")->setValue(2);
        get<QDoubleSpinBox>("postDofFocus")->setValue(3.7);
        break;
      case 17:
        capture("combined");
        action("guiSkewSave");
        break;
      case 18:
        capture("saved");
        action("guiSkewOpen");
        action("guiSkewPrepare");
        break;
      case 19:
        capture("opened");
        get<QPushButton>("postEffectsRestoreAll")->click();
        break;
      case 20: {
        capture("restored");
        timer.stop();
        QFile file(dir + "/display-optics.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Display optics proof unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }
  void color_step()
  {
    switch (stage++) {
      case 3:
        capture("baseline");
        get<QCheckBox>("postEffectsEnabled")->setChecked(true);
        get<QToolButton>("postColorExpand")->click();
        break;
      case 4:
        capture("master-only");
        get<QCheckBox>("postColorEnabled")->setChecked(true);
        break;
      case 5:
        capture("color-defaults");
        get<QDoubleSpinBox>("postColorSaturation")->setValue(0);
        break;
      case 6:
        capture("desaturated");
        get<QDoubleSpinBox>("postColorSaturation")->setValue(1);
        get<QDoubleSpinBox>("postColorSepia")->setValue(1);
        break;
      case 7:
        capture("sepia");
        get<QDoubleSpinBox>("postColorSepia")->setValue(0);
        get<QDoubleSpinBox>("postColorTemperature")->setValue(3500);
        break;
      case 8:
        capture("warm");
        get<QDoubleSpinBox>("postColorTemperature")->setValue(6500);
        get<QDoubleSpinBox>("postColorContrast")->setValue(1.4);
        get<QDoubleSpinBox>("postColorBrightness")->setValue(1.1);
        get<QDoubleSpinBox>("postColorBias")->setValue(.02);
        break;
      case 9:
        capture("contrast");
        get<QPushButton>("postColorRestore")->click();
        break;
      case 10:
        capture("color-reset");
        get<QCheckBox>("postColorEnabled")->setChecked(false);
        get<QToolButton>("postColorExpand")->click();
        get<QToolButton>("postToneExpand")->click();
        get<QCheckBox>("postToneEnabled")->setChecked(true);
        break;
      case 11:
        capture("tone-defaults");
        get<QDoubleSpinBox>("postToneExposure")->setValue(2);
        break;
      case 12:
        capture("exposed");
        get<QDoubleSpinBox>("postToneGamma")->setValue(2);
        break;
      case 13:
        capture("gamma");
        get<QComboBox>("postToneFunction")->setCurrentIndex(1);
        break;
      case 14:
        capture("reinhard");
        get<QComboBox>("postToneFunction")->setCurrentIndex(2);
        get<QDoubleSpinBox>("postToneMappingFactor")->setValue(16);
        break;
      case 15:
        capture("photoreceptor");
        get<QPushButton>("postToneRestore")->click();
        break;
      case 16:
        capture("tone-reset");
        get<QDoubleSpinBox>("postToneExposure")->setValue(2);
        get<QCheckBox>("postColorEnabled")->setChecked(true);
        get<QDoubleSpinBox>("postColorSaturation")->setValue(.5);
        break;
      case 17:
        capture("combined");
        action("guiShaderPaintUndo");
        break;
      case 18:
        capture("undo-saturation");
        action("redoDocument");
        break;
      case 19:
        capture("redo-saturation");
        action("guiSkewSave");
        break;
      case 20:
        capture("saved");
        action("guiSkewOpen");
        action("guiSkewPrepare");
        break;
      case 21:
        capture("opened");
        get<QPushButton>("postEffectsRestoreAll")->click();
        break;
      case 22: {
        capture("restored");
        timer.stop();
        QFile file(dir + "/display-color.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Display color proof unavailable");
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
    const auto s = state();
    if (stage > 1 && (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
                      s["render_frames"].toInt() < after_frame))
      return;
    if (lens_mode && stage >= 3) {
      lens_step();
      after_frame = state()["render_frames"].toInt() + 40;
      return;
    }
    if (optics_mode && stage >= 3) {
      optics_step();
      after_frame = state()["render_frames"].toInt() + 40;
      return;
    }
    if (color_mode && stage >= 3) {
      color_step();
      after_frame = state()["render_frames"].toInt() + 40;
      return;
    }
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
        get<QToolButton>("displaySettingsButton")->click();
        break;
      case 3:
        capture("baseline");
        get<QToolButton>("postVignetteExpand")->click();
        break;
      case 4:
        capture("headers-disabled");
        get<QCheckBox>("postEffectsEnabled")->setChecked(true);
        break;
      case 5:
        capture("master-only");
        get<QToolButton>("postVignetteExpand")->click();
        get<QDoubleSpinBox>("postVignetteStrength")->setValue(1);
        break;
      case 6:
        capture("armed");
        get<QCheckBox>("postVignetteEnabled")->setChecked(true);
        break;
      case 7:
        capture("enabled");
        get<QDoubleSpinBox>("postVignetteStrength")->setValue(.2);
        break;
      case 8:
        capture("weaker");
        get<QCheckBox>("postVignetteEnabled")->setChecked(false);
        break;
      case 9:
        capture("disabled-retained");
        action("guiShaderPaintUndo");
        break;
      case 10:
        capture("undo-effect");
        get<QCheckBox>("postVignetteEnabled")->setChecked(false);
        get<QDoubleSpinBox>("postVignetteStrength")->setValue(1);
        get<QCheckBox>("postVignetteEnabled")->setChecked(true);
        break;
      case 11:
        capture("final-enabled");
        action("guiSkewSave");
        break;
      case 12:
        capture("saved");
        action("guiSkewOpen");
        action("guiSkewPrepare");
        break;
      case 13:
        capture("opened");
        get<QCheckBox>("postEffectsEnabled")->setChecked(false);
        break;
      case 14:
        capture("master-off-retained");
        get<QPushButton>("postEffectsRestoreAll")->click();
        break;
      case 15: {
        capture("restored");
        timer.stop();
        QFile file(dir + "/display-post.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Display post proof unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures}})
                       .toJson());
        file.close();
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
void start_display_post_demo(QWidget *w,
                             QWidget *v,
                             const QString &dir,
                             const QString &mesh,
                             int size,
                             std::function<QJsonObject()> state)
{
  new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
