#pragma once
#include "FillProjection.hh"
#include <QMatrix4x4>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <functional>
class QMouseEvent;
class QKeyEvent;
class QPainter;
namespace bts {
struct WarpViewContext {
  QRectF pane;
  QMatrix4x4 matrix;
  double extent = 1;
  std::function<QPointF(const WarpVector &)> screen;
  std::function<std::optional<WarpVector>(QPointF)> surface;
  std::function<std::optional<WarpVector>(QPointF, const WarpVector &)> camera_plane;
};
class WarpEditor {
 public:
  std::function<void(const FillProjection &)> changed;
  std::function<void()> started;
  std::function<void(bool)> finished;
  std::function<void(std::vector<int>)> selection_changed;
  std::function<void()> split_cancelled;
  std::function<void(const QString &)> error;
  void synchronize(const std::optional<FillProjection> &, const QString &target);
  void set_selection(std::vector<int> selected);
  void set_split_mode(int mode)
  {
    split_mode_ = mode;
    hover_.reset();
  }
  void set_local_space(bool local)
  {
    local_space_ = local;
  }
  bool press(QMouseEvent *, const FillProjection &, const WarpViewContext &);
  bool move(QMouseEvent *, const FillProjection &, const WarpViewContext &);
  bool release(QMouseEvent *, const FillProjection &, const WarpViewContext &);
  bool key(QKeyEvent *, const FillProjection &);
  void paint(QPainter &, const FillProjection &, const WarpViewContext &) const;
  std::array<QPointF, 3> axis_positions(const FillProjection &, const WarpViewContext &) const;
  void finish(bool cancel);
  QJsonObject diagnostic_drag(const FillProjection &, const WarpViewContext &) const;
  const std::vector<int> &selection() const
  {
    return selected_;
  }

 private:
  struct Drag {
    FillProjection before;
    QMatrix4x4 matrix;
    QPointF start;
    std::vector<int> selection;
    int vertex = -1, tangent = -1, gizmo_axis = -1;
    double tangent_factor = 1;
    bool moved = false, rectangle = false, transaction = false;
    int modifiers = 0;
    std::optional<WarpVector> plane_start;
  };
  std::optional<Drag> drag_;
  std::vector<int> selected_;
  std::optional<WarpCoordinate> hover_;
  QPointF rectangle_end_;
  int split_mode_ = 0;
  bool local_space_ = false;
  QString target_;
  std::shared_ptr<const WarpGrid> previous_;
  struct Hit {
    int vertex = -1, tangent = -1, gizmo_axis = -1;
    double factor = 1;
  };
  Hit hit(QPointF, const FillProjection &, const WarpViewContext &) const;
  std::optional<WarpCoordinate> coordinate(QPointF,
                                           const FillProjection &,
                                           const WarpViewContext &) const;
  void notify_selection();
};
}  // namespace bts
