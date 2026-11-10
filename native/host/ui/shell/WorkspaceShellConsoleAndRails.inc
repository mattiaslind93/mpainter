// SPDX-License-Identifier: GPL-3.0-or-later
// The console, the right rail, shader dialog, texture loading bar and signal wiring. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  console_ = new QWidget(inspector_splitter_);
  inspector_splitter_->addWidget(console_);
  console_->setObjectName("console");
  console_->setMinimumHeight(90);
  auto *console_layout = column(console_);
  QToolButton *close_console;
  console_layout->addWidget(header(console_, "Console", &close_console));
  console_text_ = new QPlainTextEdit(console_);
  console_text_->setObjectName("consoleLog");
  console_text_->setReadOnly(true);
  console_text_->setPlaceholderText("No messages");
  console_text_->setMaximumBlockCount(1000);
  console_layout->addWidget(console_text_, 1);
  for (int i = 0; i < inspector_splitter_->count(); ++i) {
    inspector_splitter_->setStretchFactor(i, 1);
  }

  auto *right = new QWidget(root);
  right->setObjectName("utilityRail");
  right->setFixedWidth(rail_width);
  auto *right_layout = column(right);
  right_layout->setContentsMargins(
      bts::theme::Space, bts::theme::Space, bts::theme::Space, bts::theme::Space);
  right_layout->setSpacing(bts::theme::Space);
  environment_panel_ = new EnvironmentPanel(this);
  environment_panel_->resource_requested = [this] {
    if (callbacks_.environment_resource_requested)
      callbacks_.environment_resource_requested();
  };
  environment_panel_->changed = [this](const QString &key, double value) {
    if (callbacks_.environment_changed)
      callbacks_.environment_changed(key, value);
  };
  auto *display_button = tool(right, Glyph::View, "Display settings", true);
  display_button->setObjectName("displaySettingsButton");
  right_layout->addWidget(display_button);
  connect(display_button, &QToolButton::clicked, this, [this] {
    environment_panel_->show();
    environment_panel_->raise();
    environment_panel_->activateWindow();
  });
  auto *shader_settings = tool(right, Glyph::Settings, "Shader settings", true);
  shader_settings->setObjectName("shaderSettingsButton");
  right_layout->addWidget(shader_settings);
  auto *shader_dialog = new QDialog(this);
  shader_dialog->setObjectName("shaderSettings");
  shader_dialog->setWindowTitle("Shader settings — Displacement & Tessellation");
  auto *shader_layout = new QVBoxLayout(shader_dialog);
  shader_dialog->resize(400, 700);
  auto *shader_scroll = new QScrollArea(shader_dialog);
  shader_scroll->setWidgetResizable(true);
  auto *shader_content = new QWidget(shader_scroll);
  shader_content->setObjectName("shaderSettingsContent");
  auto *shader_parameters_layout = new QVBoxLayout(shader_content);
  shader_scroll->setWidget(shader_content);
  shader_layout->addWidget(shader_scroll);
  connect(shader_settings, &QToolButton::clicked, this, [=, this] {
    shader_parameters_layout->addWidget(displacement_panel_);
    displacement_panel_->show();
    for (auto *button : shader_dialog->findChildren<QPushButton *>()) {
      button->setAutoDefault(false);
      button->setDefault(false);
    }
    shader_dialog->show();
    shader_dialog->raise();
  });
  connect(shader_dialog, &QDialog::finished, this, [=, this] {
    settings_layout->addWidget(displacement_panel_);
    displacement_panel_->show();
  });
  right_layout->addWidget(tool(right, Glyph::History, "History"));
  auto *console_button = tool(right, Glyph::Console, "Console", true);
  console_button->setDefaultAction(console_action_);
  console_button->setToolTip("Show or hide Console");
  right_layout->addWidget(console_button);
  auto *shelf_button = tool(right, Glyph::Shelf, "Shelf", true);
  shelf_button->setDefaultAction(shelf_action_);
  shelf_button->setToolTip("Show or hide Shelf");
  right_layout->addWidget(shelf_button);
  right_layout->addStretch();
  auto *reset_button = tool(right, Glyph::Reset, "Reset reference layout", true);
  reset_button->setDefaultAction(reset_action);
  reset_button->setToolTip("Reset reference layout");
  right_layout->addWidget(reset_button);
  root_layout->addWidget(right);

  asset_thumbnail_ready(this, [this] { refresh_alpha_shelf(); });
  texture_loading_bar_ = new QWidget(shelf_);
  texture_loading_bar_->setObjectName("shelfTextureLoading");
  auto *texture_loading_layout = new QHBoxLayout(texture_loading_bar_);
  texture_loading_layout->setContentsMargins(4, 2, 4, 2);
  texture_loading_notice_ = new QLabel(texture_loading_bar_);
  texture_loading_notice_->setObjectName("shelfTextureLoadingText");
  texture_loading_notice_->setWordWrap(true);
  texture_loading_layout->addWidget(texture_loading_notice_, 1);
  texture_loading_progress_ = new QProgressBar(texture_loading_bar_);
  texture_loading_progress_->setMaximumWidth(120);
  texture_loading_layout->addWidget(texture_loading_progress_);
  texture_loading_cancel_ = new QPushButton("Cancel", texture_loading_bar_);
  texture_loading_cancel_->setObjectName("cancelTextureLoads");
  texture_loading_layout->addWidget(texture_loading_cancel_);
  shelf_->layout()->addWidget(texture_loading_bar_);
  texture_loading_bar_->hide();
  connect(texture_loading_cancel_, &QPushButton::clicked, this, [this] {
    if (callbacks_.cancel_texture_loads)
      callbacks_.cancel_texture_loads();
  });
  statusBar()->setObjectName("workspaceStatus");
  statusBar()->setFixedHeight(22);
  statusBar()->setSizeGripEnabled(false);

  connect(shelf_action_, &QAction::toggled, this, &WorkspaceShell::set_shelf_visible);
  connect(console_action_, &QAction::toggled, this, &WorkspaceShell::set_console_visible);
  connect(reset_action, &QAction::triggered, this, &WorkspaceShell::reset_reference_layout);
  connect(close_shelf, &QToolButton::clicked, this, [this] { set_shelf_visible(false); });
  connect(close_console, &QToolButton::clicked, this, [this] { set_console_visible(false); });
  connect(texture_sets_, &QListWidget::currentRowChanged, this, [this](int index) {
    active_tile_ = 0;
    if (callbacks_.texture_set_selected) {
      const auto *item = texture_sets_->item(index);
      callbacks_.texture_set_selected(index, item ? item->text() : QString{});
    }
  });
  connect(texture_sets_, &QListWidget::itemClicked, this, [this] {
    if (active_tile_) {
      active_tile_ = 0;
      if (callbacks_.tile_selected)
        callbacks_.tile_selected(0);
    }
  });
  connect(uv_tiles_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
    const int tile = item ? item->data(0, Qt::UserRole).toInt() : 0;
    if (active_tile_ == tile)
      return;
    active_tile_ = tile;
    if (callbacks_.tile_selected)
      callbacks_.tile_selected(tile);
  });
  // GPU requests already coalesce at frame start. Submit the value before
  // Qt schedules its paint instead of delaying it by a second frame timer.
  connect(roughness_slider_, &QSlider::sliderPressed, this, [this] {
    setProperty("roughnessGesture", true);
    if (callbacks_.roughness_started)
      callbacks_.roughness_started();
  });
  connect(roughness_slider_, &QSlider::sliderReleased, this, [this] {
    if (callbacks_.roughness_changed)
      callbacks_.roughness_changed(roughness_control_->value());
    if (callbacks_.roughness_finished)
      callbacks_.roughness_finished();
    setProperty("roughnessGesture", false);
  });
  connect(roughness_slider_, &QSlider::valueChanged, this, [this](int value) {
    roughness_control_->setValue(double(value) / 1000.0);
  });
  connect(roughness_control_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    const QSignalBlocker blocker(roughness_slider_);
    roughness_slider_->setValue(qRound(value * 1000.0));
    if (roughness_control_->isEnabled() && callbacks_.roughness_changed) {
      callbacks_.roughness_changed(value);
    }
  });
  connect(view_layout_control_, &QComboBox::currentIndexChanged, this, [this](int index) {
    view_layout_.mode = index == 0 ? ViewLayout::SceneOnly :
                        index == 1 ? ViewLayout::UvOnly :
                                     ViewLayout::Split;
    if (view_layout_.mode == ViewLayout::Split) {
      view_layout_.uv_first = index == 3;
    }
    if (callbacks_.view_layout_changed) {
      callbacks_.view_layout_changed(view_layout_);
    }
  });
  connect(projection_control_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (callbacks_.projection_changed) {
      callbacks_.projection_changed(index == 1 ? OrbitCamera::Projection::Orthographic :
                                                 OrbitCamera::Projection::Perspective);
    }
  });
  // These brush quantities are percentages too, even when the older chrome
  // omitted their unit. Keep them identical to layer/effect percent editors.
  for (const auto *name : {"brushSize",
                           "toolbarBrushSize",
                           "brushFlow",
                           "toolbarBrushFlow",
                           "brushStrokeOpacity",
                           "toolbarStrokeOpacity",
                           "brushSpacing",
                           "toolbarBrushSpacing",
                           "brushMinimumSize",
                           "brushMinimumFlow",
                           "brushSizeJitter",
                           "brushFlowJitter",
                           "brushPositionJitter",
                           "lazyMouseDistance"})
    if (auto *number = findChild<QDoubleSpinBox *>(name)) {
      number->setDecimals(2);
      number->setSuffix(" %");
      if (number->maximumWidth() < 80)
        number->setFixedWidth(80);
    }
