#pragma once

#include <array>
#include <vector>

#include <QColor>
#include <QPointF>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace bts {

struct UvTriangle {
  std::array<QPointF, 3> uv;
  // Display-encoded color supplied by the caller, not a scene-linear Blender socket value.
  // An invalid QColor means the base color is unavailable: draw edges without inventing a fill.
  QColor base_color;
  // Edge i joins uv[i] to uv[(i + 1) % 3]. Hide tessellation diagonals in the snapshot adapter.
  std::array<bool, 3> visible_edges = {true, true, true};
};

// Diagnostic UV/Base Color fallback. The live viewport uses UvPreview GPU images
// and UvContourOverlay; constant fallback colors never evaluate the layer stack.
// All methods run on the GUI thread. No Blender objects or borrowed geometry are retained.
class UvView final : public QWidget {
 public:
  explicit UvView(QWidget *parent = nullptr);

  // Owns a copy when passed an lvalue, or takes ownership of an rvalue. Does not change framing.
  // Rejects non-finite coordinates or an overflowing aggregate extent, preserving previous data.
  bool set_triangles(std::vector<UvTriangle> triangles);
  const std::vector<UvTriangle> &triangles() const
  {
    return triangles_;
  }

  void frame_all();
  void reset_view();
  void set_camera(QPointF center, double pixels_per_uv);
  // Draws this preview into an existing viewport pane. keep_underlay leaves the
  // GPU material image unchanged and only adds contours, axes and labels.
  void paint_into(QPainter &painter, const QRectF &target, bool keep_underlay = false) const;
  QPointF view_center() const
  {
    return center_;
  }
  double pixels_per_uv() const
  {
    return pixels_per_uv_;
  }
  QPointF uv_to_widget(const QPointF &uv) const;
  QPointF widget_to_uv(const QPointF &point) const;
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

 private:
  std::vector<UvTriangle> triangles_;
  QPointF center_ = {0.5, 0.5};
  double pixels_per_uv_ = 256.0;
  bool initial_frame_pending_ = true;
  Qt::MouseButton pan_button_ = Qt::NoButton;
  QPointF last_pointer_;
};

}  // namespace bts
