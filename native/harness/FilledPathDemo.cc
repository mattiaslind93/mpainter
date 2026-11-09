// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilledPathDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
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
class FilledDemo final : public QObject {
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
      qFatal("Filled action missing: %s", name);
    a->trigger();
  }
  void number(const char *name, double value)
  {
    auto *c = w->findChild<QDoubleSpinBox *>(name);
    if (!c)
      qFatal("Filled number missing: %s", name);
    c->setValue(value);
  }
  void click(int index)
  {
    const auto p = w->property("ribbonPoints").toList().at(index).toPointF();
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
      QMouseEvent e(type,
                    p,
                    v->mapToGlobal(p),
                    Qt::LeftButton,
                    type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                    Qt::NoModifier);
      QCoreApplication::sendEvent(v, &e);
    }
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
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
        action("guiRibbonPrepare");
        w->findChild<QToolButton *>("addPaintLayer")->click();
        break;
      case 3:
        action("selectSurfacePath");
        w->findChild<QComboBox *>("surfacePathMode")->setCurrentIndex(2);
        number("roughnessControl", .8);
        number("metallicControl", .4);
        break;
      case 4:
        click(0);
        break;
      case 5:
        click(1);
        break;
      case 6:
        capture("two-points");
        click(2);
        break;
      case 7:
        click(3);
        break;
      case 8:
        capture("filled");
        number("brushStrokeOpacity", 50);
        break;
      case 9:
        capture("half-opacity");
        action("undoDocument");
        break;
      case 10:
        capture("undo-opacity");
        number("roughnessControl", .2);
        break;
      case 11:
        capture("roughness");
        w->findChild<QToolButton *>("brushChannel2")->click();
        break;
      case 12:
        capture("roughness-disabled");
        action("undoDocument");
        break;
      case 13:
        capture("undo-channel");
        click(1);
        w->findChild<QToolButton *>("surfaceVertexSmooth")->click();
        break;
      case 14: {
        capture("curved");
        auto *picker = w->findChild<QWidget *>("materialColorPicker");
        picker->findChild<QPushButton *>("colorSwatch")->click();
        auto *hex = picker->findChild<QLineEdit *>("colorHex");
        hex->setText("#FF0000");
        QMetaObject::invokeMethod(hex, "editingFinished", Qt::DirectConnection);
        picker->findChild<QDialog *>("colorPickerDialog")->hide();
        break;
      }
      case 15:
        capture("colored");
        action("undoDocument");
        break;
      case 16:
        capture("undo-color");
        action("redoDocument");
        break;
      case 17:
        capture("redo-color");
        action("guiRibbonSave");
        break;
      case 18:
        capture("saved");
        action("guiRibbonOpen");
        break;
      case 19:
        capture("opened");
        action("selectSurfacePath");
        w->findChild<QListWidget *>("pathsList")->setCurrentRow(0);
        w->findChild<QToolButton *>("closeSurfacePath")->click();
        break;
      case 20:
        capture("open-contour");
        action("undoDocument");
        break;
      case 21:
        capture("undo-close");
        w->findChild<QComboBox *>("surfacePathMode")->setCurrentIndex(0);
        break;
      case 22:
        capture("brush-mode");
        action("undoDocument");
        break;
      case 23: {
        capture("undo-mode");
        timer.stop();
        QFile f(dir + "/filled-path.json");
        if (!f.open(QIODevice::WriteOnly))
          qFatal("Filled proof write failed");
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
  FilledDemo(QWidget *workspace,
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
    timer.setInterval(800);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_filled_path_demo(QWidget *w,
                            QWidget *v,
                            const QString &d,
                            const QString &m,
                            int r,
                            std::function<QJsonObject()> state)
{
  new FilledDemo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
