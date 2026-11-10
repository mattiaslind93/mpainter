// SPDX-License-Identifier: GPL-3.0-or-later
// Material channel rows: base color, metallic, roughness, normal, bump, displacement. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  color_row_ = new QWidget(material);
  auto *color_layout = column(color_row_);
  // Label on the left, a small filled swatch on the right, the way every other
  // property row in the panel is shaped. Krita, Substance Painter and Blender
  // all show a brush colour as a swatch and keep the numbers inside the picker;
  // a panel-wide button with the hex printed on it read as a button and cost a
  // caption line of its own. docs/design/ui.md §7.5.
  auto *color_line = new QWidget(color_row_);
  auto *color_row_layout = row(color_line);
  auto *color_caption = new QLabel("Base Color", color_line);
  color_caption->setProperty("fillCaption", true);
  color_caption->setToolTip("Scene-linear ACEScg");
  color_row_layout->addWidget(color_caption);
  color_row_layout->addStretch();
  color_picker_ = new ColorPicker(color_line);
  color_picker_->setObjectName("materialColorPicker");
  color_picker_->set_compact();
  color_controls_ = color_picker_->linear_fields();
  color_row_layout->addWidget(color_picker_);
  color_row_layout->addSpacing(bts::theme::RemoveButtonPx + bts::theme::Space);
  color_layout->addWidget(color_line);
  color_picker_->started = [this] {
    if (callbacks_.color_started)
      callbacks_.color_started();
  };
  color_picker_->changed = [this](const auto &value) {
    if (callbacks_.color_changed)
      callbacks_.color_changed(value);
  };
  color_picker_->finished = [this] {
    if (callbacks_.color_finished)
      callbacks_.color_finished();
  };
  color_picker_->pick_requested = [this] {
    if (callbacks_.color_pick_requested)
      callbacks_.color_pick_requested();
  };
  secondary_color_row_ = new QWidget(color_row_);
  auto *secondary_layout = row(secondary_color_row_);
  auto *secondary_caption = new QLabel("Secondary", secondary_color_row_);
  secondary_caption->setProperty("fillCaption", true);
  secondary_caption->setToolTip("Painted while Ctrl is held");
  secondary_layout->addWidget(secondary_caption);
  secondary_layout->addStretch();
  secondary_color_picker_ = new ColorPicker(secondary_color_row_);
  secondary_color_picker_->setObjectName("secondaryColorPicker");
  secondary_color_picker_->set_compact();
  for (int i = 0; i < 3; ++i)
    secondary_color_picker_->linear_fields()[i]->setObjectName(QString("secondaryColor%1").arg(i));
  secondary_layout->addWidget(secondary_color_picker_);
  // Swapping the pair is one click in Krita and in Photoshop, and the two
  // colours are useless to compare while they sit in different panels.
  auto *swap_colors = new QToolButton(secondary_color_row_);
  swap_colors->setObjectName("swapBrushColors");
  swap_colors->setText("⇄");
  swap_colors->setToolTip("Swap base and secondary colour");
  swap_colors->setFixedSize(bts::theme::RemoveButtonPx, bts::theme::SwatchHeightPx);
  secondary_layout->addWidget(swap_colors);
  connect(swap_colors, &QToolButton::clicked, this, [this] {
    const auto base = color_picker_->color(), other = secondary_color_picker_->color();
    color_picker_->accept_sample(other);
    secondary_color_picker_->accept_sample(base);
  });
  color_layout->addWidget(secondary_color_row_);
  std::array<float, 4> secondary{1, 1, 1, 1};
  {
    QSettings settings("mPainter", "Preferences");
    const auto saved = settings.value("Brush/secondaryColor").toList();
    if (saved.size() == 4)
      for (int i = 0; i < 4; ++i) {
        const auto value = saved[i].toDouble();
        if (std::isfinite(value) && value >= 0 && value <= 65504)
          secondary[i] = value;
      }
  }
  secondary_color_picker_->set_color(secondary);
  if (callbacks_.secondary_color_changed)
    callbacks_.secondary_color_changed(secondary);
  secondary_color_picker_->changed = [this](const auto &value) {
    if (callbacks_.secondary_color_changed)
      callbacks_.secondary_color_changed(value);
  };
  secondary_color_picker_->finished = [this] {
    QVariantList values;
    for (float value : secondary_color_picker_->color())
      values.append(value);
    QSettings settings("mPainter", "Preferences");
    settings.setValue("Brush/secondaryColor", values);
  };
  secondary_color_picker_->pick_requested = [this] {
    if (callbacks_.secondary_pick_requested)
      callbacks_.secondary_pick_requested();
  };
  material_layout->addWidget(color_row_);
  color_row_->hide();
  metallic_row_ = new QWidget(material);
  auto *metallic_layout = new QVBoxLayout(metallic_row_);
  metallic_layout->setContentsMargins(0, 0, 0, 0);
  metallic_layout->setSpacing(0);
  auto *metallic_caption = new QLabel("Metallic", metallic_row_);
  metallic_caption->setProperty("fillCaption", true);
  auto *metallic_value = new QHBoxLayout;
  metallic_value->setContentsMargins(0, 0, 0, 0);
  metallic_value->addWidget(metallic_caption);
  metallic_control_ = new bts::NumericSpinBox(metallic_row_);
  metallic_control_->setObjectName("metallicControl");
  metallic_control_->setLocale(QLocale::c());
  metallic_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  metallic_control_->setAlignment(Qt::AlignRight);
  metallic_control_->setRange(0, 1);
  metallic_control_->setDecimals(3);
  metallic_control_->setSingleStep(.01);
  metallic_control_->setKeyboardTracking(false);
  metallic_control_->setMinimumWidth(0);
  metallic_value->addWidget(metallic_control_, 1);
  metallic_layout->addLayout(metallic_value);
  auto *metallic_slider = new QSlider(Qt::Horizontal, metallic_row_);
  metallic_slider->setObjectName("metallicSlider");
  metallic_slider->setRange(0, 1000);
  metallic_layout->addWidget(metallic_slider);
  auto *metallic_gradient = new FillGradient(metallic_slider, metallic_control_, metallic_row_);
  metallic_gradient->setObjectName("fillGradientMetallic");
  metallic_layout->addWidget(metallic_gradient);
  metallic_control_->setProperty("fillHideInFill", true);
  metallic_slider->setProperty("fillHideInFill", true);
  connect(metallic_slider, &QSlider::valueChanged, this, [this](int value) {
    metallic_control_->setValue(value / 1000.);
  });
  connect(metallic_control_,
          &QDoubleSpinBox::valueChanged,
          this,
          [this, metallic_slider](double value) {
            const QSignalBlocker block(metallic_slider);
            metallic_slider->setValue(qRound(value * 1000));
            if (callbacks_.metallic_changed)
              callbacks_.metallic_changed(value);
          });
  material_layout->addWidget(metallic_row_);
  metallic_row_->hide();
  auto *roughness_row = new QWidget(material);
  roughness_row_ = roughness_row;
  auto *roughness_layout = row(roughness_row, 0);
  auto *roughness_label = new QLabel("Roughness", roughness_row);
  roughness_label->setProperty("fillCaption", true);
  roughness_layout->addWidget(roughness_label);
  roughness_control_ = new bts::NumericSpinBox(roughness_row);
  roughness_control_->setObjectName("roughnessControl");
  roughness_control_->setLocale(QLocale::c());
  roughness_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  roughness_control_->setAlignment(Qt::AlignRight);
  roughness_control_->setRange(0, 1);
  roughness_control_->setDecimals(3);
  roughness_control_->setSingleStep(0.01);
  roughness_control_->setMinimumWidth(0);
  roughness_control_->setKeyboardTracking(false);
  roughness_control_->setAccessibleName("Material roughness");
  roughness_control_->setEnabled(false);
  roughness_control_->setToolTip("No editable material selected.");
  roughness_label->setBuddy(roughness_control_);
  roughness_layout->addWidget(roughness_control_, 1);
  material_layout->addWidget(roughness_row);
  roughness_slider_ = new QSlider(Qt::Horizontal, material);
  roughness_slider_->setObjectName("roughnessSlider");
  roughness_slider_->setRange(0, 1000);
  roughness_slider_->setSingleStep(10);
  roughness_slider_->setPageStep(100);
  roughness_slider_->setAccessibleName("Material roughness");
  roughness_slider_->setEnabled(false);
  roughness_slider_->setToolTip("No editable material selected.");
  material_layout->addWidget(roughness_slider_);
  roughness_row_->setProperty("fillHideInFill", true);
  roughness_slider_->setProperty("fillHideInFill", true);
  auto *roughness_gradient = new FillGradient(roughness_slider_, roughness_control_, material);
  roughness_gradient->setObjectName("fillGradientRoughness");
  material_layout->addWidget(roughness_gradient);
  normal_label_ = new QLabel("Normal", material);
  normal_label_->setProperty("fillCaption", true);
  material_layout->addWidget(normal_label_);
  normal_row_ = new QWidget(material);
  auto *normal_box = column(normal_row_);
  auto *tangent_bar = new TangentBumpBar(normal_row_);
  normal_box->addWidget(tangent_bar);
  auto *tangent_values = new QWidget(normal_row_);
  tangent_values->setObjectName("tangentBumpValues");
  tangent_values->hide();
  auto *normal_inputs = row(tangent_values);
  normal_box->addWidget(tangent_values);
  for (int component = 0; component < 3; ++component) {
    auto *control = new bts::NumericSpinBox(tangent_values);
    control->setObjectName(QString("materialNormal%1").arg(component));
    control->setLocale(QLocale::c());
    control->setMinimumWidth(0);
    control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    control->setRange(-1, 1);
    control->setDecimals(3);
    control->setSingleStep(.05);
    control->setPrefix(QString("%1 ").arg(QLatin1Char("XYZ"[component])));
    normal_values_[component] = control;
    normal_inputs->addWidget(control);
    connect(control,
            &QDoubleSpinBox::valueChanged,
            this,
            [this, component, tangent_bar](double value) {
              tangent_bar->set_normal({float(normal_values_[0]->value()),
                                       float(normal_values_[1]->value()),
                                       float(normal_values_[2]->value())});
              if (callbacks_.normal_changed)
                callbacks_.normal_changed(component, value);
            });
  }
  tangent_bar->edited = [this](int component, double value) {
    normal_values_[component]->setValue(value);
  };
  material_layout->addWidget(normal_row_);
  normal_row_->hide();
  auto *bump_editor = new QWidget(material);
  bump_editor->setObjectName("bumpEditor");
  auto *bump_box = column(bump_editor);
  auto *bump_line = new QWidget(bump_editor);
  auto *bump_value_line = row(bump_line, 0);
  bump_label_ = new QLabel("Bump", bump_line);
  bump_label_->setProperty("fillCaption", true);
  bump_value_line->addWidget(bump_label_);
  bump_control_ = new bts::NumericSpinBox(bump_line);
  bump_control_->setObjectName("materialBump");
  bump_control_->setLocale(QLocale::c());
  bump_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  bump_control_->setAlignment(Qt::AlignRight);
  bump_control_->setRange(-1, 1);
  bump_control_->setDecimals(3);
  bump_control_->setSingleStep(.01);
  bump_control_->setMinimumWidth(0);
  bump_control_->setKeyboardTracking(false);
  bump_value_line->addWidget(bump_control_, 1);
  bump_box->addWidget(bump_line);
  auto *bump_slider = new QSlider(Qt::Horizontal, bump_editor);
  bump_slider->setObjectName("bumpSlider");
  bump_slider->setRange(-1000, 1000);
  bump_box->addWidget(bump_slider);
  auto *bump_gradient = new FillGradient(bump_slider, bump_control_, bump_editor);
  bump_gradient->setObjectName("fillGradientBump");
  bump_gradient->setToolTip(
      "Bump controls surface detail. Native Normal Map and Bump chains remain normals in the viewport and are combined into one grayscale Bump texture at export.");
  bump_box->addWidget(bump_gradient);
  bump_control_->setProperty("fillHideInFill", true);
  bump_slider->setProperty("fillHideInFill", true);
  material_layout->addWidget(bump_editor);
  bump_editor->hide();
  connect(bump_slider, &QSlider::valueChanged, this, [this](int value) {
    bump_control_->setValue(value / 1000.);
  });
  connect(bump_control_, &QDoubleSpinBox::valueChanged, this, [this, bump_slider](double value) {
    const QSignalBlocker block(bump_slider);
    bump_slider->setValue(qRound(value * 1000));
    if (callbacks_.bump_changed)
      callbacks_.bump_changed(value);
  });
  auto *disp_row = new QWidget(material);
  disp_row->setObjectName("materialDisplacementRow");
  auto *disp_layout = column(disp_row);
  auto *disp_line = new QHBoxLayout;
  disp_line->setContentsMargins(0, 0, 0, 0);
  auto *disp_toggle = new QToolButton(channel_line);
  disp_toggle->setObjectName("brushDisplacement");
  disp_toggle->setProperty("artistChannelToggle", true);
  disp_toggle->setText("disp");
  disp_toggle->setToolTip("Displacement. Moves the surface. Separate from grayscale Bump.");
  disp_toggle->setCheckable(true);
  brush_channel_layout->insertWidget(brush_channel_layout->indexOf(brush_ao_), disp_toggle);
  disp_line->addWidget(new QLabel("Displacement", disp_row));
  auto *disp_value = new bts::NumericSpinBox(disp_row);
  disp_value->setObjectName("materialDisplacement");
  disp_value->setLocale(QLocale::c());
  disp_value->setButtonSymbols(QAbstractSpinBox::NoButtons);
  disp_value->setAlignment(Qt::AlignRight);
  disp_value->setRange(-1, 1);
  disp_value->setDecimals(4);
  disp_value->setSingleStep(.05);
  disp_value->setKeyboardTracking(false);
  disp_value->setMinimumWidth(0);
  disp_line->addWidget(disp_value, 1);
  disp_layout->addLayout(disp_line);
  auto *disp_slider = new QSlider(Qt::Horizontal, disp_row);
  disp_slider->setObjectName("displacementSlider");
  disp_slider->setRange(-1000, 1000);
  disp_layout->addWidget(disp_slider);
  auto *disp_gradient = new FillGradient(disp_slider, disp_value, disp_row);
  disp_gradient->setObjectName("fillGradientDisplacement");
  disp_gradient->setToolTip("Displacement. Moves the surface. Separate from grayscale Bump.");
  disp_layout->addWidget(disp_gradient);
  disp_value->setProperty("fillHideInFill", true);
  disp_slider->setProperty("fillHideInFill", true);
  material_layout->addWidget(disp_row);
  disp_row->hide();
  connect(disp_toggle, &QToolButton::toggled, this, [this](bool enabled) {
    if (callbacks_.displacement_brush_changed)
      callbacks_.displacement_brush_changed(enabled);
  });
  connect(disp_slider, &QSlider::valueChanged, disp_value, [disp_value](int value) {
    disp_value->setValue(value / 1000.);
  });
  connect(disp_value, &QDoubleSpinBox::valueChanged, this, [this, disp_slider](double value) {
    const QSignalBlocker block(disp_slider);
    disp_slider->setValue(qRound(value * 1000));
    if (callbacks_.displacement_value_changed)
      callbacks_.displacement_value_changed(value);
  });
  normal_label_->hide();
  normal_row_->hide();
  bump_label_->hide();
  ao_value_row_ = new QWidget(material);
  auto *ao_box = column(ao_value_row_);
  auto *ao_caption = new QLabel("Ambient occlusion", ao_value_row_);
  ao_caption->setProperty("fillCaption", true);
  auto *ao_value_line = new QHBoxLayout;
  ao_value_line->setContentsMargins(0, 0, 0, 0);
  ao_value_line->addWidget(ao_caption);
  ao_value_ = new bts::NumericSpinBox(ao_value_row_);
  ao_value_->setObjectName("materialAmbientOcclusion");
  ao_value_->setLocale(QLocale::c());
  ao_value_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  ao_value_->setAlignment(Qt::AlignRight);
  ao_value_->setRange(0, 1);
  ao_value_->setDecimals(3);
  ao_value_->setSingleStep(.05);
  ao_value_->setValue(1);
  ao_value_->setMinimumWidth(0);
  ao_value_->setKeyboardTracking(false);
  ao_value_line->addWidget(ao_value_, 1);
  ao_box->addLayout(ao_value_line);
  auto *ao_slider = new QSlider(Qt::Horizontal, ao_value_row_);
  ao_slider->setObjectName("ambientOcclusionSlider");
  ao_slider->setRange(0, 1000);
  ao_slider->setValue(1000);
  ao_box->addWidget(ao_slider);
  auto *ao_gradient = new FillGradient(ao_slider, ao_value_, ao_value_row_);
  ao_gradient->setObjectName("fillGradientAmbientOcclusion");
  ao_box->addWidget(ao_gradient);
  ao_value_->setProperty("fillHideInFill", true);
  ao_slider->setProperty("fillHideInFill", true);
  material_layout->addWidget(ao_value_row_);
  ao_value_row_->hide();
  connect(ao_slider, &QSlider::valueChanged, this, [this](int value) {
    ao_value_->setValue(value / 1000.);
  });
  connect(ao_value_, &QDoubleSpinBox::valueChanged, this, [this, ao_slider](double value) {
    const QSignalBlocker block(ao_slider);
    ao_slider->setValue(qRound(value * 1000));
    if (callbacks_.ao_changed)
      callbacks_.ao_changed(value);
  });
  for (int slot = 0; slot < custom_channel_count; ++slot) {
    auto *panel = new QWidget(material);
    user_value_rows_[slot] = panel;
    auto *layout = column(panel);
    auto *label = new QLabel(panel);
    label->setProperty("fillCaption", true);
    user_value_labels_[slot] = label;
    auto *line = new QHBoxLayout;
    line->addWidget(label);
    user_fill_enabled_[slot] = new QToolButton(panel);
    user_fill_enabled_[slot]->setObjectName(QString("fillUserEnabled%1").arg(slot));
    user_fill_enabled_[slot]->setCheckable(true);
    user_fill_enabled_[slot]->setText("Enabled");
    line->addWidget(user_fill_enabled_[slot]);
    connect(user_fill_enabled_[slot], &QToolButton::toggled, this, [this, slot](bool value) {
      if (callbacks_.user_brush_changed)
        callbacks_.user_brush_changed(slot, value);
    });
    auto *value = new bts::NumericSpinBox(panel);
    user_values_[slot] = value;
    value->setObjectName(QString("userChannelValue%1").arg(slot));
    value->setLocale(QLocale::c());
    value->setButtonSymbols(QAbstractSpinBox::NoButtons);
    value->setAlignment(Qt::AlignRight);
    value->setRange(0, 1);
    value->setDecimals(3);
    value->setSingleStep(.01);
    value->setKeyboardTracking(false);
    line->addWidget(value, 1);
    auto *invert = new QToolButton(panel);
    invert->setObjectName(QString("invertUserValue%1").arg(slot));
    invert->setProperty("fillHideInFill", true);
    invert->setText("↔");
    invert->setToolTip("Invert channel value");
    connect(invert, &QToolButton::clicked, this, [value] { value->setValue(1 - value->value()); });
    line->addWidget(invert);
    layout->addLayout(line);
    auto *slider = new QSlider(Qt::Horizontal, panel);
    user_sliders_[slot] = slider;
    slider->setRange(0, 1000);
    layout->addWidget(slider);
    auto *gradient = new FillGradient(slider, value, panel);
    gradient->setObjectName(QString("fillGradientUser%1").arg(slot));
    layout->addWidget(gradient);
    value->setProperty("fillHideInFill", true);
    slider->setProperty("fillHideInFill", true);
    connect(slider, &QSlider::valueChanged, this, [value](int v) { value->setValue(v / 1000.); });
    connect(value, &QDoubleSpinBox::valueChanged, this, [this, slot, slider](double v) {
      const QSignalBlocker block(slider);
      slider->setValue(qRound(v * 1000));
      if (callbacks_.user_value_changed)
        callbacks_.user_value_changed(slot, v);
    });
    connect(slider, &QSlider::sliderPressed, this, [this, slot, slider] {
      if (!slider->property("userFillPropertyEdit").toBool())
        return;
      slider->setProperty("userFillPropertyDragging", true);
      if (callbacks_.property_edit_started)
        callbacks_.property_edit_started(QString("User channel %1 value").arg(slot + 1));
    });
    connect(slider, &QSlider::sliderReleased, this, [this, slider] {
      if (!slider->property("userFillPropertyDragging").toBool())
        return;
      slider->setProperty("userFillPropertyDragging", false);
      if (callbacks_.property_edit_finished)
        callbacks_.property_edit_finished();
    });
    connect(value, &QDoubleSpinBox::editingFinished, this, [this, slider] {
      if (slider->isSliderDown() || !slider->property("userFillPropertyEdit").toBool())
        return;
      if (callbacks_.property_edit_finished)
        callbacks_.property_edit_finished();
    });
    material_layout->addWidget(panel);
    panel->hide();
  }
  fill_panel_->register_uniform_controls(0, {color_row_});
  fill_panel_->register_uniform_controls(1, {metallic_row_});
  fill_panel_->register_uniform_controls(2,
                                         {roughness_row_, roughness_slider_, roughness_gradient});
  fill_panel_->register_uniform_controls(3, {normal_label_, normal_row_});
  fill_panel_->register_uniform_controls(4, {bump_editor});
  fill_panel_->register_uniform_controls(5, {ao_value_row_});
  for (int slot = 0; slot < 8; ++slot)
    fill_panel_->register_uniform_controls(slot + 6, {user_value_rows_[slot]});
  fill_panel_->register_uniform_controls(14, {disp_row});
  // The field is the slider for the material channels too, the way it already
  // is for the brush: drag across it to change the value, click it to type one.
  // The separate track under each caption goes away, which is a line of height
  // per channel. The gesture still runs through the hidden slider, so undo
  // grouping and the live material preview are untouched. docs/design/ui.md §7.4.
  std::vector<std::pair<QDoubleSpinBox *, QSlider *>> channel_rows{
      {metallic_control_, metallic_slider},
      {roughness_control_, roughness_slider_},
      {bump_control_, bump_slider},
      {ao_value_, ao_slider},
      {disp_value, disp_slider}};
  for (int slot = 0; slot < custom_channel_count; ++slot)
    channel_rows.push_back({user_values_[slot], user_sliders_[slot]});
  for (auto *row : std::initializer_list<QWidget *>{color_row_,
                                                    metallic_row_,
                                                    roughness_row_,
                                                    ao_value_row_,
                                                    disp_row,
                                                    bump_editor,
                                                    normal_row_})
    row->setSizePolicy(row->sizePolicy().horizontalPolicy(), QSizePolicy::Maximum);
  for (const auto &[field, slider] : channel_rows) {
    if (auto *numeric = dynamic_cast<bts::NumericSpinBox *>(field)) {
      numeric->set_track(true);
      // Pressing and releasing the hidden slider is what scoped one edit for
      // undo and for the preview, so a drag across the field does exactly that
      // rather than repeating the bookkeeping here.
      numeric->drag_started = [slider] {
        if (slider)
          slider->setSliderDown(true);
      };
      numeric->drag_finished = [slider] {
        if (slider)
          slider->setSliderDown(false);
      };
    }
    // FillPanel::refresh_channel_visibility shows every control marked
    // fillHideInFill again when the panel leaves fill context, which put these
    // tracks straight back under their fields. The slider stays alive for the
    // gesture it still carries, but it is not a visible control any more.
    if (slider) {
      slider->setProperty("fillHideInFill", false);
      slider->hide();
    }
  }
  // One label column down the channel list. These rows nest a value line inside
  // a column so the fill ramp can sit under them, which the flat pass above
  // cannot see.
  {
    std::vector<QLabel *> captions;
    int widest = 0;
    for (auto *row : std::initializer_list<QWidget *>{
             color_row_, metallic_row_, roughness_row_, ao_value_row_, disp_row, bump_editor})
      for (auto *label : row->findChildren<QLabel *>())
        if (label->property("fillCaption").toBool() && !label->text().isEmpty()) {
          captions.push_back(label);
          widest = std::max(widest, label->sizeHint().width());
        }
    for (auto *label : captions)
      label->setMinimumWidth(widest);
  }
  // Keep the shared channel controls under one stable parent. Reparenting the
  // whole tree on Paint/Fill selection repolishes hundreds of Qt controls.
  material_layout->insertWidget(material_layout->indexOf(brush_controls_) + 1,
                                fill_panel_->channel_panel());
  fill_panel_->texture_cancelled = [this](int c) {
    if (callbacks_.fill_texture_cancelled)
      callbacks_.fill_texture_cancelled(c);
  };
  fill_panel_->channel_enabled = [this](int c, bool enabled) {
    if (callbacks_.fill_channel_enabled)
      callbacks_.fill_channel_enabled(c, enabled);
  };
  fill_panel_->files_dropped = [this](int c, const QStringList &paths) {
    if (callbacks_.fill_texture_files_dropped)
      callbacks_.fill_texture_files_dropped(c, paths);
  };
  fill_panel_->asset_dropped = [this](int c, const QString &id) {
    if (callbacks_.fill_texture_asset_dropped)
      callbacks_.fill_texture_asset_dropped(c, id);
  };
  material_layout->addStretch();
  properties_scroll->setWidget(material);
  properties_layout->addWidget(properties_scroll, 1);

