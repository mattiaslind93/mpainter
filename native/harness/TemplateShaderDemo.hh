// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class TemplateShaderDemo final : public QObject {
  QWidget *w, *v;
  QString directory, mesh;
  int stage = 0, frame = 0, ticks = 0;
  QTimer timer, dialogs;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("Template shader control absent: %s", name);
    return p;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  QString preset(bool legacy = false) const
  {
    return directory + (legacy ? "/legacy.btst" : "/templates/coated.btst");
  }
  void select_file(const char *name, const QString &path)
  {
    auto *picker = get<QFileDialog>(name);
    picker->setOption(QFileDialog::DontConfirmOverwrite);
    picker->selectFile(path);
    QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
  }
  void create(const QString &builtin = QString())
  {
    auto *d = get<QDialog>("newProjectDialog");
    auto *choice = d->findChild<QComboBox *>("projectTemplate");
    if (!builtin.isNull()) {
      const int row = choice->findData(builtin);
      if (row < 0)
        qFatal("Built-in template absent");
      choice->setCurrentIndex(row);
    }
    d->findChild<QLineEdit *>("meshPath")->setText(mesh);
    auto *size = d->findChild<QComboBox *>("documentResolution");
    size->setCurrentIndex(size->findData(128));
    if (!d->findChild<QPushButton *>("createProject")->isEnabled())
      qFatal("Template New project is disabled");
    d->findChild<QPushButton *>("createProject")->click();
  }
  void browse(bool legacy)
  {
    get<QPushButton>("browseProjectTemplate")->click();
    select_file("projectTemplatePicker", preset(legacy));
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    captures.append(data);
    if (!capture_viewport_framebuffer(v).save(directory + "/" + name + ".png"))
      qFatal("Template real FBO unavailable");
    if (QString(name) == "template-loaded")
      capture_demo_window(w, v).save(directory + "/workspace.png");
  }
  void advance()
  {
    if (++ticks > 1500)
      qFatal("Template shader demo timeout");
    for (auto *box : w->findChildren<QMessageBox *>())
      if (box->isVisible()) {
        if (box->standardButtons() & QMessageBox::Discard) {
          box->button(QMessageBox::Discard)->click();
          return;
        }
        qFatal("Template unexpected dialog: %s", qPrintable(box->text()));
      }
    const auto s = state();
    if (stage > 1 && (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
                      s["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        create(":openpbr-compatibility");
        break;
      case 2:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 3:
        capture("builtin");
        get<QDoubleSpinBox>("openpbrParameter_coat_weight")->setValue(.6);
        break;
      case 4:
        capture("configured");
        action("guiSkewSave");
        break;
      case 5:
        QTimer::singleShot(0, this, [this] { select_file("saveTemplatePicker", preset()); });
        action("saveProjectTemplate");
        break;
      case 6: {
        QFile file(preset());
        if (!file.open(QIODevice::ReadOnly))
          qFatal("Template was not written");
        auto data = QJsonDocument::fromJson(file.readAll()).object();
        data["version"] = 1;
        data["shader"] = "principled-metallic-roughness";
        QFile legacy(preset(true));
        if (!legacy.open(QIODevice::WriteOnly))
          qFatal("Legacy template fixture write failed");
        legacy.write(QJsonDocument(data).toJson());
        get<QDialog>("shaderSettings")->accept();
        action("newProject");
        break;
      }
      case 7:
        browse(false);
        break;
      case 8:
        create();
        break;
      case 9:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 10:
        capture("template-loaded");
        action("guiSkewSave");
        break;
      case 11:
        action("guiSkewOpen");
        break;
      case 12:
        capture("opened");
        get<QDialog>("shaderSettings")->accept();
        action("newProject");
        break;
      case 13:
        create(QString(""));
        break;
      case 14:
        capture("no-template");
        action("newProject");
        break;
      case 15:
        create(":pbr");
        break;
      case 16:
        capture("pbr");
        action("newProject");
        break;
      case 17:
        browse(true);
        break;
      case 18:
        create();
        break;
      case 19:
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 20: {
        capture("legacy-template");
        timer.stop();
        QFile file(directory + "/template-shaders.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Template proof unavailable");
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
  TemplateShaderDemo(QWidget *window,
                     QWidget *viewport,
                     QString dir,
                     QString fixture,
                     std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        directory(dir),
        mesh(fixture),
        state(std::move(snapshot))
  {
    QDir().mkpath(directory);
    dialogs.setInterval(30);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      for (auto *box : w->findChildren<QMessageBox *>())
        if (box->isVisible()) {
          if (box->standardButtons() & QMessageBox::Discard) {
            box->button(QMessageBox::Discard)->click();
            return;
          }
          qFatal("Template unexpected modal dialog: %s", qPrintable(box->text()));
        }
    });
    dialogs.start();
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
