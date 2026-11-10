// SPDX-License-Identifier: GPL-3.0-or-later
// The asset shelf: filters, categories, the asset grid and brush alphas. Part of the
// WorkspaceShell constructor (WorkspaceShell.cc), included in order inside its body; it shares
// that function's locals and is not a standalone file.
shelf_ = new QWidget(viewport_shelf_splitter_);
viewport_shelf_splitter_->insertWidget(0, shelf_);
shelf_->setObjectName("assetShelf");
shelf_->setMinimumHeight(shelf_minimum_height);
shelf_->setMinimumWidth(280);
shelf_->setMaximumWidth(420);
auto *shelf_layout = column(shelf_);
QToolButton *close_shelf;
shelf_layout->addWidget(header(shelf_, "ASSETS", &close_shelf));
auto *library_header = new QLabel("All libraries  /  Your assets", shelf_);
library_header->setObjectName("assetLibraryHeader");
library_header->setContentsMargins(8, 4, 8, 4);
library_header->setStyleSheet(bts::theme::qss(
    "color: @Muted@; font-size: 10px; font-weight: 600; background: @Canvas@; border-bottom: 1px solid @Canvas@;"));
shelf_layout->addWidget(library_header);
auto *filter = new QWidget(shelf_);
filter->setObjectName("shelfFilter");
filter->setFixedHeight(30);
auto *filter_layout = row(filter, 6);
filter_layout->setSpacing(4);
auto *import_resources = tool(filter, Glyph::Folder, "Import resources", true);
import_resources->setObjectName("importResources");
import_resources->setText("Import");
import_resources->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
import_resources->setFixedSize(90, 24);
filter_layout->addWidget(import_resources);
connect(import_resources, &QToolButton::clicked, this, [this] {
  if (callbacks_.resource_import_requested)
    callbacks_.resource_import_requested();
});
auto *shelf_preferences = tool(filter, Glyph::Settings, "Preferences — Shelf and Viewport", true);
shelf_preferences->setObjectName("shelfPreferences");
shelf_preferences->setFixedSize(24, 24);
filter_layout->addWidget(shelf_preferences);
connect(shelf_preferences, &QToolButton::clicked, this, [this] {
  if (callbacks_.shelf_preferences_requested)
    callbacks_.shelf_preferences_requested();
});
auto *shelf_preferences_action = new QAction("&Preferences…", this);
shelf_preferences_action->setObjectName("shelfPreferencesAction");
shelf_preferences_action->setShortcut(QKeySequence::Preferences);
addAction(shelf_preferences_action);
connect(shelf_preferences_action, &QAction::triggered, shelf_preferences, &QToolButton::click);
asset_scope_control_ = new QComboBox(filter);
asset_scope_control_->setMinimumWidth(104);
asset_scope_control_->setMaximumWidth(130);
asset_scope_control_->setObjectName("shelfLibraryScope");
asset_scope_control_->addItems({"All libraries", "Built-in", "Session", "Project", "User"});
filter_layout->addWidget(asset_scope_control_);
connect(asset_scope_control_, &QComboBox::currentIndexChanged, this, [this](int i) {
  asset_scope_ = i - 1;
  if (shelf_categories_ && shelf_categories_->currentRow() == 1 && i != 3) {
    const QSignalBlocker block(shelf_categories_);
    shelf_categories_->setCurrentRow(0);
  }
  refresh_alpha_shelf();
});
asset_reset_ = tool(filter, Glyph::Reset, "Reset filters", false);
asset_reset_->setObjectName("resetShelfFilters");
asset_reset_->setFixedSize(24, 24);
filter_layout->addWidget(asset_reset_);
connect(asset_reset_, &QToolButton::clicked, this, [this] {
  asset_search_->clear();
  shelf_categories_->setCurrentRow(0);
  asset_scope_control_->setCurrentIndex(0);
  refresh_alpha_shelf();
});
auto *filter_glyph_btn = tool(filter, Glyph::Filter, "Filter assets");
filter_glyph_btn->setFixedSize(24, 24);
filter_layout->addWidget(filter_glyph_btn);
asset_category_chip_ = new QToolButton(filter);
asset_category_chip_->setObjectName("shelfCategoryChip");
asset_category_chip_->setToolButtonStyle(Qt::ToolButtonTextOnly);
asset_category_chip_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
asset_category_chip_->setFixedHeight(22);
asset_category_chip_->setMaximumWidth(160);
asset_category_chip_->hide();
filter_layout->addWidget(asset_category_chip_);
connect(asset_category_chip_, &QToolButton::clicked, this, [this] {
  if (shelf_categories_)
    shelf_categories_->setCurrentRow(0);
});
filter_layout->addStretch(1);
auto *search = new QLineEdit(shelf_);
asset_search_ = search;
search->setClearButtonEnabled(true);
search->setObjectName("shelfSearch");
search->setPlaceholderText("Search assets…");
search->setToolTip("Combine n:name s:library u:usage p:path; append = for exact matches");
search->setStyleSheet(bts::theme::qss(
    "QLineEdit#shelfSearch { margin: 4px 6px 2px 6px; padding: 2px 6px; min-height: 22px; background: @Separator@; border: 1px solid @Hover@; border-radius: @Radius@; } QLineEdit#shelfSearch:focus { border-color: @Accent@; }"));
connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
  alpha_search_ = text;
  refresh_alpha_shelf();
});
shelf_layout->addWidget(search);
shelf_layout->addWidget(filter);
auto *download_materials = new QPushButton("Download free materials", shelf_);
download_materials->setObjectName("downloadFreeMaterialsShelf");
download_materials->setToolTip(
    "Poly Haven materials are CC0. Download, then import or drop the texture files here.");
connect(download_materials, &QPushButton::clicked, this, [] {
  QDesktopServices::openUrl(QUrl("https://polyhaven.com/textures"));
});
shelf_layout->addWidget(download_materials);

auto *shelf_body = new QWidget(shelf_);
auto *shelf_body_layout = column(shelf_body);
shelf_body_layout->setSpacing(0);
auto *categories = new ShelfCategoryList(shelf_body);
shelf_categories_ = categories;
categories->setObjectName("shelfCategories");
categories->setItemDelegate(new ShelfCategoryDelegate(categories));
categories->setViewMode(QListView::IconMode);
categories->setFlow(QListView::LeftToRight);
categories->setWrapping(true);
categories->setResizeMode(QListView::Adjust);
categories->setMovement(QListView::Static);
categories->setSpacing(0);
categories->setGridSize(QSize(88, 24));
categories->setFixedHeight(126);
categories->addItems(shelf_category_names());
// A row that can never hold anything is greyed out rather than left to be
// clicked for an empty grid. See unsupported_shelf_categories().
for (int row = 0; row < categories->count(); ++row) {
  auto *item = categories->item(row);
  if (shelf_category_supported(item->text()))
    continue;
  item->setFlags(item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable));
  item->setToolTip(item->text() + " — no resources of this kind yet");
}
categories->setCurrentRow(0);
categories->update_grid();
shelf_body_layout->addWidget(categories);
auto *assets = new EmptyList("No assets", shelf_body);
alpha_assets_ = assets;
assets->setObjectName("shelfAssets");
auto *asset_names = new ShelfCardDelegate(assets);
assets->setItemDelegate(asset_names);
assets->setEditTriggers(QAbstractItemView::EditKeyPressed);
new NameListShortcuts(assets);
asset_names->renamed = [this](const QModelIndex &index, const QString &name) {
  const QUuid id(index.data(Qt::UserRole).toString());
  QTimer::singleShot(0, this, [this, id, name] {
    if (callbacks_.asset_renamed)
      callbacks_.asset_renamed(id, name);
  });
};
assets->setViewMode(QListView::IconMode);
assets->setResizeMode(QListView::Adjust);
assets->setMovement(QListView::Static);
// QListView::setMovement turns dragging off for Static, so the shelf has to
// be made draggable after that call and not before it. Setting it first is
// silently undone, which is what stopped assets being dragged onto a mesh.
assets->setDragEnabled(true);
assets->setDragDropMode(QAbstractItemView::DragOnly);
assets->setDefaultDropAction(Qt::CopyAction);
assets->setIconSize(QSize(84, 84));
assets->setGridSize(QSize(90, 114));
assets->setSpacing(bts::theme::SpaceTight);
shelf_body_layout->addWidget(assets, 1);
{
  auto *empty_materials = new QWidget(shelf_body);
  empty_materials->setObjectName("emptyMaterials");
  auto *empty_layout = column(empty_materials);
  auto *empty_note = new QLabel(
      "Your material library is empty. Poly Haven materials are CC0. Import or drop downloaded textures here.",
      empty_materials);
  empty_note->setObjectName("emptyMaterialsNote");
  empty_note->setWordWrap(true);
  empty_layout->addWidget(empty_note);
  auto *empty_download = new QPushButton("Download free materials", empty_materials);
  empty_download->setObjectName("downloadFreeMaterialsEmpty");
  connect(empty_download, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl("https://polyhaven.com/textures"));
  });
  empty_layout->addWidget(empty_download);
  empty_materials->hide();
  shelf_body_layout->addWidget(empty_materials);
}

shelf_layout->addWidget(shelf_body, 1);
connect(categories, &QListWidget::currentTextChanged, this, [this, assets](const QString &text) {
  {
    const QSignalBlocker block(asset_scope_control_);
    asset_scope_ = text == "Project" ? int(AssetScope::Project) : -1;
    asset_scope_control_->setCurrentIndex(asset_scope_ + 1);
  }
  {
    const QSignalBlocker block(asset_search_);
    asset_search_->clear();
    alpha_search_.clear();
  }
  assets->set_empty_text(text == "All" ? "No assets" : "No assets in " + text);
  refresh_alpha_shelf();
});
auto *smart_mask_click = new QTimer(assets);
smart_mask_click->setSingleShot(true);
smart_mask_click->setInterval(QApplication::doubleClickInterval());
connect(assets, &QListWidget::itemClicked, this, [this, smart_mask_click](QListWidgetItem *item) {
  smart_mask_click->stop();
  if (asset_catalog_) {
    const QUuid id(item->data(Qt::UserRole).toString());
    for (const auto &asset : asset_catalog_->assets())
      if (asset.id == id && asset.usages.contains("brush")) {
        if (callbacks_.asset_selected)
          callbacks_.asset_selected(asset);
        return;
      }
  }
  if (QDateTime::currentMSecsSinceEpoch() -
              smart_mask_click->property("doubleClickedAt").toLongLong() <
          QApplication::doubleClickInterval() ||
      !asset_catalog_)
    return;
  const QUuid id(item->data(Qt::UserRole).toString());
  for (const auto &asset : asset_catalog_->assets())
    if (asset.id == id && (asset.smart_mask || is_spmsk_asset(asset))) {
      smart_mask_click->setProperty("asset", id);
      smart_mask_click->setProperty("target",
                                    layers_ && layers_->currentItem() ?
                                        layers_->currentItem()->data(Qt::UserRole) :
                                        QVariant{});
      smart_mask_click->start();
      break;
    }
});
connect(smart_mask_click, &QTimer::timeout, this, [this, smart_mask_click] {
  if (!asset_catalog_ || !layers_ || !layers_->currentItem() ||
      layers_->currentItem()->data(Qt::UserRole) != smart_mask_click->property("target"))
    return;
  const auto id = smart_mask_click->property("asset").toUuid();
  for (const auto &asset : asset_catalog_->assets())
    if (asset.id == id && (asset.smart_mask || is_spmsk_asset(asset))) {
      if (callbacks_.asset_selected)
        callbacks_.asset_selected(asset);
      break;
    }
});
connect(assets,
        &QListWidget::itemDoubleClicked,
        this,
        [this, smart_mask_click](QListWidgetItem *item) {
          smart_mask_click->stop();
          smart_mask_click->setProperty("doubleClickedAt", QDateTime::currentMSecsSinceEpoch());
          const auto key = item->data(Qt::UserRole).toString();
          if (asset_catalog_)
            for (const auto &asset : asset_catalog_->assets())
              if (asset.id == QUuid(key) && asset.usages.contains("brush"))
                return;
          if (key.startsWith("baked:")) {
            if (callbacks_.baked_asset && callbacks_.asset_selected)
              callbacks_.asset_selected(callbacks_.baked_asset(key));
            return;
          }
          if (asset_catalog_) {
            const auto id = QUuid(key);
            for (const auto &asset : asset_catalog_->assets())
              if (asset.id == id) {
                if (callbacks_.asset_selected)
                  callbacks_.asset_selected(asset);
                break;
              }
          }
          else if (callbacks_.alpha_selected)
            callbacks_.alpha_selected(alpha_resources_.at(item->data(Qt::UserRole).toInt()));
        });
assets->setContextMenuPolicy(Qt::CustomContextMenu);
new ImageDropFilter(assets, [this](const QStringList &paths) {
  if (callbacks_.texture_assets_import_requested)
    callbacks_.texture_assets_import_requested(paths);
});
connect(assets, &QListWidget::customContextMenuRequested, this, [this, assets](QPoint point) {
  if (asset_catalog_) {
    auto *item = assets->itemAt(point);
    const StudioAsset *brush = nullptr;
    if (item)
      for (const auto &a : asset_catalog_->assets())
        if (a.id == QUuid(item->data(Qt::UserRole).toString()) && a.usages.contains("brush")) {
          brush = &a;
          break;
        }
    if (brush || !item) {
      auto *menu = new QMenu(assets);
      menu->setAttribute(Qt::WA_DeleteOnClose);
      menu->setObjectName("brushPresetContextMenu");
      auto *create = menu->addAction("Create brush preset…");
      create->setObjectName("createBrushPreset");
      connect(create, &QAction::triggered, menu, [this] {
        if (callbacks_.save_brush_preset)
          callbacks_.save_brush_preset();
      });
      auto *import_brush = menu->addAction("Import brush preset…");
      import_brush->setObjectName("importBrushPreset");
      connect(import_brush, &QAction::triggered, menu, [this] {
        if (callbacks_.resource_import_requested)
          callbacks_.resource_import_requested();
      });
      if (brush) {
        const auto id = brush->id;
        for (const auto &entry :
             std::array<std::pair<const char *, const char *>, 3>{
                 {{"update", "Update from current brush"},
                  {"rename", "Rename…"},
                  {"delete", "Delete…"}}})
        {
          auto *action = menu->addAction(entry.second);
          action->setObjectName(QString("brushPreset_") + entry.first);
          action->setEnabled(QString(entry.first) == "rename" ?
                                 brush->scope != AssetScope::Builtin :
                                 brush->scope == AssetScope::User);
          connect(action, &QAction::triggered, menu, [this, id, operation = QString(entry.first)] {
            if (callbacks_.brush_preset_action)
              callbacks_.brush_preset_action(operation, id);
          });
        }
        auto *export_action = menu->addAction("Export brush preset…");
        export_action->setObjectName("exportBrushPreset");
        connect(export_action, &QAction::triggered, menu, [this, id] {
          for (const auto &a : asset_catalog_->assets())
            if (a.id == id && callbacks_.asset_export_requested) {
              callbacks_.asset_export_requested(a);
              break;
            }
        });
      }
      menu->popup(assets->viewport()->mapToGlobal(point));
      return;
    }
  }
  auto *item = assets->itemAt(point);
  if (!item || !asset_catalog_)
    return;
  const QUuid id(item->data(Qt::UserRole).toString());
  const auto found = std::find_if(asset_catalog_->assets().begin(),
                                  asset_catalog_->assets().end(),
                                  [&](const auto &asset) { return asset.id == id; });
  if (found == asset_catalog_->assets().end() ||
      (!found->material_preset && !found->smart_mask && !is_spsm_archive_asset(*found)))
    return;
  assets->setCurrentItem(item);
  auto *menu = new QMenu(assets);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->setObjectName("presetResourceContextMenu");
  if (found->smart_mask || is_spmsk_asset(*found)) {
    auto *apply = menu->addAction("Apply mask generator");
    apply->setObjectName("applyShelfSmartMask");
    apply->setEnabled(asset_project_ready_);
    connect(apply, &QAction::triggered, menu, [this, id] {
      if (!asset_catalog_)
        return;
      for (const auto &asset : asset_catalog_->assets())
        if (asset.id == id) {
          if (callbacks_.asset_selected)
            callbacks_.asset_selected(asset);
          break;
        }
    });
  }
  if (is_spsm_archive_asset(*found)) {
    auto *inspect = menu->addAction("Inspect SPSM import report…");
    inspect->setObjectName("inspectSpsmImportReport");
    connect(inspect, &QAction::triggered, menu, [this, id] {
      if (!asset_catalog_)
        return;
      for (const auto &asset : asset_catalog_->assets())
        if (asset.id == id) {
          show_spsm_source_report(this, asset);
          break;
        }
    });
  }
  auto *save = menu->addAction("Export preset…");
  save->setObjectName("exportResourcePreset");
  connect(save, &QAction::triggered, menu, [this, id] {
    if (!asset_catalog_)
      return;
    for (const auto &asset : asset_catalog_->assets())
      if (asset.id == id) {
        if (callbacks_.asset_export_requested)
          callbacks_.asset_export_requested(asset);
        break;
      }
  });
  menu->popup(assets->viewport()->mapToGlobal(point));
});
add_alpha_resource(BrushAlpha{});
BrushAlpha plus;
plus.kind = AlphaKind::Plus;
add_alpha_resource(plus);
