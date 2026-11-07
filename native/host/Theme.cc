#include "Theme.hh"
#include "SliderHitArea.hh"
#include <QApplication>
#include <QComboBox>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QPointer>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QPainter>
#include <QPainterPath>
#include <limits>

namespace bts::theme {
namespace {
class OptionStyle final : public QProxyStyle {
 public:
  OptionStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
  void drawControl(ControlElement element, const QStyleOption *option,
                   QPainter *painter, const QWidget *widget = nullptr) const override {
    const auto *menu = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
    if (element != CE_MenuItem || !menu || qobject_cast<const QComboBox *>(widget) || menu->checkType == QStyleOptionMenuItem::NotCheckable) {
      QProxyStyle::drawControl(element, option, painter, widget); return;
    }
    auto text = *menu;
    text.checked = false;
    QProxyStyle::drawControl(element, &text, painter, widget);
    QStyleOption indicator;
    indicator.state = menu->state;
    indicator.state &= ~(State_On | State_Off);
    indicator.state |= menu->checked ? State_On : State_Off;
    if(menu->state & State_Selected) indicator.state |= State_MouseOver;
    const QRect logical(menu->rect.left()+7, menu->rect.center().y()-7, 16, 16);
    indicator.rect = visualRect(menu->direction, menu->rect, logical);
    drawPrimitive(menu->checkType == QStyleOptionMenuItem::Exclusive ? PE_IndicatorRadioButton : PE_IndicatorCheckBox,
                  &indicator, painter, widget);
  }
  void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                     QPainter *painter, const QWidget *widget = nullptr) const override {
    if (element != PE_IndicatorCheckBox && element != PE_IndicatorItemViewItemCheck &&
        element != PE_IndicatorRadioButton && element != PE_IndicatorMenuCheckMark) {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
      return;
    }
    const bool enabled = option->state & State_Enabled;
    const bool hovered = enabled && (option->state & (State_MouseOver | State_HasFocus));
    const bool checked = option->state & State_On;
    const bool mixed = option->state & State_NoChange;
    const bool radio = element == PE_IndicatorRadioButton;
    const QRectF box = QRectF(option->rect).adjusted(1, 1, -1, -1);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const bool texture_row=widget && widget->objectName()=="textureSetLoaded";
    painter->setPen(QPen(QColor(hovered ? Accent : texture_row ? Muted : OptionOutline), hovered ? 2 : 1));
    painter->setBrush(QColor(enabled ? OptionInner : Raised));
    if (radio) painter->drawEllipse(box);
    else painter->drawRoundedRect(box, RadiusPx, RadiusPx);
    if (checked || mixed || element == PE_IndicatorMenuCheckMark) {
      // XML's item is white; Mattias requests the XML turquoise accent for the mark.
      const QColor mark = enabled ? QColor(Accent) : QColor(Accent).lighter(125);
      painter->setPen(QPen(mark, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      if (radio) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(mark);
        painter->drawEllipse(box.adjusted(3, 3, -3, -3));
      } else if (mixed) {
        painter->drawLine(QPointF(box.left() + 3, box.center().y()), QPointF(box.right() - 3, box.center().y()));
      } else {
        QPainterPath check;
        check.moveTo(box.left() + box.width() * .22, box.top() + box.height() * .52);
        check.lineTo(box.left() + box.width() * .43, box.top() + box.height() * .73);
        check.lineTo(box.left() + box.width() * .80, box.top() + box.height() * .27);
        painter->drawPath(check);
      }
    }
    painter->restore();
  }
};
// Catch the small halo outside a widget too, without growing its painted handle.
// Forward a centered press and retain the pointer offset throughout the gesture:
// grabbing the halo must never jump the value or change native undo/signals.
class SliderHitFilter final : public QObject {
  QPointer<QWidget> active_;
  QPointF offset_;
  bool forwarding_ = false;
 public:
  explicit SliderHitFilter(QObject *parent) : QObject(parent) {}
  bool eventFilter(QObject *object, QEvent *event) override {
    if (forwarding_) return false;
    const auto type = event->type();
    const bool tablet = type == QEvent::TabletPress || type == QEvent::TabletMove || type == QEvent::TabletRelease;
    const bool mouse = type == QEvent::MouseButtonPress || type == QEvent::MouseMove || type == QEvent::MouseButtonRelease;
    if (!mouse && !tablet) return false;
    auto *receiver = qobject_cast<QWidget *>(object);
    if (!receiver) return false;
    auto *pointer = static_cast<QSinglePointEvent *>(event);
    const bool press = type == QEvent::MouseButtonPress || type == QEvent::TabletPress;
    const bool release = type == QEvent::MouseButtonRelease || type == QEvent::TabletRelease;
    if (release && pointer->button() != Qt::LeftButton) return false;
    if (press) {
      if (pointer->button() != Qt::LeftButton || active_) return false;
      QWidget *best = nullptr;
      QPointF center;
      double distance = std::numeric_limits<double>::max();
      auto *window = receiver->window();
      auto candidates = window->findChildren<QWidget *>();
      candidates.prepend(window);
      for (auto *widget : candidates) {
        if (widget->window() != window || !widget->isVisible() || !widget->isEnabled()) continue;
        const auto local = widget->mapFromGlobal(pointer->globalPosition());
        for (const auto &visual : slider_handles(widget)) {
          if (!doubled_handle(visual).contains(local) || !widget->visibleRegion().contains(visual.center().toPoint())) continue;
          const auto delta = local - visual.center();
          const double d = delta.x()*delta.x() + delta.y()*delta.y();
          if (d < distance) { best = widget; center = visual.center(); distance = d; }
        }
      }
      if (!best) return false; // Track clicks, keyboard and wheel retain their existing behavior.
      active_ = best;
      offset_ = best->mapFromGlobal(pointer->globalPosition()) - center;
      best->grabMouse();
    }
    if (!active_) return false;
    auto target = active_;
    const auto position = target->mapFromGlobal(pointer->globalPosition()) - offset_;
    const auto mapped = press ? QEvent::MouseButtonPress : release ? QEvent::MouseButtonRelease : QEvent::MouseMove;
    QMouseEvent forwarded(mapped, position, target->mapToGlobal(position),
                          press || release ? Qt::LeftButton : Qt::NoButton,
                          release ? Qt::NoButton : Qt::LeftButton, pointer->modifiers());
    forwarding_ = true;
    QCoreApplication::sendEvent(target, &forwarded);
    forwarding_ = false;
    if (release) {
      active_.clear();
      if (target && QWidget::mouseGrabber() == target) target->releaseMouse();
    }
    event->accept();
    return true;
  }
};
}

QPalette palette()
{
  QPalette p;
  const auto set = [&](QPalette::ColorRole role, const char *token) { p.setColor(role, QColor(token)); };
  set(QPalette::Window, Surface); set(QPalette::WindowText, Text);
  set(QPalette::Base, Canvas); set(QPalette::AlternateBase, ListAlternate);
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
  app.installEventFilter(new SliderHitFilter(&app));
  app.setStyle(new OptionStyle);
  app.setPalette(palette());
  auto menu_palette=palette();menu_palette.setColor(QPalette::Window,QColor(Menu));
  app.setPalette(menu_palette,"QMenu");
  app.setStyleSheet(qss(QString::fromLatin1(R"(
  QWidget { color: @Text@; }
  QDialog, QMessageBox, QMainWindow, QDockWidget, QScrollArea, QStackedWidget {
    background: @Surface@;
  }
  QWidget:disabled { color: @Disabled@; }
  QDockWidget::title { background: @Surface@; color: @Heading@; padding: 6px; }
  QToolBar { background: @Surface@; border: 0; spacing: 4px; }
  QToolBar::separator { background: @Border@; width: 1px; margin: 4px; }
  QGroupBox { background: @Panel@; border: 1px solid @Border@; border-radius: @Radius@;
    margin-top: 12px; padding-top: 8px; }
  QGroupBox::title { color: @Heading@; subcontrol-origin: margin; left: 8px; padding: 0 4px; }
  QPushButton { background: @Raised@; color: @ButtonText@; border: 1px solid @Border@;
    border-radius: @Radius@; padding: 4px 8px; min-height: 20px; }
  QPushButton:hover { background: @Hover@; }
  QPushButton:pressed, QPushButton:checked { background: @Selection@; color: @Heading@; }
  QPushButton:default, QPushButton:focus { border-color: @Accent@; }
  QPushButton:disabled { background: @Surface@; color: @Disabled@; }
  QAbstractItemView { background: @Canvas@; alternate-background-color: @ListAlternate@;
    color: @Text@; border: 1px solid @Border@; selection-background-color: @Selection@;
    selection-color: @Heading@; outline: none; }
  QAbstractItemView::item:hover { background: @Hover@; }
  QAbstractItemView::item:selected { background: @ListSelection@; color: @Heading@; }
  QHeaderView, QHeaderView::section, QTableCornerButton::section {
    background: @Surface@; color: @Heading@; border: 0; padding: 4px;
  }
  QTextEdit, QPlainTextEdit, QLineEdit, QComboBox, QAbstractSpinBox {
    background: @Canvas@; color: @Text@; border: 1px solid @Border@; border-radius: @Radius@;
    selection-background-color: @Selection@; selection-color: @Heading@; padding: 2px 4px;
  }
  QAbstractSpinBox:focus { border-color: @Accent@; }
  QComboBox QAbstractItemView { background: @Canvas@; color: @Text@;
    selection-background-color: @Selection@; selection-color: @Heading@; }
  QCheckBox, QRadioButton { spacing: 6px; background: transparent; }
  QCheckBox::indicator, QRadioButton::indicator, QGroupBox::indicator,
  QAbstractItemView::indicator { width: 16px; height: 16px; }
  QProgressBar { background: @Canvas@; color: @Heading@; border: 1px solid @Border@;
    border-radius: @Radius@; text-align: center; }
  QProgressBar::chunk { background: @Selection@; }
  QLabel#projectSection { color: @Heading@; font-weight: 600; }
  QLabel#projectValidationError { color: @Error@; }
  QLabel#uvWorkflowDescription { color: @Muted@; }

  QMainWindow, QWidget#workspaceRoot, QWidget#toolRail, QWidget#utilityRail,
  QWidget#materialProperties, QScrollArea#propertiesScroll {
    background: @Surface@; color: @Heading@;
  }
  QWidget#contextToolbar, QWidget#shelfFilter { background: @Header@; }
  QWidget#dockHeader { background: @Surface@; border-bottom: 1px solid @Separator@; }
  QLabel#dockTitle { color: @Heading@; font-size: 11px; font-weight: 600; }
  QLabel#sectionTitle { color: @Heading@; font-weight: 600; }
  QLabel#propertySectionTitle { color: @Muted@; font-weight: 600; letter-spacing: 0.5px; }
  QLabel { color: @Text@; background: transparent; }
  QLabel:disabled { color: @Muted@; }
  QSplitter::handle { background: @Separator@; }
  QSplitter::handle:hover { background: @Accent@; }
  QToolButton { background: @Tool@; border: 1px solid transparent; border-radius: @Radius@; color: @Text@; }
  QToolButton:hover { background: @Hover@; border-color: @Hover@; }
  QToolButton:checked { background: @Selection@; color: @Heading@; border-color: @Accent@; }
  QWidget#assetShelf, QWidget#textureSetPanel { background: @Canvas@; }
  QListWidget#shelfCategories { background: @Canvas@; border-bottom: 1px solid @Separator@; padding: 2px; }
  QListWidget#shelfCategories::item { background: transparent; border: none; padding: 0; }
  QListWidget#shelfAssets { background: @Canvas@; padding: 2px; }
  QListWidget#shelfAssets::item { padding: 0; border: 0; }
  QToolButton:disabled { color: @Muted@; background: transparent; border-color: transparent; }
  QToolButton:checked:disabled, QPushButton:checked:disabled,
  QCheckBox[fillChannelToggle="true"]:checked:disabled {
    background: @ListSelection@; border-color: @Accent@; color: @Disabled@;
  }
  QToolButton::menu-indicator { image: none; }
  QToolButton#shelfCategoryChip { background: @Canvas@; border: 1px solid @Hover@; padding: 2px 4px; }
  QLineEdit, QComboBox, QDoubleSpinBox {
    background: @Canvas@; color: @Text@; border: 1px solid @Border@;
    border-radius: @Radius@; min-height: 20px; padding: 0 4px;
  }
  QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: @Accent@; }
  QLineEdit:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @Muted@; }
  QComboBox::drop-down { width: 14px; border: none; }
  QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 12px; border: none; }
  QTabWidget::pane { background: @Surface@; border: none; border-top: 1px solid @Separator@; }
  QTabBar::tab {
    color: @TabText@; background: @Tab@; border: 1px solid transparent;
    padding: 0 8px; height: 32px; font-size: 10px; font-weight: 600;
  }
  QTabBar::tab:selected { background: @Surface@; border-top-color: @Accent@; color: @Heading@; }
  QTabBar::tab:hover { background: @Hover@; }
  QListWidget { background: @Canvas@; color: @Heading@; border: none; outline: none; }
  QListWidget::item { min-height: 22px; border: 1px solid transparent; padding: 0 4px; }
  QListWidget::item:selected { background: @ListSelection@; border-color: @Accent@; color: @Heading@; }
  QListWidget::item:hover { background: @Hover@; }
  QListWidget#layerStack::item { padding: 0; }
  QListWidget#shelfCategories { border-right: 1px solid @Separator@; font-size: 10px; }
  QListWidget#shelfCategories::item { min-height: 18px; }
  QPlainTextEdit { background: @Canvas@; color: @Text@; border: 1px solid @Border@; }
  QSlider::groove:horizontal { height: 2px; background: @Border@; }
  QSlider::handle:horizontal {
    width: 7px; height: 7px; margin: -3px 0; border-radius: 4px; background: @Accent@;
  }
  QSlider::handle:horizontal:disabled { background: @Muted@; }
  /* The filled part of a slider shows a value; it is not the thing being acted
     on, and a panel of them carried every bit of accent on screen at once. The
     accent stays on the handle. docs/design/ui.md §3.4, §7.4. */
  QSlider::sub-page:horizontal { background: @Number@; }
  QSlider::sub-page:horizontal:disabled { background: @Hover@; }
  QScrollBar:vertical { background: @Surface@; width: 8px; margin: 0; }
  QScrollBar:horizontal { background: @Surface@; height: 8px; margin: 0; }
  QScrollBar::handle { background: @ScrollHandle@; min-height: 20px; min-width: 20px; }
  QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
  QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
  QStatusBar { background: @Status@; color: @Text@; border-top: 1px solid @Separator@; }
  QStatusBar::item { border: none; }
  /* QMenu uses the palette and OptionStyle, including unchecked indicators. */
  QMenuBar { background: @MenuBar@; color: @Heading@; border-bottom: 1px solid @Separator@; }
  QMenuBar::item { padding: 4px 8px; background: transparent; }
  QMenuBar::item:selected { background: @Selection@; }
  QToolTip { background: @Menu@; color: @Heading@; border: 1px solid @Hover@; }
  QCheckBox[fillChannelToggle="true"] {
    spacing: 6px; padding: 4px 6px; color: @Text@; background: @Hover@; border: 1px solid @Hover@;
    border-radius: @Radius@;
  }
  QCheckBox[fillChannelToggle="true"]:checked { background: @Selection@; border-color: @Accent@; color: @Heading@; }
  QToolButton[artistChannelToggle="true"] { background: @Hover@; border: 1px solid @Hover@; color: @Text@; }
  QToolButton[artistChannelToggle="true"]:checked { background: @Selection@; border-color: @Accent@; color: @Heading@; }
  QToolButton#fillMaterialMode, QToolButton[fillChannelHeader="true"] {
    background: @Hover@; border: 1px solid @Hover@; padding: 4px 8px; text-align: left;
  }
  QPushButton#colorSwatch { min-height: 28px; border: 1px solid @Hover@; }
  QToolButton#fillSettingsTab, QToolButton#fillMaterialTab { min-width: 28px; min-height: 20px; }
  QDoubleSpinBox#metallicControl, QDoubleSpinBox#roughnessControl, QDoubleSpinBox#materialBump,
  QDoubleSpinBox#materialDisplacement, QDoubleSpinBox#materialAmbientOcclusion, QDoubleSpinBox[userChannelValue="true"] {
    background: transparent; border: none; padding: 0; min-height: 16px;
  }
  QLineEdit, QTextEdit, QPlainTextEdit { background: @Input@; }
  QAbstractSpinBox, QDoubleSpinBox { background: @Number@; }
  QComboBox { background: @MenuButton@; }
  QWidget#toolRail QToolButton, QWidget#utilityRail QToolButton { background: @ToolbarItem@; }
  QWidget#toolRail QToolButton:checked, QWidget#utilityRail QToolButton:checked { background: @Selection@; }
  QScrollBar::handle:hover { background: @ScrollHover@; }
)")));
}
} // namespace bts::theme
