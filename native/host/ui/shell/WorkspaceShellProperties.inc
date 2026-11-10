// SPDX-License-Identifier: GPL-3.0-or-later
// The Properties tab and its layer, mask and effect panels. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  auto *properties = properties_panel_ = new QWidget(properties_slot_);
  properties_slot_layout->addWidget(properties);
  properties->setObjectName("properties");
  properties->setMinimumHeight(properties_minimum_height);
  auto *properties_layout = column(properties);
  QToolButton *properties_close = nullptr;
  auto *properties_header = header(properties, "Properties", &properties_close);
  properties_layout->addWidget(properties_header);
  properties_close->setObjectName("propertiesClose");
  properties_close->setToolTip("Close properties");
  properties_close->setAccessibleName("Close properties");
  properties_undock_ = new QToolButton(properties_header);
  properties_undock_->setObjectName("propertiesUndock");
  properties_undock_->setText(QString::fromUtf8("⧉"));
  properties_undock_->setToolTip("Undock properties");
  properties_undock_->setAccessibleName("Undock properties");
  properties_undock_->setFixedSize(22, 22);
  static_cast<QHBoxLayout *>(properties_header->layout())
      ->insertWidget(
          static_cast<QHBoxLayout *>(properties_header->layout())->indexOf(properties_close),
          properties_undock_);
  connect(properties_undock_, &QToolButton::clicked, this, [this] {
    if (properties_float_)
      dock_properties();
    else
      float_properties();
  });
  connect(properties_close, &QToolButton::clicked, this, [this] {
    if (properties_float_)
      dock_properties();
    hide_properties_tab();
    const QSignalBlocker block(properties_action_);
    properties_action_->setChecked(false);
  });
  connect(properties_action_, &QAction::toggled, this, [this](bool visible) {
    if (visible) {
      if (!properties_float_)
        show_properties_tab();
    }
    else {
      if (properties_float_)
        dock_properties();
      hide_properties_tab();
    }
  });
  properties->findChild<QLabel *>("dockTitle")->setObjectName("propertiesDockTitle");
  auto *properties_scroll = new QScrollArea(properties);
  properties_scroll->setObjectName("propertiesScroll");
  properties_scroll->setWidgetResizable(true);
  properties_scroll->setFrameShape(QFrame::NoFrame);
  // Folding a section can take the column past the viewport. A bar that claims
  // its own width would reflow every row in here on the way, so it floats.
  bts::overlay_scrollbar(properties_scroll);
  auto *material = new QWidget(properties_scroll);
  material->setObjectName("materialProperties");
  auto *material_layout = column(material);
  material_body_ = material;
  material_layout_ = material_layout;
  material_layout->setAlignment(Qt::AlignTop);
  material_layout->setContentsMargins(8, 8, 8, 8);
  material_layout->setSpacing(6);
  auto *material_label = new QLabel("MATERIAL", material);
  material_label->setObjectName("sectionTitle");
  material_label_ = material_label;
  material_layout->addWidget(material_label);
  // The panel used to be one column of panels with no stated grouping, so it
  // read as an undifferentiated list. The controls are unchanged; they now sit
  // in named sections that hide themselves when empty. docs/design/ui.md §7.3.
  material_layout->setSpacing(bts::theme::SpaceSection);
  content_section_ = make_section("Content", material);
  blending_section_ = make_section("Blending", material, true);
  projection_section_ = make_section("Projection", material, true);
  material_layout->addWidget(content_section_);
  material_layout->addWidget(blending_section_);
  material_layout->addWidget(projection_section_);
  levels_panel_ = new LevelsPanel(material);
  levels_panel_->edit_started = [this] {
    if (callbacks_.property_edit_started)
      callbacks_.property_edit_started("Levels");
  };
  levels_panel_->edit_finished = [this] {
    if (callbacks_.property_edit_finished)
      callbacks_.property_edit_finished();
  };
  levels_panel_->histogram_requested = [this](const TextureSet &set,
                                              const HistogramRequest &request) {
    return callbacks_.histogram_requested ? callbacks_.histogram_requested(set, request) : 0;
  };
  levels_panel_->histogram_result = [this](const QUuid &id) {
    return callbacks_.histogram_result ? callbacks_.histogram_result(id) : std::nullopt;
  };
  levels_panel_->hide();
  content_section_->add(levels_panel_);
  levels_panel_->changed = [this](const LevelsEffect &effect) {
    if (callbacks_.effect_changed)
      callbacks_.effect_changed(effect);
  };
  levels_panel_->component_selected = [this](QUuid id, int component) {
    if (callbacks_.effect_component_selected)
      callbacks_.effect_component_selected(id, component);
  };
  auto *projection_modes = new QWidget(material);
  projection_modes->setObjectName("propertiesProjectionModes");
  auto *projection_modes_layout = row(projection_modes, 0);
  for (int mode : {0, 1}) {
    auto *button = new QToolButton(projection_modes);
    button->setObjectName(mode == 0 ? "propertiesUVProjection" : "propertiesTriplanarProjection");
    button->setText(mode == 0 ? "UV" : "Triplanar");
    button->setCheckable(true);
    projection_modes_layout->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, mode] {
      auto projection = displayed_fill_;
      projection.mode = mode;
      edit_fill_projection(projection);
    });
  }
  projection_modes_layout->addStretch();
  projection_section_->add(projection_modes);
  projection_modes->hide();
  fill_panel_ = new FillPanel(material);
  mask_fill_panel_ = new MaskFillPanel(material);
  mask_fill_panel_->hide();
  content_section_->add(mask_fill_panel_);
  mask_fill_panel_->changed = [this](const MaskEffect &effect) {
    if (callbacks_.mask_effect_changed)
      callbacks_.mask_effect_changed(effect);
  };
  mask_fill_panel_->anchor_requested = [this](QUuid id) {
    if (callbacks_.anchor_requested)
      callbacks_.anchor_requested(id);
  };
  mask_fill_panel_->edit_started = [this] {
    if (callbacks_.property_edit_started)
      callbacks_.property_edit_started("Mask Fill");
  };
  mask_fill_panel_->edit_finished = [this] {
    if (callbacks_.property_edit_finished)
      callbacks_.property_edit_finished();
  };
  mask_fill_panel_->texture_cancelled = [this](int channel) {
    if (callbacks_.fill_texture_cancelled)
      callbacks_.fill_texture_cancelled(channel);
  };
  mask_fill_panel_->color_space_changed = [this](const QString &space) {
    if (callbacks_.fill_color_space_changed)
      callbacks_.fill_color_space_changed(space);
  };
  imported_effect_panel_ = new ImportedEffectPanel(material);
  imported_effect_panel_->hide();
  content_section_->add(imported_effect_panel_);
  imported_effect_panel_->content_changed = [this](const LevelsEffect &effect) {
    if (callbacks_.effect_changed)
      callbacks_.effect_changed(effect);
  };
  imported_effect_panel_->mask_changed = [this](const MaskEffect &effect) {
    if (callbacks_.mask_effect_changed)
      callbacks_.mask_effect_changed(effect);
  };
  imported_effect_panel_->child_selected = [this](const QUuid &id) {
    if (layers_->currentItem() && callbacks_.effect_selected)
      callbacks_.effect_selected(QUuid(layers_->currentItem()->data(Qt::UserRole).toString()), id);
  };
  mask_generator_panel_ = new MaskGeneratorPanel(material);
  mask_generator_panel_->hide();
  content_section_->add(mask_generator_panel_);
  mask_generator_panel_->changed = [this](const MaskEffect &effect) {
    if (callbacks_.mask_effect_changed)
      callbacks_.mask_effect_changed(effect);
  };
  mask_generator_panel_->content_changed = [this](const MaskEffect &value, int channel) {
    LevelsEffect effect;
    effect.kind = ContentEffectKind::Generator;
    static_cast<MeshGeneratorSettings &>(effect) = value;
    effect.id = value.id;
    effect.enabled = value.enabled;
    effect.name = value.name;
    effect.channel = channel;
    effect.opacity = value.opacity;
    effect.blending = value.blending;
    if (callbacks_.effect_changed)
      callbacks_.effect_changed(effect);
  };
  mask_generator_panel_->input_requested = [this](int role, int tile) {
    if (callbacks_.generator_input_requested)
      callbacks_.generator_input_requested(role, tile);
  };
  mask_generator_panel_->anchor_requested = [this](const QUuid &id) {
    if (callbacks_.anchor_requested)
      callbacks_.anchor_requested(id);
  };
  mask_generator_panel_->bake_requested = [this] {
    if (callbacks_.bake_mesh_maps)
      callbacks_.bake_mesh_maps();
  };
  compare_mask_panel_ = new CompareMaskPanel(material);
  compare_mask_panel_->histogram_requested = [this](const TextureSet &set,
                                                    const HistogramRequest &request) {
    return callbacks_.histogram_requested ? callbacks_.histogram_requested(set, request) : 0;
  };
  compare_mask_panel_->histogram_result = [this](const QUuid &id) {
    return callbacks_.histogram_result ? callbacks_.histogram_result(id) : std::nullopt;
  };
  compare_mask_panel_->hide();
  content_section_->add(compare_mask_panel_);
  compare_mask_panel_->changed = [this](const MaskEffect &effect) {
    if (callbacks_.mask_effect_changed)
      callbacks_.mask_effect_changed(effect);
  };
  color_selection_panel_ = new ColorSelectionPanel(material);
  color_selection_panel_->hide();
  content_section_->add(color_selection_panel_);
  color_selection_panel_->changed = [this](const MaskEffect &effect) {
    if (callbacks_.mask_effect_changed)
      callbacks_.mask_effect_changed(effect);
  };
  color_selection_panel_->pick_changed = [this](bool active) {
    if (callbacks_.id_pick_changed)
      callbacks_.id_pick_changed(active);
  };
  paint_effect_mix_ = new QWidget(material);
  paint_effect_mix_->setObjectName("paintEffectMix");
  auto *paint_mix_layout = row(paint_effect_mix_, 0);
  paint_effect_blend_ = new QComboBox(paint_effect_mix_);
  paint_effect_blend_->setObjectName("paintEffectBlend");
  for (int i :
       {0, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24})
    paint_effect_blend_->addItem(layer_blend_names[i], i);
  paint_effect_blend_->setMinimumWidth(0);
  paint_effect_blend_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  paint_effect_opacity_ = new bts::NumericSpinBox(paint_effect_mix_);
  paint_effect_opacity_->setObjectName("paintEffectOpacity");
  paint_effect_opacity_->setRange(0, 100);
  paint_effect_opacity_->setSuffix(" %");
  paint_effect_opacity_->setKeyboardTracking(false);
  paint_mix_layout->addWidget(paint_effect_blend_, 1);
  paint_mix_layout->addWidget(paint_effect_opacity_);
  blending_section_->add(paint_effect_mix_);
  paint_effect_mix_->hide();
  const auto paint_mix = [this] {
    if (callbacks_.paint_effect_mix_changed)
      callbacks_.paint_effect_mix_changed(paint_effect_opacity_->value() / 100,
                                          LayerBlend(paint_effect_blend_->currentData().toInt()));
  };
  connect(paint_effect_opacity_, &QDoubleSpinBox::valueChanged, this, [paint_mix](double) {
    paint_mix();
  });
  connect(paint_effect_blend_, &QComboBox::currentIndexChanged, this, [paint_mix](int i) {
    if (i >= 0)
      paint_mix();
  });
  filter_panel_ = new FilterPanel(material);
  filter_panel_->edit_started = [this] {
    if (callbacks_.property_edit_started)
      callbacks_.property_edit_started("Filter");
  };
  filter_panel_->edit_finished = [this] {
    if (callbacks_.property_edit_finished)
      callbacks_.property_edit_finished();
  };
  filter_panel_->hide();
  content_section_->add(filter_panel_);
  filter_panel_->changed =
      [this](const FilterSettings &filter, int channel, float opacity, LayerBlend blending) {
        if (callbacks_.filter_changed)
          callbacks_.filter_changed(filter, channel, opacity, blending);
      };
  anchor_panel_ = new AnchorPanel(material);
  anchor_panel_->hide();
  content_section_->add(anchor_panel_);
  anchor_panel_->renamed = [this](QUuid id, const QString &name) {
    if (callbacks_.anchor_renamed)
      callbacks_.anchor_renamed(id, name);
  };
  anchor_panel_->consumer_requested = [this](QUuid parent, QUuid id) {
    if (callbacks_.effect_selected)
      callbacks_.effect_selected(parent, id);
  };
  auto *linear_properties = new QLabel(material);
  linear_properties->setObjectName("linearColorProperties");
  linear_properties->setWordWrap(true);
  linear_properties->hide();
  content_section_->add(linear_properties);
  auto *folder_properties = new FolderPropertiesPanel(material);
  folder_properties->hide();
  content_section_->add(folder_properties);
  folder_properties->visibility_changed = [this](QUuid id, bool value) {
    if (callbacks_.layer_visibility_changed)
      callbacks_.layer_visibility_changed(id, value);
  };
  folder_properties->opacity_changed = [this](QUuid id, int channel, double value) {
    if (callbacks_.layer_opacity_changed)
      callbacks_.layer_opacity_changed(id, channel, value);
  };
  folder_properties->blend_changed = [this](QUuid id, int channel, LayerBlend value) {
    if (callbacks_.layer_blend_changed)
      callbacks_.layer_blend_changed(id, channel, value);
  };
  instance_panel_ = new LayerInstancePanel(material);
  instance_panel_->hide();
  content_section_->add(instance_panel_);
  instance_panel_->activated = [this](QUuid set, QUuid layer) {
    if (callbacks_.instance_navigate)
      callbacks_.instance_navigate(set, layer);
  };
  instance_empty_ = new QLabel(
      "Material values are inherited from the source layer. Open the instance hierarchy to edit its source.",
      material);
  instance_empty_->setObjectName("instanceNoProperties");
  instance_empty_->setWordWrap(true);
  instance_empty_->hide();
  content_section_->add(instance_empty_);
  geometry_panel_ = new GeometryMaskPanel(material);
  geometry_panel_->hide();
  content_section_->add(geometry_panel_);
  geometry_panel_->mesh_changed = [this](QUuid id, const QString &name, bool include) {
    if (callbacks_.geometry_mesh_changed)
      callbacks_.geometry_mesh_changed(id, name, include);
  };
  geometry_panel_->type_changed = [this](QUuid id, int type) {
    if (callbacks_.geometry_type_changed)
      callbacks_.geometry_type_changed(id, type);
  };
  geometry_panel_->tile_changed = [this](QUuid id, int tile, bool include) {
    if (callbacks_.geometry_tile_changed)
      callbacks_.geometry_tile_changed(id, tile, include);
  };
  geometry_panel_->all_changed = [this](QUuid id, bool include) {
    if (callbacks_.geometry_all_changed)
      callbacks_.geometry_all_changed(id, include);
  };
  property_content_ = new QWidget(material);
  material_layout->addWidget(property_content_);
  // Without this, folding a section hands its height to the rows above it and
  // every gap in the panel grows. The slack belongs at the bottom.
  material_layout->addStretch();
  material_layout = column(property_content_);
  material_layout->setContentsMargins(0, 0, 0, 0);
  fill_panel_->color_space_changed = [this](const QString &space) {
    if (callbacks_.fill_color_space_changed)
      callbacks_.fill_color_space_changed(space);
  };
  fill_panel_->warp_panel()->selection_changed = [this](std::vector<int> ids) {
    if (callbacks_.warp_selection_changed)
      callbacks_.warp_selection_changed(std::move(ids));
  };
  fill_panel_->warp_panel()->split_requested = [this](int mode) {
    if (callbacks_.warp_split_requested)
      callbacks_.warp_split_requested(mode);
  };
  fill_panel_->warp_panel()->space_changed = [this](bool local) {
    if (callbacks_.warp_space_changed)
      callbacks_.warp_space_changed(local);
  };
  fill_panel_->hide();
  material_layout->addWidget(fill_panel_);
  fill_panel_->edit_started = [this] {
    if (callbacks_.property_edit_started)
      callbacks_.property_edit_started("Fill mapping");
  };
  fill_panel_->edit_finished = [this] {
    if (callbacks_.property_edit_finished)
      callbacks_.property_edit_finished();
  };
  fill_panel_->anchor_requested = [this](QUuid id) {
    if (callbacks_.anchor_requested)
      callbacks_.anchor_requested(id);
  };
  fill_panel_->changed = [this](const FillProjection &p) {
    if (callbacks_.fill_projection_changed)
      callbacks_.fill_projection_changed(p);
  };
  fill_panel_->texture_requested = [this](bool file, int) {
    if (file) {
      if (callbacks_.fill_texture_file_requested)
        callbacks_.fill_texture_file_requested();
    }
    else {
      set_shelf_visible(true);
      asset_scope_ = -1;
      shelf_categories_->setCurrentRow(0);
      asset_search_->setText("u:texture");
      asset_search_->setFocus();
      refresh_alpha_shelf();
    }
  };
  mask_fill_panel_->texture_requested = fill_panel_->texture_requested;
  mask_fill_panel_->warp_panel()->selection_changed = fill_panel_->warp_panel()->selection_changed;
  mask_fill_panel_->warp_panel()->split_requested = fill_panel_->warp_panel()->split_requested;
  mask_fill_panel_->warp_panel()->space_changed = fill_panel_->warp_panel()->space_changed;
  mask_fill_panel_->files_dropped = [this](int channel, const QStringList &paths) {
    if (callbacks_.fill_texture_files_dropped)
      callbacks_.fill_texture_files_dropped(channel, paths);
  };
  mask_fill_panel_->asset_dropped = [this](int channel, const QString &id) {
    if (callbacks_.fill_texture_asset_dropped)
      callbacks_.fill_texture_asset_dropped(channel, id);
  };
  fill_panel_->material_requested = [this] {
    setProperty("fillMaterialPick", true);
    set_shelf_visible(true);
    asset_scope_ = -1;
    if (shelf_categories_)
      shelf_categories_->setCurrentRow(0);
    asset_search_->setText("u:material");
    asset_search_->setFocus();
    refresh_alpha_shelf();
  };
  mask_value_row_ = new QWidget(material);
  auto *gray_layout = row(mask_value_row_);
  gray_layout->addWidget(new QLabel("Grayscale", mask_value_row_));
  mask_value_ = new bts::NumericSpinBox(mask_value_row_);
  mask_value_->setObjectName("maskGrayscale");
  mask_value_->setRange(0, 1);
  mask_value_->setDecimals(3);
  mask_value_->setSingleStep(.05);
  mask_value_->setKeyboardTracking(false);
  gray_layout->addWidget(mask_value_);
  auto *invert_gray = new QToolButton(mask_value_row_);
  invert_gray->setText("Invert");
  gray_layout->addWidget(invert_gray);
  connect(invert_gray, &QToolButton::clicked, this, [this] {
    mask_value_->setValue(1 - mask_value_->value());
  });
  connect(mask_value_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (callbacks_.mask_value_changed)
      callbacks_.mask_value_changed(value);
  });
  material_layout->addWidget(mask_value_row_);
  mask_value_row_->hide();
  polygon_controls_ = make_polygon_controls(1, material);
  material_layout->addWidget(polygon_controls_);
