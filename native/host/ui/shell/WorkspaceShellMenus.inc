// SPDX-License-Identifier: GPL-3.0-or-later
// Window setup, the shell's actions and the menu bar. Part of the WorkspaceShell constructor (WorkspaceShell.cc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
  install_panel_scrolling();
  if (!viewport) {
    qFatal("WorkspaceShell requires a viewport widget");
  }
  setObjectName("workspaceShell");
  setWindowTitle("mPainter");
  setMinimumSize(1000, 680);
  resize(1440, 900);
  QFont body_font = font();
  body_font.setPixelSize(11);
  setFont(body_font);

  auto *root = new QWidget(this);
  root->setObjectName("workspaceRoot");
  setCentralWidget(root);
  auto *root_layout = row(root, 0);
  root_layout->setSpacing(0);

  shelf_action_ = new QAction("Shelf", this);
  shelf_action_->setIcon(icon(Glyph::Shelf));
  shelf_action_->setObjectName("toggleShelf");
  shelf_action_->setCheckable(true);
  shelf_action_->setChecked(true);
  console_action_ = new QAction("Console", this);
  console_action_->setIcon(icon(Glyph::Console));
  console_action_->setObjectName("toggleConsole");
  console_action_->setCheckable(true);
  console_action_->setChecked(true);
  auto *reset_action = new QAction("Reset UI", this);
  reset_action->setIcon(icon(Glyph::Reset));
  reset_action->setObjectName("resetReferenceLayout");
  addActions({shelf_action_, console_action_, reset_action});

  auto *viewport_menu = menuBar()->addMenu("&Viewport");
  viewport_menu->setObjectName("viewportMenu");
  quick_mask_enable_ = viewport_menu->addAction("Enable quick mask");
  quick_mask_enable_->setObjectName("enableQuickMask");
  quick_mask_enable_->setCheckable(true);
  quick_mask_edit_action_ = viewport_menu->addAction("Edit quick mask");
  quick_mask_edit_action_->setObjectName("editQuickMask");
  quick_mask_edit_action_->setCheckable(true);
  quick_mask_invert_ = viewport_menu->addAction("Invert quick mask");
  quick_mask_invert_->setObjectName("invertQuickMask");
  quick_mask_invert_->setShortcut(QKeySequence(Qt::Key_I));
  quick_mask_clear_ = viewport_menu->addAction("Clear quick mask");
  quick_mask_clear_->setObjectName("clearQuickMask");
  quick_mask_clear_->setShortcut(QKeySequence(Qt::Key_Y));
  for (auto *action :
       {quick_mask_enable_, quick_mask_edit_action_, quick_mask_invert_, quick_mask_clear_})
    addAction(action);
  connect(quick_mask_enable_, &QAction::triggered, this, [this] {
    if (callbacks_.quick_mask_action)
      callbacks_.quick_mask_action(0);
  });
  connect(quick_mask_edit_action_, &QAction::triggered, this, [this] {
    if (callbacks_.quick_mask_action)
      callbacks_.quick_mask_action(1);
  });
  connect(quick_mask_invert_, &QAction::triggered, this, [this] {
    if (callbacks_.quick_mask_action)
      callbacks_.quick_mask_action(2);
  });
  connect(quick_mask_clear_, &QAction::triggered, this, [this] {
    if (callbacks_.quick_mask_action)
      callbacks_.quick_mask_action(3);
  });
  set_quick_mask_state(false, false);

  auto *window_menu = menuBar()->addMenu("&Window");
  auto *views_menu = window_menu->addMenu("Views");
  views_menu->addActions({shelf_action_, console_action_});
  properties_action_ = new QAction("Properties", this);
  properties_action_->setObjectName("toggleProperties");
  properties_action_->setCheckable(true);
  properties_action_->setChecked(true);
  views_menu->addAction(properties_action_);
  window_menu->addSeparator();
  window_menu->addAction(reset_action);

  auto *help_menu = menuBar()->addMenu("&Help");
  help_menu->setObjectName("helpMenu");
  auto *documentation = help_menu->addAction("Documentation");
  documentation->setObjectName("openDocumentation");
  connect(documentation, &QAction::triggered, this, [] {
    QDesktopServices::openUrl(QUrl(documentation_url));
  });
  auto *report_bug = help_menu->addAction("Report Bug…");
  report_bug->setObjectName("reportBug");
  connect(report_bug, &QAction::triggered, this, [this] { BugReportDialog(this).exec(); });

  auto *workspace_menu = new QMenu(this);
  workspace_menu->addActions({shelf_action_, console_action_, properties_action_});
  workspace_menu->addSeparator();
  workspace_menu->addAction(reset_action);

