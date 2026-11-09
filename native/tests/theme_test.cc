// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "SliderHitArea.hh"
#include "FillGradient.hh"
#include "LevelsRampEditor.hh"
#include "CompareRampEditor.hh"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QAbstractItemView>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTreeWidget>
#include <QRadioButton>
#include <QToolButton>
#include <QGridLayout>
#include <QActionGroup>
#include <QStyleOptionButton>
#include <QPainter>
#include <QStyleOptionMenuItem>
#include <cassert>
static void dropdown_pixels(QApplication &app)
{
  for (int delegate = 0; delegate < 3; ++delegate) for (bool rtl : {false, true}) {
    QDialog dialog;
    dialog.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
    QGridLayout layout(&dialog);
    QComboBox combo;
    if (delegate == 1) combo.setEditable(true);
    if (delegate == 2) combo.setView(new QListView);
    combo.addItems({"128", "256", "512", "1024", "2048", "4096", "8192"});
    layout.addWidget(&combo);
    dialog.resize(320, 100);
    dialog.show();
    app.processEvents();
    combo.showPopup();
    app.processEvents();
    const auto image = combo.view()->viewport()->grab().toImage();
    const auto screenshot = qEnvironmentVariable("BTS_DROPDOWN_SCREENSHOT");
    if (!screenshot.isEmpty() && delegate == 0 && !rtl) assert(combo.view()->window()->grab().save(screenshot));
    for (int row = 0; row < combo.count(); ++row) {
      const auto rect = combo.view()->visualRect(combo.model()->index(row, 0)).intersected(image.rect());
      int text = 0, inner = 0;
      for (int y = rect.top(); y <= rect.bottom(); ++y)
        for (int x = rect.left(); x <= rect.right(); ++x) {
          const auto color = image.pixelColor(x, y);
          text += color.red() > 140 && color.green() > 140 && color.blue() > 140;
          inner += color == QColor(bts::theme::OptionInner);
        }
      qInfo("Dropdown delegate=%d rtl=%d row=%d: text=%d indicatorFill=%d", delegate, rtl, row, text, inner);
      assert(text > 10);
      assert(inner < 20);
    }
    combo.hidePopup();
  }
}
static void indicator_pixels(QApplication &app)
{
 for(auto primitive:{QStyle::PE_IndicatorCheckBox,QStyle::PE_IndicatorItemViewItemCheck,QStyle::PE_IndicatorRadioButton})
  for(bool enabled:{false,true})for(bool checked:{false,true})for(bool hover:{false,true}){
   QImage image(20,20,QImage::Format_ARGB32);image.fill(QColor(bts::theme::Surface));QPainter painter(&image);
   QStyleOptionButton option;option.rect={2,2,16,16};option.state=(enabled?QStyle::State_Enabled:QStyle::State_None)|(checked?QStyle::State_On:QStyle::State_Off)|(hover?QStyle::State_MouseOver:QStyle::State_None);
   app.style()->drawPrimitive(primitive,&option,&painter);painter.end();
   int changed=0,accent=0;for(int y=3;y<17;++y)for(int x=3;x<17;++x){const auto c=image.pixelColor(x,y);changed+=c!=QColor(bts::theme::Surface);if(x>=6&&x<14&&y>=6&&y<14)accent+=c.green()>c.red()+20&&c.blue()>c.red()+10;}
   assert(changed>50);assert(checked?accent>=3:accent==0);
  }
 for(bool checked:{false,true}){
  QImage image(160,26,QImage::Format_ARGB32);image.fill(QColor(bts::theme::Menu));QPainter painter(&image);QStyleOptionMenuItem option;option.rect=image.rect();option.state=QStyle::State_Enabled;option.menuItemType=QStyleOptionMenuItem::Normal;option.checkType=QStyleOptionMenuItem::NonExclusive;option.checked=checked;option.text="Menu choice";option.maxIconWidth=16;
  app.style()->drawControl(QStyle::CE_MenuItem,&option,&painter);painter.end();int gray=0,accent=0;for(int y=6;y<19;++y)for(int x=8;x<22;++x){const auto c=image.pixelColor(x,y);gray+=c==QColor(bts::theme::OptionInner);accent+=c.green()>c.red()+20&&c.blue()>c.red()+10;}assert(gray>20);assert(checked?accent>3:accent==0);
 }
}
static void indicator_gallery(QApplication &app)
{
  QDialog gallery;
  gallery.setWindowTitle("Turquoise Dark · Checkbox states");
  auto *layout = new QGridLayout(&gallery);
  const QStringList states{"Off", "On", "Mixed", "Disabled off", "Disabled on", "Hover off", "Hover on"};
  for (int column = 0; column < states.size(); ++column) {
    layout->addWidget(new QLabel(states[column], &gallery), 0, column);
    auto *check = new QCheckBox("Checkbox", &gallery);
    check->setCheckState(column == 2 ? Qt::PartiallyChecked : column == 1 || column == 4 || column == 6 ? Qt::Checked : Qt::Unchecked);
    check->setEnabled(column != 3 && column != 4);
    check->setAttribute(Qt::WA_UnderMouse, column >= 5);
    layout->addWidget(check, 1, column);
    auto *radio = new QRadioButton("Radio", &gallery);
    radio->setAutoExclusive(false);
    radio->setChecked(column == 1 || column == 4 || column == 6);
    radio->setEnabled(check->isEnabled());
    radio->setAttribute(Qt::WA_UnderMouse, column >= 5);
    layout->addWidget(radio, 2, column);
    auto *toggle = new QToolButton(&gallery);
    toggle->setText("Toggle"); toggle->setCheckable(true); toggle->setChecked(radio->isChecked());
    toggle->setEnabled(check->isEnabled());
    toggle->setAttribute(Qt::WA_UnderMouse, column >= 5);
    layout->addWidget(toggle, 3, column);
    auto *channel = new QCheckBox("Channel", &gallery);
    channel->setProperty("fillChannelToggle", true); channel->setChecked(radio->isChecked());
    channel->setEnabled(check->isEnabled());
    layout->addWidget(channel, 4, column);
  }
  auto *tree = new QTreeWidget(&gallery);
  tree->setHeaderLabel("Tree / outliner");
  auto *list = new QListWidget(&gallery);
  auto *menu = new QMenu(&gallery);
  menu->setWindowFlags(Qt::Widget);
  auto *radios = new QActionGroup(menu);
  for (int row = 0; row < 5; ++row) {
    auto *item = new QTreeWidgetItem(tree, {states[row]});
    item->setCheckState(0, row == 2 ? Qt::PartiallyChecked : row == 1 || row == 4 ? Qt::Checked : Qt::Unchecked);
    item->setDisabled(row >= 3);
    auto *entry = new QListWidgetItem(states[row], list);
    entry->setCheckState(item->checkState(0));
    if (row >= 3) entry->setFlags(entry->flags() & ~Qt::ItemIsEnabled);
    auto *action = menu->addAction("Menu " + states[row]);
    action->setCheckable(true); action->setChecked(row == 1 || row == 4); action->setEnabled(row < 3);
  }
  for (bool checked : {false, true}) {
    auto *action = menu->addAction(checked ? "Exclusive on" : "Exclusive off");
    action->setCheckable(true); radios->addAction(action); action->setChecked(checked);
  }
  layout->addWidget(tree, 5, 0, 1, 3);
  layout->addWidget(list, 5, 3, 1, 2);
  layout->addWidget(menu, 5, 5, 1, 2);
  auto *group = new QGroupBox("Checkable group", &gallery);
  group->setCheckable(true);
  layout->addWidget(group, 6, 0, 1, 3);
  gallery.resize(1120, 440);
  gallery.show();
  menu->show();
  app.processEvents();
  const auto menu_image=menu->grab().toImage();
  for(auto *action:menu->actions())if(action->isCheckable()){
    const auto area=menu->actionGeometry(action);int box=0;
    for(int y=area.center().y()-5;y<=area.center().y()+5;++y)for(int x=8;x<22;++x){const auto color=menu_image.pixelColor(x,y);box+=color==QColor(bts::theme::OptionInner)||color==QColor(bts::theme::Raised);}
    assert(box>12); // Actual styled QMenu, including unchecked/exclusive/disabled rows.
  }
  const auto screenshot = qEnvironmentVariable("BTS_CHECKBOX_SCREENSHOT");
  if (!screenshot.isEmpty()) assert(gallery.grab().save(screenshot));
}
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  bts::theme::apply(app);
  dropdown_pixels(app);
  indicator_pixels(app);
  indicator_gallery(app);
  assert(!app.styleSheet().contains('@'));
  assert(app.palette().color(QPalette::Accent) == QColor(bts::theme::Accent));
  assert(app.palette().color(QPalette::Disabled, QPalette::Text) == QColor(bts::theme::Disabled));
  // Parentless dialogs and scroll contents must inherit the same dark palette.
  QDialog dialog; dialog.resize(600, 400);
  QScrollArea scroll(&dialog); scroll.setGeometry(10, 10, 200, 100);
  auto *body = new QWidget; scroll.setWidget(body); scroll.setWidgetResizable(true);
  QPushButton button("Create", &dialog); button.move(230, 20);
  QListWidget list(&dialog); list.setGeometry(230, 80, 200, 200);
  list.addItems({"Studio Environment", "Sphere"}); list.setCurrentRow(0);
  dialog.show(); app.processEvents();
  assert(dialog.palette().color(QPalette::Window) == QColor(bts::theme::Surface));
  assert(body->palette().color(QPalette::Window) == QColor(bts::theme::Surface));
  assert(dialog.grab().toImage().pixelColor(590, 390) == QColor(bts::theme::Surface));
  assert(list.viewport()->grab().toImage().pixelColor(10, 160) == QColor(bts::theme::Canvas));
  QCheckBox check("Import cameras", &dialog); check.setGeometry(20, 250, 150, 30);
  check.show(); app.processEvents();
  const auto unchecked = check.grab().toImage();
  check.setChecked(true); app.processEvents();
  assert(check.grab().toImage() != unchecked); // Native checked mark remains visible.
  QMenu menu; menu.addAction("New project"); menu.ensurePolished();
  assert(menu.palette().color(QPalette::Window) == QColor(bts::theme::Menu));
  // Expanded hit regions work beyond the widget, at endpoints, in RTL and
  // vertically, while preserving native pressed/released/value semantics.
  QWidget handles; handles.resize(600,400);
  QSlider slider(Qt::Horizontal,&handles); slider.setGeometry(30,30,240,8);
  slider.setRange(0,1000); slider.setFocusPolicy(Qt::NoFocus);
  handles.show(); app.processEvents();
  const auto send = [&](QWidget *receiver,QEvent::Type type,QPointF global) {
    QMouseEvent event(type,receiver->mapFromGlobal(global),global,
      type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
      type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(receiver,&event);
  };
  int pressed=0,released=0;
  QObject::connect(&slider,&QSlider::sliderPressed,[&]{++pressed;});
  QObject::connect(&slider,&QSlider::sliderReleased,[&]{++released;});
  for(auto orientation:{Qt::Horizontal,Qt::Vertical})for(bool rtl:{false,true})for(bool inverted:{false,true})for(int value:{0,500,1000}) {
    slider.setOrientation(orientation);slider.setLayoutDirection(rtl?Qt::RightToLeft:Qt::LeftToRight);slider.setInvertedAppearance(inverted);
    slider.setGeometry(30,30,orientation==Qt::Horizontal?240:8,orientation==Qt::Horizontal?8:240);
    slider.setValue(value);app.processEvents();
    const auto visual=bts::slider_handles(&slider).front(),hit=bts::doubled_handle(visual);
    assert(hit.width()==2*visual.width() && hit.height()==2*visual.height());
    // Diagonal corner proves both dimensions, outside the visible handle.
    const QPointF point=visual.center()+QPointF(visual.width()*.75,visual.height()*.75);
    assert(!visual.contains(point) && hit.contains(point));
    const auto outside=slider.mapToGlobal(hit.bottomRight()+QPointF(1,1));
    const int outsideCount=pressed;send(&handles,QEvent::MouseButtonPress,outside);
    assert(!slider.isSliderDown() && pressed==outsideCount);
    const auto global=slider.mapToGlobal(point);const auto before=slider.grab().toImage();
    const int previous=pressed;
    send(&handles,QEvent::MouseButtonPress,global);
    assert(slider.isSliderDown() && slider.value()==value && pressed==previous+1);
    assert(slider.grab().toImage()==before); // No visual enlargement.
    send(&handles,QEvent::MouseMove,global+(orientation==Qt::Horizontal?QPointF(20,0):QPointF(0,20)));
    send(&handles,QEvent::MouseButtonRelease,global);
    assert(!slider.isSliderDown() && released==pressed);
  }
  slider.setEnabled(false);
  auto global=slider.mapToGlobal(bts::slider_handles(&slider).front().center());
  const int previous=pressed;send(&handles,QEvent::MouseButtonPress,global);assert(pressed==previous);
  slider.hide();
  // Actual custom controls use exactly the same event routing.
  QSlider valueSlider(Qt::Horizontal);valueSlider.setRange(0,1000);valueSlider.setValue(500);
  QDoubleSpinBox number;number.setRange(0,1);number.setValue(.5);
  bts::FillGradient gradient(&valueSlider,&number,&handles);gradient.setGeometry(30,30,240,36);gradient.show();
  bts::HandleRamp ramp(&handles);ramp.setGeometry(30,110,240,48);ramp.positions={.5f};ramp.show();
  bts::CompareRampEditor curve(&handles);curve.setGeometry(30,200,240,100);curve.show();
  app.processEvents();
  for(QWidget *widget:{static_cast<QWidget *>(&gradient),static_cast<QWidget *>(&ramp),static_cast<QWidget *>(&curve)}) {
    const auto visual=bts::slider_handles(widget).at(widget==&curve?2:0);
    const auto global=widget->mapToGlobal(visual.center()+QPointF(visual.width()*.75,visual.height()*.75));
    const auto before=valueSlider.value();const auto rampBefore=ramp.positions;const auto curveBefore=curve.points;
    send(&handles,QEvent::MouseButtonPress,global);
    assert(valueSlider.value()==before && ramp.positions==rampBefore && curve.points==curveBefore);
    send(&handles,QEvent::MouseMove,global+QPointF(20,10));
    if(widget==&gradient)assert(valueSlider.value()>before);
    if(widget==&ramp)assert(ramp.positions[0]>.5f);
    if(widget==&curve)assert(curve.points!=curveBefore);
    send(&handles,QEvent::MouseButtonRelease,global+QPointF(20,10));
  }
}
