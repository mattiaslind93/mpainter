#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
namespace bts {
class BlendImagesDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh, library, cache;
  QTimer timer, dialogs, ping;
  QElapsedTimer heartbeat;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  double max_ping = 0;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Missing image material control %s", name);
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
  bool choose(const char *name)
  {
    auto *list = w->findChild<QListWidget *>("blendMaterialList");
    if (!list || !list->isEnabled())
      return false;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == name) {
        list->setCurrentRow(i);
        return true;
      }
    qFatal("Material not listed %s", name);
    return false;
  }
  void apply(bool replace = false)
  {
    if (replace)
      w->setProperty("fillMaterialPick", true);
    auto *list = get<QListWidget>("shelfAssets");
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->text() == "Image textures") {
        list->setCurrentRow(i);
        list->itemDoubleClicked(list->item(i));
        return;
      }
    qFatal("Image material missing in Shelf");
  }
  void finish()
  {
    capture("cancelled-unchanged");
    timer.stop();
    dialogs.stop();
    ping.stop();
    QFile file(output + "/blend-images.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("Image material proof unavailable");
    file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                         {"max_import_ping_ms", max_ping},
                                         {"pid", QCoreApplication::applicationPid()}})
                   .toJson());
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 2500)
      qFatal("Image material timeout %d", stage);
    if (w->property("blendMaterialImporting").toBool())
      return;
    const auto data = state();
    if (stage > 1 && (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
                      data["render_frames"].toInt() < frame))
      return;
    if ((stage == 3 || stage == 20) && !choose(stage == 3 ? "Image textures" : "Linked image"))
      return;
    if (stage == 23 && !choose("Missing tile"))
      return;
    if (stage == 25 && !choose("Colored scalar"))
      return;
    if (stage == 27 && !choose("Image textures"))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QCheckBox>("preserveUvTiles")->setChecked(true);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        action("guiBlendImagesPrepare");
        break;
      case 2:
        capture("before-import");
        import_action();
        break;
      case 3:
        get<QPushButton>("importBlendMaterial")->click();
        break;
      case 4:
        capture("shelf-imported");
        apply();
        break;
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
        action("guiBlendImagesAlter");
        break;
      case 8:
        capture("before-replace");
        apply(true);
        break;
      case 9:
        capture("replaced");
        action("undoDocument");
        break;
      case 10:
        capture("undo-replace");
        action("redoDocument");
        break;
      case 11:
        capture("redo-replace");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 12: {
        const auto folder = QFileInfo(library).absolutePath() + "/images";
        if (!QFile::rename(library, library + ".held") ||
            !QDir().rename(folder, folder + ".held") || !QDir().rename(cache, cache + ".held"))
          qFatal("Cannot hide private image sources");
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      }
      case 13:
        capture("opened-without-sources");
        if (!QFile::rename(library + ".held", library) ||
            !QDir().rename(QFileInfo(library).absolutePath() + "/images.held",
                           QFileInfo(library).absolutePath() + "/images") ||
            !QDir().rename(cache, cache + ".reopened") || !QDir().rename(cache + ".held", cache))
          qFatal("Cannot restore private sources");
        apply();
        break;
      case 14:
        capture("reused-after-open");
        action("undoDocument");
        break;
      case 15:
        capture("undo-reused");
        import_action();
        stage = 20;
        break;
      case 20:
        get<QPushButton>("importBlendMaterial")->click();
        break;
      case 21:
        if (!get<QLabel>("blendMaterialStatus")->text().contains("phase 2", Qt::CaseInsensitive))
          qFatal("Missing generated image error");
        capture("unsupported-unchanged");
        get<QDialog>("blendMaterialDialog")->reject();
        import_action();
        stage = 23;
        break;
      case 23:
        get<QPushButton>("importBlendMaterial")->click();
        break;
      case 24:
        if (!get<QLabel>("blendMaterialStatus")->text().contains("tile 1011"))
          qFatal("Missing tile was not rejected");
        capture("missing-tile-unchanged");
        get<QDialog>("blendMaterialDialog")->reject();
        import_action();
        break;
      case 25:
        get<QPushButton>("importBlendMaterial")->click();
        break;
      case 26:
        if (!get<QLabel>("blendMaterialStatus")->text().contains("grayscale"))
          qFatal("Colored scalar was not rejected");
        capture("colored-scalar-unchanged");
        get<QDialog>("blendMaterialDialog")->reject();
        import_action();
        break;
      case 27:
        get<QPushButton>("importBlendMaterial")->click();
        get<QDialog>("blendMaterialDialog")->reject();
        break;
      case 28:
        finish();
        break;
    }
    frame = state()["render_frames"].toInt() + 2;
  }

 public:
  BlendImagesDemo(QWidget *window,
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
        cache(qEnvironmentVariable("BTS_RESOURCE_CACHE")),
        state(std::move(snapshot))
  {
    if (!QFileInfo(cache).absoluteFilePath().startsWith(QFileInfo(output).absoluteFilePath() +
                                                        "/"))
      qFatal("Image demo requires isolated resource cache");
    timer.setInterval(30);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
    heartbeat.start();
    ping.setInterval(10);
    connect(&ping, &QTimer::timeout, this, [this] {
      if (w->property("blendMaterialImporting").toBool())
        max_ping = std::max(max_ping, double(heartbeat.nsecsElapsed()) / 1e6);
      heartbeat.restart();
    });
    ping.start();
    dialogs.setInterval(20);
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
            qFatal("Image material GUI error %s", qPrintable(box->text()));
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
      }
    });
    dialogs.start();
  }
};
}  // namespace bts
