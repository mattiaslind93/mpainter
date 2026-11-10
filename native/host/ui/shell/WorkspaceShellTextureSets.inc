// SPDX-License-Identifier: GPL-3.0-or-later
// The inspector splitter and the Texture Sets panel. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  inspector_splitter_ = splitter(Qt::Vertical, workspace_splitter_, "inspectorSplitter");
  workspace_splitter_->addWidget(inspector_splitter_);
  inspector_splitter_->setMinimumWidth(280);
  workspace_splitter_->setStretchFactor(0, 1);
  workspace_splitter_->setStretchFactor(1, 0);
  auto *texture_panel = new QWidget;
  texture_panel->setObjectName("textureSetPanel");
  texture_panel->setMinimumHeight(195);
  auto *texture_layout = column(texture_panel);
  auto *set_header = header(texture_panel, "Texture sets");
  auto *set_settings = new QToolButton(set_header);
  set_settings->setObjectName("textureSetListSettings");
  set_settings->setText("Settings");
  set_settings->setToolTip("Texture Sets settings");
  set_header->layout()->addWidget(set_settings);
  texture_layout->addWidget(set_header);
  texture_sets_ = new EmptyList("No texture sets", texture_panel);
  texture_sets_->setObjectName("textureSets");
  texture_sets_->setMinimumHeight(64);
  texture_sets_->setToolTip("Uncheck a Texture Set to hide and unload it; check to reload");
  connect(texture_sets_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
    if (item->data(Qt::CheckStateRole).isValid() && callbacks_.residency_changed)
      callbacks_.residency_changed(
          item->data(Qt::UserRole).toUInt(), 0, item->checkState() == Qt::Checked);
  });
  auto *rename_set = new QAction(this);
  rename_set->setObjectName("renameTextureSet");
  connect(rename_set, &QAction::triggered, this, [this, rename_set] {
    const auto values = rename_set->data().toMap();
    if (callbacks_.texture_set_renamed)
      callbacks_.texture_set_renamed(values["uid"].toUInt(), values["name"].toString());
  });
  new NameListShortcuts(texture_sets_);
  auto *set_names = new TextureSetNameDelegate(texture_sets_);
  texture_sets_->setItemDelegate(set_names);
  texture_sets_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed);
  set_names->renamed = [this](const QModelIndex &index, const QString &name) {
    const auto uid = index.data(Qt::UserRole).toUInt();
    QTimer::singleShot(0, this, [this, uid, name] {
      if (callbacks_.texture_set_renamed)
        callbacks_.texture_set_renamed(uid, name);
    });
  };
  texture_sets_->setSelectionMode(QAbstractItemView::SingleSelection);
  texture_sets_->setAccessibleName("Texture set selection");
  texture_sets_->setEnabled(bool(callbacks_.texture_set_selected));
  if (!callbacks_.texture_set_selected) {
    texture_sets_->setToolTip("Texture set selection is unavailable.");
  }
  texture_layout->addWidget(texture_sets_, 1);
  uv_tiles_ = new QTreeWidget(texture_panel);
  uv_tiles_->setObjectName("uvTiles");
  uv_tiles_->setHeaderHidden(true);
  connect(uv_tiles_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int) {
    const int tile = item->data(0, Qt::UserRole).toInt();
    if (tile && texture_sets_->currentItem() && callbacks_.residency_changed)
      callbacks_.residency_changed(texture_sets_->currentItem()->data(Qt::UserRole).toUInt(),
                                   tile,
                                   item->checkState(0) == Qt::Checked);
  });
  uv_tiles_->setMaximumHeight(100);
  uv_tiles_->hide();
  texture_layout->addWidget(uv_tiles_);

