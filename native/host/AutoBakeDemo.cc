#include "AutoBakeDemo.hh"
#include "Performance.hh"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QScreen>
#include <QElapsedTimer>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *viewport;
  QString dir, mesh;
  int size, stage = 0, manual_role = 0, ticks = 0;
  bool dragging = false;
  QTimer timer, input, ping;
  QElapsedTimer heartbeat;
  double max_ping = 0, drag_ping = 0;
  QJsonArray drag_pings;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Auto bake control missing: %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void map_button(const char *name, int role)
  {
    get<QToolButton>((QString(name) + QString::number(role)).toUtf8().constData())->click();
  }
  void capture(const char *name)
  {
    auto s = state();
    s["stage"] = name;
    QJsonArray automatic, enabled;
    for (int r = 0; r < 7; ++r) {
      auto *b = get<QToolButton>((QString("bakeAuto") + QString::number(r)).toUtf8().constData());
      automatic.append(b->isChecked());
      enabled.append(b->isEnabled());
    }
    s["auto_buttons"] = automatic;
    s["auto_enabled"] = enabled;
    s["quality_role"] = get<QComboBox>("bakeQualityRole")->currentIndex();
    s["quality_rays"] = get<QSpinBox>("bakeMapRays")->value();
    s["quality_dilation"] = get<QSpinBox>("bakeMapDilation")->value();
    captures.append(s);
    if (QString(name) == "ao-drag" || QString(name) == "sets-opened")
      w->screen()->grabWindow(w->winId()).save(dir + "/" + name + ".png");
  }
  void new_project(const QString &path)
  {
    auto *d = get<QDialog>("newProjectDialog");
    d->findChild<QLineEdit *>("meshPath")->setText(path);
    auto *r = d->findChild<QComboBox *>("documentResolution");
    r->setCurrentIndex(r->findData(size));
    d->findChild<QPushButton *>("createProject")->click();
  }
  void drag(bool distance)
  {
    performance::begin(distance ? "distance-drag" : "ao-drag");
    drag_ping = 0;
    ticks = 0;
    dragging = true;
    input.setProperty("distance", distance);
    heartbeat.start();
    ping.start();
    input.start();
  }
  void advance()
  {
    if (dragging)
      return;
    const auto s = state();
    if (stage > 1 && (s["composite_pending"].toBool() || s["skew"].toObject()["pending"].toBool()))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        new_project(mesh);
        break;
      case 2:
        action("guiSkewPrepare");
        action("enterBakingMode");
        capture("initial");
        break;
      case 3:
        map_button("bakePreview", 0);
        map_button("bakeNow", manual_role);
        break;
      case 4:
        capture((QString("baked-") + QString::number(manual_role)).toUtf8().constData());
        if (++manual_role < 7) {
          map_button("bakeNow", manual_role);
          --stage;
        }
        break;
      case 5:
        get<QComboBox>("bakeQualityRole")->setCurrentIndex(3);
        get<QSpinBox>("bakeMapRays")->setValue(8);
        get<QSpinBox>("bakeMapDilation")->setValue(2);
        get<QCheckBox>("bakeMapDiffusion")->setChecked(true);
        break;
      case 6:
        performance::begin("ao-preview");
        capture("quality-before-auto");
        map_button("bakeAuto", 3);
        map_button("bakePreview", 3);
        break;
      case 7:
        capture("ao-auto");
        drag(false);
        break;
      case 8:
        capture("ao-drag");
        action("checkGuiComposite");
        action("guiSkewReference");
        break;
      case 9:
        capture("ao-reference");
        map_button("bakeAuto", 0);
        break;
      case 10:
        capture("normal-auto");
        map_button("bakeAuto", 0);
        break;
      case 11:
        capture("auto-off");
        get<QSpinBox>("bakeMapDilation")->setValue(0);
        break;
      case 12:
        capture("off-edited");
        map_button("bakeNow", 0);
        break;
      case 13:
        capture("manual-normal");
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 14:
        action("undoDocument");
        break;
      case 15:
        action("undoDocument");
        break;
      case 16:
        capture("manual-undo");
        action("redoDocument");
        break;
      case 17:
        capture("manual-redo");
        get<QCheckBox>("skewUseProjectMesh")->setChecked(false);
        break;
      case 18:
        capture("missing-source");
        get<QCheckBox>("skewUseProjectMesh")->setChecked(true);
        get<QCheckBox>("skewUseCage")->setChecked(true);
        break;
      case 19:
        capture("missing-cage");
        get<QCheckBox>("skewUseCage")->setChecked(false);
        get<QLineEdit>("skewHighFiles")->setText(qEnvironmentVariable("BTS_SKEW_HIGH_SOURCE"));
        QMetaObject::invokeMethod(
            get<QLineEdit>("skewHighFiles"), "editingFinished", Qt::DirectConnection);
        get<QCheckBox>("skewUseProjectMesh")->setChecked(false);
        get<QCheckBox>("skewRelativeDistances")->setChecked(false);
        get<QDoubleSpinBox>("skewFrontalDistance")->setValue(.2);
        get<QDoubleSpinBox>("skewRearDistance")->setValue(.2);
        break;
      case 20:
        map_button("bakeAuto", 0);
        break;
      case 21:
        capture("external-normal");
        drag(true);
        break;
      case 22:
        capture("distance-drag");
        action("guiSkewReference");
        break;
      case 23:
        capture("distance-reference");
        get<QComboBox>("autoBakeWidth")
            ->setCurrentIndex(get<QComboBox>("autoBakeWidth")->findData(8192));
        break;
      case 24:
        capture("budget-error");
        break;
      case 25:
      case 26:
      case 27:
      case 28:
        break;
      case 29:
        capture("budget-error-stable");
        get<QComboBox>("autoBakeWidth")
            ->setCurrentIndex(get<QComboBox>("autoBakeWidth")->findData(128));
        break;
      case 30:
        capture("recovered");
        action("guiSkewSave");
        break;
      case 31:
        capture("saved");
        action("guiSkewOpen");
        break;
      case 32:
        capture("opened");
        action("newProject");
        break;
      case 33:
        new_project(qEnvironmentVariable("BTS_SKEW_SETS_LOW"));
        break;
      case 34:
        action("guiSkewPrepare");
        action("enterBakingMode");
        get<QComboBox>("bakeQualityRole")->setCurrentIndex(3);
        get<QSpinBox>("bakeMapRays")->setValue(12);
        get<QSpinBox>("bakeMapDilation")->setValue(1);
        get<QCheckBox>("bakeMapDiffusion")->setChecked(true);
        break;
      case 35:
        map_button("bakeAuto", 3);
        break;
      case 36:
        capture("sets-before-sync");
        map_button("bakeSync", 3);
        break;
      case 37:
        capture("sets-synced");
        get<QListWidget>("textureSets")->setCurrentRow(1);
        break;
      case 38:
        capture("sets-second");
        get<QSpinBox>("bakeMapRays")->setValue(24);
        break;
      case 39:
        capture("sets-independent");
        action("undoDocument");
        break;
      case 40:
        capture("sets-undo-edit");
        action("undoDocument");
        break;
      case 41:
        capture("sets-undo-sync");
        action("redoDocument");
        break;
      case 42:
        capture("sets-redo-sync");
        action("redoDocument");
        break;
      case 43:
        capture("sets-redo-edit");
        action("guiSkewReference");
        break;
      case 44:
        capture("sets-reference");
        action("guiSkewSave");
        break;
      case 45:
        capture("sets-saved");
        action("guiSkewOpen");
        break;
      case 46: {
        capture("sets-opened");
        timer.stop();
        QFile f(dir + "/automatic-bake.json");
        if (!f.open(QIODevice::WriteOnly))
          qFatal("Auto bake proof unavailable");
        f.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                          {"captures", captures},
                                          {"max_ping_ms", max_ping},
                                          {"drag_pings", drag_pings},
                                          {"timings", performance::snapshot()}})
                    .toJson());
        f.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  Demo(QWidget *window,
       QWidget *v,
       QString directory,
       QString path,
       int resolution,
       std::function<QJsonObject()> get_state)
      : QObject(window),
        w(window),
        viewport(v),
        dir(directory),
        mesh(path),
        size(resolution),
        state(std::move(get_state))
  {
    QDir().mkpath(dir);
    timer.setObjectName("autoDemoAdvance");
    input.setObjectName("autoDemoInput");
    ping.setObjectName("autoDemoPing");
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    input.setInterval(10);
    input.setTimerType(Qt::PreciseTimer);
    connect(&input, &QTimer::timeout, this, [this] {
      performance::Scope timing("auto_demo_input_tick");
      if (++ticks > 100) {
        input.stop();
        ping.stop();
        drag_pings.append(QJsonObject{{"distance", input.property("distance").toBool()},
                                      {"max_ping_ms", drag_ping}});
        performance::begin("idle");
        dragging = false;
        return;
      }
      if (input.property("distance").toBool())
        get<QDoubleSpinBox>("skewFrontalDistance")->setValue(.1 + ticks * .002);
      else
        get<QSpinBox>("bakeMapRays")->setValue(8 + ticks * 88 / 100);
    });
    ping.setInterval(10);
    ping.setTimerType(Qt::PreciseTimer);
    connect(&ping, &QTimer::timeout, this, [this] {
      const double elapsed = heartbeat.restart();
      max_ping = std::max(max_ping, elapsed);
      drag_ping = std::max(drag_ping, elapsed);
    });
    timer.start();
  }
};
}  // namespace
void start_auto_bake_demo(QWidget *w,
                          QWidget *v,
                          const QString &dir,
                          const QString &mesh,
                          int size,
                          std::function<QJsonObject()> state)
{
  new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
