#include "PanelScrolling.hh"
#include <QAbstractScrollArea>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <cmath>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QWheelEvent>
namespace bts {namespace {
QAbstractScrollArea *scroll_parent(QWidget *widget,Qt::Orientation axis,bool either=false)
{
  for(auto *p=widget;p;p=p->parentWidget())if(auto *area=qobject_cast<QAbstractScrollArea *>(p)){
    auto *bar=axis==Qt::Horizontal?area->horizontalScrollBar():area->verticalScrollBar();
    auto *other=axis==Qt::Horizontal?area->verticalScrollBar():area->horizontalScrollBar();
    if(bar->maximum()>bar->minimum()||(either&&other->maximum()>other->minimum()))return area;
  }
  return nullptr;
}
bool wheel_control(QWidget *widget)
{
  for(auto *p=widget;p;p=p->parentWidget()){
    if(qobject_cast<QScrollBar *>(p)||qobject_cast<QAbstractScrollArea *>(p))return false;
    if(qobject_cast<QAbstractSlider *>(p)||qobject_cast<QAbstractSpinBox *>(p)||qobject_cast<QComboBox *>(p))return true;
  }
  return false;
}
class PanelScrolling final:public QObject {
 QPointer<QAbstractScrollArea> panning_;QPointF origin_;int horizontal_=0,vertical_=0;double horizontal_unit_=1,vertical_unit_=1;QCursor cursor_;bool explicit_cursor_=false;
 void finish(){
   if(!panning_)return;
   auto *viewport=panning_->viewport();panning_=nullptr;
   if(explicit_cursor_)viewport->setCursor(cursor_);else viewport->unsetCursor();
   if(QWidget::mouseGrabber()==viewport)viewport->releaseMouse();
 }
 bool eventFilter(QObject *object,QEvent *event)override{
   auto *widget=qobject_cast<QWidget *>(object);
   if(event->type()==QEvent::Wheel&&widget&&wheel_control(widget)){
     auto *wheel=static_cast<QWheelEvent *>(event);
     const auto delta=wheel->pixelDelta().isNull()?wheel->angleDelta():wheel->pixelDelta();
     auto *area=scroll_parent(widget,std::abs(delta.x())>std::abs(delta.y())?Qt::Horizontal:Qt::Vertical);
     if(area){auto *viewport=area->viewport();QWheelEvent forwarded(viewport->mapFromGlobal(wheel->globalPosition()),wheel->globalPosition(),wheel->pixelDelta(),wheel->angleDelta(),wheel->buttons(),wheel->modifiers(),wheel->phase(),wheel->inverted(),wheel->source(),wheel->pointingDevice());QCoreApplication::sendEvent(viewport,&forwarded);}
     // A toolbar control with no scroll parent must still retain its value.
     event->accept();return true;
   }
   if(panning_){
     if(event->type()==QEvent::MouseMove){
       auto *mouse=static_cast<QMouseEvent *>(event);
       if(!(mouse->buttons()&Qt::MiddleButton)){finish();return true;}
       const auto delta=mouse->globalPosition()-origin_;
       panning_->horizontalScrollBar()->setValue(horizontal_-qRound(delta.x()/horizontal_unit_));
       panning_->verticalScrollBar()->setValue(vertical_-qRound(delta.y()/vertical_unit_));
       event->accept();return true;
     }
     if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent *>(event)->button()==Qt::MiddleButton){finish();event->accept();return true;}
     if(event->type()==QEvent::ApplicationDeactivate||(event->type()==QEvent::WindowDeactivate&&object==panning_->window())||((event->type()==QEvent::Hide||event->type()==QEvent::Destroy||event->type()==QEvent::UngrabMouse)&&(object==panning_||object==panning_->viewport())))finish();
   }
   if(widget&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick)){
     auto *mouse=static_cast<QMouseEvent *>(event);
     if(mouse->button()==Qt::MiddleButton){
       auto *area=scroll_parent(widget,Qt::Vertical,true);
       if(area&&area->viewport()->rect().contains(area->viewport()->mapFromGlobal(mouse->globalPosition().toPoint()))){
         finish();panning_=area;origin_=mouse->globalPosition();horizontal_=area->horizontalScrollBar()->value();vertical_=area->verticalScrollBar()->value();
         horizontal_unit_=vertical_unit_=1;
         // Item views may express scrollbar values in rows rather than pixels.
         // Preserve their scroll mode and convert the drag distance accordingly.
         if(auto *view=qobject_cast<QAbstractItemView *>(area)){
           auto index=view->indexAt(view->viewport()->rect().center());if(!index.isValid())index=view->indexAt(QPoint(1,1));
           if(index.isValid()){const auto rect=view->visualRect(index);if(view->horizontalScrollMode()==QAbstractItemView::ScrollPerItem)horizontal_unit_=std::max(1,rect.width());if(view->verticalScrollMode()==QAbstractItemView::ScrollPerItem)vertical_unit_=std::max(1,rect.height());}
         }
         auto *viewport=area->viewport();cursor_=viewport->cursor();explicit_cursor_=viewport->testAttribute(Qt::WA_SetCursor);viewport->setCursor(Qt::ClosedHandCursor);viewport->grabMouse();event->accept();return true;
       }
     }
   }
   return QObject::eventFilter(object,event);
 }
 public:explicit PanelScrolling(QObject *parent):QObject(parent){}
};}
void install_panel_scrolling(){if(!qApp||qApp->property("btsPanelScrollingInstalled").toBool())return;qApp->setProperty("btsPanelScrollingInstalled",true);qApp->installEventFilter(new PanelScrolling(qApp));}
}
