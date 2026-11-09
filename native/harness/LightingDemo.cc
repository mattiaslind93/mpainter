// SPDX-License-Identifier: GPL-3.0-or-later
#include "ViewportDemoCapture.hh"
#include "LightingDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

namespace bts {
namespace {
class LightingDemo final : public QObject {
  QWidget *workspace_, *viewport_;
  QString directory_, mesh_;
  int resolution_, stage_ = 0;
  std::function<QJsonObject()> state_;
  QTimer timer_;
  QJsonArray captures_;
  void action(const char *name)
  {
    auto *a = workspace_->findChild<QAction *>(name);
    if (!a)
      qFatal("Lighting action missing %s", name);
    a->trigger();
  }
  void rotation(double angle)
  {
    workspace_->findChild<QDoubleSpinBox *>("environmentRotation")->setValue(angle);
  }
  void follows(bool enabled)
  {
    workspace_->findChild<QCheckBox *>("lightingFollowsView")->setChecked(enabled);
  }
  void capture(const char *name)
  {
    auto data = state_();
    data["stage"] = name;
    data["checkbox"] = workspace_->findChild<QCheckBox *>("lightingFollowsView")->isChecked();
    captures_.append(data);
    capture_viewport_framebuffer(viewport_).save(directory_ + "/" + name + ".png");
  }
  void orbit(double degrees)
  {
    const auto panes = state_()["view"].toObject()["panes"].toArray();
    QPointF start(viewport_->width() * .25, viewport_->height() * .5);
    for (auto p : panes)
      if (p.toObject()["view"] == "3D") {
        const auto rect = p.toObject()["rect"].toArray();
        start = {rect[0].toDouble() + rect[2].toDouble() * .5,
                 rect[1].toDouble() + rect[3].toDouble() * .5};
        start /= viewport_->devicePixelRatioF();
      }
    const auto send = [&](QEvent::Type type, QPointF point) {
      QMouseEvent event(type,
                        point,
                        viewport_->mapToGlobal(point.toPoint()),
                        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                        Qt::AltModifier);
      QCoreApplication::sendEvent(viewport_, &event);
    };
    send(QEvent::MouseButtonPress, start);
    for (int i = 1; i <= 6; ++i)
      send(QEvent::MouseMove, start + QPointF(-degrees / .35 * i / 6, 0));
    send(QEvent::MouseButtonRelease, start + QPointF(-degrees / .35, 0));
  }
  void advance()
  {
    switch (stage_++) {
      case 0: {
        capture("startup");
        QAction *action = nullptr;
        for (auto *candidate : workspace_->findChildren<QAction *>())
          if (candidate->text() == "&New…")
            action = candidate;
        if (!action)
          qFatal("Lighting GUI New missing");
        action->trigger();
        break;
      }
      case 1: {
        QDialog *dialog = nullptr;
        for (auto *candidate : workspace_->findChildren<QDialog *>())
          if (candidate->findChild<QLineEdit *>("meshPath"))
            dialog = candidate;
        if (!dialog)
          qFatal("Lighting New dialog missing");
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh_);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution_));
        dialog->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2: {
        capture("created");
        workspace_->findChild<QDialog *>("displaySettings")->show();
        workspace_->findChild<QToolButton *>("addFillLayer")->click();
        workspace_->findChild<QDoubleSpinBox *>("metallicControl")->setValue(1);
        workspace_->findChild<QDoubleSpinBox *>("roughnessControl")->setValue(.1);
        rotation(0);
        break;
      }
      case 3:
        capture("follow0");
        orbit(90);
        break;
      case 4:
        capture("follow-orbit90");
        rotation(45);
        break;
      case 5:
        capture("follow-rotation45");
        rotation(0);
        follows(false);
        orbit(-90);
        break;
      case 6:
        capture("world0");
        orbit(90);
        break;
      case 7:
        capture("world-orbit90");
        rotation(45);
        break;
      case 8:
        capture("world-rotation45");
        action("undoDocument");
        break;
      case 9:
        capture("undo-rotation");
        action("undoDocument");
        break;
      case 10:
        capture("undo-alignment");
        action("redoDocument");
        break;
      case 11:
        capture("redo-alignment");
        rotation(30);
        action("guiLightingSave");
        follows(true);
        action("guiLightingOpen");
        break;
      case 12:
        capture("opened-world");
        workspace_->findChild<QDialog *>("displaySettings")
            ->grab()
            .save(directory_ + "/settings-world.png");
        follows(true);
        action("guiLightingSave");
        follows(false);
        action("guiLightingOpen");
        break;
      case 13:
        capture("opened-follow");
        action("guiLightingLegacy");
        break;
      case 14: {
        capture("legacy-opened");
        timer_.stop();
        QFile file(directory_ + "/lighting.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Lighting proof output failed");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures_}})
                       .toJson());
        file.close();
        capture_demo_window(workspace_, viewport_).save(directory_ + "/workspace.png");
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  LightingDemo(
      QWidget *w, QWidget *v, QString d, QString m, int r, std::function<QJsonObject()> state)
      : QObject(w),
        workspace_(w),
        viewport_(v),
        directory_(d),
        mesh_(m),
        resolution_(r),
        state_(std::move(state))
  {
    QDir().mkpath(directory_);
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start();
  }
};
}  // namespace
void start_lighting_demo(QWidget *w,
                         QWidget *v,
                         const QString &d,
                         const QString &m,
                         int r,
                         std::function<QJsonObject()> state)
{
  new LightingDemo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
