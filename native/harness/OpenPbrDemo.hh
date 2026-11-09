#pragma once
#include "ViewportDemoCapture.hh"
#include "ColorPicker.hh"
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
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class OpenPbrDemo final : public QObject {
  QWidget *w, *v;
  QString directory, mesh;
  int resolution, stage = 0, frame = 0, ticks = 0;
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("OpenPBR demo control absent: %s", name);
    return p;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void number(const char *id, double value)
  {
    get<QDoubleSpinBox>((QString("openpbrParameter_") + id).toUtf8().constData())->setValue(value);
  }
  void tint(PickerColor color)
  {
    auto *p = dynamic_cast<ColorPicker *>(get<QWidget>("openpbrParameter_coat_color"));
    if (!p)
      qFatal("OpenPBR tint picker absent");
    p->accept_sample(color);
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    data["error_visible"] = !get<QLabel>("openpbrParameterError")->isHidden();
    data["unsupported_disabled"] =
        !get<QWidget>("openpbrParameter_coat_roughness_anisotropy")->isEnabled();
    data["normal_artist_row_absent"] = !w->findChild<QWidget *>(
        "openpbrParameter_geometry_normal");
    data["shader_undo"] = get<QPushButton>("shaderUndo")->isEnabled();
    data["shader_redo"] = get<QPushButton>("shaderRedo")->isEnabled();
    captures.append(data);
    if (!capture_viewport_framebuffer(v).save(directory + "/" + name + ".png"))
      qFatal("OpenPBR FBO unavailable");
    if (QString(name) == "lobes") {
      capture_demo_window(w, v).save(directory + "/workspace.png");
      get<QDialog>("shaderSettings")->grab().save(directory + "/settings.png");
    }
  }
  void advance()
  {
    if (++ticks > 1500)
      qFatal("OpenPBR demo timeout");
    const auto data = state();
    if (stage > 1 && (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
                      data["render_frames"].toInt() < frame))
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
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 3:
        capture("principled");
        get<QComboBox>("shaderWorkflow")->setCurrentIndex(1);
        break;
      case 4:
        capture("openpbr-defaults");
        number("specular_weight", 1.6);
        number("coat_weight", 1);
        number("coat_roughness", .12);
        number("coat_ior", 1.8);
        number("base_diffuse_roughness", .4);
        tint({.3f, .5f, .8f, 1});
        break;
      case 5:
        capture("lobes");
        tint({2, 0, 0, 1});
        break;
      case 6:
        capture("invalid-rejected");
        get<QPushButton>("shaderUndo")->click();
        break;
      case 7:
        capture("undo");
        get<QPushButton>("shaderRedo")->click();
        break;
      case 8:
        capture("redo");
        get<QCheckBox>("openpbrParameter_thin_film_weight")->setChecked(true);
        number("thin_film_thickness", .3);
        get<QCheckBox>("openpbrParameter_geometry_thin_walled")->setChecked(true);
        break;
      case 9:
        capture("film-thin-wall");
        get<QPushButton>("shaderUndo")->click();
        get<QPushButton>("shaderUndo")->click();
        break;
      case 10:
        capture("undo-film");
        number("base_weight", .2);
        break;
      case 11:
        capture("base-weight");
        get<QComboBox>("shaderWorkflow")->setCurrentIndex(0);
        break;
      case 12:
        capture("restored");
        get<QComboBox>("shaderWorkflow")->setCurrentIndex(1);
        break;
      case 13:
        capture("defaults-again");
        number("coat_weight", .8);
        break;
      case 14:
        capture("saved");
        action("guiSkewSave");
        break;
      case 15:
        action("guiSkewOpen");
        break;
      case 16:
        capture("opened");
        get<QPushButton>("shaderRestoreDefaults")->click();
        break;
      case 17:
        capture("shader-defaults");
        get<QPushButton>("shaderUndo")->click();
        break;
      case 18:
        capture("undo-defaults");
        get<QComboBox>("shaderWorkflow")->setCurrentIndex(0);
        break;
      case 19: {
        capture("opened-restored");
        timer.stop();
        QFile file(directory + "/openpbr.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("OpenPBR proof file unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures}})
                       .toJson());
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 24;
  }

 public:
  OpenPbrDemo(QWidget *window,
              QWidget *viewport,
              QString dir,
              QString fixture,
              int size,
              std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        directory(dir),
        mesh(fixture),
        resolution(size),
        state(std::move(snapshot))
  {
    QDir().mkpath(directory);
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
