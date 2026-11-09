#pragma once
#include "LevelsRampEditor.hh"
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
namespace bts {
class LevelsEditorDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh;
  QTimer timer, dialogs;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Missing levels control: %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    w->setProperty("levelsSample", true);
    auto data = state();
    w->setProperty("levelsSample", false);
    data["stage"] = name;
    captures.append(data);
    auto *control = get<QWidget>("filterLevelsRamp")->isVisible() ?
                        get<QWidget>("filterLevelsRamp") :
                        get<QWidget>("levelsHistogram");
    if (auto *scroll = w->findChild<QScrollArea *>("propertiesScroll"))
      scroll->ensureWidgetVisible(control);
    capture_demo_window(w, v).save(output + "/" + name + ".png");
  }
  void drag(const char *ramp_name, int handle, float value)
  {
    auto *ramp = dynamic_cast<HandleRamp *>(get<QWidget>(ramp_name));
    if (!ramp)
      qFatal("Wrong levels ramp type");
    const auto start = ramp->handle_position(handle),
               end = QPointF(9 + value * (ramp->width() - 18), 32);
    auto send = [ramp](QEvent::Type type, QPointF point) {
      QMouseEvent event(type,
                        point,
                        ramp->mapToGlobal(point.toPoint()),
                        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(ramp, &event);
    };
    send(QEvent::MouseButtonPress, start);
    busy = true;
    auto *moves = new QTimer(this);
    auto *step = new int(0);
    connect(moves, &QTimer::timeout, this, [=, this] {
      const auto p = start + (end - start) * (++*step / 30.);
      send(QEvent::MouseMove, p);
      if (*step == 30) {
        send(QEvent::MouseButtonRelease, end);
        moves->stop();
        moves->deleteLater();
        delete step;
        busy = false;
      }
    });
    moves->start(16);
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 1600)
      qFatal("Levels demo timeout stage=%d", stage);
    const auto data = state();
    if (stage > 1 && (data["composite_pending"].toBool() || data["property_pending"].toBool() ||
                      data["render_frames"].toInt() < frame))
      return;
    if (stage >= 4 && stage <= 9 &&
        !get<QWidget>("levelsHistogram")->property("gpuHistogramReady").toBool())
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        auto *res = get<QComboBox>("documentResolution");
        res->setCurrentIndex(res->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      }
      case 2:
        get<QToolButton>("addFillLayer")->click();
        action("addLevelsEffect");
        break;
      case 3: {
        auto *channel = get<QComboBox>("levelsChannel");
        channel->setCurrentIndex(channel->findData(2));
        break;
      }
      case 4:
        capture("levels-default");
        drag("levelsOutputRamp", 0, .2);
        break;
      case 5:
        capture("levels-output-drag");
        action("undoDocument");
        break;
      case 6:
        capture("levels-undo");
        action("redoDocument");
        break;
      case 7:
        capture("levels-redo");
        get<QComboBox>("levelsChannel")
            ->setCurrentIndex(get<QComboBox>("levelsChannel")->findData(0));
        break;
      case 8:
        capture("histogram-color");
        get<QComboBox>("levelsChannel")
            ->setCurrentIndex(get<QComboBox>("levelsChannel")->findData(2));
        break;
      case 9:
        capture("histogram-roughness");
        action("addFilterEffect");
        action("filterLevels");
        get<QComboBox>("filterChannel")
            ->setCurrentIndex(get<QComboBox>("filterChannel")->findData(2));
        break;
      case 10:
        capture("filter-default");
        drag("filterOutputRamp", 1, .7);
        break;
      case 11:
        capture("filter-output-drag");
        action("undoDocument");
        break;
      case 12:
        capture("filter-undo");
        action("redoDocument");
        break;
      case 13:
        capture("filter-redo");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 14:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 15: {
        capture("reopened");
        timer.stop();
        QFile file(output + "/levels-native.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Cannot write Levels proof");
        file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                             {"pid", QCoreApplication::applicationPid()}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 5;
  }

 public:
  LevelsEditorDemo(QWidget *window,
                   QWidget *viewport,
                   QString directory,
                   QString source,
                   std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        output(directory),
        mesh(source),
        state(std::move(snapshot))
  {
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(100);
    connect(&dialogs, &QTimer::timeout, this, [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical)
            qFatal("Levels GUI error: %s", qPrintable(box->text()));
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
    });
    dialogs.start(40);
  }
};
}  // namespace bts
