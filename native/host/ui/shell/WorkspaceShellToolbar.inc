// SPDX-License-Identifier: GPL-3.0-or-later
// The context toolbar above the viewport: tools, brush quick controls, symmetry. Part of the
// WorkspaceShell constructor (WorkspaceShell.cc), included in order inside its body; it shares
// that function's locals and is not a standalone file.
auto *center = new QWidget(workspace_splitter_);
workspace_splitter_->addWidget(center);
center->setMinimumWidth(640);
auto *center_layout = column(center);
auto *toolbar = new QWidget(center);
toolbar->setObjectName("contextToolbar");
toolbar->setFixedHeight(36);
auto *toolbar_layout = row(toolbar);
fill_toolbar_ = new QWidget(toolbar);
auto *fill_toolbar_layout = row(fill_toolbar_, 0);
fill_toolbar_->hide();
toolbar_layout->addWidget(fill_toolbar_);
fill_manipulator_ = new QToolButton(fill_toolbar_);
fill_manipulator_->setObjectName("fillToolbarManipulator");
fill_manipulator_->setText("UV frame");
fill_manipulator_->setCheckable(true);
fill_toolbar_layout->addWidget(fill_manipulator_);
connect(fill_manipulator_, &QToolButton::toggled, this, [this](bool v) {
  auto p = displayed_fill_;
  p.manipulator = v;
  if (callbacks_.fill_projection_changed)
    callbacks_.fill_projection_changed(p);
});
fill_spatial_toolbar_ = new QWidget(fill_toolbar_);
auto *gizmo_layout = row(fill_spatial_toolbar_, 0);
fill_toolbar_layout->addWidget(fill_spatial_toolbar_);
fill_gizmo_size_ = new bts::NumericSpinBox(fill_spatial_toolbar_);
fill_gizmo_size_->setObjectName("fillManipulatorSize");
fill_gizmo_size_->setRange(.01, 10);
fill_gizmo_size_->setDecimals(2);
fill_gizmo_size_->setValue(.4);
fill_gizmo_size_->setKeyboardTracking(false);
fill_gizmo_size_->setToolTip("Projection manipulator size");
gizmo_layout->addWidget(fill_gizmo_size_);
connect(fill_gizmo_size_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
  auto p = displayed_fill_;
  p.gizmo_size = v;
  if (callbacks_.fill_projection_changed)
    callbacks_.fill_projection_changed(p);
});
for (int mode = 0; mode < 3; ++mode) {
  auto *button = new QToolButton(fill_spatial_toolbar_);
  button->setText(QStringList{"Translate", "Rotate", "Scale"}[mode]);
  button->setCheckable(true);
  button->setObjectName(QString("fillGizmoMode%1").arg(mode));
  fill_gizmos_[mode] = button;
  gizmo_layout->addWidget(button);
  connect(button, &QToolButton::clicked, this, [this, mode] {
    auto p = displayed_fill_;
    p.gizmo = mode;
    if (callbacks_.fill_projection_changed)
      callbacks_.fill_projection_changed(p);
  });
  auto *action = new QAction(button->text(), this);
  action->setShortcut(QKeySequence(mode == 0 ? Qt::Key_W : mode == 1 ? Qt::Key_E : Qt::Key_R));
  action->setEnabled(false);
  fill_gizmo_actions_[mode] = action;
  addAction(action);
  connect(action, &QAction::triggered, button, &QToolButton::click);
}
gizmo_layout->addWidget(new QLabel("Mapping", fill_spatial_toolbar_));
warp_geometry_toggle_ = new QToolButton(fill_toolbar_);
warp_geometry_toggle_->setObjectName("warpToGeometry");
warp_geometry_toggle_->setText("Warp to geometry");
warp_geometry_toggle_->setCheckable(true);
warp_geometry_toggle_->setToolTip(
    "Automatically fit the Warp grid to the active Texture Set surface; turning off freezes the current setup");
fill_toolbar_layout->addWidget(warp_geometry_toggle_);
warp_geometry_toggle_->hide();
connect(warp_geometry_toggle_, &QToolButton::toggled, this, [this](bool enabled) {
  if (displayed_fill_.mode != 5)
    return;
  edit_fill_projection(toggle_warp_geometry(displayed_fill_, enabled));
});
const auto make_polygon_controls = [&](int which, QWidget *parent) {
  auto *panel = new QWidget(parent);
  QBoxLayout *layout = which == 0 ? static_cast<QBoxLayout *>(row(panel)) : column(panel);
  auto *buttons = new QWidget(panel);
  auto *buttons_layout = row(buttons, 0);
  for (int mode = 0; mode < 5; ++mode) {
    auto *button = new QToolButton(buttons);
    button->setText(QStringList{"△", "□", "◇", "▦", "A"}[mode]);
    button->setToolTip(QStringList{"Triangle fill",
                                   "Polygon fill",
                                   "Mesh / connected element fill",
                                   "UV island fill",
                                   "Auto: object/connected element in 3D; UV island in 2D"}[mode]);
    button->setObjectName(QString("polygonMode%1_%2").arg(which).arg(mode));
    button->setCheckable(true);
    button->setChecked(mode == 4);
    polygon_modes_[which][mode] = button;
    buttons_layout->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, which, mode] {
      if (callbacks_.polygon_fill_changed)
        callbacks_.polygon_fill_changed(mode, polygon_values_[which]->value());
    });
  }
  layout->addWidget(buttons);
  auto *value_line = new QWidget(panel);
  auto *value_layout = row(value_line, 0);
  value_layout->addWidget(new QLabel("Grayscale", value_line));
  auto *number = new bts::NumericSpinBox(value_line);
  number->setRange(0, 1);
  number->setDecimals(3);
  number->setSingleStep(.1);
  number->setValue(1);
  number->setKeyboardTracking(false);
  number->setObjectName(QString("polygonGrayscale%1").arg(which));
  polygon_values_[which] = number;
  value_layout->addWidget(number);
  auto *invert = new QToolButton(value_line);
  invert->setText("⇄");
  invert->setToolTip("Invert grayscale (X)");
  value_layout->addWidget(invert);
  connect(
      invert, &QToolButton::clicked, this, [number] { number->setValue(1 - number->value()); });
  layout->addWidget(value_line);
  auto *slider = new QSlider(Qt::Horizontal, panel);
  slider->setRange(0, 1000);
  slider->setValue(1000);
  polygon_sliders_[which] = slider;
  layout->addWidget(slider);
  connect(slider, &QSlider::valueChanged, this, [number](int v) { number->setValue(v / 1000.); });
  connect(number, &QDoubleSpinBox::valueChanged, this, [this, which](double v) {
    int mode = 4;
    for (int i = 0; i < 5; ++i)
      if (polygon_modes_[which][i]->isChecked())
        mode = i;
    if (callbacks_.polygon_fill_changed)
      callbacks_.polygon_fill_changed(mode, v);
  });
  panel->hide();
  return panel;
};
polygon_toolbar_ = make_polygon_controls(0, toolbar);
toolbar_layout->addWidget(polygon_toolbar_);
brush_toolbar_ = new QWidget(toolbar);
brush_toolbar_->hide();
auto *brush_toolbar_layout = row(brush_toolbar_, 0);
toolbar_layout->addWidget(brush_toolbar_);
for (const char *name : {"Size", "Flow", "Stroke opacity", "Spacing"}) {
  if (QString::fromLatin1(name) == "Size") {
    auto *group = new QWidget(toolbar);
    auto *layout = column(group);
    group->setFixedWidth(98);
    auto *line = new QWidget(group);
    auto *line_layout = row(line, 0);
    line_layout->addWidget(new QLabel("Size", line));
    toolbar_size_ = new bts::NumericSpinBox(line);
    toolbar_size_->setRange(.1, 100);
    toolbar_size_->setDecimals(1);
    toolbar_size_->setValue(10);
    toolbar_size_->setKeyboardTracking(false);
    toolbar_size_->setObjectName("toolbarBrushSize");
    toolbar_size_->setFixedHeight(19);
    line_layout->addWidget(toolbar_size_);
    layout->addWidget(line);
    group->setFixedWidth(108);
    toolbar_pressure_[0] = tool(line, Glyph::Brush, "Use pen pressure for Size", true);
    toolbar_pressure_[0]->setFixedSize(20, 19);
    toolbar_pressure_[0]->setCheckable(true);
    toolbar_pressure_[0]->setObjectName("toolbarPressureSize");
    line_layout->addWidget(toolbar_pressure_[0]);
    connect(toolbar_pressure_[0], &QToolButton::toggled, this, [this](bool value) {
      if (callbacks_.brush_pressure_changed)
        callbacks_.brush_pressure_changed(0, value);
    });
    toolbar_size_slider_ = new QSlider(Qt::Horizontal, group);
    toolbar_size_slider_->setRange(1, 1000);
    toolbar_size_slider_->setValue(100);
    layout->addWidget(toolbar_size_slider_);
    brush_toolbar_layout->addWidget(group);
    connect(toolbar_size_slider_, &QSlider::valueChanged, this, [this](int value) {
      toolbar_size_->setValue(value / 10.0);
    });
    connect(toolbar_size_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      const QSignalBlocker blocker(toolbar_size_slider_);
      toolbar_size_slider_->setValue(qRound(value * 10));
      if (callbacks_.brush_size_changed)
        callbacks_.brush_size_changed(value);
    });
    continue;
  }
  if (QString::fromLatin1(name) == "Flow") {
    auto *group = new QWidget(toolbar);
    auto *layout = column(group);
    group->setFixedWidth(98);
    auto *line = new QWidget(group);
    auto *line_layout = row(line, 0);
    line_layout->addWidget(new QLabel("Flow", line));
    toolbar_flow_ = new bts::NumericSpinBox(line);
    toolbar_flow_->setRange(0, 100);
    toolbar_flow_->setDecimals(1);
    toolbar_flow_->setValue(100);
    toolbar_flow_->setKeyboardTracking(false);
    toolbar_flow_->setObjectName("toolbarBrushFlow");
    toolbar_flow_->setFixedHeight(19);
    line_layout->addWidget(toolbar_flow_);
    layout->addWidget(line);
    group->setFixedWidth(108);
    toolbar_pressure_[1] = tool(line, Glyph::Brush, "Use pen pressure for Flow", true);
    toolbar_pressure_[1]->setFixedSize(20, 19);
    toolbar_pressure_[1]->setCheckable(true);
    toolbar_pressure_[1]->setObjectName("toolbarPressureFlow");
    line_layout->addWidget(toolbar_pressure_[1]);
    connect(toolbar_pressure_[1], &QToolButton::toggled, this, [this](bool value) {
      if (callbacks_.brush_pressure_changed)
        callbacks_.brush_pressure_changed(1, value);
    });
    toolbar_flow_slider_ = new QSlider(Qt::Horizontal, group);
    toolbar_flow_slider_->setRange(0, 1000);
    toolbar_flow_slider_->setValue(1000);
    layout->addWidget(toolbar_flow_slider_);
    brush_toolbar_layout->addWidget(group);
    connect(toolbar_flow_slider_, &QSlider::valueChanged, this, [this](int value) {
      toolbar_flow_->setValue(value / 10.0);
    });
    connect(toolbar_flow_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      if (callbacks_.brush_flow_changed)
        callbacks_.brush_flow_changed(value);
    });
    continue;
  }
  if (QString::fromLatin1(name) == "Stroke opacity") {
    auto *group = new QWidget(toolbar);
    auto *layout = column(group);
    group->setFixedWidth(148);
    auto *line = new QWidget(group);
    auto *line_layout = row(line, 0);
    line_layout->addWidget(new QLabel("Stroke opacity", line));
    toolbar_opacity_ = new bts::NumericSpinBox(line);
    toolbar_opacity_->setRange(0, 100);
    toolbar_opacity_->setDecimals(1);
    toolbar_opacity_->setValue(100);
    toolbar_opacity_->setKeyboardTracking(false);
    toolbar_opacity_->setObjectName("toolbarStrokeOpacity");
    toolbar_opacity_->setFixedHeight(19);
    line_layout->addWidget(toolbar_opacity_);
    layout->addWidget(line);
    toolbar_opacity_slider_ = new QSlider(Qt::Horizontal, group);
    toolbar_opacity_slider_->setRange(0, 1000);
    toolbar_opacity_slider_->setValue(1000);
    layout->addWidget(toolbar_opacity_slider_);
    brush_toolbar_layout->addWidget(group);
    connect(toolbar_opacity_slider_, &QSlider::valueChanged, this, [this](int value) {
      toolbar_opacity_->setValue(value / 10.0);
    });
    connect(toolbar_opacity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      if (callbacks_.brush_opacity_changed)
        callbacks_.brush_opacity_changed(value);
    });
    continue;
  }
  if (QString::fromLatin1(name) == "Spacing") {
    auto *group = new QWidget(toolbar);
    auto *layout = column(group);
    group->setFixedWidth(110);
    auto *line = new QWidget(group);
    auto *line_layout = row(line, 0);
    line_layout->addWidget(new QLabel("Spacing", line));
    toolbar_spacing_ = new bts::NumericSpinBox(line);
    toolbar_spacing_->setRange(1, 500);
    toolbar_spacing_->setDecimals(1);
    toolbar_spacing_->setValue(20);
    toolbar_spacing_->setKeyboardTracking(false);
    toolbar_spacing_->setObjectName("toolbarBrushSpacing");
    toolbar_spacing_->setFixedHeight(19);
    line_layout->addWidget(toolbar_spacing_);
    layout->addWidget(line);
    toolbar_spacing_slider_ = new QSlider(Qt::Horizontal, group);
    toolbar_spacing_slider_->setRange(10, 5000);
    toolbar_spacing_slider_->setValue(200);
    layout->addWidget(toolbar_spacing_slider_);
    brush_toolbar_layout->addWidget(group);
    connect(toolbar_spacing_slider_, &QSlider::valueChanged, this, [this](int value) {
      toolbar_spacing_->setValue(value / 10.0);
    });
    connect(toolbar_spacing_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
      if (callbacks_.brush_spacing_changed)
        callbacks_.brush_spacing_changed(value);
    });
    continue;
  }
}
lazy_toggle_ = tool(toolbar, Glyph::LazyMouse, "Lazy mouse", true);
lazy_toggle_->setCheckable(true);
lazy_toggle_->setObjectName("lazyMouseToggle");
lazy_toggle_->setToolTip("Lazy mouse (D)");
brush_toolbar_layout->addWidget(lazy_toggle_);
lazy_controls_ = new QWidget(toolbar);
auto *lazy_layout = column(lazy_controls_);
lazy_controls_->setFixedWidth(108);
auto *lazy_line = new QWidget(lazy_controls_);
auto *lazy_row = row(lazy_line, 0);
lazy_row->addWidget(new QLabel("Distance", lazy_line));
lazy_distance_ = new bts::NumericSpinBox(lazy_line);
lazy_distance_->setObjectName("lazyMouseDistance");
lazy_distance_->setRange(.1, 100);
lazy_distance_->setDecimals(1);
lazy_distance_->setValue(8);
lazy_distance_->setFixedHeight(19);
lazy_distance_->setKeyboardTracking(false);
lazy_distance_->setToolTip("Radius as a percentage of view height");
lazy_row->addWidget(lazy_distance_);
lazy_layout->addWidget(lazy_line);
lazy_slider_ = new QSlider(Qt::Horizontal, lazy_controls_);
lazy_slider_->setRange(1, 1000);
lazy_slider_->setValue(80);
lazy_layout->addWidget(lazy_slider_);
brush_toolbar_layout->addWidget(lazy_controls_);
lazy_controls_->setEnabled(false);
connect(lazy_slider_, &QSlider::valueChanged, this, [this](int v) {
  lazy_distance_->setValue(v / 10.);
});
connect(lazy_distance_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
  if (callbacks_.lazy_mouse_changed)
    callbacks_.lazy_mouse_changed(lazy_toggle_->isChecked(), v);
});
connect(lazy_toggle_, &QToolButton::toggled, this, [this](bool v) {
  if (callbacks_.lazy_mouse_changed)
    callbacks_.lazy_mouse_changed(v, lazy_distance_->value());
});
lazy_action_ = new QAction("Lazy mouse", this);
lazy_action_->setObjectName("toggleLazyMouse");
lazy_action_->setShortcut(QKeySequence(Qt::Key_D));
addAction(lazy_action_);
connect(lazy_action_, &QAction::triggered, lazy_toggle_, &QToolButton::click);
toolbar_layout->addStretch();
// A quiet line at the end of the toolbar. It carries no space of its own until
// a newer release has been confirmed, so nothing is shown when nothing is new.
update_notice_ = new QWidget(toolbar);
update_notice_->setObjectName("updateNotice");
auto *update_layout = row(update_notice_, 0);
update_label_ = new QLabel(update_notice_);
update_label_->setObjectName("updateNoticeText");
update_layout->addWidget(update_label_);
auto *update_get = new QToolButton(update_notice_);
update_get->setObjectName("updateNoticeGet");
update_get->setText("Get");
update_get->setAutoRaise(true);
update_get->setToolTip("Open the download page for this release");
update_layout->addWidget(update_get);
toolbar_layout->addWidget(update_notice_);
update_notice_->hide();
connect(update_get, &QToolButton::clicked, this, [this] {
  if (update_page_.isValid())
    QDesktopServices::openUrl(update_page_);
});
auto *undo_toolbar = tool(toolbar, Glyph::Undo, "Undo");
undo_toolbar->setObjectName("undoToolbar");
toolbar_layout->addWidget(undo_toolbar);
auto *redo_toolbar = tool(toolbar, Glyph::Redo, "Redo");
redo_toolbar->setObjectName("redoToolbar");
toolbar_layout->addWidget(redo_toolbar);
auto *hide_excluded = tool(toolbar, Glyph::HideExcluded, "Hide excluded geometry", true);
hide_excluded->setObjectName("hideExcludedGeometry");
hide_excluded->setCheckable(true);
hide_excluded->setAccessibleName("Paint strokes ignore masked UV Tiles");
hide_excluded->setToolTip("Hide/Ignore excluded geometry (Alt+H)");
toolbar_layout->addWidget(hide_excluded);
connect(hide_excluded, &QToolButton::toggled, this, [this](bool enabled) {
  if (callbacks_.hide_excluded_changed)
    callbacks_.hide_excluded_changed(enabled);
});
auto *hide_action = new QAction(this);
hide_action->setObjectName("toggleHideExcludedGeometry");
hide_action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_H));
addAction(hide_action);
connect(hide_action, &QAction::triggered, hide_excluded, &QToolButton::toggle);
symmetry_toggle_ = tool(toolbar, Glyph::Symmetry, "Symmetry", true);
symmetry_toggle_->setCheckable(true);
symmetry_toggle_->setObjectName("symmetryToggle");
symmetry_toggle_->setToolTip("3D mirror / radial symmetry (L)");
toolbar_layout->addWidget(symmetry_toggle_);
auto *symmetry_menu_button = tool(toolbar, Glyph::More, "Symmetry settings", true);
symmetry_menu_button->setObjectName("symmetryMenuButton");
symmetry_menu_button->setToolTip("Symmetry settings");
symmetry_menu_button->setPopupMode(QToolButton::InstantPopup);
toolbar_layout->addWidget(symmetry_menu_button);
auto *symmetry_menu = new QMenu(symmetry_menu_button);
symmetry_menu_button->setMenu(symmetry_menu);
auto *symmetry_action = new QWidgetAction(symmetry_menu);
auto *symmetry_panel = new QWidget(symmetry_menu);
auto *symmetry_layout = column(symmetry_panel);
symmetry_layout->setContentsMargins(12, 12, 12, 12);
symmetry_mode_ = new QComboBox(symmetry_panel);
symmetry_mode_->setObjectName("symmetryMode");
symmetry_mode_->addItems({"Mirror", "Radial"});
symmetry_layout->addWidget(new QLabel("Symmetry mode", symmetry_panel));
symmetry_layout->addWidget(symmetry_mode_);
symmetry_axis_ = new QComboBox(symmetry_panel);
symmetry_axis_->setObjectName("symmetryAxis");
symmetry_axis_->addItems({"X", "Y", "Z"});
symmetry_layout->addWidget(new QLabel("World axis", symmetry_panel));
symmetry_layout->addWidget(symmetry_axis_);
symmetry_offset_ = new bts::NumericSpinBox(symmetry_panel);
symmetry_offset_->setObjectName("symmetryOffset");
symmetry_offset_->setRange(-1e6, 1e6);
symmetry_offset_->setDecimals(4);
symmetry_offset_->setSingleStep(.1);
symmetry_offset_->setKeyboardTracking(false);
symmetry_layout->addWidget(new QLabel("Mirror plane offset", symmetry_panel));
symmetry_layout->addWidget(symmetry_offset_);
symmetry_copies_ = new QSpinBox(symmetry_panel);
symmetry_copies_->setObjectName("symmetryCopies");
symmetry_copies_->setRange(2, 32);
symmetry_layout->addWidget(new QLabel("Radial copies", symmetry_panel));
symmetry_layout->addWidget(symmetry_copies_);
for (int i = 0; i < 3; ++i) {
  symmetry_center_[i] = new bts::NumericSpinBox(symmetry_panel);
  symmetry_center_[i]->setObjectName(QString("symmetryCenter%1").arg(i));
  symmetry_center_[i]->setRange(-1e6, 1e6);
  symmetry_center_[i]->setDecimals(4);
  symmetry_center_[i]->setSingleStep(.1);
  symmetry_center_[i]->setKeyboardTracking(false);
  symmetry_layout->addWidget(
      new QLabel(QString("Radial center %1").arg(QChar::fromLatin1("XYZ"[i])), symmetry_panel));
  symmetry_layout->addWidget(symmetry_center_[i]);
}
symmetry_intersection_ = new QCheckBox("Show intersection", symmetry_panel);
symmetry_intersection_->setObjectName("symmetryIntersection");
symmetry_layout->addWidget(symmetry_intersection_);
symmetry_action->setDefaultWidget(symmetry_panel);
symmetry_menu->addAction(symmetry_action);
symmetry_manipulator_ = tool(toolbar, Glyph::SymmetryManipulator, "Symmetry manipulator", true);
symmetry_manipulator_->setCheckable(true);
symmetry_manipulator_->setObjectName("symmetryManipulator");
symmetry_manipulator_->setToolTip("Show/Hide symmetry manipulator");
toolbar_layout->addWidget(symmetry_manipulator_);
const auto change_symmetry = [this] {
  auto s = symmetry_;
  s.enabled = symmetry_toggle_->isChecked();
  s.manipulator = symmetry_manipulator_->isChecked();
  s.mode = SymmetryMode(symmetry_mode_->currentIndex());
  s.axis = symmetry_axis_->currentIndex();
  s.offset = symmetry_offset_->value();
  s.copies = symmetry_copies_->value();
  s.intersection = symmetry_intersection_->isChecked();
  for (int i = 0; i < 3; ++i)
    s.center[i] = symmetry_center_[i]->value();
  if (callbacks_.symmetry_changed)
    callbacks_.symmetry_changed(s);
};
connect(symmetry_toggle_, &QToolButton::toggled, this, change_symmetry);
connect(symmetry_manipulator_, &QToolButton::toggled, this, change_symmetry);
connect(symmetry_mode_, &QComboBox::currentIndexChanged, this, change_symmetry);
connect(symmetry_axis_, &QComboBox::currentIndexChanged, this, change_symmetry);
connect(symmetry_offset_, &QDoubleSpinBox::valueChanged, this, change_symmetry);
connect(symmetry_copies_, &QSpinBox::valueChanged, this, change_symmetry);
connect(symmetry_intersection_, &QCheckBox::toggled, this, change_symmetry);
for (auto *control : symmetry_center_)
  connect(control, &QDoubleSpinBox::valueChanged, this, change_symmetry);
auto *symmetry_shortcut = new QAction(this);
symmetry_shortcut->setObjectName("toggleSymmetry");
symmetry_shortcut->setShortcut(QKeySequence(Qt::Key_L));
addAction(symmetry_shortcut);
connect(symmetry_shortcut, &QAction::triggered, symmetry_toggle_, &QToolButton::click);
set_symmetry(SymmetrySettings{});
trackball_toggle_ = new QCheckBox("Trackball", toolbar);
trackball_toggle_->setObjectName("trackballNavigation");
trackball_toggle_->setToolTip(
    "Free orbit with roll (Alt + left drag). Shift retains turntable snap.");
toolbar_layout->addWidget(trackball_toggle_);
connect(trackball_toggle_, &QCheckBox::toggled, this, [this](bool enabled) {
  if (callbacks_.trackball_changed)
    callbacks_.trackball_changed(enabled);
});
view_layout_control_ = new QComboBox(toolbar);
view_layout_control_->setObjectName("viewLayoutControl");
view_layout_control_->setAccessibleName("Viewport layout");
view_layout_control_->addItems({"3D only", "2D only", "3D/2D", "2D/3D"});
view_layout_control_->setMinimumWidth(72);
view_layout_control_->setMaximumWidth(80);
view_layout_control_->setEnabled(bool(callbacks_.view_layout_changed));
view_layout_control_->setToolTip(callbacks_.view_layout_changed ?
                                     "F1: split  F2: 3D  F3: 2D  F4: swap split views" :
                                     "Viewport layouts are unavailable.");
toolbar_layout->addWidget(view_layout_control_);
reference_image_toggle_ = new QCheckBox("Reference Image", toolbar);
reference_image_toggle_->setObjectName("referenceImageToggle");
reference_image_toggle_->setAccessibleName("Reference Image");
reference_image_toggle_->setToolTip(
    "Show a 2D reference image in the 2D view instead of the UV layout and pick colors from it");
toolbar_layout->addWidget(reference_image_toggle_);
connect(reference_image_toggle_, &QCheckBox::toggled, this, [this](bool enabled) {
  if (callbacks_.reference_image_toggled)
    callbacks_.reference_image_toggled(enabled);
});
open_reference_image_button_ = new QPushButton("Open Image...", toolbar);
open_reference_image_button_->setObjectName("openReferenceImageButton");
open_reference_image_button_->setAccessibleName("Open Reference Image");
open_reference_image_button_->setToolTip("Open a reference image for the 2D view");
toolbar_layout->addWidget(open_reference_image_button_);
connect(open_reference_image_button_, &QPushButton::clicked, this, [this] {
  if (callbacks_.open_reference_image_requested)
    callbacks_.open_reference_image_requested();
});
projection_control_ = new QComboBox(toolbar);
projection_control_->setObjectName("projectionControl");
projection_control_->setMaximumWidth(110);
projection_control_->setAccessibleName("Camera projection");
projection_control_->addItems({"Perspective view", "Orthographic view"});
projection_control_->setEnabled(bool(callbacks_.projection_changed));
projection_control_->setToolTip(callbacks_.projection_changed ?
                                    "F5: Perspective view  F6: Orthographic view" :
                                    "Camera projection is unavailable.");
toolbar_layout->addWidget(projection_control_);
display_control_ = new QComboBox(toolbar);
display_control_->setObjectName("displayTransform");
display_control_->setAccessibleName("Display transform");
display_control_->setToolTip(
    "Display transform; authored texture values stay in the working space");
display_control_->setMaximumWidth(88);
toolbar_layout->addWidget(display_control_);
auto *toolbar_bake = new QToolButton(toolbar);
toolbar_bake->setObjectName("toolbarBakeMeshMaps");
toolbar_bake->setText("Bake Mesh Maps");
toolbar_bake->setToolTip("Bake mesh maps for this project");
toolbar_bake->setEnabled(false);
toolbar_layout->addWidget(toolbar_bake);
connect(toolbar_bake, &QToolButton::clicked, this, [this] {
  if (callbacks_.bake_mesh_maps)
    callbacks_.bake_mesh_maps();
});
connect(display_control_, &QComboBox::currentTextChanged, this, [this](const QString &name) {
  if (callbacks_.display_transform_changed)
    callbacks_.display_transform_changed(name);
});
// Preserve control widths when the viewport is narrower than the context tools.
toolbar_layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
auto *toolbar_scroll = new QScrollArea(center);
toolbar_scroll->setObjectName("contextToolbarScroll");
toolbar_scroll->setWidgetResizable(true);
toolbar_scroll->setFrameShape(QFrame::NoFrame);
toolbar_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
toolbar_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
toolbar_scroll->setFixedHeight(56);
toolbar_scroll->setWidget(toolbar);
center_layout->addWidget(toolbar_scroll);
viewport_shelf_splitter_ = splitter(Qt::Horizontal, center, "viewportShelfSplitter");
center_layout->addWidget(viewport_shelf_splitter_, 1);
viewport_shelf_splitter_->addWidget(viewport);
viewport_shelf_splitter_->setStretchFactor(1, 1);
