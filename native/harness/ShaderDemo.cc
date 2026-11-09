// SPDX-License-Identifier: GPL-3.0-or-later
#include "ViewportDemoCapture.hh"
#include "ShaderDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int size, stage = 0, after_frame = 0;
  QTimer timer;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Shader control missing: %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void name(const QString &text)
  {
    auto *edit = get<QLineEdit>("shaderInstanceName");
    edit->setFocus();
    edit->setText(text);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(edit, &press);
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    data["dialog_visible"] = get<QDialog>("shaderSettings")->isVisible();
    data["displacement_visible"] = get<QWidget>("displacementPanel")->isVisible();
    data["displacement_parent"] = get<QWidget>("displacementPanel")->parentWidget()->objectName();
    QJsonArray rows;
    auto *list = get<QListWidget>("textureSets");
    for (int i = 0; i < list->count(); ++i) {
      auto *item = list->item(i);
      rows.append(QJsonObject{{"name", item->text()},
                              {"shader", item->data(Qt::UserRole + 30).toString()},
                              {"instance", item->data(Qt::UserRole + 31).toString()}});
    }
    data["rows"] = rows;
    data["shader_undo"] = get<QPushButton>("shaderUndo")->isEnabled();
    data["shader_redo"] = get<QPushButton>("shaderRedo")->isEnabled();
    captures.append(data);
    capture_viewport_framebuffer(v).save(dir + "/" + name + "-viewport.png");
    if (QString(name) == "independent") {
      capture_demo_window(w, v).save(dir + "/shader-instances.png");
      get<QDialog>("shaderSettings")->grab().save(dir + "/shader-panel.png");
    }
  }
  void independent()
  {
    timer.stop();
    auto *list = get<QListWidget>("textureSets");
    QTimer::singleShot(60, w, [this] {
      auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
      if (!menu)
        qFatal("Shader assignment menu absent");
      auto *create = menu->findChild<QAction *>("newShaderInstance");
      QTimer::singleShot(60, w, [] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!dialog)
          qFatal("Shader naming dialog absent");
        dialog->setTextValue("Independent shader");
        dialog->accept();
      });
      create->trigger();
      menu->close();
    });
    auto *button = qobject_cast<QWidget *>(list->itemWidget(list->item(list->currentRow())))
                       ->findChild<QPushButton *>("textureSetShader");
    button->click();
    timer.start();
  }
  void advance()
  {
    const auto s = state();
    if (stage > 1 && (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
                      s["render_frames"].toInt() < after_frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *resolution = d->findChild<QComboBox *>("documentResolution");
        resolution->setCurrentIndex(resolution->findData(size));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2: {
        action("guiSkewPrepare");
        auto *list = get<QListWidget>("textureSets");
        for (int row = 0; row < list->count(); ++row) {
          list->setCurrentRow(row);
          get<QToolButton>("addFillLayer")->click();
          action("addChannel5");
          get<QDoubleSpinBox>("materialNormal0")->setValue(.6);
          get<QDoubleSpinBox>("materialNormal2")->setValue(.8);
          get<QDoubleSpinBox>("materialAmbientOcclusion")->setValue(.3);
        }
        list->setCurrentRow(0);
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      }
      case 3:
        capture("initial");
        name("BTS Shared Reference");
        break;
      case 4:
        capture("renamed");
        get<QPushButton>("shaderUndo")->click();
        break;
      case 5:
        capture("rename-undo");
        get<QPushButton>("shaderRedo")->click();
        break;
      case 6:
        capture("rename-redo");
        get<QDoubleSpinBox>("shaderNormalIntensity")->setValue(2);
        get<QDoubleSpinBox>("shaderAOIntensity")->setValue(.75);
        break;
      case 7:
        capture("shared-parameters");
        independent();
        break;
      case 8:
        capture("inherited");
        get<QDoubleSpinBox>("shaderNormalIntensity")->setValue(0);
        get<QDoubleSpinBox>("shaderAOIntensity")->setValue(.25);
        break;
      case 9:
        capture("independent");
        get<QPushButton>("shaderDock")->click();
        break;
      case 10:
        capture("docked");
        get<QPushButton>("shaderDock")->click();
        get<QDialog>("shaderSettings")->accept();
        get<QToolButton>("addFillLayer")->click();
        break;
      case 11:
        capture("paint-history-add");
        action("guiShaderPaintUndo");
        break;
      case 12:
        capture("paint-history-undo");
        get<QToolButton>("shaderSettingsButton")->click();
        get<QPushButton>("shaderUndo")->click();
        break;
      case 13:
        capture("shader-undo");
        get<QPushButton>("shaderRedo")->click();
        break;
      case 14:
        capture("shader-redo");
        action("guiSkewSave");
        break;
      case 15:
        capture("saved");
        action("guiSkewOpen");
        break;
      case 16:
        capture("opened");
        timer.stop();
        {
          QFile file(dir + "/shaders.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Shader proof unavailable");
          file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                               {"captures", captures}})
                         .toJson());
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
    after_frame = state()["render_frames"].toInt() + 32;
  }

 public:
  Demo(QWidget *window,
       QWidget *viewport,
       QString directory,
       QString fixture,
       int resolution,
       std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        dir(directory),
        mesh(fixture),
        size(resolution),
        state(std::move(snapshot))
  {
    QDir().mkpath(dir);
    timer.setInterval(200);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_shader_demo(QWidget *w,
                       QWidget *v,
                       const QString &dir,
                       const QString &mesh,
                       int size,
                       std::function<QJsonObject()> state)
{
  new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
