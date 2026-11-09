#include "LayerEditDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class LayerDemo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int resolution, stage = 0, paint = 0, operation = 0;
  std::function<QJsonObject()> state;
  QTimer timer, ping;
  QElapsedTimer heartbeat, clock;
  double max_ping = 0, return_ms = 0, visible_ms = -1;
  bool measuring = false, expect_change = false;
  QJsonArray captures, operations;
  QJsonObject before;
  QString middle, fill;
  QVariantList points;
  int frames = 0;
  QAction *action(const char *name)
  {
    auto *a = w->findChild<QAction *>(name);
    if (!a)
      qFatal("Layer demo action missing: %s", name);
    return a;
  }
  void capture(const char *name)
  {
    auto s = state();
    s["stage"] = name;
    captures.append(s);
  }
  QListWidget *stack()
  {
    return w->findChild<QListWidget *>("layerStack");
  }
  QString selected()
  {
    return stack()->currentItem()->data(Qt::UserRole).toUuid().toString();
  }
  void visibility(const QString &id, bool shown)
  {
    for (int i = 0; i < stack()->count(); ++i) {
      auto *item = stack()->item(i);
      if (item->data(Qt::UserRole).toUuid().toString() == id) {
        item->setCheckState(shown ? Qt::Checked : Qt::Unchecked);
        return;
      }
    }
    qFatal("Layer control not found");
  }
  void mouse(QEvent::Type type, QPointF p)
  {
    QMouseEvent e(type,
                  p,
                  v->mapToGlobal(p.toPoint()),
                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                  Qt::NoModifier);
    QCoreApplication::sendEvent(v, &e);
  }
  void color(const char *hex)
  {
    w->findChild<QWidget *>("materialColorPicker")
        ->findChild<QPushButton *>("colorSwatch")
        ->click();
    auto *dialog = w->findChild<QDialog *>("colorPickerDialog");
    auto *field = dialog->findChild<QLineEdit *>("colorHex");
    field->setText(hex);
    QMetaObject::invokeMethod(field, "editingFinished", Qt::DirectConnection);
    dialog->hide();
  }
  void begin()
  {
    before = state();
    max_ping = 0;
    visible_ms = -1;
    frames = before["render_frames"].toInt();
    expect_change = operation != 0;
    heartbeat.start();
    clock.start();
    measuring = true;
    ping.start();
    if (operation == 0)
      w->findChild<QToolButton *>("addPaintLayer")->click();
    else if (operation == 1) {
      w->findChild<QToolButton *>("addFillLayer")->click();
      fill = selected();
    }
    else if (operation == 2)
      visibility(fill, false);
    else
      visibility(middle, (operation - 3) % 2 != 0);
    return_ms = clock.nsecsElapsed() / 1e6;
    timer.setInterval(10);
  }
  void measure()
  {
    auto current = state();
    if (visible_ms < 0 && current["render_frames"].toInt() > frames &&
        (!expect_change || current["probe"] != before["probe"]))
      visible_ms = clock.nsecsElapsed() / 1e6;
    if (clock.elapsed() < 250)
      return;
    measuring = false;
    ping.stop();
    operations.append(QJsonObject{{"operation", operation},
                                  {"return_ms", return_ms},
                                  {"max_ping_ms", max_ping},
                                  {"visible_ms", visible_ms},
                                  {"before", before},
                                  {"after", current}});
    ++operation;
    stage = 7;
    timer.setInterval(20);
  }
  void advance()
  {
    if (measuring) {
      measure();
      return;
    }
    switch (stage++) {
      case 0: {
        for (auto *a : w->findChildren<QAction *>())
          if (a->text() == "&New…") {
            a->trigger();
            return;
          }
        qFatal("Layer New action missing");
      }
      case 1: {
        QDialog *d = nullptr;
        for (auto *candidate : w->findChildren<QDialog *>())
          if (candidate->findChild<QLineEdit *>("meshPath"))
            d = candidate;
        if (!d)
          qFatal("Layer New dialog missing");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2: {
        action("guiLayerEditPrepare")->trigger();
        timer.setInterval(500);
        break;
      }
      case 3: {
        points = w->property("layerEditPoints").toList();
        w->findChild<QToolButton *>("addPaintLayer")->click();
        color(paint == 0 ? "#FF0000" : paint == 1 ? "#00FF00" : "#0000FF");
        w->findChild<QDoubleSpinBox *>("brushSize")->setValue(24);
        if (auto *b = w->findChild<QDoubleSpinBox *>("materialBump"))
          b->setValue(.08);
        if (paint == 1)
          middle = selected();
        break;
      }
      case 4: {
        auto p = points.at(paint).toPointF();
        mouse(QEvent::MouseButtonPress, p);
        mouse(QEvent::MouseMove, p + QPointF(3, 0));
        mouse(QEvent::MouseButtonRelease, p + QPointF(3, 0));
        break;
      }
      case 5: {
        if (state()["composite_pending"].toBool()) {
          --stage;
          return;
        }
        if (++paint < 3) {
          stage = 3;
          return;
        }
        capture("painted-three");
        timer.setInterval(100);
        break;
      }
      case 6:
      case 7: {
        if (operation < 23) {
          begin();
          return;
        }
        if (state()["composite_pending"].toBool()) {
          stage = 7;
          return;
        }
        capture("final");
        action("checkGuiComposite")->trigger();
        capture("reference");
        action("undoDocument")->trigger();
        break;
      }
      case 8: {
        if (state()["composite_pending"].toBool()) {
          --stage;
          return;
        }
        capture("undo");
        action("redoDocument")->trigger();
        break;
      }
      case 9: {
        if (state()["composite_pending"].toBool()) {
          --stage;
          return;
        }
        capture("redo");
        action("guiLayerEditSave")->trigger();
        capture("saved");
        action("guiLayerEditOpen")->trigger();
        break;
      }
      case 10: {
        if (state()["composite_pending"].toBool()) {
          --stage;
          return;
        }
        capture("opened");
        timer.stop();
        w->grab().save(dir + "/workspace.png");
        QFile file(dir + "/layer-edit.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Layer proof unavailable");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"resolution", resolution},
                                             {"operations", operations},
                                             {"captures", captures}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  LayerDemo(QWidget *workspace,
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
    timer.setInterval(1000);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    ping.setTimerType(Qt::PreciseTimer);
    ping.setInterval(10);
    connect(&ping, &QTimer::timeout, this, [this] {
      max_ping = std::max(max_ping, heartbeat.nsecsElapsed() / 1e6);
      heartbeat.restart();
    });
    timer.start();
  }
};
}  // namespace
void start_layer_edit_demo(QWidget *w,
                           QWidget *v,
                           const QString &d,
                           const QString &m,
                           int r,
                           std::function<QJsonObject()> state)
{
  new LayerDemo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
