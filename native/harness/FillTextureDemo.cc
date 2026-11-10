// SPDX-License-Identifier: GPL-3.0-or-later
#include "FillTextureDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <set>
namespace bts {
namespace {
class TextureDemo final : public QObject {
  QWidget *w;
  QString dir, mesh, path;
  int resolution, stage = 0;
  QTimer timer;
  std::function<QJsonObject()> state;
  QJsonArray captures;
  QAction *action(const char *name)
  {
    auto *a = w->findChild<QAction *>(name);
    if (!a)
      qFatal("Texture GUI action missing: %s", name);
    return a;
  }
  QComboBox *resource()
  {
    return w->findChild<QComboBox *>("fillBaseColorResource");
  }
  void capture(const char *name)
  {
    auto s = state();
    s["stage"] = name;
    s["resource_label"] = resource()->currentText();
    s["shelf_filter"] = w->findChild<QLineEdit *>("shelfSearch")->text();
    auto *item = w->findChild<QListWidget *>("layerStack")->currentItem();
    if (item) {
      const auto thumb = item->icon().pixmap(48).toImage();
      std::set<QRgb> colors;
      for (int y = 0; y < thumb.height(); ++y)
        for (int x = 0; x < thumb.width(); ++x)
          colors.insert(thumb.pixel(x, y));
      s["thumbnail_colors"] = int(colors.size());
      thumb.save(dir + "/" + name + "-thumbnail.png");
    }
    captures.append(s);
    w->grab().save(dir + "/" + name + ".png");
  }
  void advance()
  {
    if (stage > 2 && state()["composite_pending"].toBool())
      return;
    switch (stage++) {
      case 0:
        for (auto *a : w->findChildren<QAction *>())
          if (a->text() == "&New…") {
            a->trigger();
            return;
          }
        qFatal("Texture New action missing");
      case 1: {
        QDialog *d = nullptr;
        for (auto *candidate : w->findChildren<QDialog *>())
          if (candidate->findChild<QLineEdit *>("meshPath"))
            d = candidate;
        if (!d)
          qFatal("Texture New dialog missing");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        action("guiFillTexturePrepare")->trigger();
        w->findChild<QToolButton *>("addFillLayer")->click();
        break;
      case 3: {
        capture("uniform");
        timer.stop();
        QTimer::singleShot(100, this, [this] {
          auto *d = w->findChild<QFileDialog *>("fillTextureFileDialog");
          if (!d)
            qFatal("Texture file dialog missing");
          d->selectFile(path);
          QMetaObject::invokeMethod(d, "accept", Qt::DirectConnection);
        });
        resource()->setCurrentIndex(2);
        timer.start(500);
        break;
      }
      case 4:
        capture("file-bound");
        w->findChild<QDoubleSpinBox *>("fillOffset0")->setValue(.5);
        break;
      case 5:
        capture("offset");
        action("undoDocument")->trigger();
        break;
      case 6:
        capture("undo-offset");
        resource()->setCurrentIndex(1);
        break;
      case 7:
        capture("atom");
        action("undoDocument")->trigger();
        break;
      case 8:
        capture("undo-atom");
        resource()->setCurrentIndex(0);
        break;
      case 9:
        capture("uniform-again");
        resource()->setCurrentIndex(3);
        break;
      case 10: {
        capture("shelf-filter");
        auto *list = w->findChild<QListWidget *>("shelfAssets");
        QListWidgetItem *item = nullptr;
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->text() == "fill-pair")
            item = list->item(i);
        if (!item || !(item->flags() & Qt::ItemIsEnabled))
          qFatal("Shelf texture disabled for fill");
        QMetaObject::invokeMethod(
            list, "itemDoubleClicked", Qt::DirectConnection, Q_ARG(QListWidgetItem *, item));
        break;
      }
      case 11:
        capture("shelf-bound");
        w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(1);
        break;
      case 12:
        capture("triplanar");
        action("checkGuiComposite")->trigger();
        capture("reference");
        action("guiFillTextureSave")->trigger();
        capture("saved");
        if (!QFile::rename(path, path + ".removed"))
          qFatal("Cannot move source texture for packed proof");
        action("guiFillTextureOpen")->trigger();
        break;
      case 13: {
        capture("opened-packed");
        timer.stop();
        QFile file(dir + "/fill-texture.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Texture proof unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures},
                                             {"source_file_absent", !QFile::exists(path)}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  TextureDemo(QWidget *workspace, QString d, QString m, int r, std::function<QJsonObject()> s)
      : QObject(workspace), w(workspace), dir(d), mesh(m), resolution(r), state(std::move(s))
  {
    QDir().mkpath(dir);
    path = dir + "/fill-pair.png";
    QImage image(128, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 128; ++x)
        image.setPixelColor(x, y, x < 64 ? QColor(Qt::red) : QColor(Qt::blue));
    if (!image.save(path))
      qFatal("Texture source write failed");
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_fill_texture_demo(QWidget *w,
                             QWidget *,
                             const QString &d,
                             const QString &m,
                             int r,
                             std::function<QJsonObject()> state)
{
  new TextureDemo(w, d, m, r, std::move(state));
}
}  // namespace bts
