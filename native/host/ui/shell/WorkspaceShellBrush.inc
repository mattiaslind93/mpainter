// SPDX-License-Identifier: GPL-3.0-or-later
// Brush settings in the Properties tab. Part of the WorkspaceShell constructor
// (WorkspaceShell.cc), included in order inside its body; it shares that function's locals and is
// not a standalone file.
brush_controls_ = new QWidget(material);
auto *brush_layout = column(brush_controls_);
brush_layout->setSpacing(bts::theme::SpaceSection);
// The brush settings are the longest list in the panel, so they are where
// folding earns its keep. Order is unchanged. docs/design/ui.md §7.2.
auto *brush_section = make_section("Brush", brush_controls_);
auto *jitter_section = make_section("Jitter", brush_controls_, true);
auto *projection_section_brush = make_section("Projection", brush_controls_, true);
auto *stencil_section = make_section("Stencil", brush_controls_, true);
auto *channels_section = make_section("Channels", brush_controls_, true);
auto *alignment_section = make_section("Alignment", brush_controls_, true);
auto *alpha_section = make_section("Alpha", brush_controls_, true);
for (auto *section : {brush_section,
                      jitter_section,
                      projection_section_brush,
                      stencil_section,
                      channels_section,
                      alignment_section,
                      alpha_section})
  brush_layout->addWidget(section);
brush_preview_ = new BrushPreview(brush_controls_);
brush_preview_->set_renderer([this](const BrushSettings &brush,
                                    std::function<void(QImage)> ready) {
  if (callbacks_.brush_preview_requested)
    callbacks_.brush_preview_requested(brush, std::move(ready));
  else
    ready({});
});
brush_section->add(brush_preview_);
auto *save_brush = new QPushButton("Save brush preset…", brush_controls_);
save_brush->setObjectName("saveBrushPresetButton");
brush_section->add(save_brush);
connect(save_brush, &QPushButton::clicked, this, [this] {
  if (callbacks_.save_brush_preset)
    callbacks_.save_brush_preset();
});
auto *size_line = new QWidget(brush_controls_);
auto *size_layout = row(size_line);
size_layout->addWidget(new QLabel("Size", size_line));
brush_size_ = new bts::NumericSpinBox(size_line);
brush_size_->setRange(.1, 100);
brush_size_->setDecimals(1);
brush_size_->setValue(10);
brush_size_->setKeyboardTracking(false);
brush_size_->setObjectName("brushSize");
size_layout->addWidget(brush_size_);
brush_section->add(size_line);
connect(brush_size_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
  if (callbacks_.brush_size_changed)
    callbacks_.brush_size_changed(value);
});
brush_pressure_[0] = tool(size_line, Glyph::Brush, "Use pen pressure for Size", true);
brush_pressure_[0]->setCheckable(true);
brush_pressure_[0]->setObjectName("brushPressureSize");
size_layout->addWidget(brush_pressure_[0]);
connect(brush_pressure_[0], &QToolButton::toggled, this, [this](bool value) {
  if (callbacks_.brush_pressure_changed)
    callbacks_.brush_pressure_changed(0, value);
});
auto *minimum_size_line = new QWidget(brush_controls_);
auto *minimum_size_layout = row(minimum_size_line);
minimum_size_layout->addWidget(new QLabel("Minimum size (%)", minimum_size_line));
brush_minimum_[0] = new bts::NumericSpinBox(minimum_size_line);
brush_minimum_[0]->setRange(0, 100);
brush_minimum_[0]->setDecimals(1);
brush_minimum_[0]->setValue(5);
brush_minimum_[0]->setKeyboardTracking(false);
brush_minimum_[0]->setObjectName("brushMinimumSize");
minimum_size_layout->addWidget(brush_minimum_[0]);
brush_section->add(minimum_size_line);
connect(brush_minimum_[0], &QDoubleSpinBox::valueChanged, this, [this](double value) {
  if (callbacks_.brush_minimum_changed)
    callbacks_.brush_minimum_changed(0, value);
});
auto *flow_line = new QWidget(brush_controls_);
auto *flow_layout = row(flow_line);
flow_layout->addWidget(new QLabel("Flow", flow_line));
brush_flow_ = new bts::NumericSpinBox(flow_line);
brush_flow_->setRange(0, 100);
brush_flow_->setDecimals(1);
brush_flow_->setValue(100);
brush_flow_->setKeyboardTracking(false);
brush_flow_->setObjectName("brushFlow");
flow_layout->addWidget(brush_flow_);
brush_section->add(flow_line);
brush_flow_slider_ = new QSlider(Qt::Horizontal, brush_controls_);
brush_flow_slider_->setRange(0, 1000);
brush_flow_slider_->setValue(1000);
brush_section->add(brush_flow_slider_);
connect(brush_flow_slider_, &QSlider::valueChanged, this, [this](int value) {
  brush_flow_->setValue(value / 10.0);
});
connect(brush_flow_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
  if (callbacks_.brush_flow_changed)
    callbacks_.brush_flow_changed(value);
});
brush_pressure_[1] = tool(flow_line, Glyph::Brush, "Use pen pressure for Flow", true);
brush_pressure_[1]->setCheckable(true);
brush_pressure_[1]->setObjectName("brushPressureFlow");
flow_layout->addWidget(brush_pressure_[1]);
connect(brush_pressure_[1], &QToolButton::toggled, this, [this](bool value) {
  if (callbacks_.brush_pressure_changed)
    callbacks_.brush_pressure_changed(1, value);
});
auto *minimum_flow_line = new QWidget(brush_controls_);
auto *minimum_flow_layout = row(minimum_flow_line);
minimum_flow_layout->addWidget(new QLabel("Minimum flow (%)", minimum_flow_line));
brush_minimum_[1] = new bts::NumericSpinBox(minimum_flow_line);
brush_minimum_[1]->setRange(0, 100);
brush_minimum_[1]->setDecimals(1);
brush_minimum_[1]->setValue(5);
brush_minimum_[1]->setKeyboardTracking(false);
brush_minimum_[1]->setObjectName("brushMinimumFlow");
minimum_flow_layout->addWidget(brush_minimum_[1]);
brush_section->add(minimum_flow_line);
connect(brush_minimum_[1], &QDoubleSpinBox::valueChanged, this, [this](double value) {
  if (callbacks_.brush_minimum_changed)
    callbacks_.brush_minimum_changed(1, value);
});
auto *opacity_line = new QWidget(brush_controls_);
auto *opacity_layout = row(opacity_line);
opacity_layout->addWidget(new QLabel("Stroke opacity", opacity_line));
brush_opacity_ = new bts::NumericSpinBox(opacity_line);
brush_opacity_->setRange(0, 100);
brush_opacity_->setDecimals(1);
brush_opacity_->setValue(100);
brush_opacity_->setKeyboardTracking(false);
brush_opacity_->setObjectName("brushStrokeOpacity");
opacity_layout->addWidget(brush_opacity_);
brush_section->add(opacity_line);
brush_opacity_slider_ = new QSlider(Qt::Horizontal, brush_controls_);
brush_opacity_slider_->setRange(0, 1000);
brush_opacity_slider_->setValue(1000);
brush_section->add(brush_opacity_slider_);
connect(brush_opacity_slider_, &QSlider::valueChanged, this, [this](int value) {
  brush_opacity_->setValue(value / 10.0);
});
connect(brush_opacity_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
  if (callbacks_.brush_opacity_changed)
    callbacks_.brush_opacity_changed(value);
});
auto *spacing_line = new QWidget(brush_controls_);
auto *spacing_layout = row(spacing_line);
spacing_layout->addWidget(new QLabel("Spacing", spacing_line));
brush_spacing_ = new bts::NumericSpinBox(spacing_line);
brush_spacing_->setRange(1, 500);
brush_spacing_->setDecimals(1);
brush_spacing_->setValue(20);
brush_spacing_->setKeyboardTracking(false);
brush_spacing_->setObjectName("brushSpacing");
spacing_layout->addWidget(brush_spacing_);
brush_section->add(spacing_line);
brush_spacing_slider_ = new QSlider(Qt::Horizontal, brush_controls_);
brush_spacing_slider_->setRange(10, 5000);
brush_spacing_slider_->setValue(200);
brush_section->add(brush_spacing_slider_);
connect(brush_spacing_slider_, &QSlider::valueChanged, this, [this](int value) {
  brush_spacing_->setValue(value / 10.0);
});
connect(brush_spacing_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
  if (callbacks_.brush_spacing_changed)
    callbacks_.brush_spacing_changed(value);
});
auto *angle_line = new QWidget(brush_controls_);
auto *angle_layout = row(angle_line);
angle_layout->addWidget(new QLabel("Angle", angle_line));
brush_angle_ = new bts::NumericSpinBox(angle_line);
brush_angle_->setObjectName("brushAngle");
brush_angle_->setRange(-180, 180);
brush_angle_->setDecimals(1);
brush_angle_->setKeyboardTracking(false);
angle_layout->addWidget(brush_angle_);
jitter_section->add(angle_line);
connect(brush_angle_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
  if (callbacks_.brush_angle_changed)
    callbacks_.brush_angle_changed(value);
});
auto *follow_line = new QWidget(brush_controls_);
auto *follow_layout = row(follow_line);
follow_layout->addWidget(new QLabel("Follow Path", follow_line));
brush_follow_path_ = new QToolButton(follow_line);
brush_follow_path_->setObjectName("brushFollowPath");
brush_follow_path_->setCheckable(true);
brush_follow_path_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
follow_layout->addWidget(brush_follow_path_);
jitter_section->add(follow_line);
connect(brush_follow_path_, &QToolButton::toggled, this, [this](bool enabled) {
  if (callbacks_.brush_follow_path_changed)
    callbacks_.brush_follow_path_changed(enabled);
});
const std::array<const char *, 4> jitter_labels{
    "Size Jitter", "Flow Jitter", "Angle Jitter", "Position Jitter"};
const std::array<const char *, 4> jitter_ids{
    "brushSizeJitter", "brushFlowJitter", "brushAngleJitter", "brushPositionJitter"};
for (int i = 0; i < 4; ++i) {
  auto *line = new QWidget(brush_controls_);
  auto *layout = row(line);
  layout->addWidget(new QLabel(jitter_labels[i], line));
  auto *spin = new bts::NumericSpinBox(line);
  brush_jitter_[i] = spin;
  spin->setObjectName(jitter_ids[i]);
  spin->setAccessibleName(jitter_labels[i]);
  spin->setRange(0, i == 2 ? 180 : 100);
  spin->setDecimals(1);
  spin->setKeyboardTracking(false);
  layout->addWidget(spin);
  jitter_section->add(line);
  auto *slider = new QSlider(Qt::Horizontal, brush_controls_);
  brush_jitter_sliders_[i] = slider;
  slider->setRange(0, i == 2 ? 1800 : 1000);
  jitter_section->add(slider);
  connect(
      slider, &QSlider::valueChanged, this, [spin](int value) { spin->setValue(value / 10.); });
  connect(spin, &QDoubleSpinBox::valueChanged, this, [this, i, slider](double value) {
    const QSignalBlocker blocker(slider);
    slider->setValue(qRound(value * 10));
    if (callbacks_.brush_jitter_changed)
      callbacks_.brush_jitter_changed(i, value);
  });
}
projection_panel_ = new ProjectionPanel(brush_controls_);
projection_section_brush->add(projection_panel_);
projection_panel_->changed = [this](const ProjectionMaterial &source) {
  if (callbacks_.projection_material_changed)
    callbacks_.projection_material_changed(source);
};
projection_panel_->resource_selected = [this](bool weave) {
  if (callbacks_.projection_resource_selected)
    callbacks_.projection_resource_selected(weave);
};
stencil_panel_ = new StencilPanel(brush_controls_);
stencil_section->add(stencil_panel_);
stencil_panel_->changed = [this](const BrushStencil &settings) {
  if (callbacks_.stencil_changed)
    callbacks_.stencil_changed(settings);
};
stencil_panel_->import_requested = [this] {
  if (callbacks_.stencil_import_requested)
    callbacks_.stencil_import_requested();
};
stencil_panel_->asset_requested = [this](QUuid id) {
  return callbacks_.stencil_asset_requested && callbacks_.stencil_asset_requested(id);
};
auto *channel_line = new QWidget(brush_controls_);
auto *brush_channel_layout = row(channel_line);
const char *labels[] = {"color", "metal", "rough", "nrm", "bump"};
for (int i = 0; i < 5; ++i) {
  auto *button = new QToolButton(channel_line);
  button->setProperty("artistChannelToggle", true);
  brush_channels_[i] = button;
  button->setText(labels[i]);
  button->setToolTip(i == 4 ? "Bump. Grayscale shading only; does not move the surface." :
                              labels[i]);
  button->setCheckable(true);
  button->setChecked(i != 3);
  if (i == 3)
    button->hide();
  button->setObjectName(QString("brushChannel%1").arg(i));
  brush_channel_layout->addWidget(button);
  connect(button, &QToolButton::toggled, this, [this, i](bool checked) {
    if (callbacks_.brush_channel_changed)
      callbacks_.brush_channel_changed(i, checked);
  });
}
brush_ao_ = new QToolButton(channel_line);
brush_ao_->setObjectName("brushChannel5");
brush_ao_->setProperty("artistChannelToggle", true);
brush_ao_->setText("ao");
brush_ao_->setCheckable(true);
brush_ao_->hide();
brush_channel_layout->addWidget(brush_ao_);
connect(brush_ao_, &QToolButton::toggled, this, [this](bool enabled) {
  if (callbacks_.brush_channel_changed)
    callbacks_.brush_channel_changed(5, enabled);
});
channels_section->add(channel_line);
auto *user_buttons = new QWidget(brush_controls_);
auto *user_grid = new QGridLayout(user_buttons);
user_grid->setContentsMargins(0, 0, 0, 0);
user_grid->setSpacing(4);
for (int slot = 0; slot < custom_channel_count; ++slot) {
  auto *button = new QToolButton(user_buttons);
  user_brush_[slot] = button;
  button->setObjectName(QString("brushUserChannel%1").arg(slot));
  button->setCheckable(true);
  user_grid->addWidget(button, slot / 4, slot % 4);
  button->hide();
  connect(button, &QToolButton::toggled, this, [this, slot](bool value) {
    if (callbacks_.user_brush_changed)
      callbacks_.user_brush_changed(slot, value);
  });
}
channels_section->add(user_buttons);

auto *alignment_line = new QWidget(brush_controls_);
auto *alignment_layout = row(alignment_line);
alignment_layout->addWidget(new QLabel("Alignment", alignment_line));
brush_alignment_ = new QComboBox(alignment_line);
brush_alignment_->setObjectName("brushAlignment");
brush_alignment_->addItems({"Camera", "Tangent | Wrap", "Tangent | Planar", "UV"});
alignment_layout->addWidget(brush_alignment_);
alignment_section->add(alignment_line);
auto *cull_line = new QWidget(brush_controls_);
auto *cull_layout = row(cull_line);
brush_cull_ = new QCheckBox("Backface culling", cull_line);
brush_cull_->setObjectName("brushBackfaceCulling");
cull_layout->addWidget(brush_cull_);
brush_cull_angle_ = new bts::NumericSpinBox(cull_line);
brush_cull_angle_->setObjectName("brushCullingAngle");
brush_cull_angle_->setRange(0, 180);
brush_cull_angle_->setDecimals(1);
brush_cull_angle_->setKeyboardTracking(false);
cull_layout->addWidget(brush_cull_angle_);
alignment_section->add(cull_line);
brush_cull_slider_ = new QSlider(Qt::Horizontal, brush_controls_);
brush_cull_slider_->setObjectName("brushCullingAngleSlider");
brush_cull_slider_->setRange(0, 1800);
alignment_section->add(brush_cull_slider_);
alignment_section->add(new QLabel("Size Space: Object", brush_controls_));
const auto projection_changed = [this] {
  if (callbacks_.brush_projection_changed)
    callbacks_.brush_projection_changed(
        brush_alignment_->currentIndex(), brush_cull_->isChecked(), brush_cull_angle_->value());
};
connect(brush_alignment_, &QComboBox::currentIndexChanged, this, projection_changed);
connect(brush_cull_, &QCheckBox::toggled, this, projection_changed);
connect(brush_cull_angle_, &QDoubleSpinBox::valueChanged, this, projection_changed);
connect(brush_cull_slider_, &QSlider::valueChanged, this, [this](int value) {
  brush_cull_angle_->setValue(value / 10.);
});
alpha_panel_ = new AlphaPanel(brush_controls_);
alpha_section->add(alpha_panel_);
alpha_panel_->dynamic_changed = [this](const DynamicStrokeParameters &value) {
  if (callbacks_.dynamic_stroke_changed)
    callbacks_.dynamic_stroke_changed(value);
};
alpha_panel_->cleared = [this] {
  BrushAlpha alpha;
  alpha.kind = AlphaKind::Uniform;
  if (callbacks_.alpha_selected)
    callbacks_.alpha_selected(alpha);
};
alpha_panel_->import_requested = [this] {
  if (callbacks_.alpha_import_requested)
    callbacks_.alpha_import_requested();
};
alpha_panel_->parameters_changed = [this](bool invert, double uniform, double hardness) {
  if (callbacks_.alpha_parameters_changed)
    callbacks_.alpha_parameters_changed(invert, uniform, hardness);
};
// The field is the slider now: drag across it to change the value, click it
// to type one. The separate track under each field goes away, which is a line
// of height per property. docs/design/ui.md §7.4.
brush_layout->addStretch();
// Some of these are held as QDoubleSpinBox, so ask rather than assume.
const auto as_track = [](QDoubleSpinBox *field) {
  if (auto *numeric = dynamic_cast<bts::NumericSpinBox *>(field))
    numeric->set_track(true);
};
as_track(brush_flow_);
as_track(brush_opacity_);
as_track(brush_spacing_);
as_track(brush_cull_angle_);
for (auto *field : brush_jitter_)
  as_track(field);
for (auto *track :
     {brush_flow_slider_, brush_opacity_slider_, brush_spacing_slider_, brush_cull_slider_})
  if (track)
    track->hide();
for (auto *track : brush_jitter_sliders_)
  if (track)
    track->hide();
// The pen-pressure buttons made their rows' fields narrower than the rest, so
// the values did not line up down the panel.
// The brush rows sit inside the sections now, so each section's contents is
// its own container for the pass. ui.md §7.3.
std::vector<QWidget *> row_groups{brush_controls_};
for (auto *section : property_sections_)
  row_groups.push_back(section->content());
align_property_rows(row_groups, 28);
material_layout->addWidget(brush_controls_);
brush_controls_->hide();
