#pragma once
#include <epoxy/gl.h>
#include <QDoubleSpinBox>
#include "ViewportDemoCapture.hh"
#include "WorkspaceShell.hh"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
namespace bts {
// Explicit proof actor: drives the production create/cache/eye actions on a
// private display and records the actual GPU viewport inside the Qt window.
class PrecomposeWorkspaceDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh;
  std::function<QJsonObject()> state;
  QTimer timer;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      throw std::runtime_error(std::string("Missing proof control: ") + name);
    return p;
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 2400)
      qFatal("Pre-compose workspace proof timeout stage=%d", stage);
    const auto before = state();
    if (stage > 1 &&
        (before["composite_pending"].toBool() || before["texture_loading"].toBool() ||
         before["property_pending"].toBool() || before["render_frames"].toInt() < frame))
      return;
    try {
      switch (stage++) {
        case 0:
          get<QAction>("newProject")->trigger();
          break;
        case 1: {
          get<QLineEdit>("meshPath")->setText(mesh);
          auto *sizes = get<QComboBox>("documentResolution");
          sizes->setCurrentIndex(sizes->findData(128));
          busy = true;
          get<QPushButton>("createProject")->click();
          busy = false;
          break;
        }
        case 2: {
          w->resize(1920, 1080);
          static_cast<WorkspaceShell *>(w)->reset_reference_layout();
          static_cast<WorkspaceShell *>(w)->set_console_visible(false);
          get<QLineEdit>("shelfSearch")->setText("user-material");
          break;
        }
        case 3: {
          auto *assets = get<QListWidget>("shelfAssets");
          if (!assets->count())
            throw std::runtime_error("user-material shelf is empty");
          assets->setCurrentRow(0);
          Q_EMIT assets->itemDoubleClicked(assets->item(0));
          break;
        }
        case 4:
          get<QToolButton>("propertiesTriplanarProjection")->click();
          break;
        case 5:
          busy = true;
          get<QToolButton>("addPrecomposeLayer")->click();
          busy = false;
          break;
        case 6:
          get<QToolButton>("addPaintLayer")->click();
          break;
        case 7: {
          int cached = 0;
          bool cache = false;
          auto *list = get<QListWidget>("layerStack");
          for (int i = 0; i < list->count(); ++i) {
            cached += list->item(i)->data(Qt::UserRole + 27).toBool();
            cache |= list->item(i)->data(Qt::UserRole + 28).toBool();
          }
          if (!cache || cached < 2)
            throw std::runtime_error("Cache layer or dimmed prefix is absent");
          const auto image = capture_demo_window(w, v);
          if (image.size() != QSize(1920, 1080) ||
              !image.save(output + "/mpainter_archive2026_ui_and_precompose.png"))
            throw std::runtime_error("Cannot write 1920x1080 workspace proof");
          QFile receipt(output + "/precompose-workspace.json");
          receipt.open(QIODevice::WriteOnly);
          receipt.write(QJsonDocument(QJsonObject{{"cached_rows", cached},
                                                  {"width", image.width()},
                                                  {"height", image.height()},
                                                  {"state", state()}})
                            .toJson());
          break;
        }
        case 8: {
          auto *list = get<QListWidget>("layerStack");
          for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole + 28).toBool()) {
              list->item(i)->setCheckState(Qt::Unchecked);
              break;
            }
          break;
        }
        case 9: {
          auto *list = get<QListWidget>("layerStack");
          for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole + 27).toBool())
              throw std::runtime_error("Eye did not release cached prefix");
          timer.stop();
          std::printf("BTS_PRECOMPOSE_WORKSPACE_PASS actual_gpu_capture=1 eye_release=1\n");
          QTimer::singleShot(100, qApp, &QCoreApplication::quit);
          break;
        }
      }
      frame = state()["render_frames"].toInt() + 6;
    }
    catch (const std::exception &e) {
      qFatal("Pre-compose workspace proof: %s", e.what());
    }
  }

 public:
  PrecomposeWorkspaceDemo(QWidget *window,
                          QWidget *viewport,
                          QString directory,
                          QString source,
                          std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        output(std::move(directory)),
        mesh(std::move(source)),
        state(std::move(snapshot))
  {
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(120);
  }
};
}  // namespace bts
