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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class OpenPbrImagesDemo final : public QObject {
  QWidget *w, *v;
  QString directory, mesh;
  int stage = 0, ticks = 0, frame = 0;
  QTimer timer, discard, ping;
  QElapsedTimer ping_clock;
  double max_ping = 0, max_import_ping = 0;
  QJsonArray captures, samples;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("USD image import control missing: %s", name);
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
    data["samples"] = samples;
    data["add_paint_enabled"] = get<QToolButton>("addPaintLayer")->isEnabled();
    data["import_status"] = w->property("importedMaterialStatus").toString();
    captures.append(data);
    capture_viewport_framebuffer(v).save(directory + "/" + name + ".png");
  }
  void create()
  {
    auto *d = get<QDialog>("newProjectDialog");
    d->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
    d->findChild<QLineEdit *>("meshPath")->setText(mesh);
    auto *size = d->findChild<QComboBox *>("documentResolution");
    size->setCurrentIndex(size->findData(128));
    d->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);
    d->findChild<QPushButton *>("createProject")->click();
    ping_clock.restart();
  }
  void finish()
  {
    timer.stop();
    QFile output(directory + "/openpbr-images.json");
    if (!output.open(QIODevice::WriteOnly))
      qFatal("USD image proof unavailable");
    output.write(
        QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                  {"captures", captures},
                                  {"max_background_ping_ms", max_ping},
                                  {"max_import_ping_ms", max_import_ping},
                                  {"visible_error", w->property("importErrorShown").toBool()}})
            .toJson());
    output.close();
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
  }
  void sample(int tile)
  {
    w->setProperty("pbrSampleTile", tile);
    action("guiOpenPbrImportSample");
  }
  void collect(int tile)
  {
    auto data = w->property("pbrSample").toJsonObject();
    data["tile"] = tile;
    samples.append(data);
  }
  void advance()
  {
    if (++ticks > 2400)
      qFatal("USD image adoption demo timeout stage=%d", stage);
    const auto data = state();
    if (ticks % 50 == 0)
      std::fprintf(
          stderr,
          "BTS_USD_IMAGE_DEMO stage=%d loading=%d pending=%d frame=%d target=%d status=%s\n",
          stage,
          int(data["texture_loading"].toBool()),
          int(data["composite_pending"].toBool()),
          data["render_frames"].toInt(),
          frame,
          qPrintable(w->property("importedMaterialStatus").toString()));
    if (stage > 1 && (data["texture_loading"].toBool() || data["composite_pending"].toBool() ||
                      data["property_pending"].toBool() || data["render_frames"].toInt() < frame ||
                      w->property("pbrSamplePending").toBool()))
      return;
    if (qEnvironmentVariableIsSet("BTS_OPENPBR_DEFERRED_DEMO")) {
      if (stage == 0 && qEnvironmentVariableIsSet("BTS_OPENPBR_DEFERRED_LEGACY")) {
        action("guiSkewOpen");
        stage = 2;
        frame = state()["render_frames"].toInt() + 20;
        return;
      }
      switch (stage++) {
        case 0:
          action("newProject");
          break;
        case 1:
          create();
          break;
        case 2:
          capture("deferred");
          action("guiSkewSave");
          break;
        case 3:
          action("guiSkewOpen");
          break;
        case 4:
          capture("deferred-opened");
          finish();
          break;
      }
      frame = state()["render_frames"].toInt() + 20;
      return;
    }
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        create();
        break;
      case 2:
        sample(1001);
        break;
      case 3:
        collect(1001);
        sample(1002);
        break;
      case 4:
        collect(1002);
        capture("imported");
        action("guiSkewSave");
        break;
      case 5: {
        const auto parent = QFileInfo(mesh).absolutePath();
        if (!QDir().rename(parent, parent + ".held"))
          qFatal("Cannot hide own USD source directory");
        action("guiSkewOpen");
        break;
      }
      case 6: {
        const auto parent = QFileInfo(mesh).absolutePath();
        if (!QDir().rename(parent + ".held", parent))
          qFatal("Cannot restore own USD source directory");
        samples = {};
        sample(1001);
        break;
      }
      case 7:
        collect(1001);
        sample(1002);
        break;
      case 8:
        collect(1002);
        capture("opened-without-sources");
        finish();
        break;
    }
    frame = state()["render_frames"].toInt() + 20;
  }

 public:
  OpenPbrImagesDemo(QWidget *window,
                    QWidget *viewport,
                    QString output,
                    QString source,
                    std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        directory(output),
        mesh(source),
        state(std::move(snapshot))
  {
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(100);
    connect(&discard, &QTimer::timeout, this, [this] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->objectName() == "textureLoadErrorDialog") {
            w->setProperty("importErrorShown", true);
            box->accept();
          }
          else if (auto *button = box->button(QMessageBox::Discard))
            button->click();
        }
    });
    discard.start(30);
    ping_clock.start();
    connect(&ping, &QTimer::timeout, this, [this] {
      const double elapsed = ping_clock.nsecsElapsed() / 1e6;
      ping_clock.restart();
      if (stage > 1 && w->property("importedMaterialLoading").toBool())
        max_ping = std::max(max_ping, elapsed);
      if (stage == 2)
        max_import_ping = std::max(max_import_ping, elapsed);
    });
    ping.start(10);
  }
};
}  // namespace bts
