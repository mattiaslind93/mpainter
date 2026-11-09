// SPDX-License-Identifier: GPL-3.0-or-later
#include "AlembicDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLabel>
#include <QFileInfo>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class AlembicDemo final : public QObject {
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
      qFatal("Alembic action missing: %s", name);
    a->trigger();
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    QJsonArray names;
    auto *sets = w->findChild<QListWidget *>("textureSets");
    for (int i = 0; i < sets->count(); ++i)
      names.append(sets->item(i)->text());
    data["texture_sets"] = names;
    captures.append(data);
    w->grab().save(dir + "/" + name + ".png");
  }
  void stroke()
  {
    const auto point = state()["stroke_point"].toArray();
    if (point.size() != 2)
      qFatal("Alembic mesh/UV pick missing");
    QPointF p(point[0].toDouble(), point[1].toDouble());
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}) {
      QMouseEvent e(type,
                    p,
                    v->mapToGlobal(p.toPoint()),
                    type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QCoreApplication::sendEvent(v, &e);
      p += QPointF(4, 0);
    }
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
        if (!d)
          qFatal("Alembic New missing");
        d->findChild<QPushButton *>("selectMesh")->click();
        break;
      }
      case 2: {
        auto *picker = w->findChild<QFileDialog *>("meshFilePicker");
        if (!picker || !picker->nameFilters().join(' ').contains("*.abc"))
          qFatal("Alembic file filter missing");
        picker->selectFile(mesh);
        QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
        auto *d = w->findChild<QDialog *>("newProjectDialog");
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 3:
        if (!state()["has_document"].toBool())
          qFatal("Alembic New failed");
        capture("created");
        w->findChild<QToolButton *>("addPaintLayer")->click();
        break;
      case 4:
        capture("empty");
        stroke();
        break;
      case 5:
        capture("painted");
        action("guiDisplacementSave");
        break;
      case 6:
        capture("saved");
        if (!QFile::rename(mesh, mesh + ".moved"))
          qFatal("Cannot move Alembic source");
        action("guiDisplacementOpen");
        break;
      case 7: {
        capture("opened-without-source");
        timer.stop();
        QTimer::singleShot(100, this, [this] {
          auto *d = w->findChild<QDialog *>("projectConfigurationDialog");
          if (!d)
            qFatal("Alembic configuration missing");
          d->findChild<QLineEdit *>("replacementMeshPath")
              ->setText(QFileInfo(mesh).absolutePath() + "/replacement.ABC");
          d->findChild<QPushButton *>("commitReplacementMesh")->click();
        });
        action("projectConfiguration");
        timer.start();
        break;
      }
      case 8:
        capture("reimported");
        action("guiDisplacementSave");
        break;
      case 9:
        capture("reimport-saved");
        action("guiDisplacementOpen");
        break;
      case 10: {
        capture("reimport-opened");
        timer.stop();
        QFile invalid(dir + "/invalid.abc");
        if (!invalid.open(QIODevice::WriteOnly))
          qFatal("Cannot write invalid archive");
        invalid.write("not an Alembic archive");
        invalid.close();
        QTimer::singleShot(100, this, [this] {
          auto *d = w->findChild<QDialog *>("projectConfigurationDialog");
          if (!d)
            qFatal("Alembic configuration missing");
          d->findChild<QLineEdit *>("replacementMeshPath")->setText(dir + "/invalid.abc");
          d->findChild<QPushButton *>("commitReplacementMesh")->click();
          if (!d->findChild<QLabel *>("replacementMeshError")->text().contains("Alembic"))
            qFatal("Invalid Alembic did not explain failure");
          d->reject();
        });
        action("projectConfiguration");
        timer.start();
        break;
      }
      case 11: {
        capture("invalid-preserved");
        timer.stop();
        QFile f(dir + "/alembic.json");
        if (!f.open(QIODevice::WriteOnly))
          qFatal("Alembic proof write failed");
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
  AlembicDemo(QWidget *workspace,
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
    timer.setInterval(900);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_alembic_demo(QWidget *w,
                        QWidget *v,
                        const QString &d,
                        const QString &m,
                        int r,
                        std::function<QJsonObject()> state)
{
  new AlembicDemo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
