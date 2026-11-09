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
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
namespace bts {
class OpenPbrExportDemo final : public QObject {
  QWidget *w, *v;
  QString directory, mesh;
  int stage = 0, frame = 0, ticks = 0;
  QTimer timer;
  QJsonArray captures, exports;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("OpenPBR export control missing: %s", name);
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
    captures.append(data);
    capture_viewport_framebuffer(v).save(directory + "/" + name + ".png");
  }
  void run_export(bool modern)
  {
    timer.stop();
    auto *poll = new QTimer(this);
    int phase = 0;
    connect(poll, &QTimer::timeout, this, [this, poll, phase, modern]() mutable {
      auto *dialog = w->findChild<QDialog *>("textureExportDialog");
      if (!dialog)
        return;
      if (phase == 0) {
        phase = 1;
        auto *naming = dialog->findChild<QComboBox *>("exportNamingConvention");
        auto *package = dialog->findChild<QCheckBox *>("exportOpenPbrMaterialX");
        auto *preset = dialog->findChild<QComboBox *>("exportPreset");
        if (modern && naming->currentIndex() != 1)
          qFatal("OpenPBR export did not default to its naming convention");
        package->setChecked(false);
        preset->setCurrentIndex(1);
        if (package->isEnabled())
          qFatal("Custom preset wrongly permits a MaterialX package");
        preset->setCurrentIndex(0);
        naming->setCurrentIndex(modern ? 1 : 0);
        if (!package->isEnabled())
          qFatal("Document channels do not permit OpenPBR MaterialX export");
        package->setChecked(modern);
        auto *format = dialog->findChild<QComboBox *>("exportFormat");
        auto *precision = dialog->findChild<QComboBox *>("exportPrecision");
        if (modern && (format->isEnabled() || precision->isEnabled() ||
                       precision->currentData().toInt() != 3))
          qFatal("MaterialX did not lock EXR32 encoding");
        format->setCurrentIndex(1);
        precision->setCurrentIndex(precision->findData(3));
        const auto output = directory + (modern ? "/openpbr" : "/classic");
        QDir().mkpath(output);
        dialog->findChild<QLineEdit *>("exportDirectory")->setText(output);
        auto *size = dialog->findChild<QComboBox *>("exportSize");
        size->setCurrentIndex(size->findData(128));
        auto *sets = dialog->findChild<QListWidget *>("exportTextureSets");
        for (int row = 0; row < sets->count(); ++row)
          sets->item(row)->setCheckState(Qt::Checked);
        auto *listed = dialog->findChild<QListWidget *>("exportFileList");
        bool modernColor = false, license = false;
        for (int row = 0; row < listed->count(); ++row) {
          const auto name = listed->item(row)->text();
          modernColor |= name.contains("_base_color_");
          license |= name == "OpenPBR-LICENSE.txt";
        }
        if (modernColor != modern || license != modern)
          qFatal("Export list does not match naming/package choice");
        dialog->findChild<QPushButton *>("runTextureExport")->click();
        return;
      }
      if (!dialog->property("exportFinished").toBool())
        return;
      if (!dialog->property("exportError").toString().isEmpty())
        qFatal("OpenPBR GUI export failed: %s",
               qPrintable(dialog->property("exportError").toString()));
      exports.append(QJsonObject{{"convention", modern ? "OpenPBR" : "Classic"},
                                 {"materialx", modern},
                                 {"directory", directory + (modern ? "/openpbr" : "/classic")}});
      dialog->grab().save(directory + (modern ? "/export-openpbr.png" : "/export-classic.png"));
      poll->stop();
      dialog->findChild<QPushButton *>("cancelTextureExport")->click();
      poll->deleteLater();
      QTimer::singleShot(100, this, [this] { timer.start(); });
    });
    poll->start(50);
    action("exportTextures");
  }
  void advance()
  {
    if (++ticks > 1500)
      qFatal("OpenPBR export demo timeout");
    const auto s = state();
    if (stage > 1 && (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
                      s["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        auto *preset = d->findChild<QComboBox *>("projectTemplate");
        preset->setCurrentIndex(preset->findData(":openpbr-compatibility"));
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        d->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(128));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        get<QToolButton>("addFillLayer")->click();
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      case 3:
        get<QCheckBox>("displacementChannel")->setChecked(true);
        get<QCheckBox>("fillChannelEnabled14")->setChecked(true);
        get<QDoubleSpinBox>("displacementMidlevel")->setValue(0);
        get<QDoubleSpinBox>("materialBump")->setValue(-.125);
        get<QDoubleSpinBox>("materialDisplacement")->setValue(-.0025);
        get<QDialog>("shaderSettings")->accept();
        break;
      case 4:
        capture("configured");
        run_export(true);
        break;
      case 5:
        capture("exported-openpbr");
        action("guiSkewSave");
        break;
      case 6:
        action("guiSkewOpen");
        break;
      case 7:
        capture("opened");
        run_export(false);
        break;
      case 8: {
        capture("exported-classic");
        timer.stop();
        QFile file(directory + "/openpbr-export.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Export proof file unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures},
                                             {"exports", exports}})
                       .toJson());
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 24;
  }

 public:
  OpenPbrExportDemo(QWidget *window,
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
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace bts
