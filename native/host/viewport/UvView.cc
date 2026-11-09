// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "UvView.hh"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QWheelEvent>

namespace bts {
namespace {

constexpr double minimum_scale = 0.01;
constexpr double maximum_scale = 65536.0;
constexpr double frame_padding = 32.0;

QRectF bounds(const std::vector<UvTriangle> &triangles)
{
  if (triangles.empty()) {
    return {};
  }
  QPointF low = triangles.front().uv[0];
  QPointF high = low;
  for (const UvTriangle &triangle : triangles) {
    for (const QPointF &uv : triangle.uv) {
      low.setX(std::min(low.x(), uv.x()));
      low.setY(std::min(low.y(), uv.y()));
      high.setX(std::max(high.x(), uv.x()));
      high.setY(std::max(high.y(), uv.y()));
    }
  }
  return QRectF(low, high);
}

// Clip before mapping into pixels: the raster paint engine cannot reliably draw huge coordinates
// when a UV island extends far outside the window at high zoom.
QPolygonF clip_polygon(QPolygonF polygon, const QRectF &rect)
{
  for (int plane = 0; plane < 4 && !polygon.empty(); ++plane) {
    const auto distance = [&rect, plane](const QPointF &p) {
      switch (plane) {
        case 0:
          return p.x() - rect.left();
        case 1:
          return rect.right() - p.x();
        case 2:
          return p.y() - rect.top();
        default:
          return rect.bottom() - p.y();
      }
    };
    QPolygonF clipped;
    QPointF previous = polygon.back();
    double previous_distance = distance(previous);
    for (const QPointF &point : polygon) {
      const double current_distance = distance(point);
      if ((previous_distance >= 0) != (current_distance >= 0)) {
        const double fraction = previous_distance / (previous_distance - current_distance);
        clipped.append(previous + fraction * (point - previous));
      }
      if (current_distance >= 0) {
        clipped.append(point);
      }
      previous = point;
      previous_distance = current_distance;
    }
    polygon = std::move(clipped);
  }
  return polygon;
}

bool clip_line(QPointF &a, QPointF &b, const QRectF &rect)
{
  const QPointF delta = b - a;
  double enter = 0;
  double leave = 1;
  for (int axis = 0; axis < 2; ++axis) {
    const double start = axis == 0 ? a.x() : a.y();
    const double direction = axis == 0 ? delta.x() : delta.y();
    const double low = axis == 0 ? rect.left() : rect.top();
    const double high = axis == 0 ? rect.right() : rect.bottom();
    if (direction == 0) {
      if (start < low || start > high) {
        return false;
      }
      continue;
    }
    const double t0 = (low - start) / direction;
    const double t1 = (high - start) / direction;
    enter = std::max(enter, std::min(t0, t1));
    leave = std::min(leave, std::max(t0, t1));
    if (enter > leave) {
      return false;
    }
  }
  b = a + leave * delta;
  a += enter * delta;
  return true;
}

}  // namespace

UvView::UvView(QWidget *parent) : QWidget(parent)
{
  setObjectName("uvView");
  setMinimumSize(120, 120);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAccessibleName("UV / Base Color");
  setToolTip(
      "Constant base colors; textures and material lighting are unavailable.\n"
      "Middle-drag to pan, wheel to zoom, F to frame UVs.");
}

bool UvView::set_triangles(std::vector<UvTriangle> triangles)
{
  for (const UvTriangle &triangle : triangles) {
    for (const QPointF &uv : triangle.uv) {
      if (!std::isfinite(uv.x()) || !std::isfinite(uv.y())) {
        return false;
      }
    }
  }
  const QRectF extent = bounds(triangles);
  if (!std::isfinite(extent.width()) || !std::isfinite(extent.height())) {
    return false;
  }
  triangles_ = std::move(triangles);
  if (isVisible())
    update();
  return true;
}

void UvView::frame_all()
{
  if (triangles_.empty()) {
    reset_view();
    return;
  }
  const QRectF extent = bounds(triangles_);
  center_ = extent.topLeft() + QPointF(extent.width() * 0.5, extent.height() * 0.5);
  const double available_width = std::max(1.0, width() - 2 * frame_padding);
  const double available_height = std::max(1.0, height() - 2 * frame_padding);
  const double scale = std::min(available_width / std::max(0.001, extent.width()),
                                available_height / std::max(0.001, extent.height()));
  pixels_per_uv_ = std::clamp(scale, minimum_scale, maximum_scale);
  initial_frame_pending_ = false;
  update();
}

void UvView::reset_view()
{
  center_ = {0.5, 0.5};
  pixels_per_uv_ = std::clamp(
      std::min(width(), height()) - 2 * frame_padding, minimum_scale, maximum_scale);
  initial_frame_pending_ = false;
  update();
}

void UvView::set_camera(QPointF center, double pixels_per_uv)
{
  if (!std::isfinite(center.x()) || !std::isfinite(center.y()) || !std::isfinite(pixels_per_uv))
    return;
  center_ = center;
  pixels_per_uv_ = std::clamp(pixels_per_uv, minimum_scale, maximum_scale);
  initial_frame_pending_ = false;
}

QPointF UvView::uv_to_widget(const QPointF &uv) const
{
  return {width() * 0.5 + (uv.x() - center_.x()) * pixels_per_uv_,
          height() * 0.5 - (uv.y() - center_.y()) * pixels_per_uv_};
}

QPointF UvView::widget_to_uv(const QPointF &point) const
{
  return {center_.x() + (point.x() - width() * 0.5) / pixels_per_uv_,
          center_.y() - (point.y() - height() * 0.5) / pixels_per_uv_};
}

QSize UvView::sizeHint() const
{
  return {640, 480};
}

void UvView::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  paint_into(painter, rect(), false);
}

void UvView::paint_into(QPainter &painter, const QRectF &target, bool keep_underlay) const
{
  painter.save();
  painter.translate(target.topLeft());
  const double pane_w = std::max(1.0, target.width());
  const double pane_h = std::max(1.0, target.height());
  const auto to_px = [&](const QPointF &uv) {
    return QPointF(pane_w * 0.5 + (uv.x() - center_.x()) * pixels_per_uv_,
                   pane_h * 0.5 - (uv.y() - center_.y()) * pixels_per_uv_);
  };
  const auto to_uv = [&](const QPointF &point) {
    return QPointF(center_.x() + (point.x() - pane_w * 0.5) / pixels_per_uv_,
                   center_.y() - (point.y() - pane_h * 0.5) / pixels_per_uv_);
  };
  if (!keep_underlay)
    painter.fillRect(QRectF(0, 0, pane_w, pane_h), QColor(bts::theme::Surface));
  const QRectF visible_uv =
      QRectF(to_uv({-2, -2}), to_uv({pane_w + 2.0, pane_h + 2.0})).normalized();

  const auto draw_grid = [&](double step, const QColor &color) {
    const double pitch = step * pixels_per_uv_;
    painter.setPen(QPen(color, 1));
    const double origin_x = pane_w * 0.5 - std::fmod(center_.x(), step) * pixels_per_uv_;
    const double origin_y = pane_h * 0.5 + std::fmod(center_.y(), step) * pixels_per_uv_;
    const double first_x = origin_x - std::floor(origin_x / pitch) * pitch;
    const double first_y = origin_y - std::floor(origin_y / pitch) * pitch;
    for (double x = first_x; x <= pane_w; x += pitch) {
      painter.drawLine(QPointF(x, 0), QPointF(x, pane_h));
    }
    for (double y = first_y; y <= pane_h; y += pitch) {
      painter.drawLine(QPointF(0, y), QPointF(pane_w, y));
    }
  };
  const double minor_step = std::pow(10.0, std::floor(std::log10(80.0 / pixels_per_uv_)));
  if (!keep_underlay) {
    draw_grid(minor_step, QColor(bts::theme::Surface));
    draw_grid(std::max(1.0, minor_step * 10.0), QColor(bts::theme::Hover));
  }

  painter.setPen(Qt::NoPen);
  // Fill without edge antialiasing so adjacent triangles do not expose background hairlines.
  std::size_t available_colors = 0;
  for (const UvTriangle &triangle : triangles_) {
    if (!triangle.base_color.isValid()) {
      continue;
    }
    ++available_colors;
    if (keep_underlay)
      continue;
    QPolygonF polygon({triangle.uv[0], triangle.uv[1], triangle.uv[2]});
    if (!polygon.boundingRect().intersects(visible_uv)) {
      continue;
    }
    polygon = clip_polygon(std::move(polygon), visible_uv);
    for (QPointF &uv : polygon) {
      uv = to_px(uv);
    }
    QColor fill = triangle.base_color;
    painter.setBrush(fill);
    painter.drawConvexPolygon(polygon);
  }

  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(bts::theme::Muted), 1));
  for (const UvTriangle &triangle : triangles_) {
    for (int edge = 0; edge < 3; ++edge) {
      if (triangle.visible_edges[edge]) {
        QPointF a = triangle.uv[edge];
        QPointF b = triangle.uv[(edge + 1) % 3];
        if (clip_line(a, b, visible_uv)) {
          painter.drawLine(to_px(a), to_px(b));
        }
      }
    }
  }

  const QPointF origin = to_px({0, 0});
  if (origin.x() >= 0 && origin.x() <= pane_w) {
    painter.setPen(QPen(QColor(bts::theme::AxisY), 1));
    painter.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), pane_h));
  }
  if (origin.y() >= 0 && origin.y() <= pane_h) {
    painter.setPen(QPen(QColor(bts::theme::AxisX), 1));
    painter.drawLine(QPointF(0, origin.y()), QPointF(pane_w, origin.y()));
  }

  QFont caption_font = font();
  caption_font.setPixelSize(10);
  painter.setFont(caption_font);
  painter.setPen(QColor(bts::theme::Text));
  // Blender's supported UDIM range is 1001..2000, with ten columns. Labels describe coordinates,
  // not the existence of an image tile, and never wrap negative coordinates or U >= 10.
  if (pixels_per_uv_ >= 72 && visible_uv.right() >= 0 && visible_uv.left() < 10 &&
      visible_uv.bottom() >= 0 && visible_uv.top() < 100)
  {
    const int first_u = int(std::clamp(std::floor(visible_uv.left()), 0.0, 9.0));
    const int last_u = int(std::clamp(std::floor(visible_uv.right()), 0.0, 9.0));
    const int first_v = int(std::clamp(std::floor(visible_uv.top()), 0.0, 99.0));
    const int last_v = int(std::clamp(std::floor(visible_uv.bottom()), 0.0, 99.0));
    for (int v = first_v; v <= last_v; ++v) {
      for (int u = first_u; u <= last_u; ++u) {
        const QPointF corner = to_px({double(u), double(v)});
        const QRectF label(corner.x() + 4, corner.y() - 17, 40, 14);
        painter.fillRect(label, bts::theme::alpha(bts::theme::Selection, 210));
        painter.drawText(label, Qt::AlignVCenter, QString::number(1001 + 10 * v + u));
      }
    }
  }

  const QPointF axis_origin(pane_w - 38.0, pane_h - 28.0);
  painter.setPen(QPen(QColor(bts::theme::AxisX), 1.5));
  painter.drawLine(axis_origin, axis_origin + QPointF(20, 0));
  painter.drawText(axis_origin + QPointF(23, 4), "U");
  painter.setPen(QPen(QColor(bts::theme::AxisY), 1.5));
  painter.drawLine(axis_origin, axis_origin + QPointF(0, -20));
  painter.drawText(axis_origin + QPointF(-3, -24), "V");

  const QRect label_rect(int(pane_w) - 126, 8, 118, 22);
  painter.fillRect(label_rect, QColor(bts::theme::Surface));
  painter.setPen(QColor(bts::theme::Text));
  painter.drawText(label_rect, Qt::AlignCenter, "UV / Base Color");
  if (!triangles_.empty())
    painter.fillRect(QRect(8, 36, 8, 8), QColor(bts::theme::Error));
  if (triangles_.empty()) {
    painter.drawText(QRectF(0, 0, pane_w, pane_h), Qt::AlignCenter, "No UV data");
  }
  else {
    const char *caption = available_colors == 0                ? "Base color unavailable" :
                          available_colors < triangles_.size() ? "Some base colors unavailable" :
                                                                 "Constant base colors";
    painter.drawText(
        QRectF(8, pane_h - 22, pane_w - 72, 16), Qt::AlignVCenter, QString::fromLatin1(caption));
  }
  painter.restore();
}

void UvView::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  if (initial_frame_pending_) {
    reset_view();
  }
  // Thereafter preserve pixels per UV unit: widening the pane reveals more tiles, as in PD-117.
}

void UvView::wheelEvent(QWheelEvent *event)
{
  const double delta = event->pixelDelta().isNull() ? double(event->angleDelta().y()) :
                                                      double(event->pixelDelta().y()) * 2.0;
  if (delta == 0) {
    event->ignore();
    return;
  }
  const QPointF anchor = widget_to_uv(event->position());
  const double factor = std::exp(std::clamp(delta, -600.0, 600.0) * 0.0015);
  pixels_per_uv_ = std::clamp(pixels_per_uv_ * factor, minimum_scale, maximum_scale);
  center_ += anchor - widget_to_uv(event->position());
  initial_frame_pending_ = false;
  setFocus(Qt::MouseFocusReason);
  update();
  event->accept();
}

void UvView::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::MiddleButton) {
    pan_button_ = Qt::MiddleButton;
    last_pointer_ = event->position();
    setCursor(Qt::ClosedHandCursor);
    setFocus(Qt::MouseFocusReason);
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void UvView::mouseMoveEvent(QMouseEvent *event)
{
  if (pan_button_ != Qt::NoButton && event->buttons().testFlag(pan_button_)) {
    const QPointF delta = event->position() - last_pointer_;
    center_ += QPointF(-delta.x(), delta.y()) / pixels_per_uv_;
    last_pointer_ = event->position();
    initial_frame_pending_ = false;
    update();
    event->accept();
    return;
  }
  pan_button_ = Qt::NoButton;
  unsetCursor();
  QWidget::mouseMoveEvent(event);
}

void UvView::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == pan_button_) {
    pan_button_ = Qt::NoButton;
    unsetCursor();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void UvView::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier) {
    frame_all();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

}  // namespace bts
