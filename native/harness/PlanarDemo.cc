#include "PlanarDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
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
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class PlanarDemo final : public QObject {
  QWidget *w;
  QString dir, mesh, image;
  int resolution, stage = 0;
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  void action(const char *name)
  {
    auto *a = w->findChild<QAction *>(name);
    if (!a)
      qFatal("Planar action missing: %s", name);
    a->trigger();
  }
  void number(const QString &name, double value)
  {
    auto *c = w->findChild<QDoubleSpinBox *>(name);
    if (!c)
      qFatal("Planar number missing: %s", qPrintable(name));
    c->setValue(value);
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    auto *back = w->findChild<QCheckBox *>("fillBackfaceCulling");
    data["backface_enabled"] = back->isChecked();
    data["backface_fields_enabled"] =
        w->findChild<QDoubleSpinBox *>("fillBackfaceAngle")->isEnabled();
    data["backface_visible"] = back->isVisible();
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
            qFatal("Planar texture picker missing");
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
        break;
      case 5:
        capture("uv-transformed");
        w->findChild<QComboBox *>("fillProjectionMode")->setCurrentIndex(2);
        break;
      case 6:
        capture("planar");
        w->findChild<QCheckBox *>("fillBackfaceCulling")->setChecked(true);
        break;
      case 7:
        capture("culled");
        action("undoDocument");
        break;
      case 8:
        capture("undo-cull");
        number("fill3d2_1", .1);
        break;
      case 9:
        capture("depth");
        w->findChild<QCheckBox *>("fillDepthCulling")->setChecked(false);
        break;
      case 10:
        capture("unlimited-depth");
        number("fill3d2_1", 1);
        w->findChild<QCheckBox *>("fillDepthCulling")->setChecked(true);
        number("fill3d0_0", .4);
        break;
      case 11:
        capture("translated");
        number("fill3d1_1", 45);
        break;
      case 12:
        capture("rotated");
        w->findChild<QComboBox *>("fillFiltering")->setCurrentIndex(2);
        break;
      case 13:
        capture("nearest");
        for (int axis = 0; axis < 3; ++axis)
          number(QString("fill3d0_%1").arg(axis),
                 qEnvironmentVariable(qPrintable(QString("BTS_PLANAR_CENTER_%1").arg(axis)))
                     .toDouble());
        w->findChild<QComboBox *>("fillWrap")->setCurrentIndex(1);
        w->findChild<QComboBox *>("fillShapeCrop")->setCurrentIndex(1);
        number("fill3d2_0", .5);
        number("fill3d2_2", .5);
        break;
      case 14:
        capture("decal");
        action("guiPlanarSave");
        break;
      case 15:
        capture("saved");
        if (!QFile::rename(image, image + ".removed"))
          qFatal("Cannot move planar source texture");
        action("guiPlanarOpen");
        break;
      case 16:
        capture("opened");
        number("fillScaleU", 1);
        break;
      case 17:
        capture("scale-one");
        action("undoDocument");
        break;
      case 18:
        capture("undo-scale");
        action("checkGuiComposite");
        break;
      case 19: {
        capture("reference");
        timer.stop();
        QFile f(dir + "/planar.json");
        if (!f.open(QIODevice::WriteOnly))
          qFatal("Planar proof write failed");
        f.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                          {"captures", captures}})
                    .toJson());
        f.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  PlanarDemo(QWidget *workspace, QString d, QString m, int r, std::function<QJsonObject()> s)
      : QObject(workspace), w(workspace), dir(d), mesh(m), resolution(r), state(std::move(s))
  {
    QDir().mkpath(dir);
    image = dir + "/planar-source.png";
    QImage source(64, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 64; ++x)
        source.setPixelColor(x, y, ((x / 8 + y / 8) % 2) ? QColor(Qt::red) : QColor(Qt::blue));
    if (!source.save(image))
      qFatal("Planar source write failed");
    timer.setInterval(750);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_planar_demo(QWidget *w,
                       QWidget *,
                       const QString &d,
                       const QString &m,
                       int r,
                       std::function<QJsonObject()> state)
{
  new PlanarDemo(w, d, m, r, std::move(state));
}
}  // namespace bts
