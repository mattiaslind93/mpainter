#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QTreeWidget>
#include <QDialog>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidgetItemIterator>
namespace bts {
class BlendAssetLibraryDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh, root, current_root, cache;
  QTimer timer, dialogs, ping;
  QElapsedTimer heartbeat;
  std::function<QJsonObject()> state;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false, cancel_next = false, cancelled_loading = false;
  double max_ping = 0;
  QJsonArray captures;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("Library control missing %s", name);
    return p;
  }
  QDialog *browser() const
  {
    return w->findChild<QDialog *>("blendAssetLibraryBrowser");
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void open()
  {
    QTimer::singleShot(0, this, [this] { action("browseBlendChannelLibraryAction"); });
  }
  void catalog(const QString &path)
  {
    auto *tree = get<QTreeWidget>("blendLibraryCatalogs");
    for (QTreeWidgetItemIterator i(tree); *i; ++i)
      if ((*i)->data(0, Qt::UserRole).toString() == path) {
        tree->setCurrentItem(*i);
        return;
      }
    qFatal("Catalog missing %s", qPrintable(path));
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    QJsonArray rows;
    if (auto *dialog = browser(); dialog && dialog->isVisible()) {
      auto *items = get<QListWidget>("blendLibraryMaterials");
      for (int i = 0; i < items->count(); ++i) {
        const auto *row = items->item(i);
        rows.append(QJsonObject{{"text", row->text()},
                                {"source", row->data(Qt::UserRole + 1).toString()},
                                {"catalog", row->data(Qt::UserRole + 2).toString()},
                                {"asset", row->data(Qt::UserRole + 3).toBool()},
                                {"saved", row->data(Qt::UserRole + 4).toBool()},
                                {"generated", row->data(Qt::UserRole + 5).toBool()},
                                {"tooltip", row->toolTip()}});
      }
      data["source_files"] = dialog->property("sourceFiles").toInt();
      data["all_materials"] = dialog->property("allMaterials").toInt();
      data["generated_previews"] = dialog->property("generatedPreviews").toInt();
      dialog->grab().save(output + "/" + name + ".png");
    }
    else
      capture_viewport_framebuffer(v).save(output + "/" + name + ".png");
    data["rows"] = rows;
    int cached = 0;
    QDirIterator files(cache, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
      files.next();
      cached += files.fileInfo().fileName() == "swatch.exr";
    }
    data["cached_originals"] = cached;
    captures.append(data);
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 2500)
      qFatal("Library demo timeout %d", stage);
    const auto data = state();
    if (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
        (frame && data["render_frames"].toInt() < frame))
      return;
    frame = 0;
    if (stage >= 3 && stage <= 10) {
      auto *dialog = browser();
      if (!dialog || !dialog->property("libraryLoaded").toBool() ||
          !dialog->property("rowsReady").toBool())
        return;
    }
    if (stage == 3 && (browser()->property("generatedPreviews").toInt() < 5 ||
                       browser()->property("previewPending").toBool()))
      return;
    if (stage == 10 && browser()->property("importedCount").toInt() < 1)
      return;
    switch (stage++) {
      case 0:
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
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 2:
        capture("before-library");
        open();
        break;
      case 3:
        capture("all-assets");
        catalog("Materials/Metals");
        break;
      case 4:
        capture("metals");
        get<QLineEdit>("blendLibrarySearch")->setText("nested");
        break;
      case 5:
        capture("nested-search");
        get<QLineEdit>("blendLibrarySearch")->clear();
        catalog("Materials/Plastics");
        break;
      case 6:
        capture("plastics");
        catalog("");
        get<QCheckBox>("blendLibraryAssetsOnly")->setChecked(false);
        break;
      case 7:
        capture("including-unmarked");
        get<QCheckBox>("blendLibraryAssetsOnly")->setChecked(true);
        get<QLineEdit>("blendLibrarySearch")->setText("blue");
        break;
      case 8:
        capture("tag-search");
        get<QPushButton>("blendLibraryImport")->click();
        stage = 10;
        break;
      case 10:
        capture("shelf-imported");
        browser()->reject();
        break;
      case 11: {
        auto *shelf = get<QListWidget>("shelfAssets");
        bool found = false;
        for (int i = 0; i < shelf->count(); ++i)
          if (shelf->item(i)->text() == "Textured generated") {
            shelf->setCurrentItem(shelf->item(i));
            shelf->itemDoubleClicked(shelf->item(i));
            found = true;
            break;
          }
        if (!found)
          qFatal("Library import absent from Shelf");
        frame = state()["render_frames"].toInt() + 1;
        break;
      }
      case 12:
        capture("applied");
        action("undoDocument");
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 13:
        capture("undone");
        action("redoDocument");
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 14:
        capture("redone");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 15:
        if (!QDir().rename(root, root + ".held") || !QDir().rename(cache, cache + ".held"))
          qFatal("Cannot hide private library/cache");
        busy = true;
        action("guiSkewOpen");
        busy = false;
        frame = state()["render_frames"].toInt() + 1;
        break;
      case 16:
        capture("opened-without-library");
        if (!QDir().rename(root + ".held", root) || !QDir().rename(cache, cache + ".reopened") ||
            !QDir().rename(cache + ".held", cache))
          qFatal("Cannot restore own library/cache");
        current_root = QFileInfo(root).absolutePath() + "/cancel-library";
        cancel_next = true;
        open();
        break;
      case 17:
        if (!cancelled_loading) {
          --stage;
          return;
        }
        capture("cancelled-scan");
        timer.stop();
        dialogs.stop();
        ping.stop();
        {
          QFile file(output + "/blend-library.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Library proof unavailable");
          file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                               {"max_browser_ping_ms", max_ping},
                                               {"cancelled_while_loading", cancelled_loading},
                                               {"pid", QCoreApplication::applicationPid()}})
                         .toJson());
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
  }

 public:
  BlendAssetLibraryDemo(
      QWidget *a, QWidget *b, QString c, QString d, std::function<QJsonObject()> f)
      : QObject(a),
        w(a),
        v(b),
        output(c),
        mesh(d),
        root(qEnvironmentVariable("BTS_BLEND_ASSET_ROOT")),
        current_root(root),
        cache(qEnvironmentVariable("BTS_RESOURCE_CACHE")),
        state(std::move(f))
  {
    timer.setInterval(30);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
    heartbeat.start();
    ping.setInterval(10);
    connect(&ping, &QTimer::timeout, this, [this] {
      if (auto *dialog = browser(); dialog && dialog->isVisible())
        max_ping = std::max(max_ping, double(heartbeat.nsecsElapsed()) / 1e6);
      heartbeat.restart();
    });
    ping.start();
    dialogs.setInterval(10);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *file = qobject_cast<QFileDialog *>(widget);
            file && file->isVisible() && file->objectName() == "blendLibraryFolderDialog")
        {
          file->selectFile(current_root);
          QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        }
        if (auto *dialog = qobject_cast<QDialog *>(widget);
            dialog && dialog->isVisible() && dialog->objectName() == "blendAssetLibraryBrowser" &&
            cancel_next)
        {
          cancelled_loading = !dialog->property("libraryLoaded").toBool();
          cancel_next = false;
          dialog->reject();
        }
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
      }
    });
    dialogs.start();
  }
};
}  // namespace bts
