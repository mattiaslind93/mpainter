// SPDX-License-Identifier: GPL-3.0-or-later
#include "ViewportDemoCapture.hh"
#include "InputSpaceDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh, path;
  int size, phase = 0, after_frame = 0, ticks = 0;
  QTimer timer;
  std::function<QJsonObject()> state;
  QJsonArray captures;
  template<class T> T *get(const char *name)
  {
    auto *widget = w->findChild<T *>(name);
    if (!widget)
      qFatal("Input-space control missing: %s", name);
    return widget;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void choose(const QString &name)
  {
    auto *combo = get<QComboBox>("fillTextureColorSpace");
    const int index = combo->findData(name);
    if (index < 0)
      qFatal("OCIO input color space unavailable: %s", qPrintable(name));
    combo->setCurrentIndex(index);
  }
  void capture(const char *name)
  {
    auto s = state();
    s["stage"] = name;
    auto *combo = get<QComboBox>("fillTextureColorSpace");
    s["selected_space"] = combo->currentData().toString();
    s["source_file_absent"] = !QFile::exists(path);
    QJsonArray names;
    for (int i = 0; i < combo->count(); ++i)
      names.append(combo->itemData(i).toString());
    s["color_space_menu"] = names;
    captures.append(s);
    QFile file(dir + "/input-space.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("Input-space proof unavailable");
    file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                         {"captures", captures}})
                   .toJson());
    file.close();
    capture_viewport_framebuffer(v).save(dir + "/" + name + "-viewport.png");
    if (QString(name) == "acescg") {
      get<QScrollArea>("propertiesScroll")
          ->ensureWidgetVisible(get<QComboBox>("fillTextureColorSpace"));
      capture_demo_window(w, v).save(dir + "/input-space-workspace.png");
    }
  }
  void advance()
  {
    if (++ticks > 1000)
      qFatal("Input-space demo timed out");
    const auto s = state();
    if (phase > 1 && (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
                      s["render_frames"].toInt() < after_frame))
      return;
    switch (phase++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *resolution = d->findChild<QComboBox *>("documentResolution");
        resolution->setCurrentIndex(resolution->findData(size));
        auto *create = d->findChild<QPushButton *>("createProject");
        if (!create->isEnabled())
          qFatal("Input-space fixture invalid");
        create->click();
        break;
      }
      case 2:
        action("guiFillTexturePrepare");
        get<QComboBox>("channelPrecision0")->setCurrentIndex(8);
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        timer.stop();
        QTimer::singleShot(100, this, [this] {
          auto *dialog = get<QFileDialog>("fillTextureFileDialog");
          dialog->selectFile(path);
          QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        get<QComboBox>("fillBaseColorResource")->setCurrentIndex(2);
        timer.start();
        break;
      case 4:
        capture("first-auto");
        get<QToolButton>("addFillLayer")->click();
        get<QComboBox>("fillBaseColorResource")->setCurrentIndex(3);
        break;
      case 5: {
        auto *list = get<QListWidget>("shelfAssets");
        QListWidgetItem *item = nullptr;
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->text() == "input-gray")
            item = list->item(i);
        if (!item || !(item->flags() & Qt::ItemIsEnabled))
          qFatal("Shared project texture absent from Shelf");
        QMetaObject::invokeMethod(
            list, "itemDoubleClicked", Qt::DirectConnection, Q_ARG(QListWidgetItem *, item));
        break;
      }
      case 6:
        capture("two-auto");
        get<QToolButton>("fillColorSpaceDisclosure")->setChecked(true);
        choose("ACEScg");
        break;
      case 7:
        capture("acescg");
        action("undoDocument");
        break;
      case 8:
        capture("undone");
        action("redoDocument");
        break;
      case 9:
        capture("redone");
        get<QToolButton>("fillTextureColorSpaceReset")->click();
        break;
      case 10:
        capture("reset-auto");
        choose("Non-Color");
        break;
      case 11:
        capture("non-color");
        choose("ACEScg");
        break;
      case 12:
        capture("restored-acescg");
        action("checkGuiComposite");
        action("guiFillTextureSave");
        break;
      case 13:
        capture("saved");
        if (!QFile::rename(path, path + ".removed"))
          qFatal("Cannot remove original image for packed input-space proof");
        action("guiFillTextureOpen");
        break;
      case 14:
        capture("opened-packed");
        timer.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
    after_frame = state()["render_frames"].toInt() + 20;
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
    path = dir + "/input-gray.png";
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(128, 128, 128, 255));
    if (!image.save(path))
      qFatal("Cannot write input-space fixture");
    timer.setInterval(250);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_input_space_demo(QWidget *w,
                            QWidget *v,
                            const QString &dir,
                            const QString &mesh,
                            int size,
                            std::function<QJsonObject()> state)
{
  new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
