// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class BlendMaterialDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh, library;
  QTimer timer, dialogs;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Missing material control: %s", name);
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
    capture_viewport_framebuffer(v).save(output + "/" + name + ".png");
  }
  void import_action()
  {
    QTimer::singleShot(0, this, [this] { action("importBlendMaterialAction"); });
  }
  bool choose(const QString &name)
  {
    auto *list = w->findChild<QListWidget *>("blendMaterialList");
    if (!list || !list->isEnabled())
      return false;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == name) {
        list->setCurrentRow(i);
        return true;
      }
    qFatal("Material not listed: %s", qPrintable(name));
    return false;
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 1600)
      qFatal("Material demo timeout stage=%d", stage);
    const auto data = state();
    if (ticks % 20 == 0) {
      std::printf("BTS_BLEND_MATERIAL_DEMO stage=%d pending=%d frames=%d\n",
                  stage,
                  int(data["composite_pending"].toBool()),
                  data["render_frames"].toInt());
      std::fflush(stdout);
    }
    if (stage > 1 && (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
                      data["render_frames"].toInt() < frame))
      return;
    if (w->property("blendMaterialImporting").toBool())
      return;
    if (stage == 3 && !choose("Copper constant"))
      return;
    if (stage == 12 && !choose("Linked image"))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *res = dialog->findChild<QComboBox *>("documentResolution");
        res->setCurrentIndex(res->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      }
      case 2:
        capture("before-import");
        import_action();
        break;
      case 3: {
        auto *list = get<QListWidget>("blendMaterialList");
        w->setProperty("blendMaterialHasSavedPreview", !list->currentItem()->icon().isNull());
        get<QPushButton>("importBlendMaterial")->click();
        break;
      }
      case 4: {
        capture("shelf-imported");
        auto *list = get<QListWidget>("shelfAssets");
        QListWidgetItem *found = nullptr;
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->text() == "Copper constant") {
            found = list->item(i);
            break;
          }
        if (!found)
          qFatal("Imported material missing from Shelf");
        list->setCurrentItem(found);
        list->itemDoubleClicked(found);
        break;
      }
      case 5:
        capture("fill-applied");
        action("undoDocument");
        break;
      case 6:
        capture("undone");
        action("redoDocument");
        break;
      case 7:
        capture("redone");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 8:
        if (!QFile::rename(library, library + ".held"))
          qFatal("Cannot hide own source");
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 9:
        capture("opened-without-source");
        if (!QFile::rename(library + ".held", library))
          qFatal("Cannot restore own source");
        import_action();
        break;
      case 10:
        --stage;
        if (!w->findChild<QListWidget *>("blendMaterialList"))
          break;
        stage = 12;
        break;
      case 12:
        get<QPushButton>("importBlendMaterial")->click();
        break;
      case 13: {
        const auto error = get<QLabel>("blendMaterialStatus")->text();
        if (!error.contains("phase 2", Qt::CaseInsensitive))
          qFatal("Expected linked socket error: %s", qPrintable(error));
        w->setProperty("blendMaterialRejected", error);
        get<QDialog>("blendMaterialDialog")->reject();
        capture("unsupported-unchanged");
        auto *shelf = get<QListWidget>("shelfAssets");
        QListWidgetItem *asset = nullptr;
        for (int i = 0; i < shelf->count(); ++i)
          if (shelf->item(i)->text() == "Copper constant") {
            asset = shelf->item(i);
            break;
          }
        if (!asset)
          qFatal("Shelf source missing");
        auto *layers = get<QListWidget>("layerStack");
        QMimeData mime;
        mime.setData("application/x-bts-asset", asset->data(Qt::UserRole).toString().toUtf8());
        QDragEnterEvent enter(
            QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(layers->viewport(), &enter);
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(layers->viewport(), &drop);
        if (!drop.isAccepted())
          qFatal("Material drop rejected");
        break;
      }
      case 14:
        capture("dropped");
        action("undoDocument");
        break;
      case 15: {
        capture("drop-undone");
        timer.stop();
        const auto error = w->property("blendMaterialRejected").toString();
        QFile file(output + "/blend-material.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Cannot write proof");
        file.write(
            QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                      {"captures", captures},
                                      {"saved_preview",
                                       w->property("blendMaterialHasSavedPreview").toBool()},
                                      {"rejected", error}})
                .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 8;
  }

 public:
  BlendMaterialDemo(QWidget *window,
                    QWidget *viewport,
                    QString directory,
                    QString source,
                    std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        output(directory),
        mesh(source),
        library(qEnvironmentVariable("BTS_BLEND_MATERIAL_SOURCE")),
        state(std::move(snapshot))
  {
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(100);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *file = qobject_cast<QFileDialog *>(widget);
            file && file->isVisible() && file->objectName() == "blendMaterialFileDialog")
        {
          file->selectFile(library);
          QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        }
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical)
            qFatal("Material GUI error: %s", qPrintable(box->text()));
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
      }
    });
    dialogs.start(40);
  }
};
}  // namespace bts
