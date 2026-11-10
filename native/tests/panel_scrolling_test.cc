// SPDX-License-Identifier: GPL-3.0-or-later
#include "PanelScrolling.hh"
#include <QApplication>
#include <QComboBox>
#include <QDial>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cassert>
#include <iostream>
namespace {
void wheel(QWidget *widget)
{
  const QPointF point = widget->rect().center();
  QWheelEvent event(point,
                    widget->mapToGlobal(point),
                    {},
                    {0, -120},
                    Qt::NoButton,
                    Qt::NoModifier,
                    Qt::NoScrollPhase,
                    false);
  QCoreApplication::sendEvent(widget, &event);
}
void mouse(QWidget *widget,
           QEvent::Type type,
           QPointF p,
           Qt::MouseButton button,
           Qt::MouseButtons buttons)
{
  QMouseEvent e(type, p, widget->mapToGlobal(p), button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(widget, &e);
}
void pan(QWidget *widget, QPointF point)
{
  mouse(widget, QEvent::MouseButtonPress, point, Qt::MiddleButton, Qt::MiddleButton);
  mouse(widget, QEvent::MouseMove, point - QPointF(0, 40), Qt::NoButton, Qt::MiddleButton);
  mouse(
      widget, QEvent::MouseButtonRelease, point - QPointF(0, 40), Qt::MiddleButton, Qt::NoButton);
}
}  // namespace
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  bts::install_panel_scrolling();
  bts::install_panel_scrolling();
  QScrollArea area;
  area.resize(320, 220);
  area.setWidgetResizable(true);
  auto *body = new QWidget;
  auto *layout = new QVBoxLayout(body);
  auto *slider = new QSlider(Qt::Horizontal);
  slider->setValue(50);
  layout->addWidget(slider);
  auto *real = new QDoubleSpinBox;
  real->setValue(.5);
  layout->addWidget(real);
  auto *integer = new QSpinBox;
  integer->setValue(12);
  layout->addWidget(integer);
  auto *dial = new QDial;
  dial->setValue(40);
  layout->addWidget(dial);
  auto *combo = new QComboBox;
  combo->addItems({"One", "Two"});
  layout->addWidget(combo);
  auto *space = new QWidget;
  space->setMinimumHeight(1200);
  layout->addWidget(space);
  area.setWidget(body);
  area.show();
  app.processEvents();
  for (QWidget *control :
       std::initializer_list<QWidget *>{
           slider, real, integer, dial, combo, real->findChild<QLineEdit *>()})
  {
    area.ensureWidgetVisible(control);
    const int before = area.verticalScrollBar()->value();
    control->setFocus();
    wheel(control);
    assert(area.verticalScrollBar()->value() > before);
    assert(slider->value() == 50 && real->value() == .5 && integer->value() == 12 &&
           dial->value() == 40 && combo->currentIndex() == 0);
  }
  auto *bar = area.verticalScrollBar();
  bar->setValue(200);
  wheel(bar);
  assert(bar->value() > 200);
  area.ensureWidgetVisible(slider);
  const int before = bar->value();
  pan(slider, slider->rect().center());
  assert(bar->value() == before + 40 && slider->value() == 50);
  assert(QWidget::mouseGrabber() != area.viewport());
  assert(area.viewport()->cursor().shape() != Qt::ClosedHandCursor);
  const QPointF point = area.viewport()->rect().center();
  mouse(area.viewport(), QEvent::MouseButtonPress, point, Qt::MiddleButton, Qt::MiddleButton);
  QEvent lost(QEvent::WindowDeactivate);
  QCoreApplication::sendEvent(&area, &lost);
  assert(QWidget::mouseGrabber() != area.viewport());
  const int canceled = bar->value();
  mouse(
      area.viewport(), QEvent::MouseMove, point - QPointF(0, 40), Qt::NoButton, Qt::MiddleButton);
  assert(bar->value() == canceled);
  QSlider toolbar(Qt::Horizontal);
  toolbar.setValue(50);
  toolbar.show();
  wheel(&toolbar);
  assert(toolbar.value() == 50);
  QListWidget list;
  list.resize(240, 180);
  for (int i = 0; i < 100; ++i)
    list.addItem(QString::number(i));
  list.setCurrentRow(2);
  list.show();
  app.processEvents();
  list.verticalScrollBar()->setValue(5);
  const int list_before = list.verticalScrollBar()->value();
  pan(list.viewport(), list.viewport()->rect().center());
  assert(list.verticalScrollBar()->value() > list_before && list.currentRow() == 2);
  std::cout
      << "Focused slider/spin/dial/combo wheels scroll ancestors; toolbar values retained; scrollbar wheels, middle pan, list selection and canceled grabs passed\n";
}
