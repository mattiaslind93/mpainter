// SPDX-License-Identifier: GPL-3.0-or-later
// The left tool rail and the workspace splitter. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  auto *left = new QWidget(root);
  left->setObjectName("toolRail");
  left->setFixedWidth(rail_width);
  auto *left_layout = column(left);
  left_layout->setContentsMargins(
      bts::theme::Space, bts::theme::Space, bts::theme::Space, bts::theme::Space);
  left_layout->setSpacing(bts::theme::Space);
  for (const auto &[glyph, name] : {std::pair{Glyph::Brush, "Paint"},
                                    {Glyph::Eraser, "Eraser"},
                                    {Glyph::Projection, "Projection"},
                                    {Glyph::Fill, "Polygon fill"},
                                    {Glyph::Smudge, "Smudge"},
                                    {Glyph::Clone, "Clone"},
                                    {Glyph::Path, "Surface paths"},
                                    {Glyph::Picker, "Material picker"}})
  {
    auto *button = tool(left, glyph, QString::fromLatin1(name));
    left_layout->addWidget(button);
    if (glyph == Glyph::Path) {
      path_tool_ = button;
      button->setObjectName("surfacePathTool");
      button->setCheckable(true);
      path_action_ = new QAction("Surface paths", this);
      path_action_->setObjectName("selectSurfacePath");
      addAction(path_action_);
      connect(path_action_, &QAction::triggered, this, [this] {
        if (callbacks_.path_selected)
          callbacks_.path_selected();
      });
      connect(button, &QToolButton::clicked, path_action_, &QAction::trigger);
    }
    if (glyph == Glyph::Clone) {
      clone_tool_ = button;
      button->setObjectName("cloneTool");
      button->setCheckable(true);
      button->setPopupMode(QToolButton::MenuButtonPopup);
      clone_action_ = new QAction("Clone", this);
      clone_action_->setObjectName("selectCloneTool");
      clone_action_->setShortcut(QKeySequence(Qt::Key_6));
      addAction(clone_action_);
      connect(clone_action_, &QAction::triggered, this, [this] {
        if (callbacks_.clone_selected)
          callbacks_.clone_selected();
      });
      connect(button, &QToolButton::clicked, clone_action_, &QAction::trigger);
      auto *menu = new QMenu(button);
      button->setMenu(menu);
      clone_relative_ = menu->addAction("Relative source");
      clone_relative_->setObjectName("cloneRelative");
      clone_relative_->setCheckable(true);
      clone_absolute_ = menu->addAction("Absolute source");
      clone_absolute_->setObjectName("cloneAbsolute");
      clone_absolute_->setCheckable(true);
      for (auto *action : {clone_relative_, clone_absolute_})
        connect(action, &QAction::triggered, this, [this, action] {
          if (callbacks_.clone_selected)
            callbacks_.clone_selected();
          if (callbacks_.clone_mode_changed)
            callbacks_.clone_mode_changed(action == clone_absolute_);
        });
    }
    if (glyph == Glyph::Smudge) {
      smudge_tool_ = button;
      button->setObjectName("smudgeTool");
      button->setCheckable(true);
      smudge_action_ = new QAction("Smudge", this);
      smudge_action_->setObjectName("selectSmudgeTool");
      smudge_action_->setShortcut(QKeySequence(Qt::Key_5));
      addAction(smudge_action_);
      connect(smudge_action_, &QAction::triggered, this, [this] {
        if (callbacks_.smudge_selected)
          callbacks_.smudge_selected();
      });
      connect(button, &QToolButton::clicked, smudge_action_, &QAction::trigger);
    }
    if (glyph == Glyph::Projection) {
      projection_tool_ = button;
      button->setObjectName("projectionTool");
      button->setCheckable(true);
      projection_action_ = new QAction("Projection", this);
      projection_action_->setObjectName("selectProjectionTool");
      projection_action_->setShortcut(QKeySequence(Qt::Key_3));
      addAction(projection_action_);
      connect(projection_action_, &QAction::triggered, this, [this] {
        if (callbacks_.projection_selected)
          callbacks_.projection_selected();
      });
      connect(button, &QToolButton::clicked, projection_action_, &QAction::trigger);
    }
    if (glyph == Glyph::Fill) {
      polygon_tool_ = button;
      button->setObjectName("polygonFillTool");
      button->setCheckable(true);
      button->setToolTip(
          "Polygon fill (4) — Auto: 3D object/element, 2D UV island; Ctrl uses secondary ink");
      polygon_action_ = new QAction("Polygon fill", this);
      polygon_action_->setObjectName("selectPolygonFill");
      polygon_action_->setShortcut(QKeySequence(Qt::Key_4));
      addAction(polygon_action_);
      connect(polygon_action_, &QAction::triggered, this, [this] {
        if (callbacks_.polygon_tool_changed)
          callbacks_.polygon_tool_changed(true);
      });
      connect(button, &QToolButton::clicked, polygon_action_, &QAction::trigger);
    }
    if (glyph == Glyph::Picker) {
      picker_tool_ = button;
      button->setObjectName("materialPickerTool");
      button->setCheckable(true);
      button->setToolTip("Material picker (P)");
      picker_action_ = new QAction("Material picker", this);
      picker_action_->setObjectName("selectMaterialPicker");
      picker_action_->setShortcut(QKeySequence(Qt::Key_P));
      addAction(picker_action_);
      connect(picker_action_, &QAction::triggered, button, &QToolButton::click);
      connect(button, &QToolButton::toggled, this, [this](bool value) {
        if (callbacks_.picker_changed)
          callbacks_.picker_changed(value);
      });
    }
    if (glyph == Glyph::Brush || glyph == Glyph::Eraser) {
      const bool erase = glyph == Glyph::Eraser;
      button->setCheckable(true);
      button->setObjectName(erase ? "eraserTool" : "paintTool");
      auto *action = new QAction(QString::fromLatin1(name), this);
      action->setObjectName(erase ? "selectEraserTool" : "selectPaintTool");
      action->setShortcut(QKeySequence(erase ? Qt::Key_2 : Qt::Key_1));
      addAction(action);
      if (erase) {
        eraser_tool_ = button;
        eraser_tool_action_ = action;
      }
      else {
        paint_tool_ = button;
        paint_tool_action_ = action;
      }
      connect(action, &QAction::triggered, this, [this, erase] {
        if (callbacks_.tool_selected)
          callbacks_.tool_selected(erase);
      });
      connect(button, &QToolButton::clicked, action, &QAction::trigger);
    }
  }
  left_layout->addStretch();
  auto *workspace_button = tool(left, Glyph::Settings, "Workspace layout", true);
  workspace_button->setObjectName("workspaceMenu");
  workspace_button->setMenu(workspace_menu);
  workspace_button->setPopupMode(QToolButton::InstantPopup);
  left_layout->addWidget(workspace_button);
  root_layout->addWidget(left);

  workspace_splitter_ = splitter(Qt::Horizontal, root, "workspaceSplitter");
  root_layout->addWidget(workspace_splitter_, 1);
