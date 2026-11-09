#pragma once
#include "GuiFileSelection.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class AbDisplacementDemo final : public QObject {
  QWidget *window_, *viewport_;
  bool detail_ = qEnvironmentVariableIsSet("BTS_AB_DETAIL");
  QElapsedTimer settled_;
  QString directory_, mesh_, fixtures_;
  int resolution_, stage_ = 0, undo_ = 0;
  QTimer timer_;
  QElapsedTimer deadline_;
  QJsonArray captures_;
  std::function<QJsonObject()> state_;
  template<class T> T *get(const char *name)
  {
    auto *result = window_->findChild<T *>(name);
    if (!result)
      qFatal("AB control missing: %s", name);
    return result;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  QJsonObject probe()
  {
    window_->setProperty("guiProbeOnly", true);
    auto result = state_();
    window_->setProperty("guiProbeOnly", false);
    return result;
  }
  bool pending()
  {
    const auto value = probe();
    return value["texture_loading"].toBool() || value["composite_pending"].toBool() ||
           value["property_pending"].toBool();
  }
  void capture(const char *name)
  {
    auto value = state_();
    value["stage"] = name;
    value["alpha_control"] = get<QCheckBox>("fillChannelAlphaMask14")->isChecked();
    value["resource_info"] = get<QLabel>("fillChannelInfo14")->text();
    captures_.append(value);
    window_->setProperty("guiCapturePath", directory_ + "/" + name + ".png");
    action("guiCaptureViewport");
  }
  void load(const QString &name)
  {
    timer_.stop();
    select_gui_test_file(window_, "fillTextureFileDialog", fixtures_ + "/" + name);
    get<QComboBox>("fillChannelResource14")->setCurrentIndex(1);
    timer_.start();
  }
  void detail_step()
  {
    if (stage_ > 3) {
      if (pending()) {
        settled_.invalidate();
        return;
      }
      if (!settled_.isValid()) {
        settled_.start();
        return;
      }
      if (settled_.elapsed() < 1600)
        return;
    }
    settled_.invalidate();
    switch (stage_++) {
      case 3: {
        get<QSpinBox>("displacementSubdivisionCount")->setValue(0);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport_, &key);
        const QPointF position(viewport_->width() * .5, viewport_->height() * .5);
        QWheelEvent wheel(position,
                          viewport_->mapToGlobal(position.toPoint()),
                          {},
                          QPoint(0, 120 * 16),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(viewport_, &wheel);
        load("detail_1001.exr");
        break;
      }
      case 4:
        capture("detail-full");
        action("checkGuiGpuStack");
        load("coarse_1001.exr");
        break;
      case 5:
        capture("detail-coarse-reference");
        load("detail_1001.exr");
        break;
      case 6:
        capture("detail-restored");
        get<QDoubleSpinBox>("displacementStrength")->setValue(0);
        break;
      case 7: {
        capture("detail-disabled");
        QFile out(directory_ + "/ab-displacement.json");
        if (!out.open(QIODevice::WriteOnly))
          qFatal("AB detail proof output unavailable");
        out.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                            {"captures", captures_}})
                      .toJson());
        timer_.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }
  void advance()
  {
    if (deadline_.elapsed() > 240000)
      qFatal("AB displacement timed out at stage %d", stage_);
    if (detail_ && stage_ >= 3) {
      detail_step();
      return;
    }
    if (stage_ > 2 && pending())
      return;
    switch (stage_++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh_);
        auto *r = d->findChild<QComboBox *>("documentResolution");
        r->setCurrentIndex(r->findData(resolution_));
        d->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        action("guiTextureImportPrepare");
        get<QCheckBox>("displacementChannel")->setChecked(true);
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        undo_ = probe()["undo_count"].toInt();
        load("ab4_1001.exr");
        break;
      case 4:
        if (probe()["undo_count"].toInt() != undo_ + 1)
          qFatal("AB image assignment needs one Undo");
        capture("signed-default");
        get<QCheckBox>("fillChannelAlphaMask14")->setChecked(true);
        break;
      case 5:
        capture("explicit-alpha");
        action("undoDocument");
        break;
      case 6:
        capture("undo-alpha");
        action("redoDocument");
        break;
      case 7:
        capture("redo-alpha");
        action("undoDocument");
        break;
      case 8:
        get<QDoubleSpinBox>("displacementMidlevel")->setValue(.23);
        break;
      case 9:
        capture("custom-midlevel");
        action("guiTextureImportSave");
        break;
      case 10:
        action("guiTextureImportHideSources");
        action("guiTextureImportOpen");
        break;
      case 11: {
        capture("packed-open");
        QFile out(directory_ + "/ab-displacement.json");
        if (!out.open(QIODevice::WriteOnly))
          qFatal("AB proof output unavailable");
        out.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                            {"captures", captures_}})
                      .toJson());
        timer_.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  AbDisplacementDemo(QWidget *window,
                     QWidget *viewport,
                     const QString &directory,
                     const QString &mesh,
                     int resolution,
                     std::function<QJsonObject()> state)
      : QObject(window),
        window_(window),
        viewport_(viewport),
        directory_(directory),
        mesh_(mesh),
        fixtures_(qEnvironmentVariable("BTS_TEXTURE_IMPORT_FIXTURES")),
        resolution_(resolution),
        state_(std::move(state))
  {
    QDir().mkpath(directory);
    deadline_.start();
    timer_.setInterval(80);
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start();
  }
};
}  // namespace bts
