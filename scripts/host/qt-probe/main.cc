#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>
#include <cstdio>

class Surface : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  QString renderer;
  int frames = 0;
  using QOpenGLWidget::QOpenGLWidget;
 protected:
  void initializeGL() override {
    initializeOpenGLFunctions();
    renderer = QString::fromLatin1(reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    std::printf("QT_GPU_PROBE renderer=%s qt=%s\n", renderer.toUtf8().constData(), qVersion());
    std::fflush(stdout);
  }
  void paintGL() override {
    glClearColor(0.12f, 0.16f, 0.22f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(50, 50, 250, 160);
    glClearColor(0.8f, 0.3f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    ++frames;
  }
};

int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setVersion(4, 3);
  format.setAlphaBufferSize(8);
  format.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(format);
  QApplication app(argc, argv);
  if (argc != 2 || !QDir().mkpath(QString::fromLocal8Bit(argv[1]))) {
    std::fprintf(stderr, "Usage: qt-gpu-probe OUTPUT_DIRECTORY\n");
    return 1;
  }
  const QString output_dir = QString::fromLocal8Bit(argv[1]);
  QMainWindow window;
  // ARGB captures the composed native window, avoiding a root-window read on XWayland.
  window.setAttribute(Qt::WA_TranslucentBackground);
  window.setAutoFillBackground(true);
  window.setWindowTitle("Qt 6.8 GPU environment probe");
  auto *surface = new Surface(&window);
  window.setCentralWidget(surface);
  window.resize(640, 400);
  window.show();
  QTimer::singleShot(1500, &window, [&] { window.resize(800, 480); });
  QTimer::singleShot(3000, &window, [&] {
    const QImage rendered = surface->grabFramebuffer();
    const QImage presented = window.screen()->grabWindow(window.winId()).toImage();
    if (!rendered.save(output_dir + "/framebuffer.png") ||
        !presented.save(output_dir + "/window.png")) {
      app.exit(2);
      return;
    }
    QFile output(output_dir + "/result.json");
    if (!output.open(QIODevice::WriteOnly)) {
      app.exit(2);
      return;
    }
    output.write(QJsonDocument(QJsonObject{{"qt", qVersion()}, {"renderer", surface->renderer},
      {"frames", surface->frames}, {"pid", app.applicationPid()},
      {"platform", app.platformName()}, {"exposed", window.windowHandle()->isExposed()},
      {"capture", "composited native ARGB window"},
      {"scope", "Qt GPU environment only; no Blender"}}).toJson());
    app.exit(surface->renderer.contains("3090") && surface->frames >= 2 ? 0 : 3);
  });
  return app.exec();
}
