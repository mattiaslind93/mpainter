// SPDX-License-Identifier: GPL-3.0-or-later
// The app's live viewport widget. main.cc includes this inside its anonymous namespace, after
// all of its own includes; it is not a standalone header.
class Viewport final : public bts::ViewportWidget, public bts::ViewportCapture {
 public:
  std::function<void()> gpu_startup_ready;
  std::function<void(double)> environment_rotation_changed;
  explicit Viewport(bts::BlenderHost &host, QWidget *parent = nullptr)
      : bts::ViewportWidget(parent), host_(host)
  {
    headless_surface_ = qEnvironmentVariableIntValue("BTS_HEADLESS_WORKSPACE") == 1 &&
                        QGuiApplication::platformName() == "offscreen";
    camera_.set_clipping(bts::load_viewport_clipping());
    trackball_enabled_ = bts::load_trackball_navigation();
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_TabletTracking, true);
#ifndef __APPLE__
    QObject::connect(this, &bts::ViewportWidget::frameSwapped, this, [] { bts::property_frame_done(); });
#endif
    setMinimumSize(400, 300);
    camera_selector_ = new QComboBox(this);
    camera_selector_->setObjectName("sceneCameraSelector");
    camera_selector_->addItem("Default camera", 0u);
    camera_selector_->hide();
    camera_selector_->setStyleSheet(bts::theme::qss(
        "QComboBox{background:@Surface@;color:@Text@;border:1px solid @Border@;padding:2px;}"));
    connect(camera_selector_, &QComboBox::currentIndexChanged, this, [this](int index) {
      choose_scene_camera(index);
    });
    camera_back_ = new QPushButton(this);
    camera_back_->setObjectName("restoreSceneCamera");
    camera_back_->hide();
    camera_notice_ = new QLabel("Scene cameras cannot be moved.", this);
    camera_notice_->setObjectName("sceneCameraNavigationNotice");
    camera_notice_->setStyleSheet(
        bts::theme::qss("background:@Surface@;color:@Text@;padding:2px"));
    camera_notice_->hide();
    connect(camera_back_, &QPushButton::clicked, this, [this] {
      if (last_imported_camera_) {
        const int index = camera_selector_->findData(*last_imported_camera_);
        if (index > 0)
          camera_selector_->setCurrentIndex(index);
      }
    });

    timer_.setInterval(16);
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { update(); });
    input_timer_.setSingleShot(true);
    input_timer_.setTimerType(Qt::PreciseTimer);
    input_timer_.setInterval(16);
    connect(&input_timer_, &QTimer::timeout, this, [this] { flush_pointer_input(); });
  }

  ~Viewport() override
  {
    disconnect(context_destroyed_);
    close_native();
  }

#ifdef __APPLE__
  void makeCurrent() {}
  void doneCurrent() {}
  QImage capture_framebuffer() override
  {
    return metal_frame_;
  }
  QImage grabFramebuffer() override
  {
    return metal_frame_;
  }
  bool isValid() const override
  {
    return headless_gpu_ready_;
  }
#else
  QOpenGLContext *context() const
  {
    return headless_surface_ ? headless_context_.get() : bts::ViewportWidget::context();
  }
  void makeCurrent()
  {
    if (headless_surface_) {
      if (headless_context_)
        headless_context_->makeCurrent(headless_native_surface_.get());
    }
    else
      bts::ViewportWidget::makeCurrent();
  }
  void doneCurrent()
  {
    if (headless_surface_) {
      if (headless_context_)
        headless_context_->doneCurrent();
    }
    else
      bts::ViewportWidget::doneCurrent();
  }
  QImage capture_framebuffer() override
  {
    return grabFramebuffer();
  }
  QImage grabFramebuffer()
  {
    if (!headless_surface_)
      return bts::ViewportWidget::grabFramebuffer();
    makeCurrent();
    return headless_framebuffer_ ? headless_framebuffer_->toImage() : QImage{};
  }
  bool isValid() const
  {
    return headless_surface_ ? headless_context_ && headless_context_->isValid() :
                               bts::ViewportWidget::isValid();
  }
#endif
  bts::EmptyProjectView *empty_project_view = nullptr;
  bool empty_project = false;
  void set_empty_project(bool empty)
  {
    empty_project = empty;
    if (!empty_project_view)
      empty_project_view = new bts::EmptyProjectView(this);
    empty_project_view->setVisible(empty);
    if (empty)
      paint_enabled = false;
    update();
  }
  bool shader_failed = false;
  bts::ViewportDrawGuard draw_guard;
  QLabel *draw_error_label = nullptr;
  std::function<void(const QString &)> draw_error;
  std::function<void(const QString &)> shader_error;
  std::function<bool()> material_import_pending;
  // The callback is lifetime-scoped by the Open controller and consumed once
  // before native shutdown, including Qt context destruction/reparenting.
  std::function<void()> before_native_close;
  void reset_shader_failure()
  {
    shader_failed = false;
    draw_guard.reset();
    if (draw_error_label)
      draw_error_label->hide();
    timer_.stop();
    update();
  }
  int mesh_map_active = -1, last_mesh_map = 0;
  std::array<bool, 7> available_mesh_maps{};
  std::function<void(int)> mesh_map_requested;
  QString current_inspection_label() const
  {
    return mesh_map_active >= 0 ? QString::fromLatin1(bts::mesh_map_names[mesh_map_active]) :
                                  inspection_label(host_.inspection_mode());
  }
  std::optional<bts::FillProjection> fill_projection;
  bts::WarpEditor warp_editor;
  bts::FillGizmo fill_gizmo;
  bts::FillGizmoGeometry fill_gizmo_geometry() const
  {
    if (!fill_projection)
      return {};
    const auto &p = *fill_projection;
    if (p.mode != 0 && !symmetry_scene)
      return {};
    const std::array<float, 3> zero{};
    return bts::fill_gizmo_geometry(
        p,
        symmetry_scene ? symmetry_scene->minimum : zero,
        symmetry_scene ? symmetry_scene->maximum : zero,
        pane_rect(p.mode == 0 ? bts::ViewKind::Uv : bts::ViewKind::Scene),
        [this](QVector3D point) { return scene_point(point); },
        [this](QPointF point) { return uv_point(point); });
  }
  QJsonObject diagnostic_fill_gizmo_drag()
  {
    if (fill_projection && fill_projection->mode == 5)
      return warp_editor.diagnostic_drag(*fill_projection, warp_view_context());
    return fill_gizmo.diagnostic(fill_gizmo_geometry());
  }
  void sync_warp_projection()
  {
    fill_gizmo.synchronize(fill_projection ? paint_target : QString{});
    warp_editor.synchronize(fill_projection, paint_target);
  }
  bts::WarpViewContext warp_view_context()
  {
    bts::WarpViewContext result;
    result.pane = pane_rect(bts::ViewKind::Scene);
    if (fill_projection && symmetry_scene) {
      const auto &p = *fill_projection;
      double extent = 0;
      for (int c = 0; c < 3; ++c)
        extent = std::max(extent, double(symmetry_scene->maximum[c] - symmetry_scene->minimum[c]));
      if (extent < 1e-6)
        extent = 1;
      result.extent = extent;
      result.matrix.translate(
          (symmetry_scene->minimum[0] + symmetry_scene->maximum[0]) * .5 + p.offset3[0] * extent,
          (symmetry_scene->minimum[1] + symmetry_scene->maximum[1]) * .5 + p.offset3[1] * extent,
          (symmetry_scene->minimum[2] + symmetry_scene->maximum[2]) * .5 + p.offset3[2] * extent);
      result.matrix.rotate(p.rotation3[2], 0, 0, 1);
      result.matrix.rotate(p.rotation3[1], 0, 1, 0);
      result.matrix.rotate(p.rotation3[0], 1, 0, 0);
      const auto size = bts::fill_physical_size(p);
      result.matrix.scale(p.scale3[0] * (p.physical ? size[0] / p.cm_per_native : extent),
                          p.scale3[1] * (p.physical ? size[2] / p.cm_per_native : extent),
                          p.scale3[2] * (p.physical ? size[1] / p.cm_per_native : extent));
    }
    result.screen = [this](const bts::WarpVector &point) {
      return scene_point({float(point[0]), float(point[1]), float(point[2])});
    };
    result.surface = [this](QPointF point) -> std::optional<bts::WarpVector> {
      const auto hit = pick_path_point(point);
      if (hit)
        return hit->position;
      return {};
    };
    result.camera_plane = [this](QPointF point,
                                 const bts::WarpVector &anchor) -> std::optional<bts::WarpVector> {
      if (!pane_rect(bts::ViewKind::Scene).contains(point))
        return {};
      const auto [origin, direction] = path_ray(point);
      const auto normal = camera_.view_matrix().inverted().column(2).toVector3D();
      double numerator = 0, denominator = 0;
      for (int c = 0; c < 3; ++c) {
        numerator += (anchor[c] - origin[c]) * normal[c];
        denominator += direction[c] * normal[c];
      }
      if (std::abs(denominator) < 1e-7)
        return {};
      bts::WarpVector result;
      for (int c = 0; c < 3; ++c)
        result[c] = origin[c] + direction[c] * numerator / denominator;
      return result;
    };
    return result;
  }
  std::function<void(int)> fill_gizmo_requested;
  const bts::BrushSettings *brush = nullptr;
  std::array<float, 4> secondary_color{1, 1, 1, 1};
  bool secondary_active_ = false;
  bool measure_brush_dabs_ = qEnvironmentVariableIsSet("BTS_STROKE_TARGET_PROFILE");
  bool rendering = false;
  bool paint_enabled = false, paint_mask = false, quick_mask_edit = false;
  std::shared_ptr<const bts::QuickMaskField> quick_mask_gate;
  std::function<void(int)> quick_mask_requested;
  bool polygon_active = false, polygon_drag = false;
  bts::PolygonFillMode polygon_mode = bts::PolygonFillMode::Auto;
  float polygon_value = 1;
  QPointF polygon_start, polygon_end;
  std::uint64_t surface_triangle_id_ = 0;
  std::function<void(bool)> polygon_tool_changed;
  std::function<void()> polygon_toggle_requested, polygon_invert_requested;
  std::function<void(const std::vector<std::size_t> &)> polygon_apply;
  bool picker_active = false, picker_click_ = false;
  std::function<void(bool)> picker_changed;
  std::function<bool(int)> effect_move_requested;
  std::function<void()> picker_toggle_requested, projection_tool_requested, smudge_tool_requested,
      clone_tool_requested;
  bool path_active = false, path_editing = false;
  std::string selected_path;
  std::optional<bts::SurfacePathPoint> pending_path_point;
  std::vector<std::shared_ptr<const bts::SurfacePath>> paths;
  bts::PathsPanel *paths_panel = nullptr;
  bts::PathCurvePanel *path_curve_panel = nullptr;
  bts::PathDisplayOptions path_display;
  int selected_vertex = -1;
  struct PathDrawCache {
    std::shared_ptr<const bts::SurfacePath> definition;
    std::vector<bts::SurfacePathDisplaySample> samples;
  };
  std::map<std::string, PathDrawCache> path_draw;
  struct PathHit {
    std::string id;
    int vertex = -1, handle = -1, segment = -1;
    double parameter = 0;
  };
  struct PathDrag {
    std::shared_ptr<const bts::SurfacePath> before;
    int vertex = -1, handle = -1;
    QPointF start;
    std::optional<bts::PathVector> plane_start;
    bool moved = false;
  };
  std::optional<PathDrag> path_drag;
  std::function<void()> path_vertex_selected;
  std::function<void(const std::string &, int, int)> path_drag_begin;
  std::function<bool(std::shared_ptr<const bts::SurfacePath>)> path_drag_update;
  std::function<void(bool)> path_drag_end;
  std::function<void(std::size_t, double)> path_insert;
  std::function<void(bts::PathCurveAction, double)> path_curve_action;
  void finish_path_drag(bool cancel)
  {
    if (!path_drag)
      return;
    path_drag.reset();
    if (path_drag_end)
      path_drag_end(cancel);
    update();
  }
  std::optional<QPointF> scene_source(const bts::ViewPane &pane, QPointF point) const
  {
    const auto scale = devicePixelRatioF();
    const auto p = host_.lens_projection().to_source(
        {(point.x() * scale - pane.x) / pane.width, (point.y() * scale - pane.y) / pane.height});
    if (!p)
      return {};
    return QPointF((*p)[0], (*p)[1]);
  }
  std::pair<QVector3D, QVector3D> path_ray(QPointF point) const
  {
    const auto pane = pane_at(point);
    if (!pane || pane->kind != bts::ViewKind::Scene)
      throw std::invalid_argument("Path editing is available in the 3D view");
    const double scale = devicePixelRatioF();
    const auto inverse = (camera_.projection_matrix({pane->width, pane->height}) *
                          camera_.view_matrix())
                             .inverted();
    const auto source = scene_source(*pane, point);
    if (!source)
      return {};
    const float x = float(2 * source->x() - 1), y = float(1 - 2 * source->y());
    const auto near_point = (inverse * QVector4D(x, y, -1, 1)).toVector3DAffine(),
               far_point = (inverse * QVector4D(x, y, 1, 1)).toVector3DAffine();
    return {near_point, (far_point - near_point).normalized()};
  }
  std::optional<bts::PathVector> path_plane_point(QPointF screen,
                                                  const bts::SurfacePathPoint &point) const
  {
    const auto [origin, direction] = path_ray(screen);
    double denominator = 0, numerator = 0;
    for (int c = 0; c < 3; ++c) {
      denominator += direction[c] * point.normal[c];
      numerator += (point.position[c] - origin[c]) * point.normal[c];
    }
    if (std::abs(denominator) < 1e-7)
      return {};
    const double t = numerator / denominator;
    bts::PathVector position;
    for (int c = 0; c < 3; ++c)
      position[c] = origin[c] + direction[c] * t;
    return position;
  }
  PathHit hit_path_detail(QPointF screen) const
  {
    PathHit best;
    const double tolerance = std::max(7., path_display.handle * .5 + 2);
    double nearest = tolerance;
    const auto project = [&](const auto &world) {
      return scene_point({float(world[0]), float(world[1]), float(world[2])});
    };
    for (auto item = paths.rbegin(); item != paths.rend(); ++item) {
      const auto &path = **item;
      if (path.id == selected_path && selected_vertex >= 0 &&
          selected_vertex < int(path.points.size()) && path.points[selected_vertex].custom &&
          path_display.tangents)
      {
        const auto handles = bts::surface_path_handles(path, selected_vertex);
        for (int h = 0; h < 2; ++h) {
          auto position = path.points[selected_vertex].position;
          for (int c = 0; c < 3; ++c)
            position[c] += handles[h][c];
          const double distance = QLineF(screen, project(position)).length();
          if (distance < tolerance && (best.handle < 0 || distance < nearest)) {
            nearest = distance;
            best = {path.id, selected_vertex, h, -1, 0};
          }
        }
      }
      for (int i = 0; i < int(path.points.size()); ++i) {
        const double distance = QLineF(screen, project(path.points[i].position)).length();
        if (distance <= nearest) {
          nearest = distance;
          best = {path.id, i, -1, -1, 0};
        }
      }
    }
    if (!best.id.empty())
      return best;
    for (auto item = paths.rbegin(); item != paths.rend(); ++item) {
      const auto found = path_draw.find((*item)->id);
      if (found == path_draw.end())
        continue;
      const auto &samples = found->second.samples;
      for (std::size_t i = 1; i < samples.size(); ++i) {
        const auto a = project(samples[i - 1].point.position),
                   b = project(samples[i].point.position), axis = b - a, delta = screen - a;
        const double length = axis.x() * axis.x() + axis.y() * axis.y(),
                     t = length < 1e-12 ?
                             0 :
                             std::clamp(
                                 (axis.x() * delta.x() + axis.y() * delta.y()) / length, 0., 1.);
        const double distance = QLineF(screen, a + t * axis).length();
        if (distance < nearest) {
          nearest = distance;
          const auto &left = samples[i - 1], &right = samples[i];
          best = {
              (*item)->id,
              -1,
              -1,
              int(right.segment),
              std::lerp(left.segment == right.segment ? left.parameter : 0., right.parameter, t)};
        }
      }
    }
    return best;
  }

  std::function<void(const bts::SurfacePathPoint &)> path_point_added;
  std::function<void(const std::string &)> path_selected, path_removed;
  std::function<void()> path_finished;
  void position_paths_panel()
  {
    if (!paths_panel)
      return;
    const auto pane = pane_rect(bts::ViewKind::Scene);
    paths_panel->setVisible(path_active && paint_enabled && !pane.isEmpty());
    if (path_curve_panel) {
      path_curve_panel->setVisible(path_active && paint_enabled && !pane.isEmpty());
      if (!pane.isEmpty())
        path_curve_panel->setGeometry(int(pane.left()) + 8,
                                      int(pane.bottom()) - 120,
                                      std::max(180, std::min(500, int(pane.width()) - 16)),
                                      112);
    }
    if (!pane.isEmpty()) {
      const int width = std::min(230, int(pane.width()) - 16);
      paths_panel->setGeometry(int(pane.right()) - width - 8, int(pane.top()) + 34, width, 180);
      paths_panel->raise();
    }
  }
  std::optional<bts::SurfacePathPoint> pick_path_point(QPointF point)
  {
    if (!selected_material_)
      return {};
    const auto pane = pane_at(point);
    if (!pane || pane->kind != bts::ViewKind::Scene)
      return {};
    const double scale = devicePixelRatioF();
    const auto inverse = (camera_.projection_matrix({pane->width, pane->height}) *
                          camera_.view_matrix())
                             .inverted();
    const auto source = scene_source(*pane, point);
    if (!source)
      return {};
    const float x = float(2 * source->x() - 1), y = float(1 - 2 * source->y());
    const auto near_point = inverse * QVector4D(x, y, -1, 1),
               far_point = inverse * QVector4D(x, y, 1, 1);
    const auto origin = near_point.toVector3DAffine(),
               direction = (far_point.toVector3DAffine() - origin).normalized();
    bts::BrushProjection projection;
    projection.cull = false;
    const auto rotation = camera_.view_matrix().inverted();
    for (int c = 0; c < 3; ++c) {
      projection.camera_right[c] = rotation.column(0)[c];
      projection.camera_up[c] = rotation.column(1)[c];
    }
    const auto hit = host_.surface_stamp(*selected_material_,
                                         {origin.x(), origin.y(), origin.z()},
                                         {direction.x(), direction.y(), direction.z()},
                                         10,
                                         true,
                                         false,
                                         projection);
    if (!hit || !hit->footprint.surface)
      return {};
    bts::SurfacePathPoint result;
    for (int c = 0; c < 3; ++c) {
      result.position[c] = hit->position[c];
      result.normal[c] = hit->footprint.surface->normal[c];
    }
    result.uv = hit->footprint.center;
    result.pressure = input_pressure_;
    return result;
  }
  std::string hit_path(QPointF point) const
  {
    for (auto path = paths.rbegin(); path != paths.rend(); ++path)
      for (std::size_t i = 1; i < (*path)->points.size(); ++i) {
        const auto project = [&](const auto &p) {
          return scene_point({float(p.position[0]), float(p.position[1]), float(p.position[2])});
        };
        const auto a = project((*path)->points[i - 1]), b = project((*path)->points[i]);
        const auto axis = b - a, delta = point - a;
        const double length = axis.x() * axis.x() + axis.y() * axis.y(),
                     t = length < 1e-12 ?
                             0 :
                             std::clamp(
                                 (axis.x() * delta.x() + axis.y() * delta.y()) / length, 0., 1.);
        if (QLineF(a + t * axis, point).length() <= 6)
          return (*path)->id;
      }
    return {};
  }
  std::optional<bts::PaintPixels::Stamp> clone_source, clone_source_display;
  bool clone_key = false;
  std::function<bool(QPointF)> picker_sample;
  std::optional<QPointF> last_picker_uv;
  bool skew_editing = false;
  std::optional<bts::SkewField> skew_field;
  std::vector<bts::SkewSurfaceVector> skew_vectors;
  std::shared_ptr<const bts::SkewGeometry> skew_geometry;
  std::function<void()> skew_invert;
  std::function<bool()> stroke_begin;
  std::function<void(const std::vector<bts::PaintPixels::Stamp> &, std::size_t)> stroke_stamps;
  std::function<void(bool)> stroke_end;
  bool painting_ = false;
  bts::ViewKind paint_view_ = bts::ViewKind::Uv;
  QPointF paint_last_, cursor_;
  bts::StrokeSpacing stroke_spacing_;
  bts::JitterSampler jitter_sampler_;
  bts::StrokeDirection stroke_direction_;
  std::uint64_t jitter_stroke_serial_ = 0;
  bool stencil_key = false, stencil_ignore = false;
  Qt::MouseButton stencil_drag = Qt::NoButton;
  std::optional<bts::BrushStencil> stencil_before;
  QPointF stencil_start;
  double stencil_drag_height = 1;
  std::function<void(const bts::BrushStencil &)> stencil_changed;
  std::array<QImage, 2> stencil_images;
  std::optional<bts::BrushStencil> stencil_image_settings;
  std::optional<bts::ProjectionMaterial> projection_image_settings;
  QByteArray projection_display_key;
  const bts::BrushStencil &active_transform() const
  {
    return brush->projected ? brush->projected->transform : brush->stencil;
  }
  bool transform_available() const
  {
    return brush && (brush->projected || brush->stencil.resource);
  }
  bool capture_jitter = false;
  QJsonArray jitter_dabs;
  bts::LazyMouse lazy_follower_;
  std::function<void()> lazy_toggle_requested;
  float input_pressure_ = 1, paint_pressure_ = 1;
  bool tablet_dispatch_ = false, tablet_contact_ = false;
  QElapsedTimer tablet_recent_;
  QPointer<const QPointingDevice> tablet_device_;
  QString pointer_name_ = "Mouse";
  bool pressure_overlay_ = false;
  bool input_spline_enabled_ = true;
  void cancel_tablet_contact()
  {
    if (tablet_contact_)
      end_paint(true);
    tablet_contact_ = false;
    if (pointer_name_.startsWith("Pen:"))
      input_pressure_ = 0;
    update();
  }
  std::array<double, 2> input_tilt_{}, paint_tilt_{};
  struct PointerSample {
    QPointF position;
    float pressure;
    quint64 timestamp;
    std::array<double, 2> tilt;
  };
  QTimer input_timer_;
  std::vector<PointerSample> pointer_samples_;
  std::vector<bts::PaintPixels::Stamp> provisional_ink_tail_;
  std::vector<bts::PaintPixels::Stamp> *frame_stamps_ = nullptr;
  bool hover_pending_ = false;
  std::uint64_t input_frames_ = 0, stroke_batches_ = 0, hover_picks_ = 0;
  float first_dab_diameter_ = 0, last_dab_diameter_ = 0;
  bool ignore_tablet_mouse(QMouseEvent *event) const
  {
    return !tablet_dispatch_ && (bts::tablet::pen(event->pointingDevice()) || tablet_contact_ ||
                                 (event->source() != Qt::MouseEventNotSynthesized &&
                                  tablet_recent_.isValid() && tablet_recent_.elapsed() < 100));
  }
  void queue_pointer(QPointF position, quint64 timestamp)
  {
    pointer_samples_.push_back({position, input_pressure_, timestamp, input_tilt_});
    ++input_points_;
    if (!input_timer_.isActive())
      input_timer_.start();
  }
  void dispatch_stamps(const std::vector<bts::PaintPixels::Stamp> &stamps,
                       std::optional<std::size_t> stable = {})
  {
    if ((!stamps.empty() || stable) && stroke_stamps) {
      ++stroke_batches_;
      stroke_stamps(stamps, stable.value_or(stamps.size()));
    }
  }
  void flush_pointer_input()
  {
    input_timer_.stop();
    auto samples = std::move(pointer_samples_);
    pointer_samples_.clear();
    if (painting_ && !samples.empty()) {
      ++input_frames_;
      std::vector<bts::PaintPixels::Stamp> stamps;
      frame_stamps_ = &stamps;
      std::size_t stable_count = 0;
      try {
        if (stable_sampling_) {
          restore_sampling(*stable_sampling_);
          for (const auto &sample : samples) {
            if (!painting_)
              break;
            QPointF point = sample.position;
            if (brush->lazy_mouse) {
              const auto filtered = lazy_follower_.follow({point.x(), point.y()},
                                                          brush->lazy_distance / 100 *
                                                              pane_rect(paint_view_).height());
              point = {filtered[0], filtered[1]};
            }
            if (auto curve = input_spline_.append(
                    {{point.x(), point.y()}, sample.pressure, sample.tilt}))
              render_curve(*curve);
          }
          stable_count = stamps.size();
          if (painting_) {
            stable_sampling_ = sampling_state();
            if (auto curve = input_spline_.tail())
              render_curve(*curve);
          }
        }
        else {
          for (const auto &sample : samples) {
            if (!painting_)
              break;
            input_pressure_ = sample.pressure;
            input_tilt_ = sample.tilt;
            paint_to(sample.position);
          }
          stable_count = stamps.size();
        }
      }
      catch (const std::exception &error) {
        qWarning("BTS_STROKE_SPLINE: %s", error.what());
        end_paint(true);
      }
      input_pressure_ = samples.back().pressure;
      input_tilt_ = samples.back().tilt;
      frame_stamps_ = nullptr;
      if (painting_) {
        provisional_ink_tail_.assign(stamps.begin() + stable_count, stamps.end());
        dispatch_stamps(stamps, stable_count);
      }
    }
    if (hover_pending_) {
      hover_pending_ = false;
      if (paint_enabled && !painting_)
        if (const auto pane = pane_at(cursor_); pane && pane->kind == bts::ViewKind::Scene) {
          ++hover_picks_;
          surface_at(*pane, cursor_);
        }
      update();
    }
  }
  bool cursor_visible_ = false;
  bts::DynamicStrokeSampler dynamic_sampler_;
  bool capture_dynamic = false;
  QJsonArray dynamic_dabs;
  bts::StrokeSpline input_spline_;
  struct SamplingState {
    bts::StrokeSpacing spacing;
    bts::JitterSampler jitter;
    bts::StrokeDirection direction;
    bts::DynamicStrokeSampler dynamic;
    QPointF last;
    float pressure;
    std::array<double, 2> tilt;
    QJsonArray jitter_dabs, dynamic_dabs, spline_dabs;
    float first_diameter, last_diameter;
  };
  std::optional<SamplingState> stable_sampling_;
  std::optional<std::array<bool, 2>> compression_before_;
  bool capture_spline = false;
  QJsonArray spline_dabs;
  std::uint64_t input_points_ = 0;
  SamplingState sampling_state() const
  {
    return {stroke_spacing_,
            jitter_sampler_,
            stroke_direction_,
            dynamic_sampler_,
            paint_last_,
            paint_pressure_,
            paint_tilt_,
            jitter_dabs,
            dynamic_dabs,
            spline_dabs,
            first_dab_diameter_,
            last_dab_diameter_};
  }
  void restore_sampling(const SamplingState &state)
  {
    stroke_spacing_ = state.spacing;
    jitter_sampler_ = state.jitter;
    stroke_direction_ = state.direction;
    dynamic_sampler_ = state.dynamic;
    paint_last_ = state.last;
    paint_pressure_ = state.pressure;
    paint_tilt_ = state.tilt;
    jitter_dabs = state.jitter_dabs;
    dynamic_dabs = state.dynamic_dabs;
    spline_dabs = state.spline_dabs;
    first_dab_diameter_ = state.first_diameter;
    last_dab_diameter_ = state.last_diameter;
  }
  void render_curve(const bts::StrokeCurve &curve)
  {
    curve.flatten([&](const bts::StrokeSample &sample) {
      if (!painting_)
        return;
      input_pressure_ = float(sample.pressure);
      input_tilt_ = sample.tilt;
      paint_to({sample.position[0], sample.position[1]}, false, true);
    });
  }
  void begin_sampling(QPointF point)
  {
    if (!input_spline_enabled_ || property("tabletTestLinearInput").toBool())
      return;
    input_spline_.reset({{point.x(), point.y()}, input_pressure_, input_tilt_});
    stable_sampling_ = sampling_state();
    ++input_points_;
    compression_before_ = std::array{
        QCoreApplication::testAttribute(Qt::AA_CompressHighFrequencyEvents),
        QCoreApplication::testAttribute(Qt::AA_CompressTabletEvents)};
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
  }
  void set_secondary_active(bool active)
  {
    if (active == secondary_active_)
      return;
    if (painting_) {
      flush_pointer_input();
      if (painting_ && stable_sampling_) {
        if (!provisional_ink_tail_.empty())
          dispatch_stamps(provisional_ink_tail_, provisional_ink_tail_.size());
        provisional_ink_tail_.clear();
        stable_sampling_ = sampling_state();
        input_spline_.reset({{paint_last_.x(), paint_last_.y()}, input_pressure_, input_tilt_});
      }
    }
    secondary_active_ = active;
    update();
  }
  struct StrokeAnchor {
    bts::ViewKind kind;
    QPointF uv;
    QVector3D world;
  };
  std::optional<StrokeAnchor> stroke_anchor;
  QString paint_target;
  bool straight_click_ = false;
  QVector3D surface_position_;
  void set_paint_target(QString target)
  {
    if (paint_target != target) {
      fill_gizmo.finish(true);
      stroke_anchor.reset();
      pending_path_point.reset();
      selected_path.clear();
      selected_vertex = -1;
      path_draw.clear();
      path_editing = false;
      clone_source.reset();
      clone_source_display.reset();
      paint_target = std::move(target);
    }
  }
  void remember_anchor()
  {
    const auto pane = pane_at(paint_last_);
    if (!pane || pane->kind != paint_view_)
      return;
    if (paint_view_ == bts::ViewKind::Uv) {
      const double scale = devicePixelRatioF();
      stroke_anchor = StrokeAnchor{paint_view_,
                                   {(paint_last_.x() * scale - pane->x - pane->width * .5) *
                                            uv_vertical_extent_ / pane->height +
                                        uv_center_.x(),
                                    -(paint_last_.y() * scale - pane->y - pane->height * .5) *
                                            uv_vertical_extent_ / pane->height +
                                        uv_center_.y()},
                                   {}};
    }
    else if (surface_at(*pane, paint_last_))
      stroke_anchor = StrokeAnchor{paint_view_, {}, surface_position_};
  }
  void end_paint(bool cancel)
  {
    if (!painting_)
      return;
    if (cancel) {
      pointer_samples_.clear();
      input_timer_.stop();
    }
    else if (!frame_stamps_)
      flush_pointer_input();
    painting_ = false;
    stable_sampling_.reset();
    provisional_ink_tail_.clear();
    if (compression_before_) {
      QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents,
                                     (*compression_before_)[0]);
      QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, (*compression_before_)[1]);
      compression_before_.reset();
    }
    if (!cancel)
      remember_anchor();
    if (stroke_end)
      stroke_end(cancel);
    update();
  }
  double cursor_radius_ = 0, cursor_diameter_ = 10;
  std::optional<bts::PaintPixels::Stamp> surface_at(
      const bts::ViewPane &pane,
      QPointF point,
      float diameter = -1,
      bool coverage = false,
      const bts::SymmetryTransform *transform = nullptr,
      bool sampling = false)
  {
    if (!brush || !selected_material_)
      return {};
    if (!transform)
      cursor_radius_ = 0;
    if (diameter < 0)
      diameter = brush->diameter_at(input_pressure_);
    if (diameter <= 0)
      return {};
    const double scale = devicePixelRatioF();
    const auto matrix = camera_.projection_matrix({pane.width, pane.height}) *
                        camera_.view_matrix();
    bool invertible;
    const auto inverse = matrix.inverted(&invertible);
    if (!invertible)
      return {};
    const auto source = scene_source(pane, point);
    if (!source)
      return {};
    const float nx = float(source->x() * 2 - 1), ny = float(1 - source->y() * 2);
    const auto near_point = (inverse * QVector4D(nx, ny, -1, 1)).toVector3DAffine();
    const auto far_point = (inverse * QVector4D(nx, ny, 1, 1)).toVector3DAffine();
    const auto direction = (far_point - near_point).normalized();
    auto projection = brush->projection;
    if (sampling) {
      projection.cull = false;
      projection.alignment = bts::BrushAlignment::Camera;
    }
    const auto camera_rotation = camera_.view_matrix().inverted();
    const auto camera_right = camera_rotation.column(0).toVector3D().normalized(),
               camera_up = camera_rotation.column(1).toVector3D().normalized();
    projection.camera_right = {camera_right.x(), camera_right.y(), camera_right.z()};
    projection.camera_up = {camera_up.x(), camera_up.y(), camera_up.z()};
    auto origin = near_point, ray = direction;
    if (transform) {
      const auto o = transform->point(origin), d = transform->vector(ray),
                 r = transform->vector(projection.camera_right),
                 u = transform->vector(projection.camera_up);
      origin = {float(o[0]), float(o[1]), float(o[2])};
      ray = {float(d[0]), float(d[1]), float(d[2])};
      for (int i = 0; i < 3; ++i) {
        projection.camera_right[i] = float(r[i]);
        projection.camera_up[i] = float(u[i]);
      }
      projection.orientation = transform->orientation;
      projection.symmetry_ray = true;
    }
    const auto sample = host_.surface_stamp(*selected_material_,
                                            {origin.x(), origin.y(), origin.z()},
                                            {ray.x(), ray.y(), ray.z()},
                                            diameter,
                                            coverage,
                                            brush && brush->alpha.kind != bts::AlphaKind::Shape,
                                            projection);
    if (!sample)
      return {};
    const auto project = [&](QVector3D world) {
      const auto clip = matrix * QVector4D(world, 1);
      const auto p = host_.lens_projection().to_display(
          {(clip.x() / clip.w() + 1) * .5, (1 - clip.y() / clip.w()) * .5});
      if (!p)
        return QPointF(-1e6, -1e6);
      return QPointF((*p)[0] * pane.width, (*p)[1] * pane.height) / scale;
    };
    const auto &p = sample->position;
    const QVector3D center(p[0], p[1], p[2]);
    if (!transform) {
      surface_position_ = center;
      surface_triangle_id_ = sample->triangle_id;
    }
    const auto right = camera_.view_matrix().inverted().column(0).toVector3D().normalized();
    const auto offset = project(center + right * sample->radius) - project(center);
    if (!transform) {
      cursor_radius_ = std::hypot(offset.x(), offset.y());
      cursor_diameter_ = diameter;
    }
    return sample->footprint;
  }
  void apply_polygon_selection(QPointF end)
  {
    if (rendering)
      return;
    if (!symmetry_scene || !selected_material_ || !polygon_apply)
      return;
    const auto pane = pane_at(polygon_start);
    if (!pane)
      return;
    std::size_t material = symmetry_scene->materials.size();
    for (std::size_t i = 0; i < symmetry_scene->materials.size(); ++i)
      if (symmetry_scene->materials[i].session_id == *selected_material_)
        material = i;
    std::vector<std::size_t> seeds;
    const auto &triangles = symmetry_scene->uv_triangles;
    const bool rectangle = QLineF(polygon_start, end).length() >= 3;
    if (!rectangle && pane->kind == bts::ViewKind::Scene) {
      if (surface_at(*pane, polygon_start, 1, false, nullptr, true))
        for (std::size_t i = 0; i < triangles.size(); ++i)
          if (triangles[i].triangle_id == surface_triangle_id_ &&
              triangles[i].material == material)
          {
            seeds.push_back(i);
            break;
          }
    }
    else {
      const QRectF selection = QRectF(polygon_start, end).normalized();
      QPainterPath rectangle_path;
      rectangle_path.addRect(selection);
      QPointF uv;
      if (!rectangle) {
        const double scale = devicePixelRatioF();
        uv = {(end.x() * scale - pane->x - pane->width * .5) * uv_vertical_extent_ / pane->height +
                  uv_center_.x(),
              -(end.y() * scale - pane->y - pane->height * .5) * uv_vertical_extent_ /
                      pane->height +
                  uv_center_.y()};
      }
      for (std::size_t i = 0; i < triangles.size(); ++i) {
        const auto &t = triangles[i];
        if (t.material != material)
          continue;
        if (!rectangle) {
          if (bts::uv_triangle_contains(t, uv.x(), uv.y())) {
            seeds.push_back(i);
            break;
          }
          continue;
        }
        QPolygonF points;
        for (int k = 0; k < 3; ++k)
          points << (pane->kind == bts::ViewKind::Uv ?
                         uv_point({t.corners[k][0], t.corners[k][1]}) :
                         scene_point({t.positions[k][0], t.positions[k][1], t.positions[k][2]}));
        QPainterPath path;
        path.addPolygon(points);
        path.closeSubpath();
        if (path.intersects(rectangle_path) || rectangle_path.contains(points[0]))
          seeds.push_back(i);
      }
    }
    try {
      const auto selection = bts::polygon_fill_selection(
          *symmetry_scene,
          material,
          bts::polygon_fill_mode(polygon_mode, pane->kind == bts::ViewKind::Uv),
          seeds);
      if (!selection.empty())
        polygon_apply(selection);
    }
    catch (const std::exception &error) {
      qWarning("BTS_POLYGON_FILL: %s", error.what());
    }
  }
  void paint_to(QPointF position, bool first = false, bool bypass_lazy = false)
  {
    if (rendering)
      return;
    if (!brush || !selected_material_)
      return;
    if (first) {
      if (capture_spline)
        spline_dabs = {};
      first_dab_diameter_ = 0;
      if (capture_dynamic)
        dynamic_dabs = {};
      dynamic_sampler_.reset(jitter_stroke_serial_ + 1);
      stroke_direction_.reset();
      jitter_sampler_.reset(++jitter_stroke_serial_);
      if (capture_jitter)
        jitter_dabs = {};
    }
    if (!bypass_lazy && brush->lazy_mouse) {
      if (first)
        lazy_follower_.reset({position.x(), position.y()});
      else {
        const auto point = lazy_follower_.follow({position.x(), position.y()},
                                                 brush->lazy_distance / 100 *
                                                     pane_rect(paint_view_).height());
        position = {point[0], point[1]};
      }
    }
    std::vector<bts::PaintPixels::Stamp> stamps;
    const QPointF segment = position - paint_last_;
    const double length_squared = segment.x() * segment.x() + segment.y() * segment.y();
    const auto emit_stamp = [&](bts::StrokeSpacing::Point sample) {
      std::optional<bts::performance::Scope> timing;
      if (measure_brush_dabs_)
        timing.emplace("stroke_geometry_dab_cpu");
      const QPointF point(sample[0], sample[1]);
      const auto pane = pane_at(point);
      if (!pane || pane->kind != paint_view_)
        return .5;
      const double t = first || length_squared < 1e-12 ?
                           1 :
                           std::clamp(((point.x() - paint_last_.x()) * segment.x() +
                                       (point.y() - paint_last_.y()) * segment.y()) /
                                          length_squared,
                                      0.,
                                      1.);
      const float pressure = float(paint_pressure_ + (input_pressure_ - paint_pressure_) * t);
      const float brush_diameter = brush->diameter_at(pressure);
      if (bts::tablet::logging())
        bts::tablet::log({{"event", "dab"},
                          {"device", pointer_name_},
                          {"used_pressure", pressure},
                          {"diameter", brush_diameter},
                          {"flow", brush->flow_at(pressure)},
                          {"pressure_size", brush->pressure_size},
                          {"pressure_flow", brush->pressure_flow},
                          {"minimum_size", brush->minimum_size},
                          {"minimum_flow", brush->minimum_flow},
                          {"stroke_opacity", brush->stroke_opacity}});
      if (capture_spline)
        spline_dabs.append(QJsonObject{{"point", QJsonArray{point.x(), point.y()}},
                                       {"pressure", pressure},
                                       {"tilt",
                                        QJsonArray{std::lerp(paint_tilt_[0], input_tilt_[0], t),
                                                   std::lerp(paint_tilt_[1], input_tilt_[1], t)}},
                                       {"diameter", brush_diameter},
                                       {"flow", brush->flow_at(pressure)}});
      if (first_dab_diameter_ == 0)
        first_dab_diameter_ = brush_diameter;
      last_dab_diameter_ = brush_diameter;
      const auto jitter = first && brush->follow_path ? bts::JitterDab{} :
                                                        jitter_sampler_.sample(brush->jitter);
      if (brush_diameter <= 0)
        return .5;
      std::optional<bts::PaintPixels::Stamp> stamp;
      const double scale = devicePixelRatioF();
      double diameter = 0;
      if (paint_view_ == bts::ViewKind::Uv) {
        if (!host_.uv_preview_summary())
          return .5;
        const double x = (point.x() * scale - pane->x - pane->width * .5) * uv_vertical_extent_ /
                             pane->height +
                         uv_center_.x();
        const double y = -(point.y() * scale - pane->y - pane->height * .5) * uv_vertical_extent_ /
                             pane->height +
                         uv_center_.y();
        const double nominal_radius = brush_diameter / 200, radius = nominal_radius * jitter.size;
        stamp = bts::PaintPixels::Stamp{
            {x + jitter.position[0] * nominal_radius, y - jitter.position[1] * nominal_radius},
            {radius, 0},
            {0, radius},
            {}};
        diameter = brush_diameter / 100 * pane->height / uv_vertical_extent_ / scale;
      }
      QPointF scattered = point;
      if (paint_view_ == bts::ViewKind::Scene) {
        stamp = surface_at(*pane, point, brush_diameter, true);
        diameter = cursor_radius_ * 2;
        scattered += QPointF(jitter.position[0], jitter.position[1]) * (diameter * .5);
        if (stamp && (jitter.size != 1 || jitter.position != std::array<double, 2>{})) {
          stamp = surface_at(*pane, scattered, float(brush_diameter * jitter.size), true);
          cursor_radius_ = diameter * .5;
        }
      }
      std::optional<double> heading;
      if (stamp) {
        if (stamp->surface)
          heading = stroke_direction_.world(
              stamp->surface->center, stamp->surface->tangent, stamp->surface->bitangent);
        else
          heading = stroke_direction_.uv(stamp->center);
      }
      const double orientation = brush->angle * 3.14159265358979323846 / 180 +
                                 (brush->follow_path && heading ? *heading : 0) + jitter.angle;
      auto emitted_alpha = brush->alpha;
      const auto append = [&](bts::PaintPixels::Stamp value) {
        value.alpha = emitted_alpha;
        value.hardness = brush->hardness;
        value.flow = brush->flow_at(pressure) / 100 * jitter.flow;
        bts::rotate_brush_stamp(value, orientation);
        value.values = bts::temporary_ink(brush->values, secondary_color, secondary_active_);
        value.channels = brush->channels;
        if ((brush->stencil.resource && !stencil_ignore) || brush->projected) {
          auto mapping = std::make_shared<bts::StencilMapping>();
          mapping->stencil = brush->stencil;
          mapping->world = paint_view_ == bts::ViewKind::Scene;
          mapping->uv_center = {uv_center_.x(), uv_center_.y()};
          mapping->uv_extent = uv_vertical_extent_;
          mapping->viewport_aspect = double(pane->width) / pane->height;
          if (mapping->world) {
            mapping->lens = host_.lens_projection();
            const auto matrix = camera_.projection_matrix({pane->width, pane->height}) *
                                camera_.view_matrix();
            for (int r = 0; r < 4; ++r)
              for (int c = 0; c < 4; ++c)
                mapping->projection[r * 4 + c] = matrix(r, c);
            if (!value.surface && symmetry_scene) {
              const double rx = std::abs(value.axis_x[0]) + std::abs(value.axis_y[0]),
                           ry = std::abs(value.axis_x[1]) + std::abs(value.axis_y[1]);
              for (const auto &triangle : symmetry_scene->uv_triangles)
                if (symmetry_scene->materials[triangle.material].session_id == *selected_material_)
                {
                  double x0 = 10000, x1 = -10000, y0 = 10000, y1 = -10000;
                  for (const auto &uv : triangle.corners) {
                    x0 = std::min(x0, double(uv[0]));
                    x1 = std::max(x1, double(uv[0]));
                    y0 = std::min(y0, double(uv[1]));
                    y1 = std::max(y1, double(uv[1]));
                  }
                  if (x1 < value.center[0] - rx || x0 > value.center[0] + rx ||
                      y1 < value.center[1] - ry || y0 > value.center[1] + ry)
                    continue;

                  bts::StencilTriangle copy;
                  for (int i = 0; i < 3; ++i) {
                    copy.uv[i] = {triangle.corners[i][0], triangle.corners[i][1]};
                    for (int c = 0; c < 3; ++c)
                      copy.position[i][c] = triangle.positions[i][c];
                  }
                  mapping->uv_world.push_back(std::move(copy));
                }
            }
          }
          if (brush->stencil.resource && !stencil_ignore)
            value.stencil = mapping;
          if (brush->projected) {
            auto projected = std::make_shared<bts::ProjectionMapping>();
            projected->mapping = *mapping;
            projected->source = *brush->projected;
            projected->source.values = bts::temporary_ink(
                brush->values, secondary_color, secondary_active_);
            if (paint_mask)
              projected->source.values.roughness = bts::temporary_mask_ink(brush->mask_value,
                                                                           secondary_active_);
            value.projected = std::move(projected);
          }
        }
        if (paint_mask) {
          value.values.roughness = bts::temporary_mask_ink(brush->mask_value, secondary_active_);
          value.channels = {false, false, true, false, false};
        }
        value.coverage_mask = quick_mask_gate;
        stamps.push_back(std::move(value));
      };
      if (stamp && (brush->smudge || !brush->follow_path || heading)) {
        if (emitted_alpha.kind == bts::AlphaKind::DynamicOutline)
          emitted_alpha.dynamic.phase = dynamic_sampler_.phase(brush->alpha.dynamic);
        if (capture_dynamic && emitted_alpha.kind == bts::AlphaKind::DynamicOutline)
          dynamic_dabs.append(emitted_alpha.dynamic.phase);
        if (capture_jitter)
          jitter_dabs.append(
              QJsonObject{{"size", jitter.size},
                          {"flow", jitter.flow},
                          {"angle", jitter.angle},
                          {"orientation", orientation},
                          {"position", QJsonArray{jitter.position[0], jitter.position[1]}},
                          {"view", paint_view_ == bts::ViewKind::Scene ? "3D" : "2D"}});
        append(*stamp);
        last_symmetry_copies = 1;
        if (paint_view_ == bts::ViewKind::Scene && symmetry && symmetry->enabled)
          for (const auto &transform : bts::symmetry_transforms(*symmetry))
            if (auto copy = surface_at(
                    *pane, scattered, float(brush_diameter * jitter.size), true, &transform))
            {
              append(*copy);
              ++last_symmetry_copies;
            }
      }
      // Half a logical pixel bounds pathological oversampling for subpixel brushes.
      return std::max(.5, diameter * brush->spacing / 100);
    };
    try {
      if (first)
        stroke_spacing_.reset({position.x(), position.y()},
                              emit_stamp({position.x(), position.y()}));
      else
        stroke_spacing_.advance({position.x(), position.y()}, emit_stamp);
      paint_last_ = position;
      paint_pressure_ = input_pressure_;
      paint_tilt_ = input_tilt_;
      if (frame_stamps_)
        frame_stamps_->insert(frame_stamps_->end(),
                              std::make_move_iterator(stamps.begin()),
                              std::make_move_iterator(stamps.end()));
      else
        dispatch_stamps(stamps);
      if (first && !bypass_lazy && painting_)
        begin_sampling(position);
    }
    catch (const std::exception &error) {
      qWarning("BTS_STROKE_INPUT: %s", error.what());
      end_paint(true);
    }
    update();
  }
  std::function<void()> prepare_frame;
  std::function<void(double)> frame_done;
  std::function<bool()> texture_interaction;
  std::function<void(bts::ViewLayoutState)> layout_changed;
  std::function<void(bts::OrbitCamera::Projection)> projection_changed;
  const bts::SymmetrySettings *symmetry = nullptr;
  const bts::SceneSnapshot *symmetry_scene = nullptr;
  bool geometry_tile_editing = false;
  std::set<int> geometry_excluded_tiles, hidden_uv_tiles;
  std::vector<std::vector<bts::GeometryClipVertex>> geometry_hatch_shapes;
  std::function<void(const bts::SymmetrySettings &)> symmetry_changed;
  std::function<void()> symmetry_toggle_requested;
  int last_symmetry_copies = 1;
  bool symmetry_drag = false;
  QPointF symmetry_previous;
  std::optional<bts::SymmetrySettings> symmetry_before;
  QVector3D symmetry_origin() const
  {
    if (!symmetry)
      return {};
    QVector3D point;
    if (symmetry->mode == bts::SymmetryMode::Mirror)
      point[symmetry->axis] = symmetry->offset;
    else
      for (int i = 0; i < 3; ++i)
        point[i] = symmetry->center[i];
    return point;
  }
  std::optional<QLineF> symmetry_handle() const
  {
    if (!symmetry || !symmetry->enabled || !symmetry->manipulator ||
        pane_rect(bts::ViewKind::Scene).isEmpty())
      return {};
    const auto center = scene_point(symmetry_origin());
    QPointF axis(1, 0);
    if (symmetry->mode == bts::SymmetryMode::Mirror) {
      QVector3D direction;
      direction[symmetry->axis] = 1;
      axis = scene_point(symmetry_origin() + direction) - center;
      const double length = std::hypot(axis.x(), axis.y());
      if (length < 1e-5)
        return {};
      axis /= length;
    }
    return QLineF(center - axis * 34, center + axis * 34);
  }
  std::function<void(bool)> tool_requested;
  std::function<void(bts::InspectionMode)> inspection_requested;
  std::array<bool, 5> inspection_channels{};
  bool inspection_mask = false, inspection_ao = false, inspection_displacement = false;
  std::array<QString, bts::custom_channel_count> inspection_users;
  QString inspection_label(bts::InspectionMode mode) const
  {
    if (mode == bts::InspectionMode::Displacement)
      return "Displacement";
    if (int(mode) >= 7)
      return inspection_users.at(int(mode) - 7).isEmpty() ? QString("User%1").arg(int(mode) - 7) :
                                                            inspection_users.at(int(mode) - 7);
    static const QStringList labels{
        "Base Color", "Metallic", "Roughness", "Normal", "Bump", "Mask", "Ambient occlusion"};
    return int(mode) < 0 ? QString("Material") : labels.at(int(mode));
  }
  bool handle_inspection_key(int key, bool previous = false)
  {
    if (!inspection_requested || (key != Qt::Key_C && key != Qt::Key_M))
      return false;
    int channel = -1;
    if (key == Qt::Key_C) {
      const int current = mesh_map_active >= 0 ? -1 : int(host_.inspection_mode());
      std::vector<int> channels;
      for (int i = 0; i < 5; ++i)
        if (i != 3 && inspection_channels[i])
          channels.push_back(i);
      if (inspection_ao)
        channels.push_back(6);
      if (inspection_displacement)
        channels.push_back(15);
      for (int slot = 0; slot < bts::custom_channel_count; ++slot)
        if (!inspection_users[slot].isEmpty())
          channels.push_back(7 + slot);
      if (channels.empty())
        return true;
      const auto found = std::find(channels.begin(), channels.end(), current);
      int index = found == channels.end() ? (previous ? 0 : -1) : int(found - channels.begin());
      index = (index + (previous ? int(channels.size()) - 1 : 1)) % int(channels.size());
      channel = channels[index];
    }
    inspection_requested(bts::InspectionMode(channel));
    return true;
  }
  std::vector<bts::ViewPane> visible_panes() const
  {
    return panes();
  }
  int frame_count = 0;
  double last_ms = 0;

  void set_trackball(bool enabled)
  {
    trackball_enabled_ = enabled;
  }
  bool trackball_enabled() const
  {
    return trackball_enabled_;
  }
  void set_viewport_clipping(bts::ViewportClipping value)
  {
    if (!camera_.set_clipping(value))
      return;
    if (default_camera_)
      default_camera_->set_clipping(value);
    update();
  }
  bts::ViewportClipping viewport_clipping() const
  {
    return camera_.clipping_settings();
  }
  void clipping_demo_front()
  {
    detach_scene_camera();
    camera_.orbit({camera_.yaw_degrees() / .35, -camera_.pitch_degrees() / .35}, false);
    set_projection(bts::OrbitCamera::Projection::Perspective);
    frame_document();
    update();
  }
  void set_scene_cameras(const std::vector<bts::SceneCamera> &cameras)
  {
    symmetry_lines_.invalidate();
    if (scene_cameras_ == cameras)
      return;
    end_gesture();
    end_paint(true);
    if (current_imported_camera_) {
      camera_.release_scene_camera(scene_pane_size());
      if (default_camera_)
        camera_ = *default_camera_;
    }
    scene_cameras_ = cameras;
    current_imported_camera_.reset();
    last_imported_camera_.reset();
    {
      const QSignalBlocker block(camera_selector_);
      camera_selector_->clear();
      camera_selector_->addItem("Default camera", 0u);
      for (const auto &camera : cameras)
        camera_selector_->addItem(QString::fromStdString(camera.name), camera.session_id);
      camera_selector_->setCurrentIndex(0);
    }
    camera_back_->hide();
    camera_notice_->hide();
    position_camera_controls();
  }
  QJsonObject scene_camera_state() const
  {
    QJsonArray cameras;
    for (const auto &camera : scene_cameras_)
      cameras.append(QJsonObject{
          {"name", QString::fromStdString(camera.name)},
          {"uid", qint64(camera.session_id)},
          {"position", QJsonArray{camera.position[0], camera.position[1], camera.position[2]}},
          {"forward", QJsonArray{camera.forward[0], camera.forward[1], camera.forward[2]}},
          {"up", QJsonArray{camera.up[0], camera.up[1], camera.up[2]}},
          {"vertical_fov", camera.vertical_fov},
          {"aspect", camera.aspect}});
    return {{"selected", camera_selector_->currentText()},
            {"cameras", cameras},
            {"back_visible", camera_back_->isVisible()},
            {"fov", camera_.fov_degrees()}};
  }
  QSize scene_pane_size() const
  {
    for (const auto &pane : panes())
      if (pane.kind == bts::ViewKind::Scene)
        return {pane.width, pane.height};
    return size();
  }
  QSize uv_pane_size() const
  {
    for (const auto &pane : panes())
      if (pane.kind == bts::ViewKind::Uv)
        return {pane.width, pane.height};
    return size();
  }
  bool reference_image_enabled = false;
  bts::LoadedReferenceImage reference_image;
  QPointF ref_center_{0.5, 0.5};
  float ref_vertical_extent_ = 1.15f;
  bts::PickerColor last_picked_color_{0.0f, 0.0f, 0.0f, 1.0f};
  std::function<void(const bts::PickerColor &)> reference_color_picked;
  std::function<void()> reference_view_changed;
  bool ref_picking_drag_ = false;
  void frame_reference_image()
  {
    ref_center_ = {0.5, 0.5};
    ref_vertical_extent_ = float(bts::reference_image_fit_vertical_extent(
        uv_pane_size(), reference_image.width, reference_image.height));
    update();
  }
  void zoom_ref(double exponent)
  {
    if (std::isfinite(exponent)) {
      ref_vertical_extent_ = float(std::clamp(
          ref_vertical_extent_ * std::exp(std::clamp(exponent, -100.0, 100.0)), 0.01, 10000.0));
      if (reference_view_changed)
        reference_view_changed();
    }
  }
  bool pick_reference_color_at(QPointF pos)
  {
    const auto pane = pane_at(pos);
    if (!pane || pane->kind != bts::ViewKind::Uv || !reference_image_enabled ||
        !reference_image.valid())
      return false;
    const double scale = devicePixelRatioF();
    const QRectF rect(pane->x / scale, pane->y / scale, pane->width / scale, pane->height / scale);
    const auto uv = bts::reference_image_uv_at(rect,
                                               reference_image.width,
                                               reference_image.height,
                                               ref_center_,
                                               ref_vertical_extent_,
                                               pos);
    if (!uv)
      return false;
    const auto sample = bts::sample_reference_image_uv_rgba(reference_image, uv->x(), uv->y());
    if (!sample)
      return false;
    last_picked_color_ = *sample;
    if (reference_color_picked)
      reference_color_picked(*sample);
    update();
    return true;
  }
  void choose_scene_camera(int index)
  {
    end_gesture();
    end_paint(true);
    if (index <= 0) {
      if (default_camera_)
        camera_ = *default_camera_;
      current_imported_camera_.reset();
    }
    else {
      const auto uid = camera_selector_->itemData(index).toUInt();
      const auto found = std::find_if(
          scene_cameras_.begin(), scene_cameras_.end(), [&](const auto &camera) {
            return camera.session_id == uid;
          });
      if (found == scene_cameras_.end())
        return;
      if (!current_imported_camera_)
        default_camera_ = camera_;
      if (!camera_.adopt_scene_camera(*found)) {
        QSignalBlocker block(camera_selector_);
        camera_selector_->setCurrentIndex(0);
        current_imported_camera_.reset();
        return;
      }
      current_imported_camera_ = uid;
    }
    camera_back_->hide();
    camera_notice_->hide();
    if (projection_changed)
      projection_changed(camera_.projection());
    update();
  }
  void detach_scene_camera()
  {
    if (!current_imported_camera_)
      return;
    last_imported_camera_ = current_imported_camera_;
    camera_.release_scene_camera(scene_pane_size());
    current_imported_camera_.reset();
    {
      QSignalBlocker block(camera_selector_);
      camera_selector_->setCurrentIndex(0);
    }
    default_camera_ = camera_;
    const int index = camera_selector_->findData(*last_imported_camera_);
    camera_back_->setText("Back to \"" + camera_selector_->itemText(index) + "\" camera");
    camera_back_->show();
    camera_notice_->show();
    position_camera_controls();
  }
  void position_camera_controls()
  {
    bool visible = false;
    const double ratio = devicePixelRatioF();
    for (const auto &pane : panes())
      if (pane.kind == bts::ViewKind::Scene && !scene_cameras_.empty()) {
        visible = true;
        const int right = qRound((pane.x + pane.width) / ratio), top = qRound(pane.y / ratio),
                  bottom = qRound((pane.y + pane.height) / ratio);
        camera_selector_->setGeometry(right - 260, top + 8, 150, 23);
        camera_back_->setGeometry(right - 280, bottom - 36, 270, 26);
        camera_notice_->setGeometry(right - 280, bottom - 59, 270, 22);
      }
    camera_selector_->setVisible(visible);
    if (!visible) {
      camera_back_->hide();
      camera_notice_->hide();
    }
  }

  void set_projection(bts::OrbitCamera::Projection projection)
  {
    end_gesture();
    detach_scene_camera();
    camera_.set_orthographic(projection == bts::OrbitCamera::Projection::Orthographic);
    if (projection_changed) {
      projection_changed(camera_.projection());
    }
    update();
  }

  void focus_uv(QPointF center, double extent)
  {
    if (!std::isfinite(center.x()) || !std::isfinite(center.y()) || !std::isfinite(extent) ||
        extent <= 0)
      throw std::invalid_argument("Invalid UV framing");
    uv_center_ = center;
    uv_vertical_extent_ = extent;
    update();
  }
  void frame_uv_document()
  {
    interaction_view_ = bts::ViewKind::Uv;
    handle_navigation_key(Qt::Key_F);
    interaction_view_ = bts::ViewKind::Scene;
  }
  void surface_demo_camera()
  {
    camera_.orbit({camera_.yaw_degrees() / .35, -camera_.pitch_degrees() / .35}, false);
    set_projection(bts::OrbitCamera::Projection::Orthographic);
    frame_document();
  }
  QPointF uv_point(QPointF uv) const
  {
    for (const auto &pane : panes())
      if (pane.kind == bts::ViewKind::Uv)
        return QPointF(pane.x + pane.width * .5 +
                           (uv.x() - uv_center_.x()) * pane.height / uv_vertical_extent_,
                       pane.y + pane.height * .5 -
                           (uv.y() - uv_center_.y()) * pane.height / uv_vertical_extent_) /
               devicePixelRatioF();
    return {};
  }
  QPointF scene_point(QVector3D world) const
  {
    for (const auto &pane : panes())
      if (pane.kind == bts::ViewKind::Scene) {
        const auto clip = camera_.projection_matrix({pane.width, pane.height}) *
                          camera_.view_matrix() * QVector4D(world, 1);
        const auto p = host_.lens_projection().to_display(
            {(clip.x() / clip.w() + 1) * .5, (1 - clip.y() / clip.w()) * .5});
        if (!p)
          return {-1e6, -1e6};
        return QPointF(pane.x + (*p)[0] * pane.width, pane.y + (*p)[1] * pane.height) /
               devicePixelRatioF();
      }
    return {};
  }
  void frame_document()
  {
    interaction_view_ = bts::ViewKind::Scene;
    uv_center_ = {.5, .5};
    uv_vertical_extent_ = 1.3f;
    handle_navigation_key(Qt::Key_F);
  }

  bool handle_navigation_key(int key)
  {
    if (key == Qt::Key_F5 || key == Qt::Key_F6) {
      set_projection(key == Qt::Key_F6 ? bts::OrbitCamera::Projection::Orthographic :
                                         bts::OrbitCamera::Projection::Perspective);
      return true;
    }
    if (key != Qt::Key_F) {
      return false;
    }
    end_gesture();
    const auto visible = panes();
    const auto target = std::find_if(visible.begin(), visible.end(), [&](const auto &pane) {
      return pane.kind == interaction_view_;
    });
    if (target == visible.end() && visible.empty()) {
      return true;
    }
    const auto &pane = target != visible.end() ? *target : visible.front();
    try {
      const bts::SceneSnapshot snapshot = host_.scene_snapshot();
      if (pane.kind == bts::ViewKind::Scene) {
        detach_scene_camera();
        if (snapshot.has_bounds) {
          const auto &a = snapshot.minimum;
          const auto &b = snapshot.maximum;
          if (camera_.frame_bounds(
                  {a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {pane.width, pane.height}))
          {
            last_framed_bounds_ = QJsonObject{{"minimum", QJsonArray{a[0], a[1], a[2]}},
                                              {"maximum", QJsonArray{b[0], b[1], b[2]}},
                                              {"pane_size", QJsonArray{pane.width, pane.height}},
                                              {"mesh_objects", qint64(snapshot.mesh_objects)}};
          }
        }
      }
      else if (reference_image_enabled) {
        frame_reference_image();
        if (reference_view_changed)
          reference_view_changed();
      }
      else {
        const auto material = std::find_if(
            snapshot.materials.begin(), snapshot.materials.end(), [&](const auto &entry) {
              return selected_material_ && entry.session_id == *selected_material_;
            });
        if (material != snapshot.materials.end()) {
          const std::size_t index = std::size_t(material - snapshot.materials.begin());
          double u0 = std::numeric_limits<double>::infinity(), v0 = u0;
          double u1 = -u0, v1 = -v0;
          for (const auto &triangle : snapshot.uv_triangles) {
            if (triangle.material != index) {
              continue;
            }
            for (const auto &corner : triangle.corners) {
              u0 = std::min(u0, double(corner[0]));
              v0 = std::min(v0, double(corner[1]));
              u1 = std::max(u1, double(corner[0]));
              v1 = std::max(v1, double(corner[1]));
            }
          }
          const double extent = 1.1 * std::max(v1 - v0, (u1 - u0) * pane.height / pane.width);
          if (std::isfinite(extent) && extent > 0 && extent <= 10000) {
            uv_center_ = {(u0 + u1) * 0.5, (v0 + v1) * 0.5};
            uv_vertical_extent_ = float(std::max(0.01, extent));
          }
        }
      }
      update();
    }
    catch (const std::exception &error) {
      qWarning("BTS_NAVIGATION_FRAME_FAILED: %s", error.what());
    }
    return true;
  }

  bts::ViewLayoutState view_layout() const
  {
    return layout_;
  }
  void enable_layouts()
  {
    layouts_enabled_ = true;
  }

  void set_layout(bts::ViewLayoutState layout)
  {
    if (rendering && layout.mode != bts::ViewLayout::SceneOnly)
      return;
    end_paint(true);
    if (!layouts_enabled_) {
      return;
    }
    if (layout.mode != layout_.mode || layout.uv_first != layout_.uv_first)
      end_gesture();
    layout_ = layout;
    if (layout_changed) {
      layout_changed(layout_);
    }
    update();
  }

  bool handle_layout_key(int key)
  {
    if (!layouts_enabled_) {
      return false;
    }
    auto next = layout_;
    switch (key) {
      case Qt::Key_F1:
        next.mode = bts::ViewLayout::Split;
        break;
      case Qt::Key_F2:
        next.mode = bts::ViewLayout::SceneOnly;
        break;
      case Qt::Key_F3:
        next.mode = bts::ViewLayout::UvOnly;
        break;
      case Qt::Key_F4:
        next.swap_split();
        break;
      default:
        return false;
    }
    set_layout(next);
    return true;
  }

  void select_material(std::optional<std::uint32_t> session_uid)
  {
    host_.set_selected_material(session_uid);
    selected_material_ = session_uid;
    update();
  }

  QRectF pane_rect(bts::ViewKind kind) const
  {
    const double scale = devicePixelRatioF();
    for (const auto &pane : panes()) {
      if (pane.kind == kind) {
        return {pane.x / scale, pane.y / scale, pane.width / scale, pane.height / scale};
      }
    }
    return {};
  }

  QJsonObject view_state() const
  {
    QJsonArray pane_states;
    for (const auto &pane : panes()) {
      pane_states.append(
          QJsonObject{{"view", pane.kind == bts::ViewKind::Scene ? "3D" : "2D"},
                      {"rect", QJsonArray{pane.x, pane.y, pane.width, pane.height}}});
    }
    const QVector3D center = camera_.center();
    QJsonArray view_matrix;
    const QMatrix4x4 view = camera_.view_matrix();
    for (int index = 0; index < 16; ++index) {
      view_matrix.append(view.constData()[index]);
    }
    const auto [near_clip, far_clip] = camera_.clipping_range();
    const auto [nearest, farthest] = camera_.bounds_depths();
    return {{"clip",
             QJsonObject{{"near", near_clip},
                         {"far", far_clip},
                         {"nearest", nearest},
                         {"farthest", farthest},
                         {"scene_camera", camera_.scene_camera_active()}}},
            {"layout",
             layout_.mode == bts::ViewLayout::SceneOnly ? "3D only" :
             layout_.mode == bts::ViewLayout::UvOnly    ? "2D only" :
             layout_.uv_first                           ? "2D/3D" :
                                                          "3D/2D"},
            {"uv_first", layout_.uv_first},
            {"selected_material_session_id",
             selected_material_ ? QJsonValue(qint64(*selected_material_)) :
                                  QJsonValue(QJsonValue::Null)},
            {"panes", pane_states},
            {"scene_center", QJsonArray{center.x(), center.y(), center.z()}},
            {"trackball", trackball_enabled_},
            {"device_pixel_ratio", devicePixelRatioF()},
            {"scene_yaw", camera_.yaw_degrees()},
            {"scene_pitch", camera_.pitch_degrees()},
            {"scene_distance", camera_.distance()},
            {"scene_projection",
             camera_.projection() == bts::OrbitCamera::Projection::Orthographic ?
                 "Orthographic view" :
                 "Perspective view"},
            {"scene_vertical_extent", camera_.vertical_extent()},
            {"scene_view_matrix", view_matrix},
            {"last_framed_bounds", last_framed_bounds_},
            {"interaction_view", interaction_view_ == bts::ViewKind::Scene ? "3D" : "2D"},
            {"active_gesture", gesture_name()},
            {"uv_center", QJsonArray{uv_center_.x(), uv_center_.y()}},
            {"uv_vertical_extent", uv_vertical_extent_},
            {"reference_image_enabled", reference_image_enabled},
            {"reference_image_path", reference_image.path},
            {"reference_image_size", QJsonArray{reference_image.width, reference_image.height}},
            {"last_picked_color",
             QJsonArray{last_picked_color_[0],
                        last_picked_color_[1],
                        last_picked_color_[2],
                        last_picked_color_[3]}},
            {"uv_triangles", qint64(host_.uv_preview_summary().uv_triangles)}};
  }

  std::optional<bts::PaintPixels::Stamp> probe_surface(QPointF point)
  {
    const auto pane = pane_at(point);
    if (!pane || pane->kind != bts::ViewKind::Scene)
      return {};
    return surface_at(*pane, point, 2, false, nullptr, true);
  }

 protected:
  void paintEvent(QPaintEvent *event) override
  {
    // Keep the first initialization/clear. Later paints must not enter Qt/GL
    // while shared worker compilation is active, including an early New. Other texture
    // preparation can still make progress once compiler work has finished.
    if (!host_.renderer().empty() &&
        qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS") == 1 &&
        host_.texture_updates_pending() && blender::GPU_shader_compiler_has_pending_work())
    {
      if (qEnvironmentVariableIsSet("BTS_GPU_EARLY_PROJECT_TEST_MESH") && !empty_project) {
        setProperty("gpuProgramNonemptyWaits",
                    property("gpuProgramNonemptyWaits").toULongLong() + 1);
        setProperty("gpuProgramNonemptyWaitMonotonicMs",
                    std::chrono::duration<double, std::milli>(
                        bts::performance::Clock::now().time_since_epoch())
                        .count());
        setProperty("gpuProgramNonemptyWaitFrame", frame_count);
      }
      timer_.start(16);
      event->accept();
      return;  // No synthetic frame/frame_done.
    }
#ifdef __APPLE__
    if (!headless_gpu_ready_) {
      try {
        host_.initialize_gpu_metal();
        headless_gpu_ready_ = true;
        setProperty("headlessGpuReady", true);
        if (gpu_startup_ready)
          gpu_startup_ready();
      }
      catch (const std::exception &error) {
        qFatal("BTS_METAL_INIT_FAILED: %s", error.what());
      }
    }
    render_frame();
    QPainter painter(this);
    painter.drawImage(rect(), metal_frame_);
    bts::property_frame_done();
    event->accept();
  }
#else
    if (!headless_surface_) {
      bts::ViewportWidget::paintEvent(event);
      return;
    }
    if (!headless_context_) {
      headless_context_ = std::make_unique<QOpenGLContext>();
      headless_context_->setFormat(QSurfaceFormat::defaultFormat());
      if (!headless_context_->create())
        qFatal("HEADLESS Qt GL context unavailable");
      headless_native_surface_ = std::make_unique<QOffscreenSurface>();
      headless_native_surface_->setFormat(headless_context_->format());
      headless_native_surface_->create();
      makeCurrent();
    }
    makeCurrent();
    const QSize size(qRound(width() * devicePixelRatioF()),
                     qRound(height() * devicePixelRatioF()));
    if (!headless_framebuffer_ || headless_framebuffer_->size() != size) {
      headless_framebuffer_ = std::make_unique<QOpenGLFramebufferObject>(
          size, QOpenGLFramebufferObject::CombinedDepthStencil);
      if (!headless_framebuffer_->isValid())
        qFatal("HEADLESS framebuffer unavailable");
    }
    headless_framebuffer_->bind();
    if (!headless_gpu_ready_) {
      host_.initialize_gpu(*headless_context_, *headless_native_surface_, [this] {
        return headless_framebuffer_->handle();
      });
      if (gpu_startup_ready)
        gpu_startup_ready();
      headless_gpu_ready_ = true;
      setProperty("headlessGpuReady", true);
    }
    if (!headless_paint_device_) {
      headless_paint_device_ = std::make_unique<QOpenGLPaintDevice>(size);
      std::printf(
          "BTS_HEADLESS_PAINT_DEVICE_CREATED width=%d height=%d\n", size.width(), size.height());
    }
    else if (headless_paint_device_->size() != size)
      headless_paint_device_->setSize(size);
    headless_paint_device_->setDevicePixelRatio(devicePixelRatioF());
    paintGL();
    bts::property_frame_done();
    event->accept();
  }
  void initializeGL() override
  {
    if (headless_surface_)
      return;
    context_destroyed_ = connect(
        context(),
        &QOpenGLContext::aboutToBeDestroyed,
        this,
        [this] { close_native(); },
        Qt::DirectConnection);
    try {
      host_.initialize_gpu(*this);
      if (gpu_startup_ready)
        gpu_startup_ready();
    }
    catch (const std::exception &error) {
      qFatal("BTS_GPU_INIT_FAILED: %s", error.what());
    }
  }

  void paintGL() override
  {
    render_frame();
  }
#endif
  void render_frame()
  {
    if (property("bakeRenderingSuspended").toBool())
      return;
    draw_guard.run(
        [&] { paint_frame(); },
        [&](const QString &message) {
          timer_.stop();
          input_timer_.stop();
          paint_enabled = false;
          qWarning("BTS_DRAW_FAILED: %s", qPrintable(message));
          if (!draw_error_label) {
            draw_error_label = new QLabel(this);
            draw_error_label->setObjectName("viewportDrawError");
            draw_error_label->setWordWrap(true);
            draw_error_label->setStyleSheet(
                bts::theme::qss("background:@ErrorSurface@;color:@Heading@;padding:16px;"));
          }
          draw_error_label->setText(
              message + "\nFile New/Open can resume rendering. Recovery checkpoint requested.");
          draw_error_label->setGeometry(16, 16, std::max(200, width() - 32), 160);
          draw_error_label->show();
          draw_error_label->raise();
          // Recovery must start after Blender/Qt have left the failed draw stack.
          QTimer::singleShot(0, this, [this, message] {
            if (draw_error)
              draw_error(message);
          });
        });
  }
  void clear_viewport()
  {
#ifdef __APPLE__
    metal_frame_ = QImage(size() * devicePixelRatioF(), QImage::Format_RGBA8888);
    metal_frame_.setDevicePixelRatio(devicePixelRatioF());
    metal_frame_.fill(QColor(bts::theme::ViewportEdge));
#else
    auto *functions = context()->functions();
    const QColor backdrop(bts::theme::ViewportEdge);
    functions->glClearColor(backdrop.redF(), backdrop.greenF(), backdrop.blueF(), 1);
    functions->glClear(GL_COLOR_BUFFER_BIT);
#endif
  }
  void paint_frame()
  {
    bts::performance::Scope full_frame_timing("viewport_full_frame");
    bts::property_paint_begin();
    if (headless_surface_ && !headless_gpu_ready_)
      return;
    if (empty_project) {
      if (host_.texture_updates_pending() || qEnvironmentVariableIsSet("BTS_TEST_CONTINUOUS_FRAMES"))
        timer_.start(16);
      else
        timer_.stop();
      if (empty_project_view) {
        const int panel_width = std::min(460, width() - 24);
        empty_project_view->setGeometry(
            (width() - panel_width) / 2, (height() - 110) / 2, panel_width, 110);
        empty_project_view->raise();
      }
      clear_viewport();
      ++frame_count;
      if (frame_done)
        frame_done(0);
      return;
    }
    if (shader_failed)
      return;
    if (prepare_frame)
      prepare_frame();
    position_camera_controls();
    position_paths_panel();
    const int w = qRound(width() * devicePixelRatioF());
    const int h = qRound(height() * devicePixelRatioF());
    std::vector<bts::ViewFrame> frames;
    for (const auto &pane : panes()) {
      if (pane.kind == bts::ViewKind::Uv && reference_image_enabled &&
          layout_.mode == bts::ViewLayout::Split)
      {
        continue;
      }
      QMatrix4x4 view, projection;
      const float aspect = float(pane.width) / pane.height;
      if (pane.kind == bts::ViewKind::Scene) {
        view = camera_.view_matrix();
        projection = camera_.projection_matrix({pane.width, pane.height});
      }
      else {
        view.lookAt({float(uv_center_.x()), float(uv_center_.y()), 10},
                    {float(uv_center_.x()), float(uv_center_.y()), 0},
                    {0, 1, 0});
        const float half_height = uv_vertical_extent_ * 0.5f;
        projection.ortho(-half_height * aspect,
                         half_height * aspect,
                         -half_height,
                         half_height,
                         0.05f,
                         1000.0f);
      }
      bts::ViewFrame frame{pane, {}, {}};
      frame.wire_source = symmetry_scene;
      frame.wireframe = polygon_active;
      frame.wireframe_triangles = polygon_mode == bts::PolygonFillMode::Triangle;
      frame.overlay_line_width = devicePixelRatioF();
      frame.uv_center = {uv_center_.x(), uv_center_.y()};
      frame.uv_extent = uv_vertical_extent_;
      frame.hidden_uv_tiles = hidden_uv_tiles;
      std::copy_n(view.constData(), 16, frame.view.begin());
      std::copy_n(projection.constData(), 16, frame.projection.begin());
      if (pane.kind == bts::ViewKind::Scene) {
        const auto clip = camera_.clipping_range();
        frame.clipping = {clip.first, clip.second};
      }
      frames.push_back(frame);
    }
    std::unique_ptr<QPainter> overlay;
    bool native_painting = false;
    try {
      // Let Qt invalidate its cached GL bindings across the external renderer.
      // Creating a separate painter only afterwards leaves cached texture/VAO
      // state from the previous frame indistinguishable from Blender's state.
#ifndef __APPLE__
      if (layouts_enabled_) {
        overlay = headless_surface_ ? std::make_unique<QPainter>(headless_paint_device_.get()) :
                                      std::make_unique<QPainter>(this);
        overlay->beginNativePainting();
        native_painting = true;
      }
#endif
      host_.set_texture_interaction(painting_ || (texture_interaction && texture_interaction()));
      const bool material_pending = material_import_pending && material_import_pending();
      last_ms = host_.draw(frames, w, h, material_pending);
      if (qEnvironmentVariableIsSet("BTS_OVERLAY_PROFILE")) {
        const auto profile = host_.overlay_profile();
        setProperty("overlayTriangles",
                    qulonglong(symmetry_scene ? symmetry_scene->uv_triangles.size() : 0));
        for (auto i = profile.begin(); i != profile.end(); ++i)
          setProperty(i.key().toLatin1(), i.value().toVariant());
      }
#ifdef __APPLE__
      metal_frame_ = host_.viewport_image();
      metal_frame_.setDevicePixelRatio(devicePixelRatioF());
      if (layouts_enabled_)
        overlay = std::make_unique<QPainter>(&metal_frame_);
#endif
      overlay_gpu_.begin();
      if (native_painting) {
        overlay->endNativePainting();
        native_painting = false;
      }
      const auto failures = host_.shader_failures();
      if (!failures.empty()) {
        shader_failed = true;
        timer_.stop();
        end_paint(true);
        paint_enabled = false;
        const QString message =
            "Shader compile failed: " + QString::fromStdString(failures) +
            ". Compiler diagnostics are written to stderr. Use File New/Open to resume rendering.";
        qWarning("%s", qPrintable(message));
        if (shader_error)
          shader_error(message);
        return;
      }
      if (layouts_enabled_) {
        draw_view_chrome(*overlay);
        if (material_pending) {
          overlay->setPen(QColor(bts::theme::Text));
          overlay->drawText(rect(), Qt::AlignCenter, "Loading material textures…");
        }
      }
      overlay.reset();
      overlay_gpu_.end();
      ++frame_count;
      if (frame_count == 1 || frame_count % 60 == 0) {
        std::printf("BTS_FRAME pid=%lld number=%d size=%dx%d cpu_ms=%.2f\n",
                    static_cast<long long>(QCoreApplication::applicationPid()),
                    frame_count,
                    w,
                    h,
                    last_ms);
        std::fflush(stdout);
      }
      if (frame_done) {
        frame_done(last_ms);
      }
      if (host_.viewport_redraw_pending() ||
          qEnvironmentVariableIsSet("BTS_TEST_CONTINUOUS_FRAMES"))
        timer_.start(std::max(1, 16 - int(std::ceil(last_ms))));
      else
        timer_.stop();
    }
    catch (const std::exception &error) {
      if (native_painting)
        overlay->endNativePainting();
      overlay.reset();
      overlay_gpu_.end();
      throw;
    }
    catch (...) {
      if (native_painting)
        overlay->endNativePainting();
      overlay.reset();
      overlay_gpu_.end();
      throw;
    }
  }

  void tabletEvent(QTabletEvent *event) override
  {
    bts::tablet::event(*event, tablet_contact_, tablet_dispatch_, input_pressure_);
    // Hover must not replace either a contacting pen or an active mouse stroke.
    // A deliberate press can transfer ownership; removal clears it separately.
    if (event->type() != QEvent::TabletPress &&
        ((tablet_contact_ && tablet_device_ != event->pointingDevice()) ||
         (painting_ && !tablet_contact_)))
    {
      event->accept();
      return;
    }
    tablet_recent_.restart();
    if (!std::isfinite(event->pressure())) {
      end_paint(true);
      tablet_contact_ = false;
      event->accept();
      return;
    }
    if (event->pointerType() == QPointingDevice::PointerType::Eraser) {
      end_paint(true);
      tablet_contact_ = false;
      input_pressure_ = 0;
      pointer_name_ = "Eraser";
      update();
      event->accept();
      return;
    }
    if (tablet_device_ != event->pointingDevice()) {
      if (tablet_contact_)
        cancel_tablet_contact();
      tablet_device_ = event->pointingDevice();
      // Device pointers, rather than IDs, survive unplug/replug and ID reuse safely.
      connect(tablet_device_, &QObject::destroyed, this, [this](QObject *removed) {
        if (tablet_device_.isNull() || tablet_device_.data() == removed)
          cancel_tablet_contact();
      });
    }
    pointer_name_ = "Pen: " + event->pointingDevice()->name();
    const bool press = event->type() == QEvent::TabletPress,
               release = event->type() == QEvent::TabletRelease;
    const bool tip = event->button() == Qt::LeftButton;
    // A master-pointer press can precede the matching TabletPress. Roll back
    // that provisional mouse stroke before its first full-pressure dab survives.
    if (press && tip) {
      if (painting_)
        end_paint(true);
      tablet_contact_ = true;
    }
    // Pressure is independent of the buttons mask. In particular, a missing
    // LeftButton on a contact move must neither reset pressure nor cancel paint.
    if (!release) {
      input_pressure_ = float(std::clamp(event->pressure(), 0., 1.));
      input_tilt_ = {std::clamp(double(event->xTilt()), -90., 90.),
                     std::clamp(double(event->yTilt()), -90., 90.)};
    }
    const auto type = press   ? QEvent::MouseButtonPress :
                      release ? QEvent::MouseButtonRelease :
                                QEvent::MouseMove;
    // Mutter applies stylus settings before XWayland maps BTN_STYLUS/2 to
    // XI buttons 2/3. Qt already reports Middle/Right; swapping here reverses
    // the user's system mapping. Preserve both the changed button and mask.
    auto buttons = event->buttons();
    if (tablet_contact_ && !release)
      buttons |= Qt::LeftButton;
    tablet_dispatch_ = true;
    QMouseEvent translated(type,
                           event->position(),
                           event->globalPosition(),
                           event->button(),
                           buttons,
                           event->modifiers(),
                           event->pointingDevice());
    translated.setTimestamp(event->timestamp());
    if (press)
      mousePressEvent(&translated);
    else if (release)
      mouseReleaseEvent(&translated);
    else
      mouseMoveEvent(&translated);
    tablet_dispatch_ = false;
    if (release && tip) {
      tablet_contact_ = false;
      input_pressure_ = 0;
    }
    update();
    event->accept();  // Prevent Qt's unhandled-tablet fallback from synthesizing a second mouse
                      // stroke.
  }

  void mousePressEvent(QMouseEvent *event) override
  {
    bts::tablet::event(*event, tablet_contact_, tablet_dispatch_, input_pressure_);
    if (rendering && event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
      event->accept();
      return;
    }
    if (ignore_tablet_mouse(event)) {
      event->accept();
      return;
    }
    set_secondary_active(event->modifiers().testFlag(Qt::ControlModifier));
    if (!tablet_dispatch_) {
      input_pressure_ = 1;
      input_tilt_ = {};
      pointer_name_ = "Mouse";
    }
    setFocus();
    if (environment_rotation_.active()) {
      event->accept();
      return;
    }
    if (!empty_project && !gesture_ && !painting_ && environment_rotation_changed &&
        event->button() == Qt::RightButton && event->modifiers() == Qt::ShiftModifier)
    {
      const auto pane = pane_at(event->position());
      if (environment_rotation_.begin(*event,
                                      pane && pane->kind == bts::ViewKind::Scene,
                                      host_.environment_state()["rotation"].toDouble()))
      {
        event->accept();
        return;
      }
    }
    if (const auto ref_pane = pane_at(event->position());
        ref_pane && ref_pane->kind == bts::ViewKind::Uv && reference_image_enabled)
    {
      if (gesture_ || event->buttons() != event->button()) {
        event->ignore();
        return;
      }
      interaction_view_ = bts::ViewKind::Uv;
      if (event->button() == Qt::LeftButton && !event->modifiers().testFlag(Qt::AltModifier)) {
        pick_reference_color_at(event->position());
        ref_picking_drag_ = true;
        picker_click_ = true;
        event->accept();
        return;
      }
      std::optional<GestureKind> kind;
      if (event->button() == Qt::MiddleButton &&
          (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::AltModifier))
      {
        kind = GestureKind::Pan;
      }
      else if (event->button() == Qt::RightButton && event->modifiers() == Qt::AltModifier) {
        kind = GestureKind::Dolly;
      }
      if (!kind) {
        event->ignore();
        return;
      }
      gesture_ = NavigationGesture{bts::ViewKind::Uv,
                                   *kind,
                                   event->button(),
                                   event->position(),
                                   std::max(1, qRound(ref_pane->height / devicePixelRatioF()))};
      event->accept();
      return;
    }
    if (fill_projection && warp_editor.press(event, *fill_projection, warp_view_context())) {
      event->accept();
      update();
      return;
    }
    if (fill_projection && fill_gizmo.press(event, *fill_projection, fill_gizmo_geometry())) {
      event->accept();
      update();
      return;
    }
    if (gesture_ || event->buttons() != event->button()) {
      event->ignore();
      return;
    }
    const auto pane = pane_at(event->position());
    if (!pane) {
      event->ignore();
      return;
    }
    if (path_active && !picker_active && event->button() == Qt::LeftButton) {
      const auto hit = pane->kind == bts::ViewKind::Scene ? hit_path_detail(event->position()) :
                                                            PathHit{};
      const bool plain = event->modifiers() == Qt::NoModifier, handle = hit.handle >= 0;
      if (paint_enabled && pane->kind == bts::ViewKind::Scene && (plain || handle)) {
        if (hit.vertex >= 0) {
          if (path_selected)
            path_selected(hit.id);
          selected_vertex = hit.vertex;
          if (path_vertex_selected)
            path_vertex_selected();
          std::shared_ptr<const bts::SurfacePath> source;
          for (const auto &path : paths)
            if (path->id == hit.id)
              source = path;
          if (source) {
            PathDrag drag{source, hit.vertex, hit.handle, event->position(), {}, false};
            if (handle)
              drag.plane_start = path_plane_point(event->position(), source->points[hit.vertex]);
            if (!handle || drag.plane_start) {
              path_drag = drag;
              if (path_drag_begin)
                path_drag_begin(hit.id, hit.vertex, hit.handle);
            }
          }
        }
        else if (!hit.id.empty()) {
          if (path_editing && hit.id == selected_path && path_insert && hit.segment >= 0)
            path_insert(std::size_t(hit.segment), hit.parameter);
          else if (path_selected)
            path_selected(hit.id);
        }
        else if (const auto point = pick_path_point(event->position()); point && path_point_added)
          path_point_added(*point);
        event->accept();
        update();
        return;
      }
      if (!event->modifiers().testFlag(Qt::AltModifier)) {
        event->accept();
        return;
      }
    }
    if (stencil_key && paint_enabled && transform_available() && stencil_changed) {
      const QRectF reset((pane->x + pane->width) / devicePixelRatioF() - 96,
                         pane->y / devicePixelRatioF() + 36,
                         88,
                         20);
      if (event->button() == Qt::LeftButton && reset.contains(event->position())) {
        auto settings = active_transform();
        settings.offset = {};
        settings.scale = 1;
        settings.angle = 0;
        stencil_changed(settings);
        event->accept();
        return;
      }
      if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton ||
          event->button() == Qt::RightButton)
      {
        stencil_drag = event->button();
        stencil_before = active_transform();
        stencil_start = event->position();
        stencil_drag_height = pane->height / devicePixelRatioF();
        event->accept();
        return;
      }
    }
    if (polygon_active && paint_enabled && event->button() == Qt::LeftButton &&
        (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ControlModifier))
    {
      polygon_drag = true;
      polygon_start = polygon_end = event->position();
      event->accept();
      update();
      return;
    }
    if (picker_active && event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier)
    {
      std::optional<QPointF> uv;
      if (pane->kind == bts::ViewKind::Uv) {
        const double scale = devicePixelRatioF();
        uv = QPointF((event->position().x() * scale - pane->x - pane->width * .5) *
                             uv_vertical_extent_ / pane->height +
                         uv_center_.x(),
                     -(event->position().y() * scale - pane->y - pane->height * .5) *
                             uv_vertical_extent_ / pane->height +
                         uv_center_.y());
      }
      else if (const auto hit = surface_at(*pane, event->position(), 1, false, nullptr, true))
        uv = QPointF(hit->center[0], hit->center[1]);
      if (uv && picker_sample && picker_sample(*uv))
        last_picker_uv = *uv;
      if (picker_changed)
        picker_changed(false);
      picker_click_ = true;
      event->accept();
      return;
    }
    const double scale = devicePixelRatioF();
    const QRectF label((pane->x + pane->width) / scale - 96, pane->y / scale + 8, 88, 20);
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier &&
        label.contains(event->position()))
    {
      QMenu menu(this);
      menu.setObjectName("viewportInspectionMenu");
      menu.setProperty("pane", pane->kind == bts::ViewKind::Scene ? "3D" : "2D");
      auto *group = new QActionGroup(&menu);
      group->setExclusive(true);
      menu.addSection("Lighting");
      for (int channel = -1; channel < 8 + bts::custom_channel_count; ++channel) {
        if (channel == 0)
          menu.addSection("Single channel");
        if (channel == 3)
          continue;
        if (channel >= 7 && channel < 15 && inspection_users[channel - 7].isEmpty())
          continue;
        if (channel == 15 && !inspection_displacement)
          continue;
        if (channel == 5 && !inspection_mask)
          continue;
        if (channel == 6 && !inspection_ao)
          continue;
        if (channel >= 0 && channel < 5 && !inspection_channels[channel])
          continue;
        auto *action = menu.addAction(inspection_label(bts::InspectionMode(channel)));
        action->setObjectName(channel < 0 ? QString("inspectMaterial") :
                                            QString("inspectChannel%1").arg(channel));
        group->addAction(action);
        action->setCheckable(true);
        action->setChecked(mesh_map_active < 0 && int(host_.inspection_mode()) == channel);
        connect(action, &QAction::triggered, this, [this, channel] {
          if (inspection_requested)
            inspection_requested(bts::InspectionMode(channel));
        });
      }
      menu.addSection("Mesh maps");
      for (int role = 0; role < 7; ++role)
        if (available_mesh_maps[role]) {
          auto *action = menu.addAction(QString::fromLatin1(bts::mesh_map_names[role]));
          action->setObjectName(QString("inspectMeshMap%1").arg(role));
          group->addAction(action);
          action->setCheckable(true);
          action->setChecked(mesh_map_active == role);
          connect(action, &QAction::triggered, this, [this, role] {
            if (mesh_map_requested)
              mesh_map_requested(role);
          });
        }
      menu.exec(event->globalPosition().toPoint());
      event->accept();
      return;
    }
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier &&
        pane->kind == bts::ViewKind::Scene)
      if (const auto handle = symmetry_handle()) {
        const auto axis = handle->p2() - handle->p1(), relative = event->position() - handle->p1();
        const double length = axis.x() * axis.x() + axis.y() * axis.y();
        const double t = std::clamp(
            (relative.x() * axis.x() + relative.y() * axis.y()) / length, 0., 1.);
        const auto d = event->position() - (handle->p1() + t * axis);
        if (std::hypot(d.x(), d.y()) < 8) {
          symmetry_drag = true;
          symmetry_previous = event->position();
          symmetry_before = *symmetry;
          event->accept();
          update();
          return;
        }
      }
    if (brush && brush->clone && clone_key && event->button() == Qt::LeftButton && paint_enabled) {
      try {
        std::optional<bts::PaintPixels::Stamp> source;
        if (pane->kind == bts::ViewKind::Scene)
          source = surface_at(*pane, event->position(), brush->diameter, true);
        else {
          const double scale = devicePixelRatioF();
          const double u = (event->position().x() * scale - pane->x - pane->width * .5) *
                               uv_vertical_extent_ / pane->height +
                           uv_center_.x(),
                       v = -(event->position().y() * scale - pane->y - pane->height * .5) *
                               uv_vertical_extent_ / pane->height +
                           uv_center_.y(),
                       radius = brush->diameter / 200;
          source = bts::PaintPixels::Stamp{{u, v}, {radius, 0}, {0, radius}};
        }
        if (source && source->surface && symmetry_scene) {
          auto surface = std::make_shared<bts::PaintPixels::SurfaceFootprint>(*source->surface);
          surface->triangles.clear();
          surface->unwrapped = false;
          for (const auto &triangle : symmetry_scene->uv_triangles)
            if (symmetry_scene->materials[triangle.material].session_id == *selected_material_) {

              bts::PaintPixels::SurfaceTriangle copy;
              for (int i = 0; i < 3; ++i) {
                copy.uv[i] = {triangle.corners[i][0], triangle.corners[i][1]};
                for (int c = 0; c < 3; ++c)
                  copy.position[i][c] = triangle.positions[i][c];
              }
              QVector3D a, b;
              for (int c = 0; c < 3; ++c) {
                a[c] = copy.position[1][c] - copy.position[0][c];
                b[c] = copy.position[2][c] - copy.position[0][c];
              }
              const auto normal = QVector3D::crossProduct(a, b).normalized();
              copy.normal = {normal.x(), normal.y(), normal.z()};
              surface->triangles.push_back(std::move(copy));
            }
          source->surface = std::move(surface);
        }
        if (source) {
          clone_source = *source;
          clone_source_display = *source;
        }
      }
      catch (const std::exception &error) {
        qWarning("BTS_CLONE_SOURCE: %s", error.what());
      }
      event->accept();
      update();
      return;
    }
    interaction_view_ = pane->kind;
    const bool straight = event->modifiers() == Qt::ShiftModifier ||
                          event->modifiers() == (Qt::ShiftModifier | Qt::ControlModifier);
    if (straight && event->button() == Qt::LeftButton && paint_enabled && stroke_begin) {
      auto end = event->position(), start = end;
      if (stroke_anchor && stroke_anchor->kind == pane->kind)
        start = pane->kind == bts::ViewKind::Uv ? uv_point(stroke_anchor->uv) :
                                                  scene_point(stroke_anchor->world);
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const auto snapped = bts::snap_straight_endpoint({start.x(), start.y()},
                                                         {end.x(), end.y()});
        end = {snapped[0], snapped[1]};
      }
      if (stroke_begin()) {
        painting_ = true;
        paint_view_ = pane->kind;
        paint_last_ = start;
        paint_to(start, true, true);
        if (painting_)
          paint_to(end, false, true);
        end_paint(false);
      }
      straight_click_ = true;
      event->accept();
      return;
    }
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier &&
        paint_enabled && stroke_begin && stroke_begin())
    {
      painting_ = true;
      paint_view_ = pane->kind;
      paint_last_ = event->position();
      paint_to(event->position(), true);
      event->accept();
      return;
    }
    const auto modifiers = event->modifiers();
    if (modifiers != Qt::AltModifier && modifiers != (Qt::AltModifier | Qt::ShiftModifier)) {
      event->ignore();
      return;
    }
    std::optional<GestureKind> kind;
    if (event->button() == Qt::LeftButton && pane->kind == bts::ViewKind::Scene) {
      kind = modifiers.testFlag(Qt::ShiftModifier) ? GestureKind::SnapOrbit : GestureKind::Orbit;
    }
    else if (event->button() == Qt::MiddleButton && modifiers == Qt::AltModifier) {
      kind = GestureKind::Pan;
    }
    else if (event->button() == Qt::RightButton && modifiers == Qt::AltModifier) {
      kind = GestureKind::Dolly;
    }
    if (!kind) {
      event->ignore();
      return;
    }
    if (pane->kind == bts::ViewKind::Scene)
      detach_scene_camera();
    gesture_ = NavigationGesture{pane->kind,
                                 *kind,
                                 event->button(),
                                 event->position(),
                                 std::max(1, qRound(pane->height / devicePixelRatioF()))};
    if (*kind == GestureKind::Orbit && trackball_enabled_) {
      gesture_->trackball = camera_.begin_trackball(
          event->position(),
          QRect(pane->x, pane->y, pane->width, pane->height),
          devicePixelRatioF());
      if (!gesture_->trackball) {
        end_gesture();
        event->ignore();
        return;
      }
    }
    event->accept();
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override
  {
    bts::tablet::event(*event, tablet_contact_, tablet_dispatch_, input_pressure_);
    if (ignore_tablet_mouse(event)) {
      event->accept();
      return;
    }
    if (path_active && event->button() == Qt::LeftButton) {
      if (path_drag)
        finish_path_drag(false);
      const auto hit = hit_path_detail(event->position());
      if (hit.vertex >= 0) {
        if (path_selected)
          path_selected(hit.id);
        selected_vertex = hit.vertex;
        if (path_vertex_selected)
          path_vertex_selected();
        for (const auto &path : paths)
          if (path->id == hit.id && path_curve_action) {
            path_curve_action(bts::PathCurveAction::Smooth, !path->points[hit.vertex].smooth);
            break;
          }
      }
      event->accept();
      return;
    }
    bts::ViewportWidget::mouseDoubleClickEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override
  {
    bts::tablet::event(*event, tablet_contact_, tablet_dispatch_, input_pressure_);
    if (environment_rotation_.active()) {
      if (const auto angle = environment_rotation_.move(*event))
        environment_rotation_changed(*angle);
      event->accept();
      return;
    }
    if (path_drag) {
      try {
        auto &drag = *path_drag;
        const double distance = QLineF(drag.start, event->position()).length();
        if (distance >= 2)
          drag.moved = true;
        if (drag.moved && path_drag_update) {
          auto path = std::make_shared<bts::SurfacePath>(*drag.before);
          if (distance < .01) {
            path_drag_update(path);
          }
          else if (drag.handle < 0) {
            if (const auto point = pick_path_point(event->position())) {
              bts::surface_path_move_vertex(*path, std::size_t(drag.vertex), *point);
              path_drag_update(path);
            }
          }
          else if (const auto position = path_plane_point(event->position(),
                                                          drag.before->points[drag.vertex]);
                   position && drag.plane_start)
          {
            const auto handles = bts::surface_path_handles(*drag.before, drag.vertex);
            auto value = handles[drag.handle];
            for (int c = 0; c < 3; ++c)
              value[c] += (*position)[c] - (*drag.plane_start)[c];
            bts::surface_path_move_handle(*path,
                                          std::size_t(drag.vertex),
                                          drag.handle,
                                          value,
                                          event->modifiers().testFlag(Qt::AltModifier),
                                          event->modifiers().testFlag(Qt::ControlModifier));
            path_drag_update(path);
          }
        }
      }
      catch (const std::exception &error) {
        qWarning("BTS_PATH_DRAG_REJECTED: %s", error.what());
      }
      event->accept();
      update();
      return;
    }
    if (stencil_drag != Qt::NoButton && stencil_before && stencil_changed) {
      const auto delta = event->position() - stencil_start;
      auto settings = *stencil_before;
      if (stencil_drag == Qt::MiddleButton) {
        settings.offset[0] = std::clamp(
            settings.offset[0] + 2 * delta.x() / stencil_drag_height, -10., 10.);
        settings.offset[1] = std::clamp(
            settings.offset[1] - 2 * delta.y() / stencil_drag_height, -10., 10.);
      }
      else if (stencil_drag == Qt::RightButton)
        settings.scale = std::clamp(
            settings.scale * std::exp(std::clamp((delta.x() - delta.y()) * .005, -20., 20.)),
            .01,
            100.);
      else {
        settings.angle += delta.x() * .5;
        if (event->modifiers().testFlag(Qt::ShiftModifier))
          settings.angle = std::round(settings.angle / 15) * 15;
        settings.angle = std::remainder(settings.angle, 360.);
      }
      stencil_changed(settings);
      event->accept();
      return;
    }

    if (ignore_tablet_mouse(event)) {
      event->accept();
      return;
    }
    set_secondary_active(event->modifiers().testFlag(Qt::ControlModifier));
    if (!tablet_dispatch_) {
      input_pressure_ = 1;
      input_tilt_ = {};
      pointer_name_ = "Mouse";
    }
    cursor_ = event->position();
    cursor_visible_ = true;
    if (fill_gizmo.move(event)) {
      event->accept();
      update();
      return;
    }
    if (fill_projection && warp_editor.move(event, *fill_projection, warp_view_context())) {
      event->accept();
      update();
      return;
    }
    if (polygon_drag) {
      if (event->buttons().testFlag(Qt::LeftButton))
        polygon_end = event->position();
      else
        polygon_drag = false;
      event->accept();
      update();
      return;
    }
    if (ref_picking_drag_) {
      if (event->buttons().testFlag(Qt::LeftButton))
        pick_reference_color_at(event->position());
      else {
        ref_picking_drag_ = false;
        picker_click_ = false;
      }
      event->accept();
      return;
    }
    if (straight_click_ || picker_click_) {
      event->accept();
      return;
    }
    if (symmetry_drag && symmetry && symmetry_changed) {
      if (!event->buttons().testFlag(Qt::LeftButton)) {
        symmetry_drag = false;
        symmetry_before.reset();
        event->accept();
        return;
      }
      auto settings = *symmetry;
      const auto delta = event->position() - symmetry_previous;
      symmetry_previous = event->position();
      const auto origin = symmetry_origin();
      const auto center = scene_point(origin);
      if (settings.mode == bts::SymmetryMode::Mirror) {
        QVector3D direction;
        direction[settings.axis] = 1;
        const auto axis = scene_point(origin + direction) - center;
        const double length = axis.x() * axis.x() + axis.y() * axis.y();
        if (length > 1e-10)
          settings.offset = std::clamp(
              settings.offset + (delta.x() * axis.x() + delta.y() * axis.y()) / length, -1e6, 1e6);
      }
      else {
        const auto inverse = camera_.view_matrix().inverted();
        const auto right = inverse.column(0).toVector3D(), up = inverse.column(1).toVector3D();
        const auto r = scene_point(origin + right) - center, u = scene_point(origin + up) - center;
        const double rx = std::hypot(r.x(), r.y()), uy = std::hypot(u.x(), u.y());
        if (rx > 1e-5 && uy > 1e-5)
          for (int i = 0; i < 3; ++i)
            settings.center[i] = std::clamp(settings.center[i] + right[i] * delta.x() / rx -
                                                up[i] * delta.y() / uy,
                                            -1e6,
                                            1e6);
      }
      symmetry_changed(settings);
      event->accept();
      update();
      return;
    }
    if (paint_enabled && !painting_) {
      hover_pending_ = true;
      if (!input_timer_.isActive())
        input_timer_.start();
    }
    if (painting_) {
      if (event->buttons().testFlag(Qt::LeftButton))
        queue_pointer(event->position(), event->timestamp());
      else
        end_paint(true);
      event->accept();
      return;
    }
    update();
    if (!gesture_) {
      if (const auto pane = pane_at(event->position())) {
        interaction_view_ = pane->kind;
      }
      event->ignore();
      return;
    }
    if (!event->buttons().testFlag(gesture_->button)) {
      end_gesture();
      event->ignore();
      return;
    }
    if (gesture_->trackball) {
      const auto &press = *gesture_->trackball;
      const auto current = panes();
      const bool unchanged = std::any_of(current.begin(), current.end(), [&](const auto &pane) {
        return pane.kind == bts::ViewKind::Scene &&
               QRect(pane.x, pane.y, pane.width, pane.height) == press.pane;
      });
      if (!unchanged || devicePixelRatioF() != press.dpr) {
        end_gesture();
        event->accept();
        return;
      }
      camera_.orbit_trackball(press, event->position());
      update();
      event->accept();
      return;
    }
    const QPointF delta = event->position() - gesture_->previous;
    gesture_->previous = event->position();
    if (gesture_->pane == bts::ViewKind::Scene) {
      switch (gesture_->kind) {
        case GestureKind::Orbit:
          camera_.orbit(delta, false);
          break;
        case GestureKind::SnapOrbit:
          camera_.orbit(delta, true);
          break;
        case GestureKind::Pan:
          camera_.pan(delta, gesture_->viewport_height);
          break;
        case GestureKind::Dolly:
          camera_.dolly(delta.y());
          break;
      }
    }
    else if (gesture_->kind == GestureKind::Pan) {
      if (reference_image_enabled) {
        const double units_per_pixel = ref_vertical_extent_ / gesture_->viewport_height;
        const double aspect = reference_image.width > 0 && reference_image.height > 0 ?
                                  double(reference_image.width) / double(reference_image.height) :
                                  1.0;
        const QPointF next = ref_center_ + QPointF(-delta.x() * units_per_pixel / aspect,
                                                   delta.y() * units_per_pixel);
        if (std::isfinite(next.x()) && std::isfinite(next.y())) {
          ref_center_ = next;
          if (reference_view_changed)
            reference_view_changed();
        }
      }
      else {
        const double units_per_pixel = uv_vertical_extent_ / gesture_->viewport_height;
        const QPointF next = uv_center_ + QPointF(-delta.x(), delta.y()) * units_per_pixel;
        if (std::isfinite(next.x()) && std::isfinite(next.y())) {
          uv_center_ = next;
        }
      }
    }
    else if (gesture_->kind == GestureKind::Dolly) {
      if (reference_image_enabled)
        zoom_ref(delta.y() * 0.01);
      else
        zoom_uv(delta.y() * 0.01);
    }
    update();
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent *event) override
  {
    bts::tablet::event(*event, tablet_contact_, tablet_dispatch_, input_pressure_);
    if (environment_rotation_.active()) {
      if (const auto angle = environment_rotation_.release(*event))
        environment_rotation_changed(*angle);
      event->accept();
      return;
    }
    if (fill_gizmo.release(event)) {
      event->accept();
      update();
      return;
    }
    if (fill_projection && warp_editor.release(event, *fill_projection, warp_view_context())) {
      event->accept();
      update();
      return;
    }
    if (path_drag && event->button() == Qt::LeftButton) {
      finish_path_drag(false);
      event->accept();
      return;
    }
    if (stencil_drag == event->button()) {
      stencil_drag = Qt::NoButton;
      stencil_before.reset();
      event->accept();
      return;
    }

    if (ignore_tablet_mouse(event)) {
      event->accept();
      return;
    }
    set_secondary_active(event->modifiers().testFlag(Qt::ControlModifier));
    if (!tablet_dispatch_) {
      input_pressure_ = 1;
      input_tilt_ = {};
      pointer_name_ = "Mouse";
    }
    if (polygon_drag && event->button() == Qt::LeftButton) {
      polygon_drag = false;
      apply_polygon_selection(event->position());
      event->accept();
      update();
      return;
    }
    if (ref_picking_drag_ && event->button() == Qt::LeftButton) {
      ref_picking_drag_ = false;
      picker_click_ = false;
      if (picker_active && picker_changed)
        picker_changed(false);
      event->accept();
      update();
      return;
    }
    if (picker_click_ && event->button() == Qt::LeftButton) {
      picker_click_ = false;
      event->accept();
      return;
    }
    if (straight_click_ && event->button() == Qt::LeftButton) {
      straight_click_ = false;
      event->accept();
      return;
    }
    if (symmetry_drag && event->button() == Qt::LeftButton) {
      symmetry_drag = false;
      symmetry_before.reset();
      event->accept();
      update();
      return;
    }
    if (painting_ && event->button() == Qt::LeftButton) {
      queue_pointer(event->position(), event->timestamp());
      end_paint(false);
      event->accept();
      return;
    }
    if (gesture_ && event->button() == gesture_->button) {
      end_gesture();
      event->accept();
    }
    else {
      event->ignore();
    }
    if (!gesture_) {
      if (const auto pane = pane_at(event->position())) {
        interaction_view_ = pane->kind;
      }
    }
  }

  void wheelEvent(QWheelEvent *event) override
  {
    const auto pane = pane_at(event->position());
    if (!pane || gesture_ || painting_) {
      event->ignore();
      return;
    }
    interaction_view_ = pane->kind;
    const double delta = event->angleDelta().y() != 0 ? event->angleDelta().y() :
                                                        event->pixelDelta().y();
    if (pane->kind == bts::ViewKind::Uv) {
      if (reference_image_enabled)
        zoom_ref(-delta * 0.0012);
      else
        zoom_uv(-delta * 0.0012);
    }
    else {
      detach_scene_camera();
      camera_.zoom_wheel(delta);
    }
    update();
    event->accept();
  }

  void keyPressEvent(QKeyEvent *event) override
  {
    if (event->key() == Qt::Key_Control && !event->isAutoRepeat()) {
      set_secondary_active(true);
      event->accept();
      return;
    }
    if (skew_editing && event->key() == Qt::Key_X && event->modifiers() == Qt::NoModifier) {
      if (polygon_active && polygon_invert_requested)
        polygon_invert_requested();
      else if (skew_invert)
        skew_invert();
      event->accept();
      return;
    }
    if (fill_gizmo.dragging() && (event->key() == Qt::Key_Escape ||
                                  event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
    {
      fill_gizmo.finish(event->key() == Qt::Key_Escape);
      event->accept();
      update();
      return;
    }
    if (fill_projection && warp_editor.key(event, *fill_projection)) {
      event->accept();
      update();
      return;
    }
    if (path_drag && (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return ||
                      event->key() == Qt::Key_Enter))
    {
      const bool cancel = event->key() == Qt::Key_Escape;
      finish_path_drag(cancel);
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_S &&
        (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier))
    {
      stencil_key = true;
      event->accept();
      update();
      return;
    }
    if (path_active && !picker_active && event->modifiers() == Qt::NoModifier &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
         event->key() == Qt::Key_Escape))
    {
      if (path_finished)
        path_finished();
      event->accept();
      return;
    }
    if (path_active && !picker_active && event->modifiers() == Qt::NoModifier &&
        event->key() == Qt::Key_Delete)
    {
      if (selected_vertex >= 0 && path_curve_action)
        path_curve_action(bts::PathCurveAction::DeleteVertex, 0);
      else if (path_removed && !selected_path.empty())
        path_removed(selected_path);
      event->accept();
      return;
    }
    if (event->modifiers() == Qt::NoModifier && quick_mask_requested &&
        (event->key() == Qt::Key_Y || event->key() == Qt::Key_I))
    {
      quick_mask_requested(event->key() == Qt::Key_Y ? 3 : 2);
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_V && event->modifiers() == Qt::NoModifier && brush && brush->clone)
    {
      clone_key = true;
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_6 && event->modifiers() == Qt::NoModifier && clone_tool_requested)
    {
      clone_tool_requested();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_5 && event->modifiers() == Qt::NoModifier && smudge_tool_requested)
    {
      smudge_tool_requested();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_3 && event->modifiers() == Qt::NoModifier &&
        projection_tool_requested)
    {
      projection_tool_requested();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_N && event->modifiers() == Qt::NoModifier) {
      stencil_ignore = true;
      event->accept();
      update();
      return;
    }
    if (event->key() == Qt::Key_Escape && stencil_before) {
      const auto before = *stencil_before;
      stencil_drag = Qt::NoButton;
      stencil_before.reset();
      if (stencil_changed)
        stencil_changed(before);
      event->accept();
      return;
    }

    if (event->modifiers() == Qt::AltModifier &&
        (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && effect_move_requested &&
        effect_move_requested(event->key() == Qt::Key_Up ? 1 : -1))
    {
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_B &&
        (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier) &&
        mesh_map_requested)
    {
      int role = mesh_map_active >= 0 ? mesh_map_active : last_mesh_map;
      const int direction = event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1;
      if (mesh_map_active >= 0)
        role = (role + direction + 7) % 7;
      for (int i = 0; i < 7; ++i) {
        if (available_mesh_maps[role]) {
          mesh_map_requested(role);
          break;
        }
        role = (role + direction + 7) % 7;
      }
      event->accept();
      return;
    }
    if (fill_projection && fill_projection->mode != 6 && event->modifiers() == Qt::NoModifier &&
        fill_gizmo_requested &&
        (event->key() == Qt::Key_W || event->key() == Qt::Key_E || event->key() == Qt::Key_R))
    {
      fill_gizmo_requested(event->key() == Qt::Key_W ? 0 : event->key() == Qt::Key_E ? 1 : 2);
      event->accept();
      return;
    }
    if (polygon_drag && event->key() == Qt::Key_Escape) {
      polygon_drag = false;
      event->accept();
      update();
      return;
    }
    if (event->key() == Qt::Key_4 && event->modifiers() == Qt::NoModifier &&
        polygon_toggle_requested)
    {
      polygon_toggle_requested();
      event->accept();
      return;
    }
    if (polygon_active && event->key() == Qt::Key_X && event->modifiers() == Qt::NoModifier &&
        polygon_invert_requested)
    {
      polygon_invert_requested();
      event->accept();
      return;
    }
    if (picker_active && event->key() == Qt::Key_Escape) {
      if (picker_changed)
        picker_changed(false);
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_P && event->modifiers() == Qt::NoModifier &&
        picker_toggle_requested)
    {
      picker_toggle_requested();
      event->accept();
      return;
    }
    if (symmetry_drag && event->key() == Qt::Key_Escape) {
      if (symmetry_before && symmetry_changed)
        symmetry_changed(*symmetry_before);
      symmetry_drag = false;
      symmetry_before.reset();
      event->accept();
      update();
      return;
    }
    if (event->key() == Qt::Key_D && event->modifiers() == Qt::NoModifier && lazy_toggle_requested)
    {
      lazy_toggle_requested();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_L && event->modifiers() == Qt::NoModifier &&
        symmetry_toggle_requested)
    {
      symmetry_toggle_requested();
      event->accept();
      return;
    }
    if (painting_) {
      if (event->key() == Qt::Key_Escape)
        end_paint(true);
      event->accept();
      return;
    }
    if (event->modifiers() == Qt::NoModifier &&
        (event->key() == Qt::Key_1 || event->key() == Qt::Key_2) && tool_requested)
    {
      tool_requested(event->key() == Qt::Key_2);
      event->accept();
      return;
    }
    if ((event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier) &&
        handle_inspection_key(event->key(), event->modifiers() == Qt::ShiftModifier))
    {
      event->accept();
      return;
    }
    if (event->modifiers() == Qt::NoModifier &&
        (handle_layout_key(event->key()) || handle_navigation_key(event->key())))
    {
      event->accept();
      return;
    }
    bts::ViewportWidget::keyPressEvent(event);
  }

  void keyReleaseEvent(QKeyEvent *event) override
  {
    if (event->key() == Qt::Key_Control && !event->isAutoRepeat()) {
      set_secondary_active(false);
      event->accept();
      return;
    }
    if (event->isAutoRepeat() &&
        (event->key() == Qt::Key_S || event->key() == Qt::Key_N || event->key() == Qt::Key_V))
    {
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_V) {
      clone_key = false;
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_S) {
      stencil_key = false;
      stencil_drag = Qt::NoButton;
      stencil_before.reset();
      event->accept();
      update();
      return;
    }
    if (event->key() == Qt::Key_N) {
      stencil_ignore = false;
      event->accept();
      update();
      return;
    }
    bts::ViewportWidget::keyReleaseEvent(event);
  }
  void focusOutEvent(QFocusEvent *event) override
  {
    fill_gizmo.finish(true);
    if (path_drag)
      finish_path_drag(true);
    tablet_contact_ = false;
    clone_key = false;
    stencil_key = stencil_ignore = false;
    stencil_drag = Qt::NoButton;
    stencil_before.reset();
    end_paint(true);
    secondary_active_ = false;
    end_gesture();
    bts::ViewportWidget::focusOutEvent(event);
  }

  void hideEvent(QHideEvent *event) override
  {
    fill_gizmo.finish(true);
    end_gesture();
    bts::ViewportWidget::hideEvent(event);
  }

  bool event(QEvent *event) override
  {
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
      fill_gizmo.finish(true);
      end_gesture();
    }
    return bts::ViewportWidget::event(event);
  }

 private:
  bool headless_surface_ = false, headless_gpu_ready_ = false;
  QImage metal_frame_;
#ifndef __APPLE__
  std::unique_ptr<QOpenGLContext> headless_context_;
  std::unique_ptr<QOffscreenSurface> headless_native_surface_;
  std::unique_ptr<QOpenGLFramebufferObject> headless_framebuffer_;
  std::unique_ptr<QOpenGLPaintDevice> headless_paint_device_;
#endif
  bts::GpuPerformance overlay_gpu_{"gpu_overlay"};
  bts::BlenderHost &host_;
  QTimer timer_;
  QMetaObject::Connection context_destroyed_;
  enum class GestureKind { Orbit, SnapOrbit, Pan, Dolly };
  struct NavigationGesture {
    bts::ViewKind pane;
    GestureKind kind;
    Qt::MouseButton button;
    QPointF previous;
    int viewport_height;
    std::optional<bts::trackball::Press> trackball;
  };

  bts::OrbitCamera camera_;
  bool trackball_enabled_ = false;
  QComboBox *camera_selector_ = nullptr;
  QPushButton *camera_back_ = nullptr;
  QLabel *camera_notice_ = nullptr;
  std::vector<bts::SceneCamera> scene_cameras_;
  std::optional<std::uint32_t> current_imported_camera_, last_imported_camera_;
  std::optional<bts::OrbitCamera> default_camera_;

  std::optional<NavigationGesture> gesture_;
  bts::EnvironmentRotationGesture environment_rotation_;
  bts::ViewKind interaction_view_ = bts::ViewKind::Scene;
  QJsonObject last_framed_bounds_;
  std::optional<std::uint32_t> selected_material_;
  bts::ViewLayoutState layout_;
  QPointF uv_center_{0.5, 0.5};
  float uv_vertical_extent_ = 1.3f;
  bool layouts_enabled_ = false;
  bool closed_ = false;
  bool closing_ = false;

  std::vector<bts::ViewPane> panes() const
  {
    return bts::view_panes(layout_,
                           qRound(width() * devicePixelRatioF()),
                           qRound(height() * devicePixelRatioF()),
                           qRound(2 * devicePixelRatioF()));
  }

  std::optional<bts::ViewPane> pane_at(const QPointF &position) const
  {
    const QPoint point(qFloor(position.x() * devicePixelRatioF()),
                       qFloor(position.y() * devicePixelRatioF()));
    for (const auto &pane : panes()) {
      if (QRect(pane.x, pane.y, pane.width, pane.height).contains(point)) {
        return pane;
      }
    }
    return std::nullopt;
  }

  void end_gesture()
  {
    environment_rotation_.finish();
    if (gesture_ && gesture_->kind == GestureKind::SnapOrbit) {
      camera_.orbit({}, false);
    }
    if (gesture_ && !current_imported_camera_ && default_camera_)
      default_camera_ = camera_;
    gesture_.reset();
  }

  QString gesture_name() const
  {
    if (!gesture_) {
      return "Idle";
    }
    switch (gesture_->kind) {
      case GestureKind::Orbit:
        return gesture_->trackball ? "Trackball" : "Orbit";
      case GestureKind::SnapOrbit:
        return "Snap orbit";
      case GestureKind::Pan:
        return "Pan";
      case GestureKind::Dolly:
        return "Dolly";
    }
    return {};
  }

  void zoom_uv(double exponent)
  {
    if (std::isfinite(exponent)) {
      uv_vertical_extent_ = float(std::clamp(
          uv_vertical_extent_ * std::exp(std::clamp(exponent, -100.0, 100.0)), 0.01, 10000.0));
    }
  }

  void draw_view_chrome(QPainter &painter)
  {
    bts::performance::Scope timing("viewport_chrome");

    if (skew_field && skew_field->settings.vectors) {
      const auto &field = *skew_field;
      double extent = 1;
      if (symmetry_scene)
        for (int k = 0; k < 3; ++k)
          extent = std::max(extent,
                            double(symmetry_scene->maximum[k] - symmetry_scene->minimum[k]));
      for (const auto &v : skew_vectors) {
        const float weight = skew_geometry ? skew_geometry->weight(
                                                 field, v.tile, v.u, v.v, v.position, v.object) :
                                             field.sample(v.tile, v.u, v.v);
        const QColor color = QColor::fromRgbF(std::min(1.f, 2 * (1 - weight)),
                                              std::min(1.f, 2 * weight),
                                              0,
                                              field.settings.vector_opacity);
        painter.save();
        painter.setPen(QPen(color, 1.3));
        const auto rect = pane_rect(bts::ViewKind::Scene);
        if (!rect.isEmpty()) {
          painter.setClipRect(rect);
          const QVector3D point(v.position[0], v.position[1], v.position[2]);
          const auto clip = camera_.projection_matrix({int(rect.width()), int(rect.height())}) *
                            camera_.view_matrix() * QVector4D(point, 1);
          if (clip.w() > 0 && std::abs(clip.z()) <= clip.w()) {
            const auto n = bts::skew_projection_normal(v.average, v.geometric, weight);
            const QVector3D end = point + QVector3D(n[0], n[1], n[2]) *
                                              float(extent * field.settings.vector_length);
            painter.drawLine(scene_point(point), scene_point(end));
          }
        }
        const auto uv_rect = pane_rect(bts::ViewKind::Uv);
        if (!uv_rect.isEmpty()) {
          painter.setClipRect(uv_rect);
          const int tile = v.tile - 1001;
          const auto point = uv_point({v.u + tile % 10, v.v + tile / 10});
          painter.drawLine(point, point + QPointF(0, -3));
        }
        painter.restore();
      }
    }

    if (brush && brush->clone && clone_source_display) {
      const auto &source = *clone_source_display;
      const auto kind = source.surface ? bts::ViewKind::Scene : bts::ViewKind::Uv;
      const auto rect = pane_rect(kind);
      if (!rect.isEmpty()) {
        const auto center = source.surface ? scene_point({float(source.surface->center[0]),
                                                          float(source.surface->center[1]),
                                                          float(source.surface->center[2])}) :
                                             uv_point({source.center[0], source.center[1]});
        const double radius = std::max(5.,
                                       double(brush->diameter) / 200 * rect.height() /
                                           (source.surface ? 1 : uv_vertical_extent_));
        painter.save();
        painter.setClipRect(rect);
        painter.setPen(QPen(QColor(bts::theme::Heading), 1, Qt::DotLine));
        for (double x : {-1., 1.})
          for (double y : {-1., 1.}) {
            const auto corner = center + QPointF(x * radius, y * radius);
            painter.drawLine(corner, corner - QPointF(x * radius * .35, 0));
            painter.drawLine(corner, corner - QPointF(0, y * radius * .35));
          }
        painter.restore();
      }
    }

    if (path_active && paint_enabled) {
      painter.save();
      const auto pane = pane_rect(bts::ViewKind::Scene);
      painter.setClipRect(pane);
      const auto project = [&](const auto &world) {
        return scene_point({float(world[0]), float(world[1]), float(world[2])});
      };
      for (const auto &path : paths) {
        const bool selected = path->id == selected_path;
        const auto color = selected ? path_display.selected : path_display.other;
        painter.setPen(QPen(color,
                            path_display.width + (selected ? .5 : 0),
                            path->visible ? Qt::SolidLine : Qt::DashLine));
        QPolygonF polygon;
        const auto found = path_draw.find(path->id);
        if (found != path_draw.end())
          for (const auto &sample : found->second.samples) {
            const auto screen = project(sample.point.position);
            if (std::isfinite(screen.x()) && std::isfinite(screen.y()))
              polygon << screen;
          }
        else
          for (const auto &point : path->points)
            polygon << project(point.position);
        painter.drawPolyline(polygon);
        if (path_display.direction && polygon.size() > 1) {
          const auto delta = polygon.back() - polygon[polygon.size() - 2];
          const double length = std::hypot(delta.x(), delta.y());
          if (length > 1e-8) {
            const auto axis = delta / length, side = QPointF(-axis.y(), axis.x());
            painter.drawLine(polygon.back(), polygon.back() - axis * 9 + side * 4);
            painter.drawLine(polygon.back(), polygon.back() - axis * 9 - side * 4);
          }
        }
        for (int i = 0; i < int(path->points.size()); ++i) {
          const auto &point = path->points[i];
          const auto screen = project(point.position);
          painter.setBrush(selected && i == selected_vertex ? QColor(bts::theme::Heading) : color);
          painter.drawEllipse(screen, path_display.handle * .5, path_display.handle * .5);
          if (path_display.normals) {
            auto end = point.position;
            const double span =
                symmetry_scene ?
                    std::max({double(symmetry_scene->maximum[0] - symmetry_scene->minimum[0]),
                              double(symmetry_scene->maximum[1] - symmetry_scene->minimum[1]),
                              double(symmetry_scene->maximum[2] - symmetry_scene->minimum[2])}) *
                        .08 :
                    .1;
            for (int c = 0; c < 3; ++c)
              end[c] += point.normal[c] * span;
            painter.drawLine(screen, project(end));
          }
        }
        if (selected && selected_vertex >= 0 && selected_vertex < int(path->points.size()) &&
            path_display.tangents)
        {
          const auto &point = path->points[selected_vertex];
          const auto handles = bts::surface_path_handles(*path, selected_vertex);
          painter.setPen(QPen(point.custom ? color : QColor(bts::theme::Muted), 1, Qt::DashLine));
          for (const auto &handle : handles) {
            auto position = point.position;
            for (int c = 0; c < 3; ++c)
              position[c] += handle[c];
            const auto screen = project(position);
            painter.drawLine(project(point.position), screen);
            if (point.custom) {
              painter.setBrush(color);
              painter.drawRect(QRectF(screen - QPointF(3, 3), QSizeF(6, 6)));
            }
          }
        }
      }
      if (pending_path_point) {
        painter.setPen(QPen(path_display.selected, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(project(pending_path_point->position), 4, 4);
      }
      painter.restore();
    }
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPixelSize(11);
    painter.setFont(font);
    const double scale = devicePixelRatioF();
    for (const auto &pane : panes()) {
      const QRectF rect(pane.x / scale, pane.y / scale, pane.width / scale, pane.height / scale);
      painter.save();
      painter.setClipRect(rect);
      if (pane.kind == bts::ViewKind::Uv && reference_image_enabled) {
        painter.fillRect(rect, QColor(bts::theme::Surface));
        if (reference_image.valid()) {
          const QRectF target = bts::reference_image_screen_rect(rect,
                                                                 reference_image.width,
                                                                 reference_image.height,
                                                                 ref_center_,
                                                                 ref_vertical_extent_);
          painter.setRenderHint(QPainter::SmoothPixmapTransform, ref_vertical_extent_ > 0.25f);
          painter.drawImage(target, reference_image.display_image);
          painter.setPen(QPen(bts::theme::alpha(bts::theme::Border, 200), 1.0));
          painter.setBrush(Qt::NoBrush);
          painter.drawRect(target);
        }
        else {
          painter.setPen(QColor(bts::theme::Text));
          painter.drawText(rect.adjusted(20, 40, -20, -40),
                           Qt::AlignCenter | Qt::TextWordWrap,
                           "No reference image loaded — click Open Image...");
        }
        painter.setPen(QColor(bts::theme::Text));
        const QString badge =
            reference_image.valid() ?
                QStringLiteral("2D REF — %1").arg(QFileInfo(reference_image.path).fileName()) :
                QStringLiteral("2D REF");
        const int badge_w = std::clamp(painter.fontMetrics().horizontalAdvance(badge) + 16,
                                       54,
                                       std::max(54, int(rect.width()) - 16));
        const QRectF badge_rect(rect.left() + 8, rect.top() + 8, badge_w, 20);
        painter.fillRect(badge_rect, bts::theme::alpha(bts::theme::Surface, 210));
        painter.drawText(badge_rect.adjusted(6, 0, -6, 0),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(badge, Qt::ElideRight, badge_w - 12));
        if (cursor_visible_ && rect.contains(cursor_) && reference_image.valid()) {
          const auto uv = bts::reference_image_uv_at(rect,
                                                     reference_image.width,
                                                     reference_image.height,
                                                     ref_center_,
                                                     ref_vertical_extent_,
                                                     cursor_);
          if (uv) {
            if (const auto sample = bts::sample_reference_image_uv_rgba(
                    reference_image, uv->x(), uv->y()))
            {
              painter.setPen(QPen(bts::theme::alpha(bts::theme::Canvas, 220), 2.5));
              painter.setBrush(Qt::NoBrush);
              painter.drawEllipse(cursor_, 7, 7);
              painter.setPen(QPen(bts::theme::alpha(bts::theme::Heading, 235), 1.2));
              painter.drawEllipse(cursor_, 7, 7);
              const QColor chip = bts::picker_to_srgb(*sample);
              const QRectF swatch(cursor_.x() + 12, cursor_.y() - 22, 18, 18);
              painter.fillRect(swatch, chip);
              painter.setPen(QPen(bts::theme::alpha(bts::theme::Heading, 220), 1.0));
              painter.drawRect(swatch);
            }
          }
        }
        painter.restore();
        continue;
      }
      if (paint_enabled && brush && transform_available() && active_transform().overlay &&
          (brush->projected || !stencil_ignore))
      {
        const auto &settings = active_transform();
        auto projected = brush->projected;
        if (projected)
          projected->values = brush->values;
        if (projected) {
          const auto key = QJsonDocument(host_.color_state()).toJson(QJsonDocument::Compact);
          if (!projection_image_settings || !projected->same_image(*projection_image_settings) ||
              key != projection_display_key)
          {
            stencil_images = {};
            projection_image_settings = projected;
            projection_display_key = key;
          }
        }
        else if (projection_image_settings) {
          stencil_images = {};
          projection_image_settings.reset();
        }
        if (!stencil_image_settings || !settings.same_image(*stencil_image_settings)) {
          stencil_images = {};
          stencil_image_settings = settings;
        }
        auto &image = stencil_images[pane.kind == bts::ViewKind::Uv ? 1 : 0];
        const double factor = 256 / std::max(rect.width(), rect.height());
        const QSize size(std::max(1, qRound(rect.width() * factor)),
                         std::max(1, qRound(rect.height() * factor)));
        if (image.size() != size) {
          image = QImage(size, QImage::Format_ARGB32_Premultiplied);
          std::vector<float> colors;
          if (projected)
            colors.resize(std::size_t(size.width()) * size.height() * 4);
          for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x) {
              const std::array<double, 2> screen{2 * (x + .5 - size.width() * .5) / size.height(),
                                                 -2 * (y + .5 - size.height() * .5) /
                                                     size.height()};
              if (projected) {
                const auto value = projected->sample(screen);
                const auto index = (std::size_t(y) * size.width() + x) * 4;
                if (value) {
                  for (int c = 0; c < 3; ++c)
                    colors[index + c] = value->base_color[c];
                  colors[index + 3] = 1;
                }
              }
              else {
                const int value = qRound(255 * settings.sample(screen));
                image.setPixel(x, y, qPremultiply(qRgba(value, value, value, 75)));
              }
            }
          if (projected) {
            host_.display_rgba(colors);
            for (int y = 0; y < size.height(); ++y)
              for (int x = 0; x < size.width(); ++x) {
                const auto index = (std::size_t(y) * size.width() + x) * 4;
                int rgb[3];
                for (int c = 0; c < 3; ++c)
                  rgb[c] = qRound(255 * std::clamp(colors[index + c], 0.f, 1.f));
                image.setPixel(
                    x,
                    y,
                    qPremultiply(qRgba(rgb[0], rgb[1], rgb[2], colors[index + 3] > 0 ? 75 : 0)));
              }
          }
        }
        painter.drawImage(rect, image);
        if (stencil_key) {
          const QRectF reset(rect.right() - 96, rect.top() + 36, 88, 20);
          painter.fillRect(reset, QColor(bts::theme::Hover));
          painter.setPen(QColor(bts::theme::Heading));
          painter.drawText(reset, Qt::AlignCenter, "Reset");
        }
      }
      if (pane.kind == bts::ViewKind::Uv) {
        draw_uv_tiles(painter, rect);
        if (qEnvironmentVariableIsSet("BTS_OVERLAY_PROFILE")) {
          const auto profile = host_.overlay_profile();
          setProperty("uvContourEdges", profile["uv_contour_edges"].toVariant());
          setProperty("gpuUvVboBuilds", profile["uv_vbo_builds"].toVariant());
          setProperty("gpuUvUploadBytes", profile["uv_vbo_upload_bytes"].toVariant());
          setProperty("polygonLines1", profile["uv_wire_edges"].toVariant());
        }
      }
      if (geometry_tile_editing) {
        painter.save();
        QPainterPath excluded;
        excluded.setFillRule(Qt::WindingFill);
        if (pane.kind == bts::ViewKind::Uv) {
          for (int tile : geometry_excluded_tiles) {
            const int number = tile - 1001;
            const auto a = uv_point({double(number % 10), double(number / 10)}),
                       b = uv_point({double(number % 10 + 1), double(number / 10 + 1)});
            excluded.addRect(QRectF(a, b).normalized());
          }
        }
        else
          for (const auto &shape : geometry_hatch_shapes) {
            QPolygonF polygon;
            for (const auto &vertex : shape)
              polygon << scene_point({float(vertex.position[0]),
                                      float(vertex.position[1]),
                                      float(vertex.position[2])});
            excluded.addPolygon(polygon);
            excluded.closeSubpath();
          }
        painter.setClipPath(excluded, Qt::IntersectClip);
        painter.fillRect(rect, bts::theme::alpha(bts::theme::Black, 30));
        painter.setPen(QPen(bts::theme::alpha(bts::theme::Canvas, 190), 2));
        for (double x = rect.left() - rect.height(); x < rect.right(); x += 9)
          painter.drawLine(QPointF(x, rect.bottom()), QPointF(x + rect.height(), rect.top()));
        painter.restore();
        painter.fillRect(QRectF(rect.left() + 8, rect.bottom() - 28, 110, 20),
                         bts::theme::alpha(bts::theme::Surface, 210));
        painter.setPen(QColor(bts::theme::Text));
        painter.drawText(QRectF(rect.left() + 12, rect.bottom() - 28, 104, 20),
                         Qt::AlignVCenter,
                         "UV TILE MASK");
      }
      painter.setPen(QColor(bts::theme::Text));
      painter.fillRect(QRectF(rect.left() + 8, rect.top() + 8, 28, 20),
                       bts::theme::alpha(bts::theme::Surface, 210));
      painter.drawText(QRectF(rect.left() + 8, rect.top() + 8, 28, 20),
                       Qt::AlignCenter,
                       pane.kind == bts::ViewKind::Scene ? "3D" : "2D");
      const QRectF material_label(rect.right() - 96, rect.top() + 8, 88, 20);
      painter.fillRect(material_label, bts::theme::alpha(bts::theme::Surface, 210));
      painter.drawText(
          material_label,
          Qt::AlignCenter,
          painter.fontMetrics().elidedText(current_inspection_label(), Qt::ElideRight, 80));
      if (pane.kind == bts::ViewKind::Uv && fill_projection && fill_projection->mode == 0)
        bts::paint_fill_gizmo(painter, fill_gizmo_geometry());
      if (pane.kind == bts::ViewKind::Scene && fill_projection && fill_projection->mode == 5)
        warp_editor.paint(painter, *fill_projection, warp_view_context());
      if (pane.kind == bts::ViewKind::Scene && fill_projection &&
          bts::fill_surface_projection(*fill_projection) && fill_projection->mode != 5 &&
          fill_projection->manipulator && symmetry_scene)
      {
        const auto &p = *fill_projection;
        const auto geometry = fill_gizmo_geometry();
        const double extent = geometry.extent;
        const auto &matrix = geometry.matrix;
        const auto point = [&](QVector3D local) {
          return scene_point((matrix * QVector4D(local, 1)).toVector3DAffine());
        };
        painter.setPen(QPen(bts::theme::alpha(bts::theme::Text, 160), 1, Qt::DashLine));
        if (p.mode == 3) {
          for (int axis = 0; axis < 3; ++axis) {
            QPolygonF circle;
            for (int step = 0; step <= 96; ++step) {
              const double angle = step * 2 * std::numbers::pi / 96;
              QVector3D local;
              local[(axis + 1) % 3] = std::cos(angle) * extent * .5;
              local[(axis + 2) % 3] = std::sin(angle) * extent * .5;
              circle << point(local);
            }
            painter.drawPolyline(circle);
          }
        }
        else if (p.mode == 4) {
          const double span = p.cylinder_angle * std::numbers::pi / 180;
          for (double z : {-.5, .5}) {
            QPolygonF arc;
            for (int step = 0; step <= 96; ++step) {
              const double a = (double(step) / 96 - .5) * span;
              arc << point({float(std::sin(a) * extent * .5),
                            float(-std::cos(a) * extent * .5),
                            float(z * extent)});
            }
            painter.drawPolyline(arc);
            if (p.cylinder_angle < 360)
              painter.drawLine(arc.first(), arc.last());
          }
          for (int step = 0; step <= 8; ++step) {
            const double a = (double(step) / 8 - .5) * span;
            const float x = std::sin(a) * extent * .5, y = -std::cos(a) * extent * .5;
            painter.drawLine(point({x, y, float(-.5 * extent)}),
                             point({x, y, float(.5 * extent)}));
          }
        }
        else
          for (int corner = 0; corner < 8; ++corner)
            for (int axis = 0; axis < 3; ++axis)
              if (!(corner & (1 << axis))) {
                QVector3D a, b;
                for (int i = 0; i < 3; ++i) {
                  a[i] = ((corner & (1 << i)) ? .5 : -.5) * extent *
                         (p.mode == 2 && i == 1 ? 2 : 1);
                  b[i] = a[i];
                }
                b[axis] *= -1;
                painter.drawLine(point(a), point(b));
              }
        bts::paint_fill_gizmo(painter, geometry);
      }

      if (polygon_drag) {
        painter.setPen(QPen(QColor(bts::theme::Warning), 1, Qt::DashLine));
        painter.fillRect(QRectF(polygon_start, polygon_end).normalized(),
                         bts::theme::alpha(bts::theme::Warning, 30));
        painter.drawRect(QRectF(polygon_start, polygon_end).normalized());
      }
      if (pane.kind == bts::ViewKind::Scene)
        draw_symmetry(painter);
      draw_axes(painter, rect, pane.kind);
      if (quick_mask_edit) {
        painter.setPen(QColor(bts::theme::Heading));
        const QRectF label(rect.left() + 8, rect.bottom() - 28, 118, 20);
        painter.fillRect(label, bts::theme::alpha(bts::theme::Surface, 220));
        painter.drawText(label, Qt::AlignCenter, "▦ QUICK MASK");
      }
      else if (paint_mask) {
        painter.setPen(QColor(bts::theme::Text));
        const QRectF label(rect.left() + 8, rect.bottom() - 28, 48, 20);
        painter.fillRect(label, bts::theme::alpha(bts::theme::Surface, 210));
        painter.drawText(label, Qt::AlignCenter, skew_field ? "SKEW" : "MASK");
      }
      if (pane.kind == bts::ViewKind::Uv && !host_.uv_preview_summary()) {
        painter.setPen(QColor(bts::theme::Text));
        const QString hint = selected_material_ ? "No UV faces for this texture set" :
                                                  "Select a texture set to view its UVs";
        painter.drawText(
            rect.adjusted(20, 40, -20, -40), Qt::AlignCenter | Qt::TextWordWrap, hint);
      }
      if (!polygon_active && paint_enabled && brush && cursor_visible_ && rect.contains(cursor_)) {
        const double radius = pane.kind == bts::ViewKind::Uv ?
                                  brush->diameter_at(input_pressure_) / 200 * rect.height() /
                                      uv_vertical_extent_ :
                                  cursor_radius_ * brush->diameter_at(input_pressure_) /
                                      cursor_diameter_;
        painter.setPen(QPen(bts::theme::alpha(bts::theme::Heading, 220), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(cursor_, radius, radius);
        if (brush->alpha.kind != bts::AlphaKind::Shape) {
          constexpr int grid = 32;
          const auto inside = [&](int x, int y) {
            return x >= 0 && x < grid && y >= 0 && y < grid &&
                   brush->alpha.sample(
                       (x + .5) * 2 / grid - 1, 1 - (y + .5) * 2 / grid, brush->hardness) > .5;
          };
          const auto point = [&](int x, int y) {
            return cursor_ + QPointF((double(x) * 2 / grid - 1) * radius,
                                     (double(y) * 2 / grid - 1) * radius);
          };
          for (int y = 0; y < grid; ++y)
            for (int x = 0; x < grid; ++x)
              if (inside(x, y)) {
                if (!inside(x - 1, y))
                  painter.drawLine(point(x, y), point(x, y + 1));
                if (!inside(x + 1, y))
                  painter.drawLine(point(x + 1, y), point(x + 1, y + 1));
                if (!inside(x, y - 1))
                  painter.drawLine(point(x, y), point(x + 1, y));
                if (!inside(x, y + 1))
                  painter.drawLine(point(x, y + 1), point(x + 1, y + 1));
              }
        }
      }
      painter.restore();
    }
    if (!polygon_active && paint_enabled && brush && brush->lazy_mouse && cursor_visible_)
      if (const auto pane = pane_at(cursor_)) {
        const double radius = brush->lazy_distance / 100 * (pane->height / devicePixelRatioF());
        painter.setBrush(bts::theme::alpha(bts::theme::Canvas, 65));
        painter.setPen(QPen(bts::theme::alpha(bts::theme::Text, 140), 1));
        painter.drawEllipse(cursor_, radius, radius);
        if (painting_) {
          painter.setBrush(Qt::NoBrush);
          painter.drawLine(cursor_, paint_last_);
          painter.drawEllipse(paint_last_, 3, 3);
        }
      }
    if (secondary_active_ && paint_enabled && cursor_visible_) {
      painter.save();
      painter.setPen(QColor(bts::theme::Warning));
      painter.drawText(cursor_ + QPointF(16, -16),
                       paint_mask ? "Ctrl: inverted mask" : "Ctrl: secondary color");
      painter.restore();
    }
    if (pressure_overlay_) {
      painter.save();
      const QRectF box(12, 12, std::min(540, width() - 24), 48);
      painter.fillRect(box, bts::theme::alpha(bts::theme::Canvas, 220));
      painter.setPen(QColor(bts::theme::Heading));
      painter.drawText(box.adjusted(8, 0, -8, -16),
                       Qt::AlignVCenter,
                       pointer_name_ + "  Pressure " + QString::number(input_pressure_, 'f', 2));
      painter.fillRect(QRectF(box.left() + 8,
                              box.bottom() - 10,
                              (box.width() - 16) * std::clamp(input_pressure_, 0.f, 1.f),
                              4),
                       QColor(bts::theme::Accent));
      painter.restore();
    }
  }

  mutable bts::SymmetryIntersectionCache symmetry_lines_;
  void draw_symmetry(QPainter &painter) const
  {
    bts::performance::Scope symmetry_timing("draw_symmetry");
    if (!symmetry || !symmetry->enabled || !symmetry_scene)
      return;
    painter.save();
    painter.setPen(QPen(bts::theme::alpha(bts::theme::Error, 220), 1.2));
    if (symmetry->intersection) {
      if (symmetry->mode == bts::SymmetryMode::Mirror) {
        const auto visible = panes();
        const auto scene = std::find_if(visible.begin(), visible.end(), [](const auto &pane) {
          return pane.kind == bts::ViewKind::Scene;
        });
        if (scene != visible.end()) {
          const auto view = camera_.view_matrix();
          const auto eye = view.inverted().column(3).toVector3D();
          const auto matrix = camera_.projection_matrix({scene->width, scene->height}) * view;
          const double ratio = devicePixelRatioF();
          const auto lens = host_.lens_projection();
          const auto project = [&](QVector3D point) -> std::optional<QPointF> {
            const auto clip = matrix * QVector4D(point, 1);
            if (std::abs(clip.w()) < 1e-12)
              return {};
            const auto p = lens.to_display(
                {(clip.x() / clip.w() + 1) * .5, (1 - clip.y() / clip.w()) * .5});
            if (!p)
              return {};
            return QPointF(scene->x + (*p)[0] * scene->width, scene->y + (*p)[1] * scene->height) /
                   ratio;
          };
          QVector<QLineF> lines;
          const auto &segments = symmetry_lines_.get(
              symmetry_scene->uv_triangles, symmetry->axis, symmetry->offset);
          lines.reserve(qsizetype(segments.size()));
          for (const auto &line : segments) {
            if (QVector3D::dotProduct(line.normal, eye - line.origin) <= 0)
              continue;
            const auto a = project(line.a), b = project(line.b);
            if (a && b)
              lines.append(QLineF(*a, *b));
          }
          painter.drawLines(lines);
        }
      }
      else {
        QVector3D axis;
        axis[symmetry->axis] = 1;
        double extent = 1;
        for (int i = 0; i < 3; ++i)
          extent = std::max(extent,
                            double(symmetry_scene->maximum[i] - symmetry_scene->minimum[i]));
        painter.drawLine(scene_point(symmetry_origin() - axis * extent),
                         scene_point(symmetry_origin() + axis * extent));
      }
    }
    if (const auto handle = symmetry_handle()) {
      painter.setPen(
          QPen(symmetry_drag ? QColor(bts::theme::Warning) : QColor(bts::theme::AxisX), 2));
      painter.drawLine(*handle);
      const auto direction = (handle->p2() - handle->p1()) / handle->length();
      const QPointF perpendicular(-direction.y(), direction.x());
      for (int i = 0; i < 2; ++i) {
        const auto tip = i ? handle->p2() : handle->p1();
        const auto inward = direction * (i ? -8 : 8);
        painter.drawLine(tip, tip + inward + perpendicular * 5);
        painter.drawLine(tip, tip + inward - perpendicular * 5);
      }
    }
    painter.restore();
  }

  void draw_uv_tiles(QPainter &painter, const QRectF &rect) const
  {
    const double units_per_pixel = uv_vertical_extent_ / rect.height();
    const double u0 = uv_center_.x() - rect.width() * units_per_pixel * 0.5;
    const double u1 = uv_center_.x() + rect.width() * units_per_pixel * 0.5;
    const double v0 = uv_center_.y() - uv_vertical_extent_ * 0.5;
    const double v1 = uv_center_.y() + uv_vertical_extent_ * 0.5;
    if (!std::isfinite(u0) || !std::isfinite(u1) || !std::isfinite(v0) || !std::isfinite(v1)) {
      return;
    }
    constexpr int line_limit = 64;
    const double span = std::max(u1 - u0, v1 - v0);
    const double step = std::pow(10.0, std::ceil(std::log10(std::max(1.0, span / line_limit))));
    const auto x = [&](double u) { return rect.left() + (u - u0) / units_per_pixel; };
    const auto y = [&](double v) { return rect.bottom() - (v - v0) / units_per_pixel; };
    painter.setPen(QPen(bts::theme::alpha(bts::theme::Muted, 90), 1));
    const double first_u = std::ceil(u0 / step) * step;
    const double first_v = std::ceil(v0 / step) * step;
    for (int index = 0; index <= line_limit; ++index) {
      const double u = first_u + index * step;
      if (u > u1) {
        break;
      }
      painter.drawLine(QPointF(x(u), rect.top()), QPointF(x(u), rect.bottom()));
    }
    for (int index = 0; index <= line_limit; ++index) {
      const double v = first_v + index * step;
      if (v > v1) {
        break;
      }
      painter.drawLine(QPointF(rect.left(), y(v)), QPointF(rect.right(), y(v)));
    }
    if (step != 1.0 || 1.0 / units_per_pixel < 48) {
      return;
    }
    painter.setPen(QColor(bts::theme::Muted));
    const double first_column = std::floor(u0);
    const double first_row = std::floor(v0);
    int labels = 0;
    for (int row = 0; row <= line_limit && labels < line_limit; ++row) {
      const double v = first_row + row;
      if (v > v1) {
        break;
      }
      for (int column = 0; column <= line_limit && labels < line_limit; ++column) {
        const double u = first_column + column;
        if (u > u1) {
          break;
        }
        const QString label = u >= 0 && u < 10 && v >= 0 && v <= 99 ?
                                  QString::number(1001 + int(u) + 10 * int(v)) :
                                  QString("(%1, %2)").arg(u, 0, 'f', 0).arg(v, 0, 'f', 0);
        const QRectF label_rect(x(u) + 5, y(v) - 21, 94, 17);
        if (rect.intersects(label_rect)) {
          painter.fillRect(label_rect, bts::theme::alpha(bts::theme::Surface, 180));
          painter.drawText(label_rect, Qt::AlignLeft | Qt::AlignVCenter, label);
        }
        ++labels;
      }
    }
  }

  void draw_axes(QPainter &painter, const QRectF &rect, bts::ViewKind kind) const
  {
    const QPointF origin(rect.right() - 38, rect.bottom() - 36);
    const auto axis = [&](const QPointF &direction, const QColor &color, const QString &label) {
      const QPointF tip = origin + direction * 19;
      painter.setPen(QPen(color, 1.5));
      painter.drawLine(origin, tip);
      const QPointF text_center = origin + direction * 28;
      painter.drawText(
          QRectF(text_center.x() - 7, text_center.y() - 7, 14, 14), Qt::AlignCenter, label);
    };
    if (kind == bts::ViewKind::Uv) {
      axis({1, 0}, QColor(bts::theme::AxisX), "U");
      axis({0, -1}, QColor(bts::theme::AxisY), "V");
    }
    else {
      const QMatrix4x4 rotation = camera_.view_matrix();
      const QVector3D x_axis = rotation.mapVector({1, 0, 0});
      const QVector3D y_axis = rotation.mapVector({0, 1, 0});
      const QVector3D z_axis = rotation.mapVector({0, 0, 1});
      axis({x_axis.x(), -x_axis.y()}, QColor(bts::theme::AxisX), "X");
      axis({y_axis.x(), -y_axis.y()}, QColor(bts::theme::AxisY), "Y");
      axis({z_axis.x(), -z_axis.y()}, QColor(bts::theme::AxisZ), "Z");
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(bts::theme::Text));
    painter.drawEllipse(origin, 2, 2);
    painter.setBrush(Qt::NoBrush);
  }

  void close_native()
  {
    if (closed_ || closing_)
      return;
    closing_ = true;
    const auto restore = qScopeGuard([this] { closing_ = false; });
    timer_.stop();
    // Clear before invocation. Terminal drain never pumps Qt events and must
    // release pending GPU owners before either Main or this context is lost.
    auto before = std::move(before_native_close);
    before_native_close = {};
    if (before)
      before();
    if (isValid()) {
      makeCurrent();
      overlay_gpu_.free();
      doneCurrent();
    }
    host_.shutdown();
    closed_ = true;
  }
};
