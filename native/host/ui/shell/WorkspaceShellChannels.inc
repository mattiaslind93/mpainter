// SPDX-License-Identifier: GPL-3.0-or-later
// Texture Set settings: channels, displacement, normal, AO, bump, padding, mesh maps. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  auto *settings_scroll = new QScrollArea(layer_settings_tabs_);
  settings_scroll->setObjectName("textureSetSettingsScroll");
  settings_scroll->setWidgetResizable(true);
  settings_scroll->setFrameShape(QFrame::NoFrame);
  auto *settings = new QWidget(settings_scroll);
  settings_general_ = settings;
  auto *scope_root = new QWidget(settings_scroll);
  auto *scope_layout = column(scope_root);
  scope_layout->setContentsMargins(0, 0, 0, 0);
  settings_tile_ = new QWidget(scope_root);
  auto *tile_layout = column(settings_tile_);
  tile_layout->setContentsMargins(10, 10, 10, 10);
  tile_layout->addWidget(new QLabel("Size", settings_tile_));
  tile_resolution_ = new TextureExtentComboBox(128, settings_tile_);
  tile_resolution_->setObjectName("uvTileResolution");
  tile_resolution_->insertItem(0, "1024 (Default)", 0);
  tile_resolution_->setCurrentIndex(0);
  tile_resolution_->insertSeparator(1);
  tile_layout->addWidget(tile_resolution_);
  scope_layout->addWidget(settings_tile_);
  scope_layout->addWidget(settings);
  scope_layout->addStretch();
  settings_tile_->hide();
  settings_scroll->setWidget(scope_root);
  connect(tile_resolution_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (active_tile_ && index >= 0 && tile_resolution_->itemData(index).isValid() &&
        callbacks_.tile_resolution_changed)
      callbacks_.tile_resolution_changed(active_tile_, tile_resolution_->itemData(index).toInt());
  });
  auto *settings_layout = column(settings);
  settings_layout->setContentsMargins(10, 10, 10, 10);
  settings_layout->setSpacing(8);
  auto *resolution_label = new QLabel("Size", settings);
  resolution_label->setEnabled(true);
  settings_layout->addWidget(resolution_label);
  auto *resolution = new TextureExtentComboBox(128, settings);
  resolution_ = resolution;
  resolution->setObjectName("textureSetResolution");
  resolution->setEnabled(false);
  resolution->setToolTip(
      "Evaluate this Texture Set at another size while retaining authored paint.");
  connect(resolution, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index >= 0 && resolution_->isEnabled() && callbacks_.texture_resolution_changed)
      callbacks_.texture_resolution_changed(resolution_->itemData(index).toInt());
  });
  auto *dimensions = new QHBoxLayout;
  resolution_lock_ = new QToolButton(settings);
  resolution_lock_->setObjectName("textureSetSizeLock");
  resolution_lock_->setCheckable(true);
  resolution_lock_->setChecked(true);
  resolution_lock_->setText("Link");
  resolution_lock_->setToolTip("Link width and height; linking sets height to width");
  resolution_height_ = new TextureExtentComboBox(128, settings);
  resolution_height_->setObjectName("textureSetHeight");
  resolution_height_->setEnabled(false);
  dimensions->addWidget(resolution, 1);
  dimensions->addWidget(resolution_lock_);
  dimensions->addWidget(resolution_height_, 1);
  settings_layout->addLayout(dimensions);
  connect(resolution_lock_, &QToolButton::toggled, this, [this](bool linked) {
    if (callbacks_.texture_size_linked)
      callbacks_.texture_size_linked(linked);
  });
  connect(resolution_height_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index >= 0 && resolution_height_->isEnabled() && callbacks_.texture_height_changed)
      callbacks_.texture_height_changed(resolution_height_->itemData(index).toInt());
  });
  auto *channels_header = new QWidget(settings);
  auto *channels_header_layout = row(channels_header);
  channels_header_layout->addWidget(new QLabel("CHANNELS", channels_header));
  channels_header_layout->addStretch();
  auto *configure_channels = new QAction("Add or remove channels…", channels_header);
  configure_channels->setObjectName("configureChannels");
  auto *configure_button = new QToolButton(channels_header);
  configure_button->setObjectName("configureChannelsButton");
  configure_button->setDefaultAction(configure_channels);
  channels_header_layout->addWidget(configure_button);
  auto *add_channel = new QToolButton(channels_header);
  add_channel->setText("+");
  add_channel->setObjectName("addChannel");
  add_channel->setToolTip("Add a supported material channel");
  add_channel->setPopupMode(QToolButton::InstantPopup);
  auto *channel_menu = new QMenu(add_channel);
  add_channel->setMenu(channel_menu);
  channels_header_layout->addWidget(add_channel);
  settings_layout->addWidget(channels_header);
  auto *channel_scroll = new QScrollArea(settings);
  channel_scroll->setWidgetResizable(true);
  channel_scroll->setFrameShape(QFrame::NoFrame);
  auto *channel_container = new QWidget(channel_scroll);
  auto *channel_layout = column(channel_container);
  for (int index = 0; index < 5; ++index) {
    channel_add_actions_[index] = channel_menu->addAction(channel_names[index]);
    channel_add_actions_[index]->setObjectName(QString("addChannel%1").arg(index));
    channel_add_actions_[index]->setEnabled(false);
    connect(channel_add_actions_[index], &QAction::triggered, this, [this, index] {
      if (callbacks_.channel_changed)
        callbacks_.channel_changed(index, true);
    });
    auto *channel_row = new QWidget(channel_container);
    channel_rows_[index] = channel_row;
    channel_row->setFixedHeight(30);
    auto *line = row(channel_row);
    line->addWidget(new QLabel(channel_names[index], channel_row), 1);
    auto *format = new QComboBox(channel_row);
    channel_formats_[index] = format;
    format->setObjectName(QString("channelPrecision%1").arg(index));
    for (auto name : channel_precision_names)
      format->addItem(name);
    format->setFixedWidth(74);
    format->setEnabled(true);
    format->setToolTip(
        "Channel evaluation precision; original authoring values are retained. L formats evaluate luminance.");
    connect(format, &QComboBox::currentIndexChanged, this, [this, index](int format) {
      if (callbacks_.channel_precision_changed)
        callbacks_.channel_precision_changed(index, format);
    });
    line->addWidget(format);
    auto *remove = new QToolButton(channel_row);
    remove->setText("−");
    remove->setObjectName(QString("removeChannel%1").arg(index));
    remove->setToolTip(QString("Remove %1; retain its authored data").arg(channel_names[index]));
    connect(remove, &QToolButton::clicked, this, [this, index] {
      if (callbacks_.channel_changed)
        callbacks_.channel_changed(index, false);
    });
    line->addWidget(remove);
    channel_layout->addWidget(channel_row);
    channel_row->hide();
  }
  ao_add_action_ = channel_menu->addAction("Ambient occlusion");
  ao_add_action_->setObjectName("addChannel5");
  connect(ao_add_action_, &QAction::triggered, this, [this] {
    if (callbacks_.channel_changed)
      callbacks_.channel_changed(5, true);
  });
  ao_channel_row_ = new QWidget(channel_container);
  ao_channel_row_->setFixedHeight(30);
  auto *ao_line = row(ao_channel_row_);
  ao_line->addWidget(new QLabel("Ambient occlusion", ao_channel_row_), 1);
  auto *ao_format = new QComboBox(ao_channel_row_);
  channel_formats_[5] = ao_format;
  ao_format->setObjectName("channelPrecision5");
  for (auto name : channel_precision_names)
    ao_format->addItem(name);
  connect(ao_format, &QComboBox::currentIndexChanged, this, [this](int format) {
    if (callbacks_.channel_precision_changed)
      callbacks_.channel_precision_changed(5, format);
  });
  ao_format->setFixedWidth(74);
  ao_line->addWidget(ao_format);
  auto *remove_ao = new QToolButton(ao_channel_row_);
  remove_ao->setText("−");
  remove_ao->setObjectName("removeChannel5");
  ao_line->addWidget(remove_ao);
  connect(remove_ao, &QToolButton::clicked, this, [this] {
    if (callbacks_.channel_changed)
      callbacks_.channel_changed(5, false);
  });
  channel_layout->addWidget(ao_channel_row_);
  ao_channel_row_->hide();
  scattering_add_ = channel_menu->addAction("Scattering");
  scattering_add_->setObjectName("addScatteringChannel");
  scattering_add_->setToolTip(
      "Intensity mask for Principled subsurface scattering; activate Subsurface Scattering in Display Settings");
  connect(scattering_add_, &QAction::triggered, this, [this] {
    if (callbacks_.scattering_added)
      callbacks_.scattering_added();
    QTimer::singleShot(0, this, [this] {
      for (int slot = 0; slot < custom_channel_count; ++slot)
        if (user_names_[slot]->isReadOnly() && user_names_[slot]->text() == "Scattering") {
          findChild<QScrollArea *>("textureChannelsScroll")->ensureWidgetVisible(user_rows_[slot]);
          findChild<QScrollArea *>("textureSetSettingsScroll")
              ->ensureWidgetVisible(user_rows_[slot]);
        }
    });
  });
  for (int slot = 0; slot < custom_channel_count; ++slot) {
    user_add_[slot] = channel_menu->addAction(QString("User%1").arg(slot));
    user_add_[slot]->setObjectName(QString("addUserChannel%1").arg(slot));
    connect(user_add_[slot], &QAction::triggered, this, [this, slot] {
      if (callbacks_.user_channel_changed)
        callbacks_.user_channel_changed(slot, true);
    });
    auto *line = new QWidget(channel_container);
    user_rows_[slot] = line;
    line->setFixedHeight(30);
    auto *layout = row(line);
    user_names_[slot] = new QLineEdit(line);
    user_names_[slot]->setObjectName(QString("userChannelName%1").arg(slot));
    user_names_[slot]->setMaxLength(std::numeric_limits<int>::max());
    layout->addWidget(user_names_[slot], 1);
    connect(user_names_[slot], &QLineEdit::editingFinished, this, [this, slot] {
      if (!user_names_[slot]->isReadOnly() && callbacks_.user_renamed)
        callbacks_.user_renamed(slot, user_names_[slot]->text());
    });
    user_formats_[slot] = new QComboBox(line);
    user_formats_[slot]->setObjectName(QString("userChannelPrecision%1").arg(slot));
    for (auto name : channel_precision_names)
      user_formats_[slot]->addItem(name);
    user_formats_[slot]->setFixedWidth(74);
    layout->addWidget(user_formats_[slot]);
    connect(user_formats_[slot], &QComboBox::currentIndexChanged, this, [this, slot](int value) {
      if (callbacks_.user_precision_changed)
        callbacks_.user_precision_changed(slot, value);
    });
    auto *remove = new QToolButton(line);
    remove->setText("−");
    remove->setObjectName(QString("removeUserChannel%1").arg(slot));
    layout->addWidget(remove);
    connect(remove, &QToolButton::clicked, this, [this, slot] {
      if (callbacks_.user_channel_changed)
        callbacks_.user_channel_changed(slot, false);
    });
    channel_layout->addWidget(line);
    line->hide();
  }
  channel_layout->addStretch();
  channel_scroll->setWidget(channel_container);
  channel_scroll->setObjectName("textureChannelsScroll");
  channel_scroll->setFixedHeight(170);
  settings_layout->addWidget(channel_scroll);
  displacement_panel_ = new DisplacementPanel(settings);
  settings_layout->addWidget(displacement_panel_);
  displacement_panel_->channel_changed = [this](bool enabled) {
    if (callbacks_.displacement_channel_changed)
      callbacks_.displacement_channel_changed(enabled);
  };
  displacement_panel_->changed = [this](const auto &state) {
    if (callbacks_.displacement_changed)
      callbacks_.displacement_changed(state);
  };
  auto *normal_row = new QWidget(settings);
  normal_row->setObjectName("normalMixingRow");
  auto *normal_layout = row(normal_row);
  normal_layout->addWidget(new QLabel("Normal mixing", normal_row));
  normal_mixing_ = new QComboBox(normal_row);
  normal_mixing_->setObjectName("normalMixing");
  normal_mixing_->setToolTip(
      "How the tangent Normal map combines with the normal derived from grayscale Bump.");
  normal_mixing_->addItem("Replace", 1);
  normal_mixing_->addItem("Combine", 0);
  normal_layout->addWidget(normal_mixing_);
  settings_layout->addWidget(normal_row);
  normal_row->hide();
  connect(normal_mixing_, &QComboBox::currentIndexChanged, this, [this](int) {
    if (callbacks_.normal_mixing_changed)
      callbacks_.normal_mixing_changed(normal_mixing_->currentData().toInt());
  });
  auto *ao_mixing_row = new QWidget(settings);
  auto *ao_mixing_layout = row(ao_mixing_row);
  ao_mixing_layout->addWidget(new QLabel("Ambient occlusion mixing", ao_mixing_row));
  ao_mixing_ = new QComboBox(ao_mixing_row);
  ao_mixing_->setObjectName("ambientOcclusionMixing");
  ao_mixing_->addItem("Replace", 1);
  ao_mixing_->addItem("Multiply", 0);
  ao_mixing_layout->addWidget(ao_mixing_);
  settings_layout->addWidget(ao_mixing_row);
  connect(ao_mixing_, &QComboBox::currentIndexChanged, this, [this](int) {
    if (callbacks_.ao_mixing_changed)
      callbacks_.ao_mixing_changed(ao_mixing_->currentData().toInt());
  });
  auto *bump_method_row = new QWidget(settings);
  auto *bump_method_layout = row(bump_method_row);
  bump_method_layout->addWidget(new QLabel("Bump to normal method", bump_method_row));
  bump_method_ = new QComboBox(bump_method_row);
  bump_method_->setObjectName("heightNormalMethod");
  bump_method_->setToolTip(
      "Converts grayscale Bump into a normal for shading. Displacement is separate and moves the surface.");
  bump_method_->addItem("Smooth (Sobel)", 0);
  bump_method_->addItem("Sharp", 1);
  bump_method_layout->addWidget(bump_method_);
  settings_layout->addWidget(bump_method_row);
  connect(bump_method_, &QComboBox::currentIndexChanged, this, [this](int) {
    if (callbacks_.bump_method_changed)
      callbacks_.bump_method_changed(bump_method_->currentData().toInt());
  });
  auto *padding_row = new QWidget(settings);
  auto *padding_layout = row(padding_row);
  padding_layout->addWidget(new QLabel("UV padding", padding_row));
  padding_control_ = new QComboBox(padding_row);
  padding_control_->setObjectName("uvPaddingMode");
  padding_control_->addItems({"2D Space Neighbor", "3D Space Neighbor"});
  padding_control_->setCurrentIndex(1);
  padding_layout->addWidget(padding_control_);
  settings_layout->addWidget(padding_row);
  connect(padding_control_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (callbacks_.padding_changed)
      callbacks_.padding_changed(index == 1);
  });
  mesh_maps_panel_ = new MeshMapsPanel(settings);
  settings_layout->addWidget(mesh_maps_panel_, 1);
  mesh_maps_panel_->bake = [this] {
    if (callbacks_.bake_mesh_maps)
      callbacks_.bake_mesh_maps();
  };
  mesh_maps_panel_->remove = [this](int role) {
    if (callbacks_.remove_mesh_map)
      callbacks_.remove_mesh_map(role);
  };
  mesh_maps_panel_->inspect = [this](int role) {
    if (callbacks_.inspect_mesh_map)
      callbacks_.inspect_mesh_map(role);
  };
  auto *bake_action = new QAction("Bake Mesh Maps", this);
  bake_action->setObjectName("bakeMeshMapsAction");
  bake_action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
  addAction(bake_action);
  connect(bake_action, &QAction::triggered, this, [this] {
    if (callbacks_.bake_mesh_maps)
      callbacks_.bake_mesh_maps();
  });
  settings_layout->addStretch();
  texture_page->addWidget(settings_scroll);
  texture_page->setStretchFactor(0, 1);
  texture_page->setStretchFactor(1, 1);
  texture_page->setSizes({260, 240});

  // Properties is a tab beside Layers and Texture Sets, and the tab that opens
  // first, instead of a second panel competing for the same column.
  properties_slot_ = new QWidget(layer_settings_tabs_);
  properties_slot_->setObjectName("propertiesSlot");
  layer_settings_tabs_->addTab(properties_slot_, "Properties");
  layer_settings_tabs_->setCurrentWidget(properties_slot_);
  auto *properties_slot_layout = new QVBoxLayout(properties_slot_);
  properties_slot_layout->setContentsMargins(0, 0, 0, 0);
