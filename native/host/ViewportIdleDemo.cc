#include "ViewportIdleDemo.hh"
#include "Performance.hh"
#include <QApplication>
#include <cmath>
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QListWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class IdleDemo final : public QObject {
  QMainWindow *window;
  QOpenGLWidget *viewport = nullptr;
  std::function<bool()> busy;
  QTimer poll;
  int phase = 0, paints = 0, steps = 0, tries = 0;
  double idle_cpu = 0;
  performance::Clock::time_point idle_wall{};
  QJsonObject result;
  bool eventFilter(QObject *watched, QEvent *event) override
  {
    if (watched == viewport && event->type() == QEvent::Paint)
      ++paints;
    return false;
  }
  void mouse(QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons buttons)
  {
    QMouseEvent event(
        type, point, viewport->mapToGlobal(point.toPoint()), button, buttons, Qt::AltModifier);
    QCoreApplication::sendEvent(viewport, &event);
  }
  void advance()
  {
    if (++tries > 1800)
      qFatal("Idle demo timed out phase %d", phase);
    if (!viewport) {
      viewport = window->findChild<QOpenGLWidget *>();
      if (!viewport)
        return;
      viewport->installEventFilter(this);
    }
    if (!viewport->isValid() || busy())
      return;
    if (phase == 0) {
      auto *sets = window->findChild<QListWidget *>("textureSets");
      if (!sets || sets->count() == 0)
        return;
      phase = 1;
      QTimer::singleShot(2000, this, [this] {
        poll.stop();
        phase = 2;
        paints = 0;
        idle_cpu = performance::thread_ms();
        idle_wall = performance::Clock::now();
        QTimer::singleShot(10000, this, [this] {
          result["idle_wall_ms"] = performance::ms(idle_wall);
          result["idle_ui_cpu_percent"] = 100 * (performance::thread_ms() - idle_cpu) /
                                          performance::ms(idle_wall);
          result["idle_paints"] = paints;
          result["idle_target_pass"] = result["idle_ui_cpu_percent"].toDouble() < 5 && paints <= 2;
          if (auto *sym = window->findChild<QToolButton *>("symmetryToggle"))
            sym->setChecked(true);
          if (auto *cut = window->findChild<QCheckBox *>("symmetryIntersection"))
            cut->setChecked(true);
          mouse(
              QEvent::MouseButtonPress, viewport->rect().center(), Qt::LeftButton, Qt::LeftButton);
          phase = 3;
          poll.start(20);
        });
      });
      return;
    }
    if (phase == 3) {
      const auto center = QPointF(viewport->rect().center());
      mouse(QEvent::MouseMove,
            center + QPointF(steps * 2, 20 * std::sin(steps * .08)),
            Qt::NoButton,
            Qt::LeftButton);
      if (++steps < 90)
        return;
      mouse(QEvent::MouseButtonRelease,
            center + QPointF(178, 20 * std::sin(89 * .08)),
            Qt::LeftButton,
            Qt::NoButton);
      phase = 4;
      poll.stop();
      QTimer::singleShot(500, this, [this] {
        result["performance"] = performance::snapshot();
        result["renderer_scope"] =
            "full Qt workspace on private Xvfb (software OpenGL), no artist display/input";
        const auto directory = qEnvironmentVariable("BTS_IDLE_DEMO_DIR");
        QDir().mkpath(directory);
        QFile file(directory + "/idle.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Idle proof unavailable");
        file.write(QJsonDocument(result).toJson());
        viewport->grabFramebuffer().save(directory + "/frame.png");
        std::printf("BTS_IDLE_DEMO cpu_percent=%.3f paints=%d\n",
                    result["idle_ui_cpu_percent"].toDouble(),
                    result["idle_paints"].toInt());
        QCoreApplication::quit();
      });
    }
  }

 public:
  IdleDemo(QMainWindow *w, std::function<bool()> b) : QObject(w), window(w), busy(std::move(b))
  {
    qunsetenv("BTS_TEST_CONTINUOUS_FRAMES");
    connect(&poll, &QTimer::timeout, this, [this] { advance(); });
    poll.start(50);
  }
};
}  // namespace
void start_viewport_idle_demo(QMainWindow *window, std::function<bool()> busy)
{
  new IdleDemo(window, std::move(busy));
}
}  // namespace bts
