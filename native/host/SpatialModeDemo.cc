#include "SpatialModeDemo.hh"
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
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class SpatialDemo final : public QObject {
  QWidget *w;
  QString dir, mesh, image;
  int resolution, stage = 0, target = 3, cylinder_stage = 0;
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  void action(const char *name)
  {
    auto *a = w->findChild<QAction *>(name);
    if (!a)
      qFatal("Spatial action missing: %s", name);
    a->trigger();
  }
  void number(const QString &name, double value)
  {
    auto *c = w->findChild<QDoubleSpinBox *>(name);
    if (!c)
      qFatal("Spatial number missing: %s", qPrintable(name));
    c->setValue(value);
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    data["depth_visible"] = w->findChild<QWidget *>("fillDepthCulling")->isVisible();
    data["backface_visible"] = w->findChild<QWidget *>("fillBackfaceCulling")->isVisible();
    data["offset_visible"] = w->findChild<QDoubleSpinBox *>("fillOffset0")->isVisible();
    data["cylinder_angle_visible"] = w->findChild<QWidget *>("fillCylinderAngle")->isVisible();
    data["scale_v_enabled"] = w->findChild<QDoubleSpinBox *>("fillScaleV")->isEnabled();
    captures.append(data);
    w->grab().save(dir + "/" + name + ".png");
  }
  void advance()
  {
    if (stage > 2 && state()["composite_pending"].toBool())
      return;
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
        action("guiPlanarPrepare");
        w->findChild<QToolButton *>("addFillLayer")->click();
        break;
      case 3: {
        timer.stop();
        QTimer::singleShot(100, this, [this] {
          auto *picker = w->findChild<QFileDialog *>("fillTextureFileDialog");
          if (!picker)
            qFatal("Spatial texture picker missing");
          picker->selectFile(image);
          QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
        });
        w->findChild<QComboBox *>("fillBaseColorResource")->setCurrentIndex(2);
        timer.start();
        break;
      }
      case 4:
        number("fillScaleU", 4);
        number("fillUvRotation", 30);
        number("fillOffset0", .1);
        number("fill3d0_0", .2);
        w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(1);
        break;
      case 5:
        capture("triplanar");
        w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(target);
        break;
      case 6:
        capture("spatial");
        number("fillScaleU", 1);
        break;
      case 7:
        capture("scale-one");
        action("undoDocument");
        break;
      case 8:
        capture("undo-scale");
        number("fill3d0_0", .4);
        break;
      case 9:
        capture("translated");
        number("fill3d1_2", 45);
        break;
      case 10:
        capture("rotated");
        number("fill3d2_0", .7);
        break;
      case 11:
        capture("scaled");
        for (int axis = 0; axis < 3; ++axis)
          number(QString("fill3d0_%1").arg(axis),
                 qEnvironmentVariable(qPrintable(QString("BTS_PLANAR_CENTER_%1").arg(axis)))
                     .toDouble());
        number("fill3d2_0", .3);
        w->findChild<QComboBox *>("fillShapeCrop")->setCurrentIndex(1);
        break;
      case 12:
        capture("cropped");
        w->findChild<QComboBox *>("fillWrap")->setCurrentIndex(1);
        break;
      case 13:
        if (target == 4 && cylinder_stage < 4) {
          --stage;
          switch (cylinder_stage++) {
            case 0:
              capture("no-wrap");
              number("fillCylinderAngle", 180);
              break;
            case 1:
              capture("half-cylinder");
              number("fillBackfaceAngle", 20);
              w->findChild<QCheckBox *>("fillBackfaceCulling")->setChecked(true);
              break;
            case 2:
              capture("culled-cylinder");
              action("undoDocument");
              break;
            case 3:
              capture("undo-cull");
              action("guiPlanarSave");
              ++stage;
              break;
          }
          break;
        }
        capture("no-wrap");
        action("guiPlanarSave");
        break;
      case 14:
        capture("saved");
        if (!QFile::rename(image, image + ".removed"))
          qFatal("Cannot move spatial source texture");
        action("guiPlanarOpen");
        break;
      case 15:
        capture("opened");
        number("fillScaleU", 1);
        break;
      case 16:
        capture("edited-after-open");
        action("undoDocument");
        break;
      case 17:
        capture("undo-open-edit");
        action("checkGuiComposite");
        break;
      case 18: {
        capture("reference");
        timer.stop();
        QFile f(dir + "/spatial.json");
        if (!f.open(QIODevice::WriteOnly))
          qFatal("Spatial proof write failed");
        f.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                          {"mode", target},
                                          {"captures", captures}})
                    .toJson());
        f.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  SpatialDemo(QWidget *workspace, QString d, QString m, int r, std::function<QJsonObject()> s)
      : QObject(workspace), w(workspace), dir(d), mesh(m), resolution(r), state(std::move(s))
  {
    QDir().mkpath(dir);
    bool ok = false;
    const int requested = qEnvironmentVariable("BTS_SPATIAL_MODE_DEMO").toInt(&ok);
    if (ok)
      target = requested;
    image = dir + "/spatial-source.png";
    QImage source(64, 32, QImage::Format_RGBA8888);
    const QColor colors[]{Qt::red, Qt::green, Qt::blue, Qt::yellow};
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 64; ++x)
        source.setPixelColor(x, y, colors[(x / 8 + y / 8) % 4]);
    if (!source.save(image))
      qFatal("Spatial source write failed");
    timer.setInterval(750);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_spatial_mode_demo(QWidget *w,
                             QWidget *,
                             const QString &d,
                             const QString &m,
                             int r,
                             std::function<QJsonObject()> state)
{
  new SpatialDemo(w, d, m, r, std::move(state));
}
}  // namespace bts
