// SPDX-License-Identifier: GPL-3.0-or-later
#include "OrbitCamera.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace bts {
namespace {

using Vector = std::array<double, 3>;

constexpr double radians_per_degree = std::numbers::pi / 180.0;
constexpr double default_tan_half_fov = 0.4142135623730950488;  // tan(45 degrees / 2).
constexpr double float_epsilon = std::numeric_limits<float>::epsilon();
// QMatrix4x4 stores floats. Reserve room for translations and projection multiplication even at
// the most extreme aspect ratio representable by QSize; bounds beyond this range are rejected.
constexpr double maximum_scalar = double(std::numeric_limits<float>::max()) /
                                  (128.0 * std::numeric_limits<int>::max());
constexpr double maximum_extent = 2.0 * maximum_scalar * default_tan_half_fov;

double dot(const Vector &a, const Vector &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector subtract(const Vector &a, const Vector &b)
{
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double maximum_absolute(const Vector &value)
{
  return std::max({std::abs(value[0]), std::abs(value[1]), std::abs(value[2])});
}

bool finite(const Vector &value)
{
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

bool finite(const QPointF &value)
{
  return std::isfinite(value.x()) && std::isfinite(value.y());
}

Vector corner(const Vector &minimum, const Vector &maximum, int index)
{
  return {index & 1 ? maximum[0] : minimum[0],
          index & 2 ? maximum[1] : minimum[1],
          index & 4 ? maximum[2] : minimum[2]};
}

double aspect_ratio(QSize size)
{
  return double(std::max(1, size.width())) / std::max(1, size.height());
}

double snapped(double degrees)
{
  return std::round(degrees / OrbitCamera::snap_degrees) * OrbitCamera::snap_degrees;
}

}  // namespace

double OrbitCamera::half_fov_tan() const
{
  return std::tan(fov_degrees_ * radians_per_degree * .5);
}
double OrbitCamera::scene_camera_gate_scale(QSize size) const
{
  return scene_camera_ && scene_camera_->aspect > 0 ?
             std::max(1., double(scene_camera_->aspect) / aspect_ratio(size)) :
             1;
}
bool OrbitCamera::adopt_scene_camera(const SceneCamera &source)
{
  Vector position, forward, up;
  for (int i = 0; i < 3; ++i) {
    position[i] = source.position[i];
    forward[i] = source.forward[i];
    up[i] = source.up[i];
  }
  if (!finite(position) || !finite(forward) || !finite(up) ||
      maximum_absolute(position) > maximum_scalar || !std::isfinite(source.vertical_fov) ||
      source.vertical_fov < 1 || source.vertical_fov > 175 || !std::isfinite(source.aspect) ||
      source.aspect < 0 || source.aspect > 10000 || !std::isfinite(source.near_clip) ||
      source.near_clip <= 0 || source.far_clip <= source.near_clip ||
      !std::isfinite(source.far_clip) ||
      (source.orthographic &&
       (!std::isfinite(source.orthographic_extent) || source.orthographic_extent <= 0)))
    return false;
  double length = std::sqrt(dot(forward, forward));
  if (length < 1e-10)
    return false;
  for (auto &v : forward)
    v /= length;
  const double along = dot(up, forward);
  for (int i = 0; i < 3; ++i)
    up[i] -= forward[i] * along;
  length = std::sqrt(dot(up, up));
  if (length < 1e-10)
    return false;
  for (auto &v : up)
    v /= length;
  auto candidate = *this;
  candidate.orientation_.reset();
  candidate.snap_active_ = false;
  candidate.roll_degrees_ = 0;
  candidate.fov_degrees_ = source.vertical_fov;
  candidate.projection_ = source.orthographic ? Projection::Orthographic : Projection::Perspective;
  candidate.orthographic_extent_ = source.orthographic_extent;
  double distance = dot(subtract(center_, position), forward);
  if (distance <= source.near_clip)
    distance = std::max(scene_radius_ * 3., double(source.near_clip) * 10);
  candidate.distance_ = distance;
  for (int i = 0; i < 3; ++i)
    candidate.center_[i] = position[i] + forward[i] * distance;
  candidate.yaw_degrees_ = std::atan2(-forward[0], forward[1]) / radians_per_degree;
  candidate.pitch_degrees_ = std::asin(std::clamp(-forward[2], -1., 1.)) / radians_per_degree;
  const auto axes = candidate.basis();
  candidate.roll_degrees_ = std::atan2(dot(up, axes[0]), dot(up, axes[1])) / radians_per_degree;
  candidate.scene_camera_ = source;
  *this = std::move(candidate);
  return true;
}
void OrbitCamera::release_scene_camera(QSize size)
{
  const double scale = scene_camera_gate_scale(size);
  fov_degrees_ = 2 * std::atan(half_fov_tan() * scale) / radians_per_degree;
  orthographic_extent_ *= scale;
  scene_camera_.reset();
}

std::array<OrbitCamera::Vector, 3> OrbitCamera::basis() const
{
  if (orientation_)
    return trackball::rows(*orientation_);
  const double yaw = (snap_active_ ? snapped(yaw_degrees_) : yaw_degrees_) * radians_per_degree;
  const double pitch = (snap_active_ ? snapped(pitch_degrees_) : pitch_degrees_) *
                       radians_per_degree;
  const double sy = std::sin(yaw), cy = std::cos(yaw);
  const double sp = std::sin(pitch), cp = std::cos(pitch);
  // Analytic right/up/backward vectors stay defined at the top and bottom poles, where lookAt
  // with the fixed world-up vector would otherwise have a zero cross product.
  Vector right{cy, sy, 0}, up{-sy * sp, cy * sp, cp};
  const double r = roll_degrees_ * radians_per_degree, c = std::cos(r), sn = std::sin(r);
  Vector rolled_right, rolled_up;
  for (int i = 0; i < 3; ++i) {
    rolled_right[i] = right[i] * c - up[i] * sn;
    rolled_up[i] = right[i] * sn + up[i] * c;
  }
  return {rolled_right, rolled_up, Vector{sy * cp, -cy * cp, sp}};
}

QVector3D OrbitCamera::center() const
{
  return {float(center_[0]), float(center_[1]), float(center_[2])};
}

double OrbitCamera::yaw_degrees() const
{
  return snap_active_ ? snapped(yaw_degrees_) : yaw_degrees_;
}

double OrbitCamera::pitch_degrees() const
{
  return snap_active_ ? snapped(pitch_degrees_) : pitch_degrees_;
}

double OrbitCamera::vertical_extent() const
{
  return projection_ == Projection::Orthographic ? orthographic_extent_ :
                                                   2.0 * distance_ * half_fov_tan();
}

double OrbitCamera::minimum_extent() const
{
  return std::max({1e-8,
                   maximum_absolute(center_) * float_epsilon * 16.0,
                   scene_radius_ * float_epsilon * 4.0});
}

QMatrix4x4 OrbitCamera::view_matrix() const
{
  const auto axes = basis();
  QMatrix4x4 matrix;
  matrix.fill(0);
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      matrix(row, column) = float(axes[row][column]);
    }
    matrix(row, 3) = float(-dot(axes[row], center_) - (row == 2 ? distance_ : 0.0));
  }
  matrix(3, 3) = 1;
  return matrix;
}

std::pair<double, double> OrbitCamera::bounds_depths() const
{
  if (!bounds_)
    return {0, 0};
  const Vector backward = basis()[2];
  double nearest = std::numeric_limits<double>::infinity();
  double farthest = -std::numeric_limits<double>::infinity();
  for (int i = 0; i < 8; ++i) {
    const Vector relative = subtract(corner(bounds_->minimum, bounds_->maximum, i), center_);
    const double depth = distance_ - dot(backward, relative);
    nearest = std::min(nearest, depth);
    farthest = std::max(farthest, depth);
  }
  return {nearest, farthest};
}

bool OrbitCamera::set_clipping(ViewportClipping value)
{
  if (!value.valid())
    return false;
  clipping_ = value;
  return true;
}

std::pair<double, double> OrbitCamera::clipping_range() const
{
  if (scene_camera_)
    return {scene_camera_->near_clip, scene_camera_->far_clip};
  // Orthographic views can see past the eye, just as a modeling viewport does. The positive
  // near preference applies to perspective only; Clip End bounds orthographic depth both ways.
  if (projection_ == Projection::Orthographic)
    return {-clipping_.far_plane, clipping_.far_plane};
  return {clipping_.near_plane, clipping_.far_plane};
}

QMatrix4x4 OrbitCamera::projection_matrix(QSize pixel_size) const
{
  const auto [near_plane, far_plane] = clipping_range();
  const double depth = far_plane - near_plane;
  const double aspect = aspect_ratio(pixel_size);
  const double gate = scene_camera_gate_scale(pixel_size);
  QMatrix4x4 matrix;
  matrix.fill(0);
  // Calculate in double before storing floats. Qt's float-only convenience constructors can
  // overflow intermediate near*far products for otherwise representable large scenes.
  if (projection_ == Projection::Perspective) {
    matrix(0, 0) = float(1.0 / (half_fov_tan() * gate * aspect));
    matrix(1, 1) = float(1.0 / (half_fov_tan() * gate));
    matrix(2, 2) = float(-(far_plane + near_plane) / depth);
    matrix(2, 3) = float(-2.0 * near_plane * (far_plane / depth));
    matrix(3, 2) = -1;
  }
  else {
    matrix(0, 0) = float(2.0 / (orthographic_extent_ * gate * aspect));
    matrix(1, 1) = float(2.0 / (orthographic_extent_ * gate));
    matrix(2, 2) = float(-2.0 / depth);
    matrix(2, 3) = float(-(far_plane + near_plane) / depth);
    matrix(3, 3) = 1;
  }
  return matrix;
}

void OrbitCamera::orbit(QPointF delta_pixels, bool snap)
{
  if (!finite(delta_pixels)) {
    return;
  }
  if (orientation_) {
    update_turntable_angles();
    orientation_.reset();
  }
  if (snap_active_ && !snap) {
    // Continue from the last visible snapped pose when Shift is released, without jumping back
    // to the sub-step accumulator. While snapping, small mouse deltas continue to accumulate.
    yaw_degrees_ = snapped(yaw_degrees_);
    pitch_degrees_ = snapped(pitch_degrees_);
  }
  snap_active_ = snap;
  yaw_degrees_ = std::remainder(yaw_degrees_ - delta_pixels.x() * 0.35, 360.0);
  pitch_degrees_ = std::clamp(pitch_degrees_ + delta_pixels.y() * 0.35, -90.0, 90.0);
}

void OrbitCamera::update_turntable_angles()
{
  const auto axes = basis();
  const auto &back = axes[2];
  // Retain a finite yaw at the pole; roll resolves the remaining orientation.
  if (std::hypot(back[0], back[1]) > 1e-12)
    yaw_degrees_ = std::atan2(back[0], -back[1]) / radians_per_degree;
  pitch_degrees_ = std::asin(std::clamp(back[2], -1., 1.)) / radians_per_degree;
  const double yaw = yaw_degrees_ * radians_per_degree,
               pitch = pitch_degrees_ * radians_per_degree;
  const Vector right{std::cos(yaw), std::sin(yaw), 0},
      up{-std::sin(yaw) * std::sin(pitch), std::cos(yaw) * std::sin(pitch), std::cos(pitch)};
  roll_degrees_ = std::atan2(dot(axes[1], right), dot(axes[1], up)) / radians_per_degree;
}

std::optional<trackball::Press> OrbitCamera::begin_trackball(QPointF logical,
                                                             QRect pane,
                                                             double dpr) const
{
  const auto point = trackball::pointer(logical, pane, dpr);
  if (!point)
    return {};
  return trackball::Press{pane, dpr, *point, trackball::from_rows(basis())};
}

bool OrbitCamera::orbit_trackball(const trackball::Press &press, QPointF logical)
{
  const auto point = trackball::pointer(logical, press.pane, press.dpr);
  if (!point)
    return false;
  double norm = 0;
  for (double x : press.orientation) {
    if (!std::isfinite(x))
      return false;
    norm += x * x;
  }
  if (norm < 1e-20 || !std::isfinite(norm))
    return false;
  orientation_ = trackball::rotation(press.pointer, *point, press.pane.size(), press.orientation);
  snap_active_ = false;
  update_turntable_angles();
  return true;
}

void OrbitCamera::pan(QPointF delta_pixels, int viewport_height)
{
  if (!finite(delta_pixels) || viewport_height <= 0) {
    return;
  }
  const auto axes = basis();
  const double units_per_pixel = vertical_extent() / viewport_height;
  Vector candidate = center_;
  for (int axis = 0; axis < 3; ++axis) {
    candidate[axis] += (-axes[0][axis] * delta_pixels.x() + axes[1][axis] * delta_pixels.y()) *
                       units_per_pixel;
  }
  // Preserve enough range for a subsequent orthographic toggle at this same footprint.
  const double limit = maximum_scalar * std::min(1.0, vertical_extent());
  if (finite(candidate) && maximum_absolute(candidate) <= limit) {
    center_ = candidate;
  }
}

void OrbitCamera::dolly(double delta_pixels)
{
  if (!std::isfinite(delta_pixels)) {
    return;
  }
  const double factor = std::exp(std::clamp(delta_pixels * 0.01, -100.0, 100.0));
  if (projection_ == Projection::Orthographic) {
    orthographic_extent_ = std::clamp(
        orthographic_extent_ * factor, minimum_extent(), maximum_extent);
  }
  else {
    distance_ = std::clamp(
        distance_ * factor, minimum_extent() / (2.0 * half_fov_tan()), maximum_scalar);
  }
}

void OrbitCamera::zoom_wheel(double angle_delta)
{
  dolly(-angle_delta * 0.12);
}

void OrbitCamera::set_orthographic(bool orthographic)
{
  const Projection requested = orthographic ? Projection::Orthographic : Projection::Perspective;
  if (requested == projection_) {
    return;
  }
  if (orthographic) {
    orthographic_extent_ = vertical_extent();
  }
  else {
    distance_ = orthographic_extent_ / (2.0 * half_fov_tan());
  }
  projection_ = requested;
}

bool OrbitCamera::frame_bounds(QVector3D minimum, QVector3D maximum, QSize viewport)
{
  const Vector low = {minimum.x(), minimum.y(), minimum.z()};
  const Vector high = {maximum.x(), maximum.y(), maximum.z()};
  if (!finite(low) || !finite(high) || viewport.width() <= 0 || viewport.height() <= 0) {
    return false;
  }
  for (int axis = 0; axis < 3; ++axis) {
    if (low[axis] > high[axis]) {
      return false;
    }
  }
  const Vector span = subtract(high, low);
  Vector pivot;
  for (int axis = 0; axis < 3; ++axis) {
    pivot[axis] = low[axis] + span[axis] * 0.5;
  }
  const double precision_extent = std::max(1e-6, maximum_absolute(pivot) * float_epsilon * 32.0);
  const double radius = std::max(std::hypot(span[0], span[1], span[2]) * 0.5,
                                 precision_extent * 0.5);
  if (maximum_absolute(pivot) > maximum_scalar || radius > maximum_scalar) {
    return false;
  }
  const auto axes = basis();
  const double aspect = aspect_ratio(viewport);
  double required_distance = std::max(radius * 1.05, precision_extent / (2.0 * half_fov_tan()));
  double required_extent = precision_extent;
  for (int i = 0; i < 8; ++i) {
    const Vector relative = subtract(corner(low, high, i), pivot);
    const double x = std::abs(dot(axes[0], relative)) * 1.1;
    const double y = std::abs(dot(axes[1], relative)) * 1.1;
    const double z = dot(axes[2], relative);
    required_distance = std::max(
        {required_distance, z + x / (aspect * half_fov_tan()), z + y / half_fov_tan()});
    required_extent = std::max({required_extent, 2.0 * x / aspect, 2.0 * y});
  }
  if (!std::isfinite(required_distance) || required_distance > maximum_scalar ||
      !std::isfinite(required_extent) || required_extent > maximum_extent)
  {
    return false;
  }
  center_ = pivot;
  scene_radius_ = radius;
  bounds_ = Bounds{low, high};
  distance_ = required_distance;
  orthographic_extent_ = required_extent;
  return true;
}

}  // namespace bts
