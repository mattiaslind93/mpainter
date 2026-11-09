// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SceneCamera.hh"
#include "ViewportClipping.hh"
#include "Trackball.hh"

#include <array>
#include <optional>
#include <utility>

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

namespace bts {

// Independent Z-up camera state. Navigation never holds or edits a Blender/document object.
class OrbitCamera {
 public:
  enum class Projection { Perspective, Orthographic };
  static constexpr double vertical_fov_degrees = 45.0;
  // Product decision pending precise reference evidence; PD-118 does not establish this angle.
  static constexpr double snap_degrees = 15.0;

  bool adopt_scene_camera(const SceneCamera &);
  void release_scene_camera(QSize viewport);
  double fov_degrees() const
  {
    return fov_degrees_;
  }
  double scene_camera_gate_scale(QSize viewport) const;
  // Bounds are diagnostic/framing data only; they never tighten the clipping planes.
  bool set_clipping(ViewportClipping value);
  ViewportClipping clipping_settings() const
  {
    return clipping_;
  }
  std::pair<double, double> clipping_range() const;
  std::pair<double, double> bounds_depths() const;
  bool scene_camera_active() const
  {
    return scene_camera_.has_value();
  }
  QMatrix4x4 view_matrix() const;
  QMatrix4x4 projection_matrix(QSize pixel_size) const;

  // Deltas and viewport_height must use the same pixel units. Rotation is constrained to Z-up.
  void orbit(QPointF delta_pixels, bool snap);
  std::optional<trackball::Press> begin_trackball(QPointF logical,
                                                  QRect physical_pane,
                                                  double dpr) const;
  bool orbit_trackball(const trackball::Press &, QPointF logical);
  void pan(QPointF delta_pixels, int viewport_height);
  // Positive dolly moves away; positive wheel angle zooms in. The event owner chooses platform
  // scroll polarity; this convention does not claim the observed macOS physical-wheel direction.
  void dolly(double delta_pixels);
  void zoom_wheel(double angle_delta);
  void set_orthographic(bool orthographic);

  // Preserves orientation and projection. Frames all eight AABB corners with a 10% XY margin.
  // Rejects non-finite/reversed bounds, empty viewport sizes, and extents outside the supported
  // float-matrix range without changing the camera. Point/line/plane bounds are accepted.
  bool frame_bounds(QVector3D minimum, QVector3D maximum, QSize viewport);

  QVector3D center() const;
  double distance() const
  {
    return distance_;
  }
  double yaw_degrees() const;
  double pitch_degrees() const;
  // Full visible height at the pivot plane, in world units, for either projection mode.
  double vertical_extent() const;
  Projection projection() const
  {
    return projection_;
  }

 private:
  using Vector = std::array<double, 3>;
  struct Bounds {
    Vector minimum;
    Vector maximum;
  };

  std::array<Vector, 3> basis() const;
  void update_turntable_angles();
  double minimum_extent() const;

  double half_fov_tan() const;
  ViewportClipping clipping_;
  double fov_degrees_ = vertical_fov_degrees, roll_degrees_ = 0;
  std::optional<SceneCamera> scene_camera_;
  Vector center_ = {0, 0, 0};
  double distance_ = 7;
  double orthographic_extent_ = 1;
  double yaw_degrees_ = 25;
  double pitch_degrees_ = 18;
  bool snap_active_ = false;
  std::optional<trackball::Quaternion> orientation_;
  Projection projection_ = Projection::Perspective;
  std::optional<Bounds> bounds_;
  double scene_radius_ = 1;
};

}  // namespace bts
