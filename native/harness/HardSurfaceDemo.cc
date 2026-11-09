// SPDX-License-Identifier: GPL-3.0-or-later
#include "PythonConsole.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QTimer>
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
      qFatal("Hard surface control missing: %s", name);
    return value;
  }
  void command(const QString &code)
  {
    get<QLineEdit>("pythonCommand")->setText("exec(" + quote(code) + ")");
    get<QPushButton>("executePythonCommand")->click();
    if (!get<QDockWidget>("pythonConsoleDock")->property("lastExecutionSucceeded").toBool())
      qFatal("Hard surface script failed: %s",
             qPrintable(get<QPlainTextEdit>("pythonOutput")->toPlainText()));
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    captures.append(data);
    QFile file(directory + "/unwrap.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("Cannot write hard-surface proof");
    file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                         {"captures", captures}})
                   .toJson());
    window->findChild<QOpenGLWidget *>()->grabFramebuffer().save(directory + "/" + name +
                                                                 "-viewport.png");
    window->grab().save(directory + "/" + name + "-workspace.png");
  }
  void create(bool hard, bool authored = false, bool recompute = false, bool cancel = false)
  {
    command("if bts.project.is_open(): bts.project.close(discard=True)");
    get<QAction>("newProject")->trigger();
    auto *dialog = get<QDialog>("newProjectDialog");
    dialog->findChild<QLineEdit *>("meshPath")
        ->setText(directory + (authored ? "/mechanical-uv.obj" : "/mechanical.obj"));
    auto *resolution = dialog->findChild<QComboBox *>("documentResolution");
    resolution->setCurrentIndex(resolution->findData(128));
    dialog->findChild<QCheckBox *>("newAutoUnwrap")->setChecked(true);
    QTimer::singleShot(30, this, [this, hard, recompute, cancel] {
      auto *options = get<QDialog>("autoUnwrapOptions");
      auto *toggle = options->findChild<QCheckBox *>("unwrapHardSurface");
      if (toggle->isChecked())
        qFatal("Hard-surface default must be off");
      toggle->setChecked(hard);
      if (recompute) {
        options->findChild<QComboBox *>("unwrapSeams")->setCurrentIndex(1);
        if (options->findChild<QComboBox *>("unwrapIslands")->isEnabled() ||
            options->findChild<QComboBox *>("unwrapPacking")->isEnabled() ||
            options->findChild<QComboBox *>("unwrapIslands")->currentIndex() != 1 ||
            options->findChild<QComboBox *>("unwrapPacking")->currentIndex() != 1)
          qFatal("Unwrap dependencies inconsistent");
      }
      options->grab().save(directory + (cancel ? "/cancel-options.png" :
                                        hard   ? "/hard-options.png" :
                                                 "/standard-options.png"));
      options->findChild<QDialogButtonBox *>()
          ->button(cancel ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)
          ->click();
    });
    dialog->findChild<QPushButton *>("newAutoUnwrapOptions")->click();
    dialog->findChild<QPushButton *>("createProject")->click();
    get<QComboBox>("viewLayoutControl")->setCurrentIndex(2);
  }
  void advance()
  {
    if (++ticks > 1000)
      qFatal("Hard surface demo timed out");
    auto *view = window->findChild<QOpenGLWidget *>();
    if (!view || !view->isValid() || busy())
      return;
    switch (phase++) {
      case 0:
        get<QAction>("showPythonConsole")->trigger();
        command("import bts");
        create(false);
        break;
      case 1:
        capture("standard");
        create(true);
        break;
      case 2:
        capture("hard");
        command("bts.project.save_as(" + quote(directory + "/hard.blend") + ")");
        break;
      case 3:
        capture("saved");
        command("bts.project.close()\nbts.project.open(" + quote(directory + "/hard.blend") + ")");
        break;
      case 4:
        capture("opened");
        create(false, true);
        break;
      case 5:
        capture("authored");
        create(true, true);
        break;
      case 6:
        capture("preserved");
        create(true, true, true);
        break;
      case 7:
        capture("recomputed");
        create(true, false, false, true);
        break;
      case 8:
        capture("cancelled-options");
        command("bts.project.close(discard=True)");
        timer.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
  }

 public:
  Demo(QMainWindow *parent, std::function<bool()> pending, std::function<QJsonObject()> snapshot)
      : QObject(parent),
        window(parent),
        directory(qEnvironmentVariable("BTS_HARD_UNWRAP_DEMO")),
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
void start_hard_surface_demo(QMainWindow *window,
                             std::function<bool()> busy,
                             std::function<QJsonObject()> snapshot)
{
  new Demo(window, std::move(busy), std::move(snapshot));
}
}  // namespace bts
