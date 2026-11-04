#include "Theme.hh"
#include <QApplication>

namespace bts::theme {
QPalette palette()
{
  QPalette p;
  const auto set = [&](QPalette::ColorRole role, const char *token) { p.setColor(role, QColor(token)); };
  set(QPalette::Window, Surface); set(QPalette::WindowText, Text);
  set(QPalette::Base, Canvas); set(QPalette::AlternateBase, Surface);
  set(QPalette::Text, Text); set(QPalette::Button, Raised); set(QPalette::ButtonText, ButtonText);
  set(QPalette::BrightText, Heading); set(QPalette::Highlight, Selection);
  set(QPalette::HighlightedText, Heading); set(QPalette::Link, Accent);
  set(QPalette::LinkVisited, Accent); set(QPalette::ToolTipBase, Menu);
  set(QPalette::ToolTipText, Heading); set(QPalette::PlaceholderText, Muted);
  set(QPalette::Light, Border); set(QPalette::Midlight, Hover);
  set(QPalette::Mid, Border); set(QPalette::Dark, Canvas); set(QPalette::Shadow, Separator);
  set(QPalette::Accent, Accent);
  for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
    p.setColor(QPalette::Disabled, role, QColor(Disabled));
  p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(Raised));
  p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(Muted));
  return p;
}
void apply(QApplication &app)
{
  app.setStyle("Fusion");
  app.setPalette(palette());
  app.setStyleSheet(qss(QString::fromLatin1(R"(
  QWidget { color: @Text@; }
  QDialog, QMessageBox, QMainWindow, QDockWidget, QScrollArea, QStackedWidget {
    background: @Surface@;
  }
  QLabel { background: transparent; }
  QWidget:disabled { color: @Disabled@; }
  QDockWidget::title { background: @Surface@; color: @Heading@; padding: 6px; }
  QToolBar { background: @Surface@; border: 0; spacing: 3px; }
  QToolBar::separator { background: @Border@; width: 1px; margin: 4px; }
  QGroupBox { background: @Panel@; border: 1px solid @Border@; margin-top: 12px; padding-top: 8px; }
  QGroupBox::title { color: @Heading@; subcontrol-origin: margin; left: 8px; padding: 0 4px; }
  QPushButton { background: @Raised@; color: @ButtonText@; border: 1px solid @Border@;
    border-radius: 2px; padding: 4px 10px; min-height: 20px; }
  QPushButton:hover { background: @Hover@; }
  QPushButton:pressed, QPushButton:checked { background: @Selection@; color: @Heading@; }
  QPushButton:default, QPushButton:focus { border-color: @Accent@; }
  QPushButton:disabled { background: @Surface@; color: @Disabled@; }
  QAbstractItemView { background: @Canvas@; alternate-background-color: @Surface@;
    color: @Text@; border: 1px solid @Border@; selection-background-color: @Selection@;
    selection-color: @Heading@; outline: none; }
  QAbstractItemView::item:hover { background: @Hover@; }
  QAbstractItemView::item:selected { background: @ListSelection@; color: @Heading@; }
  QHeaderView, QHeaderView::section, QTableCornerButton::section {
    background: @Surface@; color: @Heading@; border: 0; padding: 4px;
  }
  QTextEdit, QPlainTextEdit, QLineEdit, QComboBox, QAbstractSpinBox {
    background: @Canvas@; color: @Text@; border: 1px solid @Border@;
    selection-background-color: @Selection@; selection-color: @Heading@; padding: 2px 4px;
  }
  QAbstractSpinBox:focus { border-color: @Accent@; }
  QComboBox QAbstractItemView { background: @Canvas@; color: @Text@;
    selection-background-color: @Selection@; selection-color: @Heading@; }
  QCheckBox, QRadioButton { spacing: 6px; background: transparent; }
  /* Keep Fusion's check marks and radio dots; their colors inherit the palette. */
  QProgressBar { background: @Canvas@; color: @Heading@; border: 1px solid @Border@; text-align: center; }
  QProgressBar::chunk { background: @Selection@; }
  QScrollBar::handle:hover { background: @ScrollHover@; }
  QLabel#projectSection { color: @Heading@; font-weight: 600; }
  QLabel#projectValidationError { color: @Error@; }
  QLabel#uvWorkflowDescription { color: @Muted@; }

  QMainWindow, QWidget#workspaceRoot, QWidget#toolRail, QWidget#utilityRail,
  QWidget#materialProperties, QScrollArea#propertiesScroll {
    background: @Canvas@; color: @Heading@;
  }
  QWidget#materialProperties, QScrollArea#propertiesScroll { background: @Surface@; }
  QWidget#contextToolbar, QWidget#shelfFilter { background: @Header@; }
  QWidget#dockHeader { background: @Surface@; border-bottom: 1px solid @Separator@; }
  QLabel#dockTitle { color: @Heading@; font-size: 11px; font-weight: 600; }
  QLabel#sectionTitle { color: @Heading@; font-weight: 600; }
  QLabel { color: @Text@; background: transparent; }
  QLabel:disabled { color: @Muted@; }
  QSplitter::handle { background: @Separator@; }
  QSplitter::handle:hover { background: @Accent@; }
  QToolButton { background: @ToolbarItem@; border: 1px solid transparent; border-radius: 0; color: @Text@; }
  QToolButton:hover { background: @Hover@; border-color: @Hover@; }
  QToolButton:checked { background: @Selection@; color: @Heading@; border-color: @Accent@; }
  QWidget#assetShelf, QWidget#textureSetPanel { background: @Canvas@; }
  QListWidget#shelfCategories { background: @Canvas@; border-bottom: 1px solid @Separator@; padding: 2px; }
  QListWidget#shelfCategories::item { background: transparent; border: none; padding: 0; }
  QListWidget#shelfAssets { background: @Canvas@; padding: 1px; }
  QListWidget#shelfAssets::item { padding: 0; border: 0; }
  QToolButton:disabled { color: @Muted@; background: transparent; border-color: transparent; }
  QToolButton::menu-indicator { image: none; }
  QToolButton#shelfCategoryChip { background: @Canvas@; border: 1px solid @Hover@; padding: 2px 4px; }
  QLineEdit, QComboBox, QDoubleSpinBox {
    background: @Canvas@; color: @Text@; border: 1px solid @Border@;
    border-radius: 1px; min-height: 20px; padding: 0 4px;
  }
  QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: @Accent@; }
  QLineEdit:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @Muted@; }
  QComboBox::drop-down { width: 14px; border: none; }
  QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 12px; border: none; }
  QTabWidget::pane { background: @Surface@; border: none; border-top: 1px solid @Separator@; }
  QTabBar::tab {
    color: @TabText@; background: @Tab@; border: 1px solid transparent;
    padding: 0 7px; height: 32px; font-size: 10px; font-weight: 600;
  }
  QTabBar::tab:selected { background: @Surface@; border-top-color: @Accent@; color: @Heading@; }
  QTabBar::tab:hover { background: @Hover@; }
  QListWidget { background: @Canvas@; color: @Heading@; border: none; outline: none; }
  QListWidget::item { min-height: 22px; border: 1px solid transparent; padding: 0 5px; }
  QListWidget::item:selected { background: @ListSelection@; border-color: @Accent@; color: @Heading@; }
  QListWidget::item:hover { background: @Hover@; }
  QListWidget#layerStack::item { padding: 0; }
  QListWidget#shelfCategories { border-right: 1px solid @Separator@; font-size: 10px; }
  QListWidget#shelfCategories::item { min-height: 18px; }
  QPlainTextEdit { background: @Canvas@; color: @Text@; border: 1px solid @Border@; }
  QSlider::groove:horizontal { height: 2px; background: @Muted@; }
  QSlider::handle:horizontal {
    width: 7px; height: 7px; margin: -3px 0; border-radius: 4px; background: @Text@;
  }
  QSlider::handle:horizontal:disabled { background: @Muted@; }
  QSlider::sub-page:horizontal { background: @Accent@; }
  QSlider::sub-page:horizontal:disabled { background: @Hover@; }
  QScrollBar:vertical { background: @Surface@; width: 8px; margin: 0; }
  QScrollBar:horizontal { background: @Surface@; height: 8px; margin: 0; }
  QScrollBar::handle { background: @ScrollHandle@; min-height: 20px; min-width: 20px; }
  QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
  QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
  QStatusBar { background: @Status@; color: @Text@; border-top: 1px solid @Separator@; }
  QStatusBar::item { border: none; }
  QMenu { background: @Menu@; color: @Heading@; border: 1px solid @Hover@; }
  QMenu::item { padding: 6px 24px; }
  QMenu::item:selected { background: @Selection@; }
  QMenu::separator { height: 1px; background: @Hover@; margin: 4px; }
  QMenuBar { background: @MenuBar@; color: @Heading@; border-bottom: 1px solid @Separator@; }
  QMenuBar::item { padding: 4px 8px; background: transparent; }
  QMenuBar::item:selected { background: @Selection@; }
  QToolTip { background: @Menu@; color: @Heading@; border: 1px solid @Hover@; }
  QCheckBox[fillChannelToggle="true"] {
    spacing: 0; padding: 3px 6px; color: @Text@; background: @Hover@; border: 1px solid @Hover@;
  }
  QCheckBox[fillChannelToggle="true"]::indicator { width: 0; height: 0; }
  QCheckBox[fillChannelToggle="true"]:checked { background: @Selection@; border-color: @Accent@; color: @Heading@; }
  QToolButton[artistChannelToggle="true"] { background: @Hover@; border: 1px solid @Hover@; color: @Text@; }
  QToolButton[artistChannelToggle="true"]:checked { background: @Selection@; border-color: @Accent@; color: @Heading@; }
  QToolButton#fillMaterialMode, QToolButton[fillChannelHeader="true"] {
    background: @Hover@; border: 1px solid @Hover@; padding: 4px 8px; text-align: left;
  }
  QPushButton#colorSwatch { min-height: 28px; border: 1px solid @Hover@; }
  QToolButton#fillSettingsTab, QToolButton#fillMaterialTab { min-width: 28px; min-height: 24px; }
  QDoubleSpinBox#metallicControl, QDoubleSpinBox#roughnessControl, QDoubleSpinBox#materialBump,
  QDoubleSpinBox#materialDisplacement, QDoubleSpinBox#materialAmbientOcclusion, QDoubleSpinBox[userChannelValue="true"] {
    background: transparent; border: none; padding: 0; min-height: 14px;
  }
  QLineEdit, QTextEdit, QPlainTextEdit { background: @Input@; }
  QAbstractSpinBox, QDoubleSpinBox { background: @Number@; }
  QComboBox { background: @Menu@; }
  QScrollBar::handle:hover { background: @ScrollHover@; }
)")));
}
} // namespace bts::theme
