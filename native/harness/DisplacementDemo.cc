// SPDX-License-Identifier: GPL-3.0-or-later
#include "DisplacementDemo.hh"
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
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include "ViewportWidget.hh"
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *workspace_, *viewport_;
  QString directory_, mesh_;
  int resolution_, stage_ = 0, mode_stage_ = 0;
  bool uv_level0_ = false;
  QTimer timer_;
  QJsonArray captures_;
  std::function<QJsonObject()> state_;
  void action(const char *name)
  {
    auto *a = workspace_->findChild<QAction *>(name);
    if (!a)
      qFatal("Displacement action missing %s", name);
    a->trigger();
  }
  void number(const char *name, double value)
  {
    auto *c = workspace_->findChild<QDoubleSpinBox *>(name);
    if (!c)
      qFatal("Displacement number missing %s", name);
    c->setValue(value);
  }
  void toggle(const char *name, bool value)
  {
    auto *c = workspace_->findChild<QCheckBox *>(name);
    if (!c)
      qFatal("Displacement toggle missing %s", name);
    c->setChecked(value);
  }
  void level(int value)
  {
    workspace_->findChild<QSpinBox *>("displacementSubdivisionCount")->setValue(value);
  }
  void key(int key)
  {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(viewport_, &event);
  }
  void stroke()
  {
    const auto data = state_();
    const auto p = data["stroke_point"].toArray();
    if (p.size() != 2)
      qFatal("No projected original surface point");
    const QPointF from(p[0].toDouble(), p[1].toDouble()), to = from + QPointF(18, 0);
    const auto send = [&](QEvent::Type type, QPointF point) {
      QMouseEvent e(type,
                    point,
                    viewport_->mapToGlobal(point.toPoint()),
                    type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QCoreApplication::sendEvent(viewport_, &e);
    };
    send(QEvent::MouseButtonPress, from);
    for (int i = 1; i <= 12; ++i)
      send(QEvent::MouseMove, from + (to - from) * i / 12.);
    send(QEvent::MouseButtonRelease, to);
  }
  void capture(const char *name)
  {
    workspace_->setProperty("displacementObj", directory_ + "/" + name + ".obj");
    action("guiDisplacementExportOriginal");
    auto data = state_();
    data["stage"] = name;
    captures_.append(data);
    workspace_->setProperty("guiCapturePath", directory_ + "/" + name + ".png");
    action("guiCaptureViewport");
  }
  void export_gui()
  {
    timer_.stop();
    auto *poll = new QTimer(this);
    int phase = 0;
    connect(poll, &QTimer::timeout, this, [this, poll, phase]() mutable {
      auto *dialog = workspace_->findChild<QDialog *>("textureExportDialog");
      if (!dialog)
        return;
      if (phase == 0) {
        phase = 1;
        const auto output = directory_ + "/gui-export";
        QDir().mkpath(output);
        dialog->findChild<QLineEdit *>("exportDirectory")->setText(output);
        auto *size = dialog->findChild<QComboBox *>("exportSize");
        if (size)
          size->setCurrentIndex(size->findData(128));
        auto *sets = dialog->findChild<QListWidget *>("exportTextureSets");
        for (int i = 0; i < sets->count(); ++i)
          sets->item(i)->setCheckState(i == 0 ? Qt::Checked : Qt::Unchecked);
        dialog->findChild<QPushButton *>("runTextureExport")->click();
        return;
      }
      if (!dialog->property("exportFinished").toBool())
        return;
      if (!dialog->property("exportError").toString().isEmpty())
        qFatal("Displacement GUI export failed");
      dialog->grab().save(directory_ + "/export-dialog.png");
      poll->stop();
      dialog->findChild<QPushButton *>("cancelTextureExport")->click();
      poll->deleteLater();
    });
    poll->start(50);
    action("exportTextures");
    timer_.start();
  }
  void advance()
  {
    if (state_()["composite_pending"].toBool())
      return;
    switch (stage_++) {
      case 0: {
        QAction *a = nullptr;
        for (auto *candidate : workspace_->findChildren<QAction *>())
          if (candidate->text() == "&New…")
            a = candidate;
        if (!a)
          qFatal("Displacement GUI New missing");
        a->trigger();
        break;
      }
      case 1: {
        QDialog *dialog = nullptr;
        for (auto *candidate : workspace_->findChildren<QDialog *>())
          if (candidate->findChild<QLineEdit *>("meshPath"))
            dialog = candidate;
        if (!dialog)
          qFatal("Displacement New dialog missing");
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh_);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution_));
        dialog->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        capture("created");
        workspace_->findChild<QToolButton *>("addPaintLayer")->click();
        break;
      case 3: {
        capture("paint-empty");
        for (int c = 0; c < 5; ++c)
          workspace_->findChild<QToolButton *>(QString("brushChannel%1").arg(c))
              ->setChecked(c == 4);
        number("materialBump", .15);
        stroke();
        break;
      }
      case 4:
        capture("bump-painted");
        workspace_->findChild<QToolButton *>("shaderSettingsButton")->click();
        toggle("displacementChannel", true);
        level(0);
        number("displacementStrength", .3);
        workspace_->findChild<QToolButton *>("brushChannel4")->setChecked(false);
        number("materialDisplacement", 1);
        break;
      case 5:
        capture("disp-empty");
        stroke();
        break;
      case 6:
        capture("disp-painted-level0");
        workspace_->findChild<QToolButton *>("addFillLayer")->click();
        number("materialDisplacement", 1);
        workspace_->findChild<QListWidget *>("layerStack")->setCurrentRow(1);
        break;
      case 7:
        if (!uv_level0_) {
          capture("fill-level0");
          key(Qt::Key_F3);
          uv_level0_ = true;
          --stage_;
        }
        else {
          capture("uv-level0");
          key(Qt::Key_F2);
          number("displacementStrength", 0);
        }
        break;
      case 8:
        capture("strength-zero");
        number("displacementStrength", .3);
        number("displacementMidlevel", 0);
        break;
      case 9:
        capture("midlevel-zero");
        number("displacementMidlevel", 1);
        break;
      case 10:
        capture("midlevel-one");
        number("displacementMidlevel", .5);
        level(2);
        break;
      case 11: {
        auto *mode = workspace_->findChild<QComboBox *>("displacementSubdivisionMode");
        switch (mode_stage_++) {
          case 0:
            capture("disp-level2");
            key(Qt::Key_F3);
            --stage_;
            break;
          case 1:
            capture("uv-uniform");
            key(Qt::Key_F2);
            mode->setCurrentIndex(mode->findData(2));
            --stage_;
            break;
          case 2:
            capture("catmull-clark");
            key(Qt::Key_F3);
            --stage_;
            break;
          case 3:
            capture("uv-catmull");
            key(Qt::Key_F2);
            action("undoDocument");
            --stage_;
            break;
          case 4:
            capture("undo-catmull");
            action("redoDocument");
            --stage_;
            break;
          case 5:
            capture("redo-catmull");
            toggle("displacementEnabled", false);
            break;
        }
        break;
      }
      case 12:
        capture("disabled-retained");
        action("undoDocument");
        break;
      case 13:
        capture("undo-enabled");
        action("redoDocument");
        break;
      case 14:
        capture("redo-disabled");
        toggle("displacementEnabled", true);
        action("guiDisplacementSave");
        break;
      case 15:
        capture("saved");
        toggle("displacementChannel", false);
        break;
      case 16:
        capture("channel-disabled-retained");
        action("guiDisplacementOpen");
        break;
      case 17:
        capture("opened");
        workspace_->findChild<QPushButton *>("displacementRestoreDefaults")->click();
        break;
      case 18:
        capture("restored-defaults");
        action("undoDocument");
        break;
      case 19: {
        capture("undo-defaults");
        auto *context = workspace_->findChild<QComboBox *>("layerChannelContext");
        context->setCurrentIndex(context->findData(14));
        action("addLevelsEffect");
        auto *levels = workspace_->findChild<QComboBox *>("levelsChannel");
        if (levels->currentData().toInt() != 14 || levels->itemText(levels->findData(4)) != "Bump")
          qFatal("Displacement Levels channel or Bump label missing");
        export_gui();
        action("guiDisplacementLegacy");
        break;
      }
      case 20: {
        capture("legacy");
        timer_.stop();
        QFile file(directory_ + "/displacement.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Displacement proof write failed");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures_}})
                       .toJson());
        file.close();
        workspace_->grab().save(directory_ + "/workspace.png");
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  Demo(QWidget *w, QWidget *v, QString d, QString m, int r, std::function<QJsonObject()> state)
      : QObject(w),
        workspace_(w),
        viewport_(v),
        directory_(d),
        mesh_(m),
        resolution_(r),
        state_(std::move(state))
  {
    QDir().mkpath(directory_);
    timer_.setInterval(900);
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start();
  }
};
}  // namespace
void start_displacement_demo(QWidget *w,
                             QWidget *v,
                             const QString &d,
                             const QString &m,
                             int r,
                             std::function<QJsonObject()> state)
{
  new Demo(w, v, d, m, r, std::move(state));
}
}  // namespace bts
