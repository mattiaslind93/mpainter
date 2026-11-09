// SPDX-License-Identifier: GPL-3.0-or-later
#include "PythonConsole.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QOpenGLWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
QString quote(const QString &value)
{
  const auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
  return QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
}
class Demo final : public QObject {
  QMainWindow *window;
  QString directory;
  std::function<bool()> busy;
  std::function<QJsonObject()> state;
  QTimer timer;
  int phase = 0, ticks = 0;
  QJsonArray captures;
  template<class T> T *get(const char *name)
  {
    auto *value = window->findChild<T *>(name);
    if (!value)
      qFatal("Fill symmetry control missing: %s", name);
    return value;
  }
  void command(const QString &code)
  {
    get<QLineEdit>("pythonCommand")->setText("exec(" + quote(code) + ")");
    get<QPushButton>("executePythonCommand")->click();
    if (!get<QDockWidget>("pythonConsoleDock")->property("lastExecutionSucceeded").toBool())
      qFatal("Fill symmetry script failed: %s",
             qPrintable(get<QPlainTextEdit>("pythonOutput")->toPlainText()));
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    data["toolbar_enabled"] = get<QToolButton>("symmetryToggle")->isEnabled();
    data["toolbar_checked"] = get<QToolButton>("symmetryToggle")->isChecked();
    auto *context = get<QWidget>("maskFillProperties")->isVisible() ?
                        get<QWidget>("maskSpatialFillPanel") :
                        get<QWidget>("fillProjectionPanel");
    data["properties_enable_visible"] = context
                                            ->findChild<QCheckBox *>(
                                                context->objectName() == "maskSpatialFillPanel" ?
                                                    "maskSpatial_fillSymmetryEnabled" :
                                                    "fillSymmetryEnabled")
                                            ->isVisible();
    captures.append(data);
    QFile proof(directory + "/symmetry.json");
    if (!proof.open(QIODevice::WriteOnly))
      qFatal("Cannot write fill symmetry proof");
    proof.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                          {"captures", captures}})
                    .toJson());
    window->findChild<QOpenGLWidget *>()->grabFramebuffer().save(directory + "/" + name +
                                                                 "-viewport.png");
    if (QString(name) == "mirror" || QString(name) == "radial")
      get<QScrollArea>("propertiesScroll")->ensureWidgetVisible(get<QWidget>("fillSymmetryPanel"));
    window->grab().save(directory + "/" + name + "-workspace.png");
  }
  void advance()
  {
    if (++ticks > 1400)
      qFatal("Fill symmetry demo timed out");
    auto *view = window->findChild<QOpenGLWidget *>();
    if (!view || !view->isValid() || (phase != 1 && busy()))
      return;
    switch (phase++) {
      case 0:
        get<QAction>("showPythonConsole")->trigger();
        command(
            "import bts, pathlib\nC=bts.textureset.ChannelType\nS=bts.layerstack.NodeStack\nP=bts.layerstack.InsertPosition\nroot=pathlib.Path(" +
            quote(directory) + ")");
        get<QAction>("newProject")->trigger();
        break;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        auto *preset = dialog->findChild<QComboBox *>("projectTemplate");
        preset->setCurrentIndex(preset->findData(":pbr"));
        dialog->findChild<QLineEdit *>("meshPath")->setText(directory + "/plane.obj");
        auto *resolution = dialog->findChild<QComboBox *>("documentResolution");
        resolution->setCurrentIndex(resolution->findData(128));
        dialog->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        command(
            "stack=bts.textureset.get_active_stack()\nfill=bts.layerstack.get_root_layer_nodes(stack)[0]\nasset=bts.resource.import_image(root/'logo.png',scope='project',usage='texture')\nfill.active_channels={C.BaseColor}\nfill.set_source(C.BaseColor,asset['id'])\np=fill.get_projection_parameters();p.mode=2;p.offset3=[.25,0,0];p.scale3=[.3,.3,.3];p.linked3=False;p.filtering=2;p.wrap=1\nfill.set_projection_parameters(p)");
        get<QComboBox>("viewLayoutControl")->setCurrentIndex(2);
        break;
      case 4:
        if (window->findChildren<QCheckBox *>("fillSymmetryEnabled").size() != 1 ||
            window->findChildren<QComboBox *>("fillProjectionMode").size() != 1)
          qFatal("Fill control identity collision");
        capture("base");
        get<QCheckBox>("fillSymmetryEnabled")->setChecked(true);
        break;
      case 5:
        capture("mirror");
        get<QCheckBox>("fillSymmetryFlipU")->setChecked(true);
        break;
      case 6:
        capture("flip-u");
        get<QCheckBox>("fillSymmetryFlipU")->setChecked(false);
        get<QCheckBox>("fillSymmetryFlipV")->setChecked(true);
        break;
      case 7:
        capture("flip-v");
        get<QCheckBox>("fillSymmetryFlipV")->setChecked(false);
        get<QComboBox>("fillSymmetryMode")->setCurrentIndex(1);
        get<QComboBox>("fillSymmetryAxis")->setCurrentIndex(1);
        get<QSpinBox>("fillSymmetryCopies")->setValue(4);
        break;
      case 8:
        capture("radial");
        get<QToolButton>("symmetryManipulator")->setChecked(true);
        get<QCheckBox>("symmetryIntersection")->setChecked(false);
        break;
      case 9:
        capture("display");
        get<QComboBox>("fillProjectionMode")->setCurrentIndex(0);
        break;
      case 10:
        capture("uv");
        get<QComboBox>("fillProjectionMode")->setCurrentIndex(2);
        break;
      case 11:
        capture("returned");
        get<QAction>("undoDocument")->trigger();
        break;
      case 12:
        capture("undo");
        get<QAction>("redoDocument")->trigger();
        break;
      case 13:
        capture("redo");
        command("bts.project.save_as(root/'fill.blend')");
        break;
      case 14:
        capture("saved");
        if (!QFile::remove(directory + "/logo.png"))
          qFatal("Cannot remove own test texture");
        command("bts.project.close()\nbts.project.open(root/'fill.blend')");
        break;
      case 15:
        capture("opened");
        command(
            "fill.add_mask(0.)\nmask=bts.layerstack.insert_fill(P.inside_node(fill,S.Mask))\nmask.set_source(None,bts.layerstack.atom_source(.8))\np=mask.get_projection_parameters();p.mode=2;p.offset3=[.25,0,0];p.scale3=[.3,.3,.3];p.linked3=False;p.symmetry=fill.get_projection_parameters().symmetry;p.symmetry=dict(p.symmetry,mode=0,axis=0,flip_u=False,flip_v=False)\nmask.set_projection_parameters(p)\nbts.layerstack.set_selected_nodes([mask])");
        break;
      case 16:
        capture("mask-on");
        get<QWidget>("maskSpatialFillPanel")
            ->findChild<QCheckBox *>("maskSpatial_fillSymmetryEnabled")
            ->setChecked(false);
        break;
      case 17:
        capture("mask-off");
        get<QAction>("undoDocument")->trigger();
        break;
      case 18:
        capture("mask-undo");
        command("bts.project.save_as(root/'mask.blend')");
        break;
      case 19:
        capture("mask-saved");
        command("bts.project.close()\nbts.project.open(root/'mask.blend')");
        break;
      case 20:
        capture("mask-opened");
        command("bts.project.close()");
        timer.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
  }

 public:
  Demo(QMainWindow *parent, std::function<bool()> pending, std::function<QJsonObject()> snapshot)
      : QObject(parent),
        window(parent),
        directory(qEnvironmentVariable("BTS_FILL_SYMMETRY_DEMO")),
        busy(std::move(pending)),
        state(std::move(snapshot))
  {
    QDir().mkpath(directory);
    timer.setInterval(350);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_fill_symmetry_demo(QMainWindow *window,
                              std::function<bool()> busy,
                              std::function<QJsonObject()> snapshot)
{
  new Demo(window, std::move(busy), std::move(snapshot));
}
}  // namespace bts
