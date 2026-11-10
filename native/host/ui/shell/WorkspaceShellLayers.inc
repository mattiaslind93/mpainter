// SPDX-License-Identifier: GPL-3.0-or-later
// The Layers tab: layer stack, layer actions, effects and smart-material menus. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  layer_settings_tabs_ = new QTabWidget(inspector_splitter_);
  inspector_splitter_->addWidget(layer_settings_tabs_);
  layer_settings_tabs_->setObjectName("layerSettingsTabs");
  layer_settings_tabs_->setMinimumHeight(145);
  layer_settings_tabs_->setDocumentMode(true);
  layer_settings_tabs_->tabBar()->setDrawBase(false);
  auto *layers = new QWidget(layer_settings_tabs_);
  auto *layers_layout = column(layers);
  auto *layer_actions = new QWidget(layers);
  layer_actions->setFixedHeight(31);
  auto *layer_actions_layout = row(layer_actions);
  auto *channel = new QComboBox(layer_actions);
  layer_context_ = channel;
  channel->setObjectName("layerChannelContext");
  channel->setAccessibleName("Layer channel context");
  channel->setToolTip("Channel used by layer thumbnails, opacity and blending");
  connect(channel, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0)
      return;
    layer_channel_ = layer_context_->itemData(index).toInt();
    if (callbacks_.layer_context_changed)
      callbacks_.layer_context_changed();
  });
  layer_actions_layout->addWidget(channel, 1);
  effect_up_ = new QAction("Move effect up", this);
  effect_up_->setObjectName("moveEffectUp");
  effect_up_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
  effect_down_ = new QAction("Move effect down", this);
  effect_down_->setObjectName("moveEffectDown");
  effect_down_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
  addAction(effect_up_);
  addAction(effect_down_);
  connect(effect_up_, &QAction::triggered, this, [this] {
    if (callbacks_.move_effect)
      callbacks_.move_effect(1);
  });
  connect(effect_down_, &QAction::triggered, this, [this] {
    if (callbacks_.move_effect)
      callbacks_.move_effect(-1);
  });
  effects_button_ = tool(layer_actions, Glyph::Filter, "Add effect", true);
  effects_button_->setIcon(painter_workflow_icon(PainterGlyph::Wand));
  effects_button_->setObjectName("addLayerEffect");
  effects_button_->setPopupMode(QToolButton::InstantPopup);
  auto *effects_menu = new QMenu(effects_button_);
  effects_button_->setMenu(effects_menu);
  auto *paint_effect = effects_menu->addAction("Add Paint");
  paint_effect->setObjectName("addPaintEffect");
  connect(paint_effect, &QAction::triggered, this, [this] {
    if (callbacks_.add_paint_effect)
      callbacks_.add_paint_effect();
  });
  auto *generator = effects_menu->addAction("Add generator");
  generator->setObjectName("addMaskGenerator");
  connect(generator, &QAction::triggered, this, [this] {
    if (callbacks_.add_mask_generator)
      callbacks_.add_mask_generator();
  });
  auto *fill_effect = effects_menu->addAction("Add Fill");
  fill_effect->setObjectName("addMaskFillEffect");
  connect(fill_effect, &QAction::triggered, this, [this] {
    if (callbacks_.add_mask_fill)
      callbacks_.add_mask_fill();
  });
  auto *levels_action = effects_menu->addAction("Add Levels");
  levels_action->setObjectName("addLevelsEffect");
  connect(levels_action, &QAction::triggered, this, [this] {
    if (callbacks_.add_levels)
      callbacks_.add_levels(layer_channel_);
  });
  auto *filter_action = effects_menu->addAction("Add Filter");
  filter_action->setObjectName("addFilterEffect");
  connect(filter_action, &QAction::triggered, this, [this] {
    if (callbacks_.add_filter_effect)
      callbacks_.add_filter_effect(layer_channel_);
  });
  auto *anchor_action = effects_menu->addAction("Add anchor point");
  anchor_action->setObjectName("addMaskAnchor");
  connect(anchor_action, &QAction::triggered, this, [this] {
    if (callbacks_.add_mask_anchor)
      callbacks_.add_mask_anchor();
  });
  layer_actions_layout->addWidget(effects_button_);

  auto *cache_button = tool(layer_actions, Glyph::Shelf, "Add Pre-compose / Cache Layer");
  cache_button->setObjectName("addPrecomposeLayer");
  layer_actions_layout->addWidget(cache_button);
  connect(cache_button, &QToolButton::clicked, this, [this] {
    if (callbacks_.precompose_layer)
      callbacks_.precompose_layer(false);
  });
  smart_material_button_ = tool(layer_actions, Glyph::Material, "Add material preset", true);
  smart_material_button_->setObjectName("addSmartMaterial");
  smart_material_button_->setPopupMode(QToolButton::InstantPopup);
  auto *smart_menu = new QMenu(smart_material_button_);
  smart_material_button_->setMenu(smart_menu);
  for (const auto &[glyph, name] : {std::pair{Glyph::Mask, "Add mask"},
                                    {Glyph::Plus, "Add paint layer"},
                                    {Glyph::Fill, "Add fill layer"},
                                    {Glyph::Folder, "Add folder"},
                                    {Glyph::Trash, "Delete layer"}})
  {
    auto *button = tool(layer_actions, glyph, QString::fromLatin1(name));
    layer_actions_layout->addWidget(button);
    if (glyph == Glyph::Plus) {
      paint_button_ = button;
      button->setIcon(painter_workflow_icon(PainterGlyph::Paint));
      button->setObjectName("addPaintLayer");
      connect(button, &QToolButton::clicked, this, [this] {
        if (callbacks_.add_paint)
          callbacks_.add_paint();
      });
      button->setPopupMode(QToolButton::MenuButtonPopup);
      auto *menu = new QMenu(button);
      button->setMenu(menu);
      add_adjustment_action_ = menu->addAction("Add adjustment paint layer (Pass Through)");
      add_adjustment_action_->setObjectName("addAdjustmentPaintLayer");
      add_adjustment_action_->setEnabled(false);
      connect(add_adjustment_action_, &QAction::triggered, this, [this] {
        if (callbacks_.add_adjustment)
          callbacks_.add_adjustment();
      });
    }
    if (glyph == Glyph::Mask) {
      mask_button_ = button;
      button->setObjectName("addLayerMask");
      button->setPopupMode(QToolButton::InstantPopup);
      auto *menu = new QMenu("Mask", button);
      button->setMenu(menu);
      for (auto [label, background] : {std::pair{"Add white mask", 1.f}, {"Add black mask", 0.f}})
      {
        auto *action = menu->addAction(label);
        action->setObjectName(background > 0 ? "addWhiteMask" : "addBlackMask");
        connect(action, &QAction::triggered, this, [this, background] {
          if (callbacks_.add_mask)
            callbacks_.add_mask(background);
        });
      }
    }
    if (glyph == Glyph::Mask) {
      auto *selection = button->menu()->addAction("Add mask with color selection");
      selection->setObjectName("addColorSelectionMask");
      connect(selection, &QAction::triggered, this, [this] {
        if (callbacks_.add_color_selection)
          callbacks_.add_color_selection();
      });
      auto *action = button->menu()->addAction("Add bitmap mask…");
      action->setObjectName("addBitmapMask");
      connect(action, &QAction::triggered, this, [this] {
        if (callbacks_.add_bitmap_mask)
          callbacks_.add_bitmap_mask();
      });
      auto *compare = button->menu()->addAction("Add mask with height combination");
      compare->setObjectName("addCompareMask");
      connect(compare, &QAction::triggered, this, [this] {
        if (callbacks_.add_compare_mask)
          callbacks_.add_compare_mask();
      });
    }
    if (glyph == Glyph::Folder) {
      folder_button_ = button;
      button->setObjectName("addLayerFolder");
      connect(button, &QToolButton::clicked, this, [this] {
        if (callbacks_.add_folder)
          callbacks_.add_folder();
      });
    }
    if (glyph == Glyph::Trash)
      button->setObjectName("deleteLayer");
    if (glyph == Glyph::Fill) {
      fill_button_ = button;
      button->setObjectName("addFillLayer");
      connect(button, &QToolButton::clicked, this, [this] {
        if (callbacks_.add_fill)
          callbacks_.add_fill();
      });
    }
  }
  const auto selected_mask_layer = [this] {
    const auto *item = layers_ ? layers_->currentItem() : nullptr;
    return item ? QUuid(item->data(Qt::UserRole).toString()) : QUuid{};
  };
  auto *apply_smart_mask = mask_button_->menu()->addAction("Apply mask generator…");
  apply_smart_mask->setObjectName("applySmartMask");
  connect(apply_smart_mask, &QAction::triggered, this, [this] {
    set_shelf_visible(true);
    asset_scope_control_->setCurrentIndex(0);
    shelf_categories_->setCurrentRow(0);
    asset_search_->setText("u:smartmask");
    asset_search_->setFocus();
  });
  mask_button_->menu()->addSeparator();
  for (const auto &[label, name] : {std::pair{"Copy mask content", "copyMaskContent"},
                                    {"Paste into mask", "pasteIntoMask"},
                                    {"Create mask generator…", "createSmartMask"},
                                    {"Invert mask background", "invertMaskBackground"},
                                    {"Remove mask", "removeMask"}})
  {
    auto *action = mask_button_->menu()->addAction(QString::fromUtf8(label));
    action->setObjectName(name);
    connect(action, &QAction::triggered, this, [this, selected_mask_layer, name] {
      const auto id = selected_mask_layer();
      if (id.isNull())
        return;
      if (QStringView(u"copyMaskContent") == QLatin1StringView(name)) {
        if (callbacks_.copy_mask)
          callbacks_.copy_mask(id);
      }
      else if (QStringView(u"pasteIntoMask") == QLatin1StringView(name)) {
        if (callbacks_.paste_mask)
          callbacks_.paste_mask(id);
      }
      else if (QStringView(u"createSmartMask") == QLatin1StringView(name)) {
        if (callbacks_.create_smart_mask)
          callbacks_.create_smart_mask(id);
      }
      else if (QStringView(u"invertMaskBackground") == QLatin1StringView(name)) {
        if (callbacks_.mask_background_inverted)
          callbacks_.mask_background_inverted(id);
      }
      else if (callbacks_.remove_mask)
        callbacks_.remove_mask(id);
    });
  }
  layer_actions_layout->addWidget(smart_material_button_);
  auto *image_layer = new QToolButton(layer_actions);
  image_layer->setText("▧");
  image_layer->setObjectName("importImageAsFillLayer");
  image_layer->setToolTip("Load image as a new fill layer…");
  layer_actions_layout->addWidget(image_layer);
  connect(image_layer, &QToolButton::clicked, this, [this] {
    if (callbacks_.layer_texture_file_requested)
      callbacks_.layer_texture_file_requested();
  });
  layers_layout->addWidget(layer_actions);
  auto *material_count = new QLabel(layers);
  material_count->setObjectName("layerMaterialLibraryCount");
  material_count->hide();
  layers_layout->addWidget(material_count);
  auto *empty = new QFrame(layers);
  empty->setObjectName("emptyLayersView");
  auto *empty_layout = new QVBoxLayout(empty);
  auto *empty_label = new QLabel("No project – File → New (Ctrl+N) / File → Open", empty);
  empty_label->setObjectName("emptyLayersMessage");
  empty_label->setWordWrap(true);
  empty_layout->addWidget(empty_label);
  for (const auto &[text, name] :
       {std::pair{"New project…", "newProject"}, std::pair{"Open project…", "openProject"}})
  {
    auto *button = new QPushButton(text, empty);
    empty_layout->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, name] {
      if (auto *action = findChild<QAction *>(name))
        action->trigger();
    });
  }
  layers_layout->addWidget(empty);
  auto *layer_list = new LayerStackList(layers);
  layer_list->layer_dropped = [this](QUuid id, int target_row) {
    if (callbacks_.layer_dropped)
      callbacks_.layer_dropped(id, target_row);
  };
  layer_list->delete_requested = [this] {
    if (!callbacks_.delete_selection)
      return;
    std::vector<std::pair<QUuid, QUuid>> effects;
    std::vector<QUuid> masks, layers_to_delete;
    for (auto *item : layers_->selectedItems()) {
      const QUuid layer(item->data(Qt::UserRole).toString());
      const QUuid effect(item->data(Qt::UserRole + 10).toString());
      if (!effect.isNull())
        effects.emplace_back(layer, effect);
      else if (item->data(Qt::UserRole + 8).toBool())
        masks.push_back(layer);
      else
        layers_to_delete.push_back(layer);
    }
    if (!effects.empty() || !masks.empty() || !layers_to_delete.empty())
      callbacks_.delete_selection(effects, masks, layers_to_delete);
  };
  if (auto *button = findChild<QToolButton *>("deleteLayer"))
    connect(button, &QToolButton::clicked, layer_list, [layer_list] {
      if (layer_list->delete_requested)
        layer_list->delete_requested();
    });
  layers_ = layer_list;
  new ImageDropFilter(
      layer_list,
      [this](const QStringList &paths) {
        if (callbacks_.layer_texture_files_dropped)
          callbacks_.layer_texture_files_dropped(paths);
      },
      {},
      [this](QWidget *source, QPoint point) {
        if (!source)
          return;
        point = source->mapTo(layers_->viewport(), point);
        const auto index = layers_->indexAt(point);
        if (!index.isValid())
          return;
        const QUuid id(index.data(Qt::UserRole).toString()),
            effect(index.data(Qt::UserRole + 10).toString());
        if (!effect.isNull()) {
          if (callbacks_.effect_selected)
            callbacks_.effect_selected(id, effect);
        }
        else if (callbacks_.mask_selected)
          callbacks_.mask_selected(
              id,
              index.data(Qt::UserRole + 6).toBool() &&
                  layer_mask_rect(index, layers_->visualRect(index)).contains(point));
      });
  layer_list->setObjectName("layerStack");
  layer_list->hierarchy_dropped =
      [this](const std::vector<QUuid> &ids, QUuid target, bool into, bool above) {
        if (callbacks_.layer_hierarchy_dropped)
          callbacks_.layer_hierarchy_dropped(ids, target, into, above);
      };
  layer_list->asset_accepted = [this](const QString &id) {
    if (id.startsWith("baked:"))
      return asset_project_ready_;
    if (!asset_catalog_)
      return false;
    for (const auto &asset : asset_catalog_->assets())
      if (asset.id == QUuid(id))
        return smart_material_target_ready(asset, asset_project_ready_, asset_smart_ready_) ||
               ((asset.material || asset.smart_mask || is_spmsk_asset(asset)) &&
                asset_project_ready_) ||
               (asset.usages.contains("texture") && asset_project_ready_);
    return false;
  };
  layer_list->asset_dropped = [this](
                                  const QString &id, int gap, bool replace, int row, bool mask) {
    if (!callbacks_.asset_dropped)
      return;
    const auto deliver = [&](const StudioAsset &asset) {
      int index = std::min(gap, layers_->count() - 1);
      if (row >= 0 &&
          (asset.smart_mask || is_spmsk_asset(asset) ||
           (asset.usages.contains("texture") &&
            (mask || !layers_->item(row)->data(Qt::UserRole + 10).toString().isEmpty()))))
        index = row;
      else {
        while (index >= 0 && !layers_->item(index)->data(Qt::UserRole + 10).toString().isEmpty())
          --index;
      }
      const auto *target = index >= 0 ? layers_->item(index) : nullptr;
      const QUuid target_id = target ? QUuid(target->data(Qt::UserRole).toString()) : QUuid{};
      if (target && asset.usages.contains("texture")) {
        const QUuid layer(target->data(Qt::UserRole).toString()),
            effect(target->data(Qt::UserRole + 10).toString());
        if (!effect.isNull()) {
          if (callbacks_.effect_selected)
            callbacks_.effect_selected(layer, effect);
        }
        else if (callbacks_.mask_selected)
          callbacks_.mask_selected(layer, mask);
      }
      callbacks_.asset_dropped(asset, target_id, gap < layers_->count(), replace);
    };
    if (id.startsWith("baked:")) {
      if (callbacks_.baked_asset)
        deliver(callbacks_.baked_asset(id));
      return;
    }
    if (!asset_catalog_)
      return;
    for (const auto &asset : asset_catalog_->assets())
      if (asset.id == QUuid(id)) {
        deliver(asset);
        break;
      }
  };

  layer_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  connect(layer_list, &QListWidget::itemSelectionChanged, this, [this] {
    if (selection_refresh_pending_)
      return;
    selection_refresh_pending_ = true;
    QTimer::singleShot(0, this, [this] {
      selection_refresh_pending_ = false;
      if (callbacks_.layer_selection_changed)
        callbacks_.layer_selection_changed();
    });
  });
  layers_layout->addWidget(layer_list, 1);
  new LayerEffectRowState(this);
  new NameListShortcuts(layer_list);
  auto *layer_delegate = new PainterLayerDelegate(layer_list);
  layer_list->setItemDelegate(layer_delegate);
  layer_list->setEditTriggers(QAbstractItemView::DoubleClicked |
                              QAbstractItemView::EditKeyPressed);
  layer_delegate->renamed = [this](const QModelIndex &index, const QString &name) {
    const QUuid layer(index.data(Qt::UserRole).toString()),
        effect(index.data(Qt::UserRole + 10).toString());
    QTimer::singleShot(0, this, [this, layer, effect, name] {
      if (callbacks_.layer_item_renamed)
        callbacks_.layer_item_renamed(layer, effect, name);
    });
  };
  const auto mix_writer = [this](const QModelIndex &index) -> std::function<void(double, int)> {
    const QUuid parent(index.data(Qt::UserRole).toString()),
        effect_id(index.data(Qt::UserRole + 10).toString());
    const int channel = layer_channel_;
    if (effect_id.isNull())
      return [this, parent, channel](double opacity, int blend) {
        if (opacity >= 0 && callbacks_.layer_opacity_changed)
          callbacks_.layer_opacity_changed(parent, channel, opacity);
        if (blend >= 0 && callbacks_.layer_blend_changed)
          callbacks_.layer_blend_changed(parent, channel, LayerBlend(blend));
      };
    const auto *state = static_cast<LayerEffectRowState *>(
        findChild<QObject *>("layerEffectRowState"));
    const auto found = state->values.find(effect_id);
    if (found == state->values.end() ||
        (index.data(Qt::UserRole + 24).isValid() && !index.data(Qt::UserRole + 24).toBool()))
      return {};
    const auto domain = found->second.index();
    const int kind = std::visit([](const auto &effect) { return int(effect.kind); },
                                found->second);
    return [this, parent, effect_id, domain, kind](double opacity, int blend) {
      // Selection commits pending Properties edits and refreshes row metadata.
      // Read again for every popup value, preserving edits made since it opened.
      // No model index or old whole-effect snapshot survives that refresh.
      if (callbacks_.effect_selected)
        callbacks_.effect_selected(parent, effect_id);
      const auto *state = static_cast<LayerEffectRowState *>(
          findChild<QObject *>("layerEffectRowState"));
      const auto found = state->values.find(effect_id);
      if (found == state->values.end() || found->second.index() != domain ||
          std::visit([](const auto &effect) { return int(effect.kind); }, found->second) != kind)
        return;
      auto value = found->second;
      std::visit(
          [&](auto &effect) {
            if (opacity >= 0)
              effect.opacity = float(opacity);
            if (blend >= 0)
              effect.blending = LayerBlend(blend);
          },
          value);
      if (const auto *mask = std::get_if<MaskEffect>(&value)) {
        if (callbacks_.mask_effect_changed)
          callbacks_.mask_effect_changed(*mask);
      }
      else if (callbacks_.effect_changed)
        callbacks_.effect_changed(std::get<LevelsEffect>(value));
    };
  };
  const auto edit_opacity = [this, mix_writer](const QModelIndex &index) {
    if (!index.isValid() || index.data(Qt::UserRole + 19).toBool())
      return;
    const auto publish = mix_writer(index);
    if (!publish)
      return;
    auto *menu = new QMenu(layers_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *action = new QWidgetAction(menu);
    auto *panel = new QWidget(menu);
    auto *layout = column(panel);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);
    layout->addWidget(new QLabel(index.data(Qt::UserRole + 10).toString().isEmpty() ?
                                     layer_context_->currentText() + " opacity" :
                                     "Effect opacity",
                                 panel));
    // Compact row percentages are integer controls; Properties retain NumericSpinBox precision.
    auto *spin = new QDoubleSpinBox(panel);
    spin->setLocale(bts::numeric_locale());
    spin->setGroupSeparatorShown(false);
    spin->setObjectName("layerChannelOpacity");
    spin->setAccessibleName("Opacity");
    spin->setRange(0, 100);
    spin->setDecimals(0);
    spin->setSuffix(" %");
    spin->setKeyboardTracking(false);
    spin->setValue(index.data(Qt::UserRole + 1).toDouble());
    auto *slider = new QSlider(Qt::Horizontal, panel);
    slider->setObjectName("layerChannelOpacitySlider");
    slider->setRange(0, 1000);
    slider->setValue(qRound(index.data(Qt::UserRole + 1).toDouble() * 10));
    layout->addWidget(spin);
    layout->addWidget(slider);
    action->setDefaultWidget(panel);
    menu->addAction(action);
    connect(spin, &QDoubleSpinBox::valueChanged, menu, [publish, slider](double value) {
      const QSignalBlocker blocker(slider);
      slider->setValue(qRound(value * 10));
      publish(value / 100, -1);
    });
    connect(slider, &QSlider::sliderPressed, menu, [this] {
      if (callbacks_.property_edit_started)
        callbacks_.property_edit_started("Opacity");
    });
    connect(slider, &QSlider::valueChanged, menu, [publish, spin](int value) {
      const QSignalBlocker blocker(spin);
      spin->setValue(value / 10.);
      publish(value / 1000., -1);
    });
    connect(spin, &QDoubleSpinBox::editingFinished, menu, [this, slider] {
      if (!slider->isSliderDown() && callbacks_.property_edit_finished)
        callbacks_.property_edit_finished();
    });
    connect(slider, &QSlider::sliderReleased, menu, [this] {
      if (callbacks_.property_edit_finished)
        callbacks_.property_edit_finished();
    });
    menu->popup(layers_->viewport()->mapToGlobal(layers_->visualRect(index).bottomRight()));
    spin->setFocus();
    spin->selectAll();
  };
  layer_delegate->effect_removed = [this](QUuid parent, QUuid effect) {
    if (callbacks_.effect_removed)
      callbacks_.effect_removed(parent, effect);
  };
  layer_delegate->instance_selected = [this](QUuid id) {
    if (callbacks_.instance_selected)
      callbacks_.instance_selected(id);
  };
  layer_delegate->geometry_selected = [this](QUuid id) {
    if (callbacks_.geometry_selected)
      callbacks_.geometry_selected(id);
  };
  layer_delegate->target_selected = [this](QUuid id, bool mask) {
    if (callbacks_.mask_selected)
      callbacks_.mask_selected(id, mask);
  };
  layer_delegate->mask_isolated = [this](QUuid id) {
    if (callbacks_.mask_isolated)
      callbacks_.mask_isolated(id);
  };
  layer_delegate->mask_bypassed = [this](QUuid id) {
    if (callbacks_.mask_bypassed)
      callbacks_.mask_bypassed(id);
  };
  layer_delegate->opacity_requested = edit_opacity;
  const auto edit_blend = [this, mix_writer](const QModelIndex &index) {
    if (!index.isValid() || index.data(Qt::UserRole + 19).toBool())
      return;
    const auto publish = mix_writer(index);
    if (!publish)
      return;
    auto *menu = new QMenu(layers_);
    menu->setObjectName("layerBlendMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    for (int mode = 0; mode < int(layer_blend_names.size()); ++mode) {
      auto *action = menu->addAction(layer_blend_names[mode]);
      action->setObjectName(QString("layerBlend%1").arg(mode));
      action->setCheckable(true);
      action->setChecked(index.data(Qt::UserRole + 5).toInt() == mode);
      if (index.data(Qt::UserRole + 12).toBool() &&
          !index.data(Qt::UserRole + 10).toString().isEmpty() &&
          index.data(Qt::UserRole + 13).toInt() == int(MaskEffectKind::Group) &&
          mode == int(LayerBlend::Passthrough))
      {
        action->setEnabled(false);
        action->setToolTip(
            "Mask Groups isolate their children over zero. Passthrough applies only to layer folders.");
      }
      connect(action, &QAction::triggered, menu, [publish, mode] { publish(-1, mode); });
    }
    menu->popup(layers_->viewport()->mapToGlobal(layers_->visualRect(index).topRight()));
  };
  layer_delegate->blend_requested = edit_blend;
  auto *blend_action = new QAction("Edit channel blending…", layers_);
  blend_action->setObjectName("editLayerBlend");
  connect(blend_action, &QAction::triggered, this, [this, edit_blend] {
    edit_blend(layers_->currentIndex());
  });
  layer_delegate->folder_toggled = [this](QUuid id) {
    if (collapsed_folders_.contains(id))
      collapsed_folders_.remove(id);
    else
      collapsed_folders_.insert(id);
    if (callbacks_.layer_context_changed)
      callbacks_.layer_context_changed();
  };
  auto *collapse_action = new QAction("Expand / collapse folder", layers_);
  collapse_action->setObjectName("toggleLayerFolder");
  connect(collapse_action, &QAction::triggered, this, [this, layer_delegate] {
    if (layers_->currentItem() && layers_->currentItem()->data(Qt::UserRole + 3).toBool())
      layer_delegate->folder_toggled(QUuid(layers_->currentItem()->data(Qt::UserRole).toString()));
  });
  auto *group_action = new QAction("Group layers", this);
  group_action->setObjectName("groupLayers");
  group_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
  addAction(group_action);
  connect(group_action, &QAction::triggered, this, [this] {
    if (callbacks_.group_layers)
      callbacks_.group_layers();
  });
  auto *flatten_action = new QAction("Flatten layer/group", this);
  flatten_action->setObjectName("flattenLayer");
  flatten_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
  addAction(flatten_action);
  connect(flatten_action, &QAction::triggered, this, [this] {
    if (callbacks_.flatten_layer)
      callbacks_.flatten_layer();
  });
  auto *export_flatten_action = new QAction("Export flattened groups to files…", this);
  export_flatten_action->setObjectName("exportFlattenedGroups");
  connect(export_flatten_action, &QAction::triggered, this, [this] {
    if (callbacks_.export_flattened)
      callbacks_.export_flattened(false);
  });
  auto *export_flatten_mask = new QAction("Export flattened mask…", this);
  export_flatten_mask->setObjectName("exportFlattenedMask");
  connect(export_flatten_mask, &QAction::triggered, this, [this] {
    if (callbacks_.export_flattened)
      callbacks_.export_flattened(true);
  });
  auto *opacity_action = new QAction("Edit channel opacity…", layers_);
  opacity_action->setObjectName("editLayerOpacity");
  connect(opacity_action, &QAction::triggered, this, [this, edit_opacity] {
    edit_opacity(layers_->currentIndex());
  });
  connect(layer_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
    if (!item)
      return;
    const QUuid effect(item->data(Qt::UserRole + 10).toString());
    if (!effect.isNull()) {
      if (callbacks_.effect_selected)
        callbacks_.effect_selected(QUuid(item->data(Qt::UserRole).toString()), effect);
    }
    else if (callbacks_.layer_selected)
      callbacks_.layer_selected(QUuid(item->data(Qt::UserRole).toString()));
  });
  connect(layer_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
    if (!item)
      return;
    const QUuid effect(item->data(Qt::UserRole + 10).toString());
    if (!effect.isNull()) {
      if (callbacks_.effect_visibility_changed)
        callbacks_.effect_visibility_changed(
            QUuid(item->data(Qt::UserRole).toString()), effect, item->checkState() == Qt::Checked);
    }
    else if (callbacks_.layer_visibility_changed)
      callbacks_.layer_visibility_changed(QUuid(item->data(Qt::UserRole).toString()),
                                          item->checkState() == Qt::Checked);
  });
  move_layer_up_ = new QAction("Move layer up", this);
  move_layer_up_->setObjectName("moveLayerUp");
  move_layer_down_ = new QAction("Move layer down", this);
  move_layer_down_->setObjectName("moveLayerDown");
  for (auto [action, offset] : {std::pair{move_layer_up_, 1}, {move_layer_down_, -1}}) {
    connect(action, &QAction::triggered, this, [this, offset] {
      if (layers_->currentItem() && callbacks_.layer_moved)
        callbacks_.layer_moved(QUuid(layers_->currentItem()->data(Qt::UserRole).toString()),
                               offset);
    });
  }
  layer_list->copy_reference = [this] {
    if (copy_instance_->isEnabled() && callbacks_.copy_layer_reference)
      callbacks_.copy_layer_reference();
  };
  layer_list->paste_instance = [this] {
    if (paste_instance_->isEnabled() && callbacks_.paste_layer_instance)
      callbacks_.paste_layer_instance();
  };
  copy_instance_ = new QAction("Copy layer reference", layers_);
  copy_instance_->setObjectName("copyLayerReference");
  copy_instance_->setShortcut(QKeySequence::Copy);
  copy_instance_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  layers_->addAction(copy_instance_);
  connect(copy_instance_, &QAction::triggered, this, [this] {
    if (callbacks_.copy_layer_reference)
      callbacks_.copy_layer_reference();
  });
  paste_instance_ = new QAction("Paste layer instance", layers_);
  paste_instance_->setObjectName("pasteLayerInstance");
  paste_instance_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
  paste_instance_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  layers_->addAction(paste_instance_);
  connect(paste_instance_, &QAction::triggered, this, [this] {
    if (callbacks_.paste_layer_instance)
      callbacks_.paste_layer_instance();
  });
  auto *material_id_action = new QAction("Material ID color…", layers_);
  material_id_action->setObjectName("assignMaterialId");
  connect(material_id_action, &QAction::triggered, this, [this] {
    if (layers_->currentItem() && callbacks_.material_id_requested)
      callbacks_.material_id_requested(
          QUuid(layers_->currentItem()->data(Qt::UserRole).toString()));
  });
  layer_list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(layer_list,
          &QListWidget::customContextMenuRequested,
          this,
          [this,
           material_id_action,
           opacity_action,
           blend_action,
           collapse_action,
           group_action,
           flatten_action,
           export_flatten_action,
           export_flatten_mask](const QPoint &point) {
            const auto geometry_index = layers_->indexAt(point);
            if (geometry_index.isValid() &&
                geometry_index.data(Qt::UserRole + 10).toString().isEmpty() &&
                !geometry_index.data(Qt::UserRole + 19).toBool() &&
                layer_geometry_rect(geometry_index, layers_->visualRect(geometry_index))
                    .contains(point))
            {
              auto *menu = new QMenu(layers_);
              menu->setAttribute(Qt::WA_DeleteOnClose);
              menu->setObjectName("geometryMaskContextMenu");
              const QUuid id(geometry_index.data(Qt::UserRole).toString());
              if (callbacks_.geometry_selected)
                callbacks_.geometry_selected(id);
              auto *copy = menu->addAction("Copy geometry mask");
              copy->setObjectName("copyGeometryMask");
              connect(copy, &QAction::triggered, menu, [this, id] {
                if (callbacks_.copy_geometry)
                  callbacks_.copy_geometry(id);
              });
              auto *paste = menu->addAction("Paste into geometry mask");
              paste->setObjectName("pasteGeometryMask");
              paste->setEnabled(geometry_clipboard_available_);
              connect(paste, &QAction::triggered, menu, [this, id] {
                if (callbacks_.paste_geometry)
                  callbacks_.paste_geometry(id);
              });
              for (bool include : {true, false}) {
                auto *all = menu->addAction(include ? "Include all" : "Exclude all");
                all->setObjectName(include ? "includeAllGeometry" : "excludeAllGeometry");
                connect(all, &QAction::triggered, menu, [this, id, include] {
                  if (callbacks_.geometry_all_changed)
                    callbacks_.geometry_all_changed(id, include);
                });
              }
              menu->popup(layers_->viewport()->mapToGlobal(point));
              return;
            }
            if (auto *item = layers_->itemAt(point); item && !item->isSelected())
              layers_->setCurrentItem(item);
            auto *menu = new QMenu(layers_);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            const auto index = layers_->indexAt(point);
            if (index.isValid() && index.data(Qt::UserRole + 6).toBool() &&
                layer_mask_rect(index, layers_->visualRect(index)).contains(point))
            {
              menu->setObjectName("maskContextMenu");
              const QUuid id(index.data(Qt::UserRole).toString());
              if (callbacks_.mask_selected)
                callbacks_.mask_selected(id, true);
              menu->addActions(mask_button_->menu()->actions());
              menu->addSeparator();
              menu->addActions(effects_button_->menu()->actions());
              menu->addSeparator();
              menu->addAction(export_flatten_mask);
              menu->popup(layers_->viewport()->mapToGlobal(point));
              return;
            }
            if (index.isValid() && !index.data(Qt::UserRole + 10).toString().isEmpty()) {
              menu->addAction(effect_up_);
              menu->addAction(effect_down_);
              auto *remove = menu->addAction("Remove effect");
              const QUuid parent(index.data(Qt::UserRole).toString()),
                  effect(index.data(Qt::UserRole + 10).toString());
              connect(remove, &QAction::triggered, menu, [this, parent, effect] {
                if (callbacks_.effect_removed)
                  callbacks_.effect_removed(parent, effect);
              });
              menu->popup(layers_->viewport()->mapToGlobal(point));
              return;
            }
            if (index.isValid() && index.data(Qt::UserRole + 3).toBool() &&
                !index.data(Qt::UserRole + 19).toBool())
            {
              const QUuid id(index.data(Qt::UserRole).toString());
              auto *folder_mat = menu->addAction("Save folder as Material…");
              folder_mat->setObjectName("createFolderMaterial");
              folder_mat->setEnabled(bool(callbacks_.create_folder_material));
              connect(folder_mat, &QAction::triggered, menu, [this, id] {
                if (callbacks_.create_folder_material)
                  callbacks_.create_folder_material(id);
              });
              auto *preset = menu->addAction("Save folder as Material preset…");
              preset->setObjectName("createSmartMaterial");
              preset->setEnabled(bool(callbacks_.create_smart_material));
              connect(preset, &QAction::triggered, menu, [this, id] {
                if (callbacks_.create_smart_material)
                  callbacks_.create_smart_material(id);
              });
              menu->addSeparator();
            }
            auto *adjustment = menu->addAction(
                "Pass Through / Adjustment Layer (sample underlying layers)");
            adjustment->setObjectName("layerAdjustmentMode");
            adjustment->setCheckable(true);
            const QUuid adjustment_id(index.data(Qt::UserRole).toString());
            adjustment->setChecked(index.data(adjustment_checked_role).toBool());
            adjustment->setEnabled(index.isValid() && layers_->isEnabled() &&
                                   layers_->selectedItems().size() == 1 &&
                                   index.data(adjustment_editable_role).toBool() &&
                                   !index.data(Qt::UserRole + 8).toBool() &&
                                   !index.data(Qt::UserRole + 17).toBool() && !quick_mask_edit_ &&
                                   bool(callbacks_.layer_adjustment_changed));
            connect(adjustment, &QAction::triggered, menu, [this, adjustment_id](bool checked) {
              if (callbacks_.layer_adjustment_changed)
                callbacks_.layer_adjustment_changed(adjustment_id, checked);
            });
            menu->addSeparator();
            menu->addMenu(mask_button_->menu());
            menu->addSeparator();
            menu->addAction(copy_instance_);
            menu->addAction(paste_instance_);
            menu->addSeparator();
            if (!index.data(Qt::UserRole + 19).toBool())
              menu->addAction(blend_action);
            menu->addAction(material_id_action);
            menu->addAction(opacity_action);
            menu->addAction(group_action);
            menu->addAction(flatten_action);
            menu->addAction(export_flatten_action);
            menu->addAction(export_flatten_mask);
            menu->addAction(collapse_action);
            menu->addSeparator();
            for (const auto &[label, update] :
                 std::array<std::pair<const char *, bool>, 2>{
                     {{"Create Pre-compose Layer Here (Cache Below)", false},
                      {"Update Pre-compose Cache", true}}})
            {
              auto *action = menu->addAction(label);
              action->setObjectName(update ? "updatePrecomposeCache" : "createPrecomposeHere");
              action->setEnabled(index.isValid() && !index.data(Qt::UserRole + 27).toBool() &&
                                 (!update || index.data(Qt::UserRole + 28).toBool()));
              connect(action, &QAction::triggered, menu, [this, update] {
                if (callbacks_.precompose_layer)
                  callbacks_.precompose_layer(update);
              });
            }
            auto *release = menu->addAction("Unfreeze / Release Pre-compose");
            release->setObjectName("releasePrecompose");
            release->setEnabled(index.data(Qt::UserRole + 28).toBool() &&
                                !index.data(Qt::UserRole + 27).toBool());
            connect(release, &QAction::triggered, menu, [this] {
              if (callbacks_.release_precompose)
                callbacks_.release_precompose();
            });
            menu->addAction(move_layer_up_);
            menu->addAction(move_layer_down_);
            menu->popup(layers_->viewport()->mapToGlobal(point));
          });
  layer_settings_tabs_->addTab(layers, "Layers");
  auto *texture_page = new QSplitter(Qt::Vertical, layer_settings_tabs_);
  texture_page->setObjectName("textureSetsPage");
  texture_page->addWidget(texture_panel);
  layer_settings_tabs_->addTab(texture_page, "Texture Sets");
