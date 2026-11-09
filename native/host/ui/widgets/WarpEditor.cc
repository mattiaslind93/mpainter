// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "WarpEditor.hh"
#include "WarpProjection.hh"
#include "ColorPicker.hh"
#include <QKeyEvent>
#include <QJsonArray>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QQuaternion>
namespace bts {
namespace {
WarpVector world(const QMatrix4x4 &matrix, const WarpVector &value)
{
  const auto p = (matrix * QVector4D(value[0], value[1], value[2], 1)).toVector3DAffine();
  return {p.x(), p.y(), p.z()};
}
WarpVector local(const QMatrix4x4 &matrix, const WarpVector &value)
{
  return world(matrix.inverted(), value);
}
struct Gizmo {
  WarpVector center{};
  std::array<WarpVector, 3> axes{};
  QPointF origin;
  std::array<QPointF, 3> tips{};
  std::array<double, 3> lengths{};
  bool active = false;
};
Gizmo gizmo(const FillProjection &p,
            const WarpViewContext &view,
            const std::vector<int> &selection,
            bool local_space)
{
  Gizmo result;
  if (p.warp_edit && selection.empty())
    return result;
  const auto grid = fill_warp_grid(p);
  WarpVector center = grid->evaluate(.5, .5).position;
  if (p.warp_edit) {
    center = {};
    for (int id : selection)
      center = warp_add(center, grid->nodes.at(id).position, 1. / selection.size());
  }
  result.center = world(view.matrix, center);
  result.origin = view.screen(result.center);
  for (int axis = 0; axis < 3; ++axis) {
    WarpVector direction{};
    direction[axis] = 1;
    if (local_space || (!p.warp_edit && p.gizmo == 2)) {
      const auto column = view.matrix.column(axis).toVector3D().normalized();
      direction = {column.x(), column.y(), column.z()};
    }
    result.axes[axis] = direction;
    double length = std::max(1e-6, view.extent * .1 * p.gizmo_size / .4);
    auto tip = view.screen(warp_add(result.center, direction, length));
    const double pixels = QLineF(result.origin, tip).length();
    if (pixels > 1)
      length *= 65 / pixels;
    result.lengths[axis] = length;
    result.tips[axis] = view.screen(warp_add(result.center, direction, length));
  }
  result.active = true;
  return result;
}
double dot2(QPointF a, QPointF b)
{
  return a.x() * b.x() + a.y() * b.y();
}
double span(const WarpGrid &grid, int vertex, bool vertical, int side)
{
  const int column = vertex % grid.width(), row = vertex / grid.width();
  const auto &knots = vertical ? grid.rows : grid.columns;
  const int index = vertical ? row : column,
            next = std::clamp(index + side, 0, int(knots.size()) - 1);
  return next != index ?
             (knots[next] - knots[index]) / 3 :
             side * (index ? knots[index] - knots[index - 1] : knots[1] - knots[0]) / 3;
}
}  // namespace
void WarpEditor::notify_selection()
{
  if (selection_changed)
    selection_changed(selected_);
}
void WarpEditor::set_selection(std::vector<int> selected)
{
  selected_ = std::move(selected);
  notify_selection();
}
void WarpEditor::synchronize(const std::optional<FillProjection> &projection,
                             const QString &target)
{
  if (!projection || projection->mode != 5 || target_ != target) {
    drag_.reset();
    selected_.clear();
    split_mode_ = 0;
    hover_.reset();
    previous_.reset();
    target_ = target;
    notify_selection();
  }
  if (!projection || projection->mode != 5)
    return;
  const auto grid = fill_warp_grid(*projection);
  if (previous_ && (previous_->columns != grid->columns || previous_->rows != grid->rows)) {
    std::vector<int> mapped;
    for (int id : selected_)
      if (id >= 0 && id < int(previous_->nodes.size())) {
        const double u = previous_->columns[id % previous_->width()],
                     v = previous_->rows[id / previous_->width()];
        for (int j = 0; j < grid->height(); ++j)
          for (int i = 0; i < grid->width(); ++i)
            if (std::abs(grid->columns[i] - u) < 1e-6 && std::abs(grid->rows[j] - v) < 1e-6)
              mapped.push_back(j * grid->width() + i);
      }
    selected_ = std::move(mapped);
    notify_selection();
  }
  previous_ = grid;
  std::erase_if(selected_, [&](int id) { return id < 0 || id >= int(grid->nodes.size()); });
}
WarpEditor::Hit WarpEditor::hit(QPointF screen,
                                const FillProjection &p,
                                const WarpViewContext &view) const
{
  const auto grid = fill_warp_grid(p);
  double nearest = std::max(7., p.warp_handle_size + 2);
  Hit result;
  if (!grid->automatic_tangents)
    for (int id : selected_)
      if (id >= 0 && id < int(grid->nodes.size()))
        for (int axis = 0; axis < 2; ++axis)
          for (int side : {-1, 1}) {
            const double factor = span(*grid, id, axis == 1, side);
            const auto &node = grid->nodes[id];
            const auto handle = warp_add(node.position, axis == 0 ? node.du : node.dv, factor);
            const double distance =
                QLineF(screen, view.screen(world(view.matrix, handle))).length();
            if (distance < nearest) {
              nearest = distance;
              result = {.vertex = id, .tangent = axis, .factor = factor};
            }
          }
  for (int id = 0; id < int(grid->nodes.size()); ++id) {
    const double distance =
        QLineF(screen, view.screen(world(view.matrix, grid->nodes[id].position))).length();
    if (distance < nearest) {
      nearest = distance;
      result = {.vertex = id};
    }
  }
  return result;
}
std::optional<WarpCoordinate> WarpEditor::coordinate(QPointF point,
                                                     const FillProjection &p,
                                                     const WarpViewContext &view) const
{
  std::optional<WarpVector> position;
  if (view.surface)
    position = view.surface(point);
  if (!position && view.camera_plane)
    position = view.camera_plane(point,
                                 world(view.matrix, fill_warp_grid(p)->evaluate(.5, .5).position));
  if (!position)
    return {};
  WarpVector metric;
  const double depth = view.matrix.column(1).toVector3D().length();
  if (depth < 1e-12)
    return {};
  for (int c = 0; c < 3; ++c)
    metric[c] = std::pow(view.matrix.column(c).toVector3D().length() / depth, 2);
  return fill_warp_grid(p)->project(local(view.matrix, *position), metric);
}
bool WarpEditor::press(QMouseEvent *event, const FillProjection &p, const WarpViewContext &view)
{
  if (p.mode != 5 || !p.manipulator || !view.pane.contains(event->position()) ||
      event->button() != Qt::LeftButton || event->modifiers().testFlag(Qt::AltModifier))
    return false;
  try {
    if (split_mode_) {
      if (const auto uv = coordinate(event->position(), p, view)) {
        auto next = p;
        auto grid = std::make_shared<WarpGrid>(*fill_warp_grid(p));
        if (split_mode_ == 1 || split_mode_ == 3)
          grid->split(true, uv->u);
        if (split_mode_ == 1 || split_mode_ == 2)
          grid->split(false, uv->v);
        next.warp = std::move(grid);
        if (started)
          started();
        if (changed)
          changed(next);
        if (finished)
          finished(false);
      }
      return true;
    }
    const auto handles = gizmo(p, view, selected_, local_space_);
    if (handles.active) {
      int axis = -1;
      double nearest = 8;
      for (int c = 0; c < 3; ++c) {
        const auto direction = handles.tips[c] - handles.origin;
        const double size = dot2(direction, direction);
        if (size < 100)
          continue;
        const double t = std::clamp(
            dot2(event->position() - handles.origin, direction) / size, .2, 1.);
        const double distance = QLineF(event->position(), handles.origin + t * direction).length();
        if (distance < nearest) {
          axis = c;
          nearest = distance;
        }
      }
      if (axis >= 0) {
        Drag drag;
        drag.before = p;
        drag.matrix = view.matrix;
        drag.start = event->position();
        drag.selection = selected_;
        drag.gizmo_axis = axis;
        drag_ = std::move(drag);
        return true;
      }
    }
    if (!p.warp_edit)
      return false;
    const auto found = hit(event->position(), p, view);
    Drag drag;
    drag.before = p;
    drag.matrix = view.matrix;
    drag.start = event->position();
    drag.vertex = found.vertex;
    drag.tangent = found.tangent;
    drag.tangent_factor = found.factor;
    drag.selection = selected_;
    drag.modifiers = int(event->modifiers());
    drag.rectangle = found.vertex < 0;
    rectangle_end_ = event->position();
    if (found.vertex >= 0) {
      if (!event->modifiers().testFlag(Qt::ControlModifier)) {
        if (!event->modifiers().testFlag(Qt::ShiftModifier) &&
            std::find(selected_.begin(), selected_.end(), found.vertex) == selected_.end())
          selected_.clear();
        if (std::find(selected_.begin(), selected_.end(), found.vertex) == selected_.end())
          selected_.push_back(found.vertex);
        notify_selection();
      }
      const auto anchor = world(view.matrix, fill_warp_grid(p)->nodes[found.vertex].position);
      if (view.camera_plane)
        drag.plane_start = view.camera_plane(event->position(), anchor);
    }
    else if (!event->modifiers().testFlag(Qt::ShiftModifier) &&
             !event->modifiers().testFlag(Qt::ControlModifier))
    {
      selected_.clear();
      notify_selection();
    }
    drag_ = std::move(drag);
    return true;
  }
  catch (const std::exception &exception) {
    if (error)
      error(QString::fromUtf8(exception.what()));
    return true;
  }
}
bool WarpEditor::move(QMouseEvent *event, const FillProjection &p, const WarpViewContext &view)
{
  if (p.mode != 5)
    return false;
  if (!p.manipulator) {
    finish(false);
    return false;
  }
  if (event->modifiers().testFlag(Qt::AltModifier) && !drag_)
    return false;
  if (split_mode_ && !drag_) {
    hover_ = view.pane.contains(event->position()) ? coordinate(event->position(), p, view) :
                                                     std::nullopt;
    return view.pane.contains(event->position());
  }
  if (!drag_)
    return false;
  try {
    auto &drag = *drag_;
    if (!(event->buttons() & Qt::LeftButton)) {
      finish(true);
      return true;
    }
    if (QLineF(drag.start, event->position()).length() >= 2)
      drag.moved = true;
    if (drag.rectangle) {
      rectangle_end_ = event->position();
      return true;
    }
    if (!drag.moved)
      return true;
    if (drag.gizmo_axis >= 0) {
      if (!drag.transaction) {
        drag.transaction = true;
        if (started)
          started();
      }
      auto next = drag.before;
      auto initial_view = view;
      initial_view.matrix = drag.matrix;
      const auto handles = gizmo(next, initial_view, drag.selection, local_space_);
      const int axis = drag.gizmo_axis;
      const auto screen_axis = handles.tips[axis] - handles.origin;
      const double size = QLineF(handles.origin, handles.tips[axis]).length();
      const double distance = size > 1 ? dot2(event->position() - drag.start, screen_axis) / size :
                                         0;
      const double amount = distance / std::max(1., size);
      const auto direction = handles.axes[axis];
      if (!next.warp_edit) {
        if (next.gizmo == 0) {
          for (int c = 0; c < 3; ++c)
            next.offset3[c] += direction[c] * amount * handles.lengths[axis] / view.extent;
        }
        else if (next.gizmo == 1) {
          const auto rotation = QQuaternion::fromAxisAndAngle(0, 0, 1, next.rotation3[2]) *
                                QQuaternion::fromAxisAndAngle(0, 1, 0, next.rotation3[1]) *
                                QQuaternion::fromAxisAndAngle(1, 0, 0, next.rotation3[0]);
          const auto delta = QQuaternion::fromAxisAndAngle(
              direction[0], direction[1], direction[2], distance);
          const auto matrix = (delta * rotation).toRotationMatrix();
          const double y = std::asin(std::clamp(-double(matrix(2, 0)), -1., 1.)),
                       x = std::abs(std::cos(y)) > 1e-7 ? std::atan2(matrix(2, 1), matrix(2, 2)) :
                                                          0,
                       z = std::abs(std::cos(y)) > 1e-7 ? std::atan2(matrix(1, 0), matrix(0, 0)) :
                                                          std::atan2(-matrix(0, 1), matrix(1, 1));
          next.rotation3 = {
              x * 180 / std::numbers::pi, y * 180 / std::numbers::pi, z * 180 / std::numbers::pi};
        }
        else {
          next.linked3 = false;
          next.scale3[axis] *= std::exp(std::clamp(amount, -8., 8.));
        }
      }
      else {
        auto grid = std::make_shared<WarpGrid>(*fill_warp_grid(next));
        const auto inverse = view.matrix.inverted();
        const auto vector_world = [&](const WarpVector &v) {
          const auto value = (view.matrix * QVector4D(v[0], v[1], v[2], 0)).toVector3D();
          return WarpVector{value.x(), value.y(), value.z()};
        };
        const auto vector_local = [&](const WarpVector &v) {
          const auto value = (inverse * QVector4D(v[0], v[1], v[2], 0)).toVector3D();
          return WarpVector{value.x(), value.y(), value.z()};
        };
        if (next.gizmo == 0) {
          const auto delta = vector_local(warp_scale(direction, amount * handles.lengths[axis]));
          for (int id : drag.selection)
            grid->nodes[id].position = warp_add(grid->nodes[id].position, delta);
          if (grid->automatic_tangents)
            grid->align_tangents();
        }
        else {
          const auto rotate = QQuaternion::fromAxisAndAngle(
              direction[0], direction[1], direction[2], distance);
          const double factor = std::exp(std::clamp(amount, -8., 8.));
          for (int id : drag.selection)
            for (auto *tangent : {&grid->nodes[id].du, &grid->nodes[id].dv, &grid->nodes[id].duv})
            {
              auto v = vector_world(*tangent);
              if (next.gizmo == 1) {
                const auto moved = rotate.rotatedVector({float(v[0]), float(v[1]), float(v[2])});
                v = {moved.x(), moved.y(), moved.z()};
              }
              else
                v = warp_add(v, direction, warp_dot(v, direction) * (factor - 1));
              *tangent = vector_local(v);
            }
          grid->automatic_tangents = false;
        }
        grid->validate();
        next.warp = std::move(grid);
      }
      validate_fill_projection(next);
      if (changed)
        changed(next);
      return true;
    }
    if (std::find(selected_.begin(), selected_.end(), drag.vertex) == selected_.end()) {
      selected_.push_back(drag.vertex);
      notify_selection();
    }
    if (!drag.transaction) {
      drag.transaction = true;
      if (started)
        started();
    }
    auto next = drag.before;
    auto grid = std::make_shared<WarpGrid>(*fill_warp_grid(drag.before));
    const auto anchor = world(view.matrix, grid->nodes[drag.vertex].position);
    std::optional<WarpVector> target;
    if (drag.tangent < 0 && p.warp_surface && !event->modifiers().testFlag(Qt::ControlModifier) &&
        view.surface)
      target = view.surface(event->position());
    else if (view.camera_plane && drag.plane_start) {
      if (const auto point = view.camera_plane(event->position(), anchor))
        target = warp_add(anchor, warp_add(*point, *drag.plane_start, -1));
    }
    if (!target)
      return true;
    if (drag.tangent >= 0) {
      const auto initial = grid->nodes[drag.vertex];
      const auto &direction = drag.tangent == 0 ? initial.du : initial.dv;
      const auto handle = warp_add(initial.position, direction, drag.tangent_factor);
      const auto initial_handle = world(view.matrix, handle);
      const auto moved_handle = warp_add(initial_handle, warp_add(*target, anchor, -1));
      const auto vector = warp_scale(
          warp_add(local(view.matrix, moved_handle), initial.position, -1),
          1 / drag.tangent_factor);
      const auto delta = warp_add(vector, direction, -1);
      for (int id : selected_) {
        auto &value = drag.tangent == 0 ? grid->nodes.at(id).du : grid->nodes.at(id).dv;
        value = warp_add(value, delta);
      }
      grid->automatic_tangents = false;
    }
    else {
      const auto delta = warp_add(
          local(view.matrix, *target), grid->nodes[drag.vertex].position, -1);
      for (int id : selected_)
        grid->nodes.at(id).position = warp_add(grid->nodes[id].position, delta);
      if (grid->automatic_tangents)
        grid->align_tangents();
    }
    grid->validate();
    next.warp = std::move(grid);
    if (changed)
      changed(next);
    return true;
  }
  catch (const std::exception &exception) {
    if (error)
      error(QString::fromUtf8(exception.what()));
    finish(true);
    return true;
  }
}
bool WarpEditor::release(QMouseEvent *event, const FillProjection &p, const WarpViewContext &view)
{
  if (!drag_ || event->button() != Qt::LeftButton)
    return false;
  auto drag = *drag_;
  if (drag.rectangle) {
    const auto grid = fill_warp_grid(p);
    const auto box = QRectF(drag.start, rectangle_end_).normalized();
    if (!drag.moved) {
      selected_.clear();
    }
    else {
      selected_ = drag.selection;
      if (!(drag.modifiers & int(Qt::ShiftModifier)) &&
          !(drag.modifiers & int(Qt::ControlModifier)))
        selected_.clear();
      for (int id = 0; id < int(grid->nodes.size()); ++id)
        if (box.contains(view.screen(world(view.matrix, grid->nodes[id].position)))) {
          const auto found = std::find(selected_.begin(), selected_.end(), id);
          if (drag.modifiers & int(Qt::ControlModifier)) {
            if (found != selected_.end())
              selected_.erase(found);
          }
          else if (found == selected_.end())
            selected_.push_back(id);
        }
    }
    notify_selection();
  }
  else if (!drag.moved && drag.gizmo_axis < 0) {
    if (drag.modifiers & int(Qt::ControlModifier))
      std::erase(selected_, drag.vertex);
    else if (!(drag.modifiers & int(Qt::ShiftModifier)))
      selected_ = {drag.vertex};
    notify_selection();
  }
  finish(false);
  return true;
}
void WarpEditor::finish(bool cancel)
{
  if (!drag_)
    return;
  const bool transaction = drag_->transaction;
  const auto selection = drag_->selection;
  drag_.reset();
  if (cancel) {
    selected_ = selection;
    notify_selection();
  }
  if (transaction && finished)
    finished(cancel);
}
bool WarpEditor::key(QKeyEvent *event, const FillProjection &p)
{
  if (p.mode != 5)
    return false;
  if (event->key() == Qt::Key_Escape) {
    if (drag_)
      finish(true);
    else if (split_mode_) {
      split_mode_ = 0;
      hover_.reset();
      if (split_cancelled)
        split_cancelled();
    }
    else
      return false;
    return true;
  }
  if (event->key() == Qt::Key_V && event->modifiers().testFlag(Qt::ShiftModifier)) {
    finish(false);
    auto next = p;
    next.warp_edit = !p.warp_edit;
    if (started)
      started();
    if (changed)
      changed(next);
    if (finished)
      finished(false);
    return true;
  }
  return false;
}
std::array<QPointF, 3> WarpEditor::axis_positions(const FillProjection &p,
                                                  const WarpViewContext &view) const
{
  return gizmo(p, view, selected_, local_space_).tips;
}
QJsonObject WarpEditor::diagnostic_drag(const FillProjection &p, const WarpViewContext &view) const
{
  if (p.mode != 5 || !p.manipulator || !p.warp_edit || view.pane.isEmpty() || !view.camera_plane)
    return {};
  const auto grid = fill_warp_grid(p);
  for (int id = 0; id < int(grid->nodes.size()); ++id) {
    const auto anchor = world(view.matrix, grid->nodes[id].position);
    const auto from = view.screen(anchor), to = from + QPointF(17, -13);
    if (!view.pane.adjusted(10, 10, -10, -10).contains(from) ||
        !view.pane.adjusted(10, 10, -10, -10).contains(to))
      continue;
    const auto found = hit(from, p, view);
    if (found.vertex != id || found.tangent >= 0)
      continue;
    // Existing gizmo press has priority over vertices; reject overlaps using the
    // very same handles as press(), rather than supplying an unclickable point.
    const auto handles = gizmo(p, view, selected_, local_space_);
    bool overlap = false;
    if (handles.active)
      for (int c = 0; c < 3; ++c) {
        const auto direction = handles.tips[c] - handles.origin;
        const double size = dot2(direction, direction);
        if (size < 100)
          continue;
        const double t = std::clamp(dot2(from - handles.origin, direction) / size, .2, 1.);
        if (QLineF(from, handles.origin + t * direction).length() < 8)
          overlap = true;
      }
    if (overlap || !view.camera_plane(from, anchor) || !view.camera_plane(to, anchor))
      continue;
    return {{"domain", "warp"},
            {"tool", p.gizmo},
            {"axis", id},
            {"from", QJsonArray{from.x(), from.y()}},
            {"to", QJsonArray{to.x(), to.y()}}};
  }
  return {};
}
void WarpEditor::paint(QPainter &painter,
                       const FillProjection &p,
                       const WarpViewContext &view) const
{
  if (p.mode != 5 || !p.manipulator || view.pane.isEmpty())
    return;
  const auto grid = fill_warp_grid(p);
  const auto color = picker_to_srgb({p.warp_color[0], p.warp_color[1], p.warp_color[2], 1});
  painter.save();
  painter.setClipRect(view.pane);
  painter.setPen(QPen(color, 1.2));
  const auto line = [&](bool vertical, double value) {
    QPolygonF points;
    for (int step = 0; step <= 64; ++step) {
      const auto node = grid->evaluate(vertical ? value : step / 64.,
                                       vertical ? step / 64. : value);
      points << view.screen(world(view.matrix, node.position));
    }
    painter.drawPolyline(points);
  };
  for (double u : grid->columns)
    line(true, u);
  for (double v : grid->rows)
    line(false, v);
  if (hover_ && split_mode_) {
    painter.setPen(QPen(QColor(bts::theme::Warning), 1.5, Qt::DashLine));
    if (split_mode_ == 1 || split_mode_ == 3)
      line(true, hover_->u);
    if (split_mode_ == 1 || split_mode_ == 2)
      line(false, hover_->v);
  }
  for (int id = 0; id < int(grid->nodes.size()); ++id) {
    const auto &node = grid->nodes[id];
    const auto point = view.screen(world(view.matrix, node.position));
    const auto derivative = grid->evaluate(grid->columns[id % grid->width()],
                                           grid->rows[id / grid->width()]);
    const auto local_normal = derivative.normal();
    const auto normal_matrix = view.matrix.normalMatrix();
    const QVector3D direction =
        QVector3D(normal_matrix(0, 0) * local_normal[0] + normal_matrix(0, 1) * local_normal[1] +
                      normal_matrix(0, 2) * local_normal[2],
                  normal_matrix(1, 0) * local_normal[0] + normal_matrix(1, 1) * local_normal[1] +
                      normal_matrix(1, 2) * local_normal[2],
                  normal_matrix(2, 0) * local_normal[0] + normal_matrix(2, 1) * local_normal[1] +
                      normal_matrix(2, 2) * local_normal[2])
            .normalized();
    const auto anchor = world(view.matrix, node.position);
    const auto endpoint = QVector3D(anchor[0], anchor[1], anchor[2]) -
                          direction *
                              float(p.warp_depth * view.matrix.column(1).toVector3D().length());
    const auto arrow = local(view.matrix, {endpoint.x(), endpoint.y(), endpoint.z()});
    painter.setPen(QPen(bts::theme::alpha(bts::theme::Success, 160), 1));
    painter.drawLine(point, view.screen(world(view.matrix, arrow)));
    if (!p.warp_edit)
      continue;
    const bool selected = std::find(selected_.begin(), selected_.end(), id) != selected_.end();
    painter.setPen(QPen(selected ? QColor(bts::theme::Warning) : color, 1.3));
    painter.setBrush(selected ? QColor(bts::theme::Warning) : QColor(bts::theme::Surface));
    painter.drawEllipse(point, p.warp_handle_size * .5, p.warp_handle_size * .5);
    if (selected && !grid->automatic_tangents) {
      for (int axis = 0; axis < 2; ++axis)
        for (int side : {-1, 1}) {
          const double factor = span(*grid, id, axis == 1, side);
          const auto endpoint = view.screen(
              world(view.matrix, warp_add(node.position, axis == 0 ? node.du : node.dv, factor)));
          painter.setPen(QPen(QColor(bts::theme::Warning), 1));
          painter.drawLine(point, endpoint);
          painter.setBrush(QColor(bts::theme::Warning));
          painter.drawRect(QRectF(endpoint - QPointF(2, 2), QSizeF(4, 4)));
        }
    }
  }
  const auto handles = gizmo(p, view, selected_, local_space_);
  if (handles.active) {
    const std::array<QColor, 3> colors{
        QColor(bts::theme::AxisX), QColor(bts::theme::AxisY), QColor(bts::theme::AxisZ)};
    for (int c = 0; c < 3; ++c) {
      if (QLineF(handles.origin, handles.tips[c]).length() < 10)
        continue;
      painter.setPen(QPen(colors[c], 2));
      painter.drawLine(handles.origin, handles.tips[c]);
      painter.setBrush(colors[c]);
      if (p.gizmo == 2)
        painter.drawRect(QRectF(handles.tips[c] - QPointF(4, 4), QSizeF(8, 8)));
      else
        painter.drawEllipse(handles.tips[c], 4, 4);
      painter.drawText(handles.tips[c] + QPointF(6, -3), QString(QChar::fromLatin1("XYZ"[c])));
    }
    painter.setPen(QColor(bts::theme::Heading));
    painter.drawText(handles.origin + QPointF(7, 18),
                     p.warp_edit ? (p.gizmo == 0 ? "Move vertices" :
                                    p.gizmo == 1 ? "Rotate tangents" :
                                                   "Scale tangents") :
                                   (p.gizmo == 0 ? "Move grid" :
                                    p.gizmo == 1 ? "Rotate grid" :
                                                   "Scale grid"));
  }
  if (drag_ && drag_->rectangle) {
    painter.setPen(QPen(QColor(bts::theme::Warning), 1, Qt::DashLine));
    painter.setBrush(bts::theme::alpha(bts::theme::Warning, 28));
    painter.drawRect(QRectF(drag_->start, rectangle_end_).normalized());
  }
  painter.restore();
}
}  // namespace bts
