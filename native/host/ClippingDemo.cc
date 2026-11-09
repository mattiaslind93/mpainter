#include "ViewportDemoCapture.hh"
#include "ClippingDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QTabWidget>
#include <QKeyEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>
namespace bts {
namespace {
class ClipDemo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int resolution, stage = 0;
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  void action(const char *name)
  {
    auto *a = w->findChild<QAction *>(name);
    if (!a)
      qFatal("Clipping action missing: %s", name);
    a->trigger();
  }
  void number(const char *name, double value)
  {
    auto *c = w->findChild<QDoubleSpinBox *>(name);
    if (!c)
      qFatal("Clipping number missing: %s", name);
    c->setValue(value);
    QMetaObject::invokeMethod(c, "editingFinished", Qt::DirectConnection);
  }
  void new_project(const QString &path)
  {
    timer.stop();
    QTimer::singleShot(50, this, [] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
        if (box->windowTitle() == "Unsaved project")
          box->button(QMessageBox::Discard)->click();
    });
    action("newProject");
    auto *d = w->findChild<QDialog *>("newProjectDialog");
    if (!d)
      qFatal("Clipping New missing");
    d->findChild<QLineEdit *>("meshPath")->setText(path);
    auto *r = d->findChild<QComboBox *>("documentResolution");
    r->setCurrentIndex(r->findData(resolution));
    d->findChild<QPushButton *>("createProject")->click();
    timer.start();
  }
  void zoom(double target)
  {
    const double distance = state()["view"].toObject()["scene_distance"].toDouble();
    const QPointF p(v->width() / 2., v->height() / 2.);
    QWheelEvent event(p,
                      v->mapToGlobal(p.toPoint()),
                      {},
                      QPoint(0, qRound(-std::log(target / distance) / .0012)),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);
    QCoreApplication::sendEvent(v, &event);
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    auto *gl = qobject_cast<QOpenGLWidget *>(v);
    const auto image = capture_viewport_framebuffer(gl);
    data["center_rgb"] = image.pixelColor(image.width() / 2, image.height() / 2).name();
    captures.append(data);
    image.save(dir + "/" + name + ".png");
    std::printf("BTS_CLIP %s %s\n",
                name,
                QJsonDocument(data["view"].toObject()).toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
  }
  void finish(const QString &file = "clipping.json")
  {
    timer.stop();
    QFile out(dir + "/" + file);
    if (!out.open(QIODevice::WriteOnly))
      qFatal("Clipping proof unavailable");
    out.write(QJsonDocument(
                  QJsonObject{{"pid", QCoreApplication::applicationPid()}, {"captures", captures}})
                  .toJson());
    out.close();
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
  }
  void preferences(int kind)
  {
    timer.stop();
    auto *poll = new QTimer(this);
    int phase = 0;
    connect(poll, &QTimer::timeout, this, [this, poll, phase, kind]() mutable {
      auto *dialog = w->findChild<QDialog *>("shelfPreferencesDialog");
      if (!dialog || !dialog->isVisible())
        return;
      auto *near = dialog->findChild<QDoubleSpinBox *>("viewportClipStart"),
           *far = dialog->findChild<QDoubleSpinBox *>("viewportClipEnd");
      if (!near || !far)
        qFatal("Viewport preferences missing");
      dialog->findChild<QTabWidget *>("preferencesSections")->setCurrentIndex(1);
      auto *buttons = dialog->findChild<QDialogButtonBox *>();
      if (kind == 1) {
        if (phase++ == 0) {
          near->setValue(.1);
          return;
        }
        capture("cancel-preview");
        dialog->reject();
        poll->stop();
        poll->deleteLater();
        return;
      }
      if (kind == 2) {
        if (phase++ == 0) {
          dialog->findChild<QPushButton *>("restoreViewportClipping")->click();
          return;
        }
        if (near->value() != .01 || far->value() != 1000)
          qFatal("Clip defaults incorrect");
        capture("defaults-preview");
        dialog->reject();
        poll->stop();
        poll->deleteLater();
        return;
      }
      switch (phase++) {
        case 0:
          near->setValue(.1);
          break;
        case 1:
          capture("preferences-near-point1");
          near->setValue(.001);
          break;
        case 2:
          capture("preferences-near-point001");
          near->setValue(2000);
          break;
        case 3: {
          if (buttons->button(QDialogButtonBox::Ok)->isEnabled() ||
              dialog->findChild<QLabel *>("viewportClipError")->text().isEmpty())
            qFatal("Invalid clipping accepted");
          capture("invalid-rejected");
          near->setValue(.005);
          far->setValue(500);
          break;
        }
        case 4:
          capture("preferences-custom");
          dialog->grab().save(dir + "/preferences.png");
          buttons->button(QDialogButtonBox::Ok)->click();
          poll->stop();
          poll->deleteLater();
          break;
      }
    });
    poll->start(500);
    action("shelfPreferencesAction");
    timer.start();
  }
  void setup_displacement()
  {
    w->findChild<QToolButton *>("addFillLayer")->click();
    w->findChild<QToolButton *>("shaderSettingsButton")->click();
    w->findChild<QCheckBox *>("displacementChannel")->setChecked(true);
    number("displacementStrength", 1);
    number("displacementMidlevel", 0);
    w->findChild<QToolButton *>("brushDisplacement")->setChecked(true);
    number("materialDisplacement", .25);
    w->findChild<QDialog *>("shaderSettings")->hide();
  }
  void advance()
  {
    if (qEnvironmentVariableIsSet("BTS_CLIPPING_RESTART")) {
      capture("restart");
      finish("clipping-restart.json");
      return;
    }
    const auto s = state();
    if (s["composite_pending"].toBool() || s["property_pending"].toBool())
      return;
    switch (stage++) {
      case 0:
        new_project(mesh);
        break;
      case 1:
        capture("demo-framed");
        new_project(dir + "/clip-plane.obj");
        break;
      case 2:
        action("guiClipFront");
        break;
      case 3:
        capture("plane-framed");
        setup_displacement();
        break;
      case 4:
        capture("displaced-framed");
        zoom(.27);
        break;
      case 5:
        capture("displaced-close");
        number("materialDisplacement", -.25);
        break;
      case 6:
        capture("negative-close");
        new_project(dir + "/camera-card.dae");
        break;
      case 7: {
        auto *c = w->findChild<QComboBox *>("sceneCameraSelector");
        if (c->count() < 2)
          qFatal("Imported camera missing");
        c->setCurrentIndex(1);
        break;
      }
      case 8:
        capture("imported-camera");
        zoom(.02);
        break;
      case 9:
        capture("detached-camera");
        if (qEnvironmentVariableIsSet("BTS_CLIPPING_BASELINE")) {
          finish();
          break;
        }
        new_project(dir + "/clip-plane.obj");
        break;
      case 10:
        action("guiClipFront");
        setup_displacement();
        break;
      case 11:
        zoom(.27);
        break;
      case 12:
        capture("detail-point02");
        preferences(0);
        break;
      case 13:
        capture("preferences-accepted");
        preferences(1);
        break;
      case 14:
        capture("cancel-restored");
        preferences(2);
        break;
      case 15: {
        capture("defaults-cancel-restored");
        QKeyEvent event(QEvent::KeyPress, Qt::Key_F6, Qt::NoModifier);
        QCoreApplication::sendEvent(v, &event);
        break;
      }
      case 16:
        capture("orthographic");
        action("guiClipFront");
        break;
      case 17:
        capture("plane-refocused");
        capture_demo_window(w, v).save(dir + "/workspace.png");
        finish();
        break;
    }
  }

 public:
  ClipDemo(QWidget *workspace,
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
    timer.setInterval(700);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_clipping_demo(QWidget *w,
                         QWidget *v,
                         const QString &d,
                         const QString &m,
                         int r,
                         std::function<QJsonObject()> s)
{
  new ClipDemo(w, v, d, m, r, std::move(s));
}
}  // namespace bts
