#include "PropertyPerformanceDemo.hh"
#include "GuiFileSelection.hh"
#include "PropertyLatency.hh"
#include "ColorPicker.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v, *wheel = nullptr;
  QString dir, mesh, image;
  int resolution, stage = 0, kind = 0, move = 0;
  QTimer timer, pulse, ping;
  QElapsedTimer ping_clock, release_clock;
  double max_ping = 0;
  bool released = false;
  std::uint64_t first_event = 0;
  QSlider *slider = nullptr;
  QJsonArray cases, atomic_cases;
  QJsonObject before, reference, restored;
  int atomic_index = 0;
  QElapsedTimer atomic_clock;
  std::function<QJsonObject()> state;
  void action(const char *name)
  {
    auto *a = w->findChild<QAction *>(name);
    if (!a)
      qFatal("Property action missing: %s", name);
    a->trigger();
  }
  void choose_image()
  {
    select_gui_test_file(w, "fillTextureFileDialog", image);
  }
  void mouse(QEvent::Type type, QPointF point)
  {
    QMouseEvent event(type,
                      point,
                      wheel->mapToGlobal(point.toPoint()),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(wheel, &event);
  }
  const char *label() const
  {
    return kind == 0 ? "color" : kind == 1 ? "opacity" : "tiling";
  }
  void start_drag()
  {
    before = state();
    property_reset_metrics();
    first_event = property_next_event;
    move = 0;
    released = false;
    max_ping = 0;
    ping_clock.start();
    ping.start();
    if (kind == 0) {
      auto *picker = static_cast<ColorPicker *>(w->findChild<QWidget *>("materialColorPicker"));
      picker->findChild<QPushButton *>("colorSwatch")->click();
      wheel = w->findChild<QDialog *>("colorPickerDialog")->findChild<QWidget *>("colorWheel");
      property_event(label());
      mouse(QEvent::MouseButtonPress, {65, 100});
    }
    else {
      if (kind == 1) {
        action("editLayerOpacity");
        slider = w->findChild<QSlider *>("layerChannelOpacitySlider");
      }
      else
        slider = w->findChild<QSlider *>("fillScaleSlider0");
      if (!slider)
        qFatal("Property slider missing");
      slider->setSliderDown(true);
    }
    pulse.start();
  }
  void finish_drag()
  {
    pulse.stop();
    property_event(label());
    if (kind == 0) {
      mouse(QEvent::MouseButtonRelease, {155, 65});
      w->findChild<QDialog *>("colorPickerDialog")->hide();
    }
    else {
      slider->setSliderDown(false);
      if (kind == 1)
        slider->window()->hide();
    }
    released = true;
    release_clock.start();
    auto data = property_latency_state();
    w->setProperty("propertyDuringDrag", data);
  }
  void tick_drag()
  {
    if (move++ >= 100) {
      finish_drag();
      return;
    }
    property_event(label());
    if (kind == 0)
      mouse(QEvent::MouseMove, {65 + move * .9, 100 - move * .35});
    else
      slider->setValue(kind == 1 ? 1000 - move * 7 : 100 + move * 3);
  }
  void record_case()
  {
    ping.stop();
    auto after = state();
    auto data = property_latency_state();
    data["property"] = label();
    data["max_ping_ms"] = max_ping;
    data["events"] = qint64(property_next_event - first_event);
    data["during_drag"] = w->property("propertyDuringDrag").value<QJsonObject>();
    data["undo_before"] = before["undo_count"];
    data["undo_after"] = after["undo_count"];
    data["after_release_ms"] = release_clock.elapsed();
    data["before_hash"] = before["composite_hash"];
    data["after_hash"] = after["composite_hash"];
    cases.append(data);
    w->setProperty("guiCapturePath", dir + "/" + label() + "-viewport.png");
    action("guiCaptureViewport");
    w->grab().save(dir + "/" + label() + ".png");
  }
  void advance()
  {
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = w->findChild<QDialog *>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        action("guiFillTexturePrepare");
        w->findChild<QToolButton *>("addFillLayer")->click();
        break;
      case 3: {
        timer.stop();
        choose_image();
        w->findChild<QComboBox *>("fillBaseColorResource")->setCurrentIndex(2);
        timer.start();
        break;
      }
      case 4:
        if (property_pipeline_pending) {
          --stage;
          return;
        }
        kind = 0;
        start_drag();
        break;
      case 5:
        if (!released || property_pipeline_pending) {
          --stage;
          return;
        }
        record_case();
        if (++kind < 3) {
          start_drag();
          --stage;
          return;
        }
        action("checkGuiComposite");
        v->repaint();
        break;
      case 6:
        if (property_pipeline_pending) {
          --stage;
          return;
        }
        before = state();
        property_reset_metrics();
        max_ping = 0;
        ping_clock.start();
        ping.start();
        atomic_clock.start();
        property_event("atomic");
        switch (atomic_index) {
          case 0:
            w->findChild<QDoubleSpinBox *>("fillOffset0")->setValue(.137);
            break;
          case 1:
            w->findChild<QDoubleSpinBox *>("fillUvRotation")->setValue(33);
            break;
          case 2:
            w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(2);
            break;
          case 3:
            w->findChild<QComboBox *>("fillFiltering")->setCurrentIndex(2);
            break;
          case 4:
            w->findChild<QComboBox *>("fillWrap")->setCurrentIndex(1);
            break;
          case 5:
            w->findChild<QToolButton *>("fill3dDisclosure")->setChecked(true);
            w->findChild<QDoubleSpinBox *>("fill3d0_0")->setValue(.1);
            break;
          case 6:
            timer.stop();
            choose_image();
            w->findChild<QComboBox *>("fillChannelResource2")->setCurrentIndex(1);
            timer.start();
            break;
        }
        break;
      case 7:
        if (atomic_clock.elapsed() < 250 || property_pipeline_pending) {
          --stage;
          return;
        }
        {
          ping.stop();
          auto after = state();
          auto data = property_latency_state();
          data["property"] = QStringList{"offset",
                                         "rotation",
                                         "projection",
                                         "filter",
                                         "wrap",
                                         "projection_transform",
                                         "channel_texture"}[atomic_index];
          data["max_ping_ms"] = max_ping;
          data["undo_steps"] = after["undo_count"].toInt() - before["undo_count"].toInt();
          data["before"] = before;
          data["after"] = after;
          action("checkGuiComposite");
          v->repaint();
          data["reference"] = state();
          atomic_cases.append(data);
          if (++atomic_index < 7) {
            stage = 6;
            return;
          }
          reference = state();
          action("undoDocument");
          break;
        }
      case 8:
        if (property_pipeline_pending) {
          --stage;
          return;
        }
        restored = state();
        action("redoDocument");
        break;
      case 9:
        if (property_pipeline_pending) {
          --stage;
          return;
        }
        restored["redo"] = state();
        action("guiFillTextureSave");
        break;
      case 10:
        if (property_pipeline_pending) {
          --stage;
          return;
        }
        if (!QFile::rename(image, image + ".removed"))
          qFatal("Property packed test source removal failed");
        action("guiFillTextureOpen");
        break;
      case 11: {
        if (property_pipeline_pending) {
          --stage;
          return;
        }
        auto final = state();
        timer.stop();
        QFile file(dir + "/property-performance.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Property metrics write failed");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"cases", cases},
                                             {"atomic", atomic_cases},
                                             {"reference", reference},
                                             {"undo", restored},
                                             {"final", final}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  Demo(QWidget *workspace,
       QWidget *viewport,
       QString d,
       QString m,
       int r,
       std::function<QJsonObject()> s)
      : QObject(workspace),
        w(workspace),
        v(viewport),
        dir(d),
        mesh(m),
        resolution(r),
        state(std::move(s))
  {
    QDir().mkpath(dir);
    image = dir + "/performance-checker.png";
    QImage source(64, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x)
        source.setPixelColor(
            x, y, (x / 8 + y / 8) % 2 ? QColor(220, 140, 60) : QColor(30, 80, 190));
    if (!source.save(image))
      qFatal("Property texture fixture failed");
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    pulse.setInterval(20);
    pulse.setTimerType(Qt::PreciseTimer);
    connect(&pulse, &QTimer::timeout, this, [this] { tick_drag(); });
    ping.setInterval(10);
    ping.setTimerType(Qt::PreciseTimer);
    connect(&ping, &QTimer::timeout, this, [this] {
      max_ping = std::max(max_ping, double(ping_clock.restart()));
    });
    timer.start();
  }
};
}  // namespace
void start_property_performance_demo(QWidget *w,
                                     QWidget *v,
                                     const QString &d,
                                     const QString &m,
                                     int r,
                                     std::function<QJsonObject()> state)
{
  new Demo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
