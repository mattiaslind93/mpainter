#pragma once
#include "ViewportDemoCapture.hh"
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
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class OpenPbrImportDemo final : public QObject {
  QWidget *w, *v;
  QString directory;
  int stage = 0, ticks = 0, frame = 0;
  QTimer timer, discard;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("USD profile import control missing: %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void create(const char *name, bool pbr = false)
  {
    auto *dialog = get<QDialog>("newProjectDialog");
    dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(pbr ? 1 : 0);
    dialog->findChild<QLineEdit *>("meshPath")->setText(directory + "/fixtures/" + name + ".usda");
    auto *resolution = dialog->findChild<QComboBox *>("documentResolution");
    resolution->setCurrentIndex(resolution->findData(128));
    dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);
    dialog->findChild<QPushButton *>("createProject")->click();
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    data["shader_workflow"] = get<QComboBox>("shaderWorkflow")->currentIndex();
    data["add_paint_enabled"] = get<QToolButton>("addPaintLayer")->isEnabled();
    captures.append(data);
    capture_viewport_framebuffer(v).save(directory + "/" + name + ".png");
  }
  void advance()
  {
    if (++ticks > 1600)
      qFatal("USD profile import demo timeout");
    const auto data = state();
    if (stage > 1 && (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
                      data["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        create("supported");
        break;
      case 2:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 3:
        capture("supported");
        get<QDialog>("shaderSettings")->accept();
        action("guiSkewSave");
        break;
      case 4:
        action("guiSkewOpen");
        break;
      case 5:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 6:
        capture("opened");
        get<QDialog>("shaderSettings")->accept();
        action("newProject");
        break;
      case 7:
        create("unsupported");
        break;
      case 8:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 9:
        capture("unsupported");
        get<QDialog>("shaderSettings")->accept();
        action("guiSkewSave");
        break;
      case 10:
        action("guiSkewOpen");
        break;
      case 11:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 12:
        capture("unsupported-opened");
        get<QDialog>("shaderSettings")->accept();
        action("newProject");
        break;
      case 13:
        create("supported", true);
        break;
      case 14:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 15: {
        capture("explicit-pbr-template");
        get<QDialog>("shaderSettings")->accept();
        timer.stop();
        QFile output(directory + "/openpbr-import.json");
        if (!output.open(QIODevice::WriteOnly))
          qFatal("USD import proof unavailable");
        output.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                               {"captures", captures}})
                         .toJson());
        output.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 20;
  }

 public:
  OpenPbrImportDemo(QWidget *window,
                    QWidget *viewport,
                    QString output,
                    std::function<QJsonObject()> snapshot)
      : QObject(window), w(window), v(viewport), directory(output), state(std::move(snapshot))
  {
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(100);
    connect(&discard, &QTimer::timeout, this, [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
          if (auto *button = box->button(QMessageBox::Discard))
            button->click();
    });
    discard.start(30);
  }
};
}  // namespace bts
