#pragma once
#include "UvView.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <functional>
#include <utility>
namespace bts {
// Headless viewport strokes at 4K and 8K, then an offscreen check that a UV
// edit moves the preview embedded in the interactive viewport.
class AdStrokeBench final : public QObject {
  QWidget *workspace_, *viewport_;
  QString directory_, mesh_;
  std::function<QJsonObject()> state_;
  QTimer timer_;
  int stage_ = 0, stroke_ = 0, tool_ = 0, resolution_;
  static constexpr int strokes_per_tool = 200;
  const char *tools_[4] = {"paint", "smudge", "clone", "erase"};
  template<class T> T *get(const char *name)
  {
    auto *value = workspace_->findChild<T *>(name);
    if (!value)
      qFatal("AD bench missing %s", name);
    return value;
  }
  bool pending()
  {
    const auto state = state_();
    return state["composite_pending"].toBool() || state["property_pending"].toBool() ||
           state["texture_loading"].toBool();
  }
  void mouse(QEvent::Type type, QPointF point)
  {
    QMouseEvent event(type,
                      point,
                      viewport_->mapToGlobal(point.toPoint()),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(viewport_, &event);
  }
  void key(QEvent::Type type)
  {
    QKeyEvent event(type, Qt::Key_V, Qt::NoModifier);
    QCoreApplication::sendEvent(viewport_, &event);
  }
  void stroke_at(int index)
  {
    const QPointF origin(viewport_->width() * 0.42,
                         viewport_->height() * (0.35 + 0.02 * (index % 8)));
    const QPointF delta(36, 8 * ((index % 3) - 1));
    mouse(QEvent::MouseButtonPress, origin);
    for (int step = 1; step <= 4; ++step)
      mouse(QEvent::MouseMove, origin + delta * (step / 4.0));
    mouse(QEvent::MouseButtonRelease, origin + delta);
  }
  void select_tool()
  {
    const char *action = tool_ == 0 ? "selectPaintTool" :
                         tool_ == 1 ? "selectSmudgeTool" :
                         tool_ == 2 ? "selectCloneTool" :
                                      "selectEraserTool";
    get<QAction>(action)->trigger();
    if (tool_ == 2) {
      viewport_->setFocus();
      key(QEvent::KeyPress);
      const QPointF source(viewport_->width() * 0.42, viewport_->height() * 0.35);
      mouse(QEvent::MouseButtonPress, source);
      mouse(QEvent::MouseButtonRelease, source);
      key(QEvent::KeyRelease);
    }
  }
  std::pair<double, int> centroid(const QImage &image)
  {
    double x = 0;
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
      for (int px = 0; px < image.width(); ++px) {
        const QRgb pixel = image.pixel(px, y);
        if (qRed(pixel) > 160 && qGreen(pixel) < 100 && qBlue(pixel) < 90) {
          x += px;
          ++count;
        }
      }
    return {count ? x / count : 0, count};
  }
  void check_uv_edit()
  {
    QDir().mkpath(directory_);
    const QString capture = directory_ + "/uv-preview-viewport.png";
    workspace_->setProperty("guiCapturePath", capture);
    get<QAction>("guiCaptureViewport")->trigger();
    QFile::remove(directory_ + "/viewport-overlay.png");
    QFile::copy(capture, directory_ + "/viewport-overlay.png");
    QImage integrated(capture);
    int marker = 0;
    for (int y = 0; y < integrated.height(); ++y)
      for (int x = 0; x < integrated.width(); ++x) {
        const QRgb pixel = integrated.pixel(x, y);
        if (qRed(pixel) > 210 && qGreen(pixel) > 40 && qGreen(pixel) < 80 && qBlue(pixel) > 100 &&
            qBlue(pixel) < 145)
          ++marker;
      }
    auto *preview = static_cast<UvView *>(viewport_->findChild<QWidget *>("uvView"));
    if (!preview || preview->triangles().empty() || integrated.isNull() || marker < 4)
      qFatal("UV preview is not painted in the viewport (triangles=%d marker=%d image=%dx%d)",
             preview ? int(preview->triangles().size()) : -1,
             marker,
             integrated.width(),
             integrated.height());
    const auto original = preview->triangles();
    preview->set_camera({0.5, 0.5}, 180);
    QImage before(320, 320, QImage::Format_ARGB32_Premultiplied);
    before.fill(Qt::black);
    {
      QPainter painter(&before);
      preview->paint_into(painter, before.rect(), false);
    }
    auto edited = original;
    for (auto &triangle : edited)
      for (auto &uv : triangle.uv)
        uv.rx() += 0.45;
    if (!preview->set_triangles(edited))
      qFatal("UV edit rejected");
    QImage after(320, 320, QImage::Format_ARGB32_Premultiplied);
    after.fill(Qt::black);
    {
      QPainter painter(&after);
      preview->paint_into(painter, after.rect(), false);
    }
    preview->set_triangles(original);
    const auto [before_x, before_n] = centroid(before);
    const auto [after_x, after_n] = centroid(after);
    before.save(directory_ + "/uv-edit-before.png");
    after.save(directory_ + "/uv-edit-after.png");
    if (before_n < 10 || after_n < 10 || after_x < before_x + 15)
      qFatal("UV edit did not move the viewport preview");
    QJsonObject proof{{"integrated_marker_pixels", marker},
                      {"triangles", int(original.size())},
                      {"before_centroid_x", before_x},
                      {"after_centroid_x", after_x},
                      {"before_red_pixels", before_n},
                      {"after_red_pixels", after_n}};
    QFile file(directory_ + "/uv-preview-edit.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("UV proof output");
    file.write(QJsonDocument(proof).toJson());
    std::printf("BTS_UV_EDIT marker=%d centroid %.2f -> %.2f triangles=%d\n",
                marker,
                before_x,
                after_x,
                int(original.size()));
    std::fflush(stdout);
  }
  void advance()
  {
    if (timer_.property("elapsed").isNull())
      timer_.setProperty("elapsed", 0);
    switch (stage_) {
      case 0:
        get<QAction>("newProject")->trigger();
        stage_ = 1;
        return;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh_);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        const int index = size->findData(resolution_);
        if (index < 0)
          qFatal("AD bench resolution %d is missing", resolution_);
        size->setCurrentIndex(index);
        dialog->findChild<QPushButton *>("createProject")->click();
        stage_ = 2;
        return;
      }
      case 2:
        if (!state_()["has_document"].toBool() || pending())
          return;
        get<QAction>("guiAdPrepare")->trigger();
        get<QToolButton>("addPaintLayer")->click();
        stage_ = 3;
        return;
      case 3:
        if (pending())
          return;
        get<QDoubleSpinBox>("brushSize")->setValue(12);
        get<QDoubleSpinBox>("brushSpacing")->setValue(40);
        get<QAction>("guiAdPrepare")->trigger();
        select_tool();
        stroke_ = 0;
        stage_ = 4;
        return;
      case 4: {
        if (stroke_ % 25 == 0) {
          std::printf("BTS_AD_STROKE_PROGRESS resolution=%d tool=%s stroke=%d\n",
                      resolution_,
                      tools_[tool_],
                      stroke_);
          std::fflush(stdout);
        }
        stroke_at(stroke_ + tool_ * 3);
        const auto limit = qgetenv("BTS_AD_STROKE_LIMIT");
        bool limit_ok = false;
        const int capped = limit.toInt(&limit_ok);
        const int stroke_count = limit_ok && capped > 0 ? capped : strokes_per_tool;
        if (++stroke_ < stroke_count)
          return;
        if (++tool_ < 4) {
          select_tool();
          stroke_ = 0;
          return;
        }
        stage_ = 6;
        return;
      }
      case 6:
        // Deferred texture flushes can stay pending after the last stroke. The preview was already
        // painted during the strokes.
        std::printf("BTS_AD_UV_CHECK resolution=%d\n", resolution_);
        std::fflush(stdout);
        viewport_->repaint();
        check_uv_edit();
        timer_.stop();
        QTimer::singleShot(50, qApp, &QCoreApplication::quit);
        return;
    }
  }

 public:
  AdStrokeBench(QWidget *workspace,
                QWidget *viewport,
                const QString &directory,
                const QString &mesh,
                int resolution,
                std::function<QJsonObject()> state)
      : QObject(workspace),
        workspace_(workspace),
        viewport_(viewport),
        directory_(directory),
        mesh_(mesh),
        state_(std::move(state)),
        resolution_(resolution)
  {
    timer_.setInterval(16);
    QObject::connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start();
  }
};
}  // namespace bts
