#include "StrokeTargetPerformanceDemo.hh"
#include "Performance.hh"
#include "GpuPerformance.hh"
#include "ViewportDemoCapture.hh"
#include <cmath>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString directory, mesh;
  int resolution, stage = 0, case_index = 0, step = 0, stroke = 0, ticks = 0, frame = 0;
  QTimer timer, ping, dialogs;
  QElapsedTimer heartbeat;
  double max_ping = 0;
  std::function<QJsonObject()> state;
  QJsonArray cases, calls;
  std::vector<performance::Record> history;
  QJsonObject transfers, before;
  QUuid layer;
  bool busy = false;
  const bool smudge = qEnvironmentVariableIsSet("BTS_STROKE_TARGET_SMUDGE");
  const bool filters = smudge || qEnvironmentVariableIsSet("BTS_STROKE_PROFILE_PROJECT");
  QPointF origin;
  bool mask() const
  {
    return case_index % 4 >= 2;
  }
  bool scene() const
  {
    return case_index >= 4;
  }
  int tile() const
  {
    return 1001 + case_index % 2;
  }
  QString label() const
  {
    return QString("%1_%2_%3")
        .arg(mask() ? (smudge ? "mask_smudge" : "mask") : (smudge ? "smudge" : "paint"),
             scene() ? "scene" : "uv")
        .arg(tile());
  }
  template<class T> T *get(const char *name)
  {
    auto *x = w->findChild<T *>(name);
    if (!x)
      qFatal("Stroke profile control missing %s", name);
    return x;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void select_layer()
  {
    auto *list = get<QListWidget>("layerStack");
    for (int i = 0; i < list->count(); ++i)
      if (QUuid(list->item(i)->data(Qt::UserRole).toString()) == layer &&
          list->item(i)->data(Qt::UserRole + 10).toString().isEmpty())
      {
        list->setCurrentRow(i);
        return;
      }
    qFatal("Profile paint layer missing");
  }
  QJsonObject transfer_delta()
  {
    auto result = gpu_transfer_snapshot();
    for (auto i = result.begin(); i != result.end(); ++i) {
      auto a = i.value().toObject(), b = transfers[i.key()].toObject();
      for (auto n = a.begin(); n != a.end(); ++n)
        n.value() = n.value().toDouble() - b[n.key()].toDouble();
      i.value() = a;
    }
    return result;
  }
  void mouse(QEvent::Type type, QPointF p)
  {
    const auto start = performance::Clock::now();
    const double cpu = performance::thread_ms();
    QMouseEvent e(type,
                  p,
                  v->mapToGlobal(p.toPoint()),
                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                  Qt::NoModifier);
    QCoreApplication::sendEvent(v, &e);
    if (stage == 5)
      calls.append(QJsonObject{{"wall_ms", performance::ms(start)},
                               {"cpu_ms", performance::thread_ms() - cpu}});
  }
  void warm()
  {
    if (filters) {
      mouse(QEvent::MouseButtonPress, origin - QPointF(0, 18));
      for (int i = 1; i <= 12; ++i)
        mouse(QEvent::MouseMove, origin + QPointF(0, -18 + 3 * i));
      mouse(QEvent::MouseButtonRelease, origin + QPointF(0, 18));
      return;
    }
    mouse(QEvent::MouseButtonPress, origin - QPointF(12, 0));
    for (int i = 1; i <= 8; ++i)
      mouse(QEvent::MouseMove, origin + QPointF(-12 + 3 * i, 0));
    mouse(QEvent::MouseButtonRelease, origin + QPointF(12, 0));
  }
  void add_filter()
  {
    action("addFilterEffect");
    for (auto *button : w->findChildren<QToolButton *>("filterResource"))
      if (button->isVisible()) {
        button->menu()->actions().at(1)->trigger();
        return;
      }
    qFatal("Visible profile filter selector missing");
  }
  void setup()
  {
    performance::begin("");
    select_layer();
    if (mask() && !state()["profile_has_mask"].toBool()) {
      action("addWhiteMask");
      if (filters) {
        add_filter();
        stage = 31;
        return;
      }
    }
    configure_and_warm();
  }
  void configure_and_warm()
  {
    w->setProperty("strokeProfileMask", mask());
    w->setProperty("strokeProfileScene", scene());
    w->setProperty("strokeProfileTile", tile());
    action("guiStrokeProfileTarget");
    action("selectPaintTool");
    action("guiStrokeProfileTarget");
    const auto point = state()["profile_point"].toArray();
    origin = {point[0].toDouble(), point[1].toDouble()};
    warm();
    if (smudge) {
      action("selectSmudgeTool");
      action("guiStrokeProfileTarget");
    }
    frame = state()["render_frames"].toInt() + 3;
    stage = 4;
  }
  void begin()
  {
    calls = {};
    before = state();
    if (smudge &&
        (!before["profile_smudge"].toBool() || before["profile_filter_count"].toInt() < 1 ||
         (mask() && before["profile_mask_filter_count"].toInt() < 1)))
      qFatal("Smudge profile source invalid: %s",
             QJsonDocument(before).toJson(QJsonDocument::Compact).constData());
    transfers = gpu_transfer_snapshot();
    max_ping = 0;
    heartbeat.restart();
    performance::begin(label().toStdString());
    step = stroke = 0;
    stage = 5;
  }
  void collect()
  {
    std::lock_guard lock(performance::mutex);
    for (auto &r : performance::records)
      if (!r.flow.empty() &&
          (r.phase == "stroke_geometry_dab_cpu" || r.phase == "stroke_raster_dab_cpu" ||
           r.phase == "BTS_STROKE_MS" || r.phase == "draw" || r.phase == "gpu_render" ||
           r.phase == "upload_staging_images" || r.phase == "gpu_paint_source_upload" ||
           r.phase == "gpu_source_allocate_or_find"))
        history.push_back(std::move(r));
    performance::records.clear();
  }
  QJsonArray phases()
  {
    collect();
    QJsonArray out;
    for (const auto &r : history)
      out.append(QJsonObject{{"flow", QString::fromStdString(r.flow)},
                             {"phase", QString::fromStdString(r.phase)},
                             {"wall_ms", r.wall},
                             {"cpu_ms", r.cpu},
                             {"ui", r.ui}});
    return out;
  }
  void complete()
  {
    auto after = state();
    if (after["undo_count"].toInt() != before["undo_count"].toInt() + 3)
      qFatal("Stroke profile expected three changed strokes case=%s", qPrintable(label()));
    performance::begin("");
    cases.append(QJsonObject{{"label", label()},
                             {"mask", mask()},
                             {"scene", scene()},
                             {"tile", tile()},
                             {"calls", calls},
                             {"max_ping_ms", max_ping},
                             {"transfers", transfer_delta()},
                             {"before", before},
                             {"after", after}});
    std::printf("BTS_STROKE_PROFILE case=%s max_ping_ms=%.3f\n", qPrintable(label()), max_ping);
    std::fflush(stdout);
    if (++case_index == 8) {
      stage = 8;
      return;
    }
    stage = 3;
  }
  void finish()
  {
    timer.stop();
    ping.stop();
    performance::begin("");
    action("checkGuiGpuStack");
    const auto final = state();
    capture_demo_window(w, v).save(directory + "/stroke-targets.png");
    QFile f(directory + "/stroke-target-performance.json");
    if (!f.open(QIODevice::WriteOnly))
      qFatal("Stroke profile proof unavailable");
    f.write(QJsonDocument(QJsonObject{{"resolution", resolution},
                                      {"platform", QGuiApplication::platformName()},
                                      {"cases", cases},
                                      {"phases", phases()},
                                      {"final", final}})
                .toJson());
    f.close();
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
  }
  void advance()
  {
    if (busy)
      return;
    collect();
    if (++ticks > 4000)
      qFatal("Stroke profile timeout stage=%d case=%d", stage, case_index);
    const auto data = state();
    switch (stage) {
      case 0:
        if (qEnvironmentVariableIsSet("BTS_STROKE_PROFILE_PROJECT")) {
          if (!data["profile_tiles"].isArray() || data["composite_pending"].toBool())
            return;
          auto *add = get<QToolButton>("addPaintLayer");
          if (!add->isEnabled())
            return;
          add->click();
          stage = 2;
        }
        else {
          action("newProject");
          stage = 1;
        }
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QCheckBox>("preserveUvTiles")->setChecked(true);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(resolution));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        stage = 2;
        break;
      case 2:
        if (data["composite_pending"].toBool())
          return;
        if (!get<QListWidget>("layerStack")->currentItem())
          qFatal("Profile layer unavailable after project startup");
        layer = QUuid(
            get<QListWidget>("layerStack")->currentItem()->data(Qt::UserRole).toString());
        action("groupLayers");
        select_layer();
        if (filters) {
          add_filter();
          stage = 30;
        }
        else
          stage = 3;
        break;
      case 3:
        if (data["composite_pending"].toBool())
          return;
        setup();
        break;
      case 30:
        if (data["property_pending"].toBool() || data["composite_pending"].toBool())
          return;
        select_layer();
        stage = 3;
        break;
      case 31:
        if (data["property_pending"].toBool() || data["composite_pending"].toBool())
          return;
        configure_and_warm();
        break;
      case 4:
        if (data["composite_pending"].toBool() || data["render_frames"].toInt() < frame)
          return;
        begin();
        break;
      case 5: {
        const auto p = origin + QPointF(-12 + 1.2 * std::min(step, 20),
                                        3 * std::sin(step * .3 + stroke * .2));
        mouse(step == 0  ? QEvent::MouseButtonPress :
              step <= 20 ? QEvent::MouseMove :
                           QEvent::MouseButtonRelease,
              p);
        if (++step > 21) {
          step = 0;
          if (++stroke == 3) {
            frame = state()["render_frames"].toInt() + 3;
            stage = 6;
          }
        }
        break;
      }
      case 6:
        if (data["composite_pending"].toBool() || data["render_frames"].toInt() < frame)
          return;
        complete();
        break;
      case 8:
        finish();
        break;
    }
  }

 public:
  Demo(QWidget *window,
       QWidget *viewport,
       QString out,
       QString source,
       int size,
       std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        directory(out),
        mesh(source),
        resolution(size),
        state(std::move(snapshot))
  {
    timer.setInterval(16);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
    ping.setInterval(10);
    ping.setTimerType(Qt::PreciseTimer);
    heartbeat.start();
    connect(&ping, &QTimer::timeout, this, [this] {
      const auto elapsed = heartbeat.nsecsElapsed() / 1e6;
      heartbeat.restart();
      if (stage == 5 || stage == 6)
        max_ping = std::max(max_ping, elapsed);
    });
    ping.start();
    connect(&dialogs, &QTimer::timeout, this, [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget);
            box && box->isVisible() && box->icon() == QMessageBox::Critical)
          qFatal("Stroke profile error: %s", qPrintable(box->text()));
    });
    dialogs.start(50);
  }
};
}  // namespace
void start_stroke_target_performance_demo(QWidget *w,
                                          QWidget *v,
                                          const QString &directory,
                                          const QString &mesh,
                                          int resolution,
                                          std::function<QJsonObject()> state)
{
  new Demo(w, v, directory, mesh, resolution, std::move(state));
}
}  // namespace bts
