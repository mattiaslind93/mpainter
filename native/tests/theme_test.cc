#include "Theme.hh"
#include "SliderHitArea.hh"
#include "FillGradient.hh"
#include "LevelsRampEditor.hh"
#include "CompareRampEditor.hh"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <cassert>
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  bts::theme::apply(app);
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
