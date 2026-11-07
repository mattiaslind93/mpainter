#pragma once
#include "Theme.hh"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionSpinBox>
#include <cmath>
namespace bts {
inline QLocale numeric_locale(){auto locale=QLocale::c();locale.setNumberOptions(QLocale::OmitGroupSeparator|QLocale::RejectGroupSeparator);return locale;}
// Call before constructing QApplication or any UI; filenames, names and dates
// are not rewritten. C's LC_NUMERIC is separately pinned after QApplication.
inline void initialize_numeric_locale(){QLocale::setDefault(numeric_locale());}
inline QString numeric_input(QString text){return text.replace(',', '.');}
// A number field that behaves the way Blender's does: drag across it to change
// the value, click it to type one. Where the value has a real range, the field
// also draws it as a filled track, which is what lets a separate slider under
// the field go away entirely. docs/design/ui.md §7.4.
class NumericSpinBox:public QDoubleSpinBox {
 public:
  explicit NumericSpinBox(QWidget *parent=nullptr):QDoubleSpinBox(parent)
  {
    setLocale(numeric_locale());setGroupSeparatorShown(false);
    if(auto *edit=lineEdit()){
      edit->installEventFilter(this);
      edit->setReadOnly(true);
      // The fill is painted by this widget, so the editor must not cover it.
      edit->setStyleSheet("background: transparent;");
      connect(this,&QAbstractSpinBox::editingFinished,this,[edit]{edit->setReadOnly(true);edit->deselect();});
    }
    // No per-widget cursor here. macOS registers a cursor rect per widget, and
    // with hundreds of these built and torn down while a project is created,
    // that over-releases inside Cocoa's autorelease pool and takes the process
    // with it. Bisected against the released build, which does not crash on the
    // same New Project flow. The filled track is what says the field is
    // draggable; Blender does not change the cursor over a number field either.
  }
  // Draw the value as a filled track. Only meaningful for a bounded range.
  void set_track(bool on){track_=on;update();}
  bool track()const{return track_;}
  void setSuffix(const QString &suffix){QDoubleSpinBox::setSuffix(suffix);if(suffix.trimmed()=="%")setDecimals(2);}
 protected:
  QValidator::State validate(QString &text,int &position)const override{
   // A comma is a decimal separator, never thousands grouping. Two separators
   // remain invalid; do not silently reinterpret 1,234.56 as another number.
   text=numeric_input(text);return QDoubleSpinBox::validate(text,position);
  }
  double valueFromText(const QString &text)const override{return QDoubleSpinBox::valueFromText(numeric_input(text));}
  QString textFromValue(double value)const override{return numeric_locale().toString(value,'f',suffix().trimmed()=="%"?2:decimals());}
  void fixup(QString &text)const override{text=numeric_input(text);QDoubleSpinBox::fixup(text);}
  bool event(QEvent *event)override{if(event->type()==QEvent::Polish&&suffix().trimmed()=="%"&&decimals()!=2)setDecimals(2);return QDoubleSpinBox::event(event);}

  bool eventFilter(QObject *target,QEvent *event)override
  {
    auto *edit=lineEdit();
    if(target!=edit||!isEnabled()||!edit->isReadOnly())return QDoubleSpinBox::eventFilter(target,event);
    if(event->type()==QEvent::MouseButtonPress){
      auto *mouse=static_cast<QMouseEvent *>(event);
      if(mouse->button()!=Qt::LeftButton)return false;
      pressed_=true;dragged_=false;press_=mouse->globalPosition().toPoint();press_value_=value();
      return true;
    }
    if(event->type()==QEvent::MouseMove&&pressed_){
      auto *mouse=static_cast<QMouseEvent *>(event);
      const int dx=mouse->globalPosition().toPoint().x()-press_.x();
      if(!dragged_&&std::abs(dx)<QApplication::startDragDistance())return true;
      dragged_=true;
      // Across the width of the field is the whole range, which is how a drag
      // stays predictable; Shift slows it down for the last decimals.
      const double span=maximum()-minimum();
      const double per_pixel=span>0&&span<1e6?span/std::max(1,width()):singleStep();
      const double scale=(mouse->modifiers()&Qt::ShiftModifier)?0.1:1.0;
      setValue(press_value_+dx*per_pixel*scale);
      return true;
    }
    if(event->type()==QEvent::MouseButtonRelease&&pressed_){
      pressed_=false;
      if(dragged_)return true;
      // A click with no drag is a request to type, so hand the field over.
      edit->setReadOnly(false);
      edit->setFocus(Qt::MouseFocusReason);
      edit->selectAll();
      return true;
    }
    return QDoubleSpinBox::eventFilter(target,event);
  }

  void paintEvent(QPaintEvent *event)override
  {
    QDoubleSpinBox::paintEvent(event);
    const double span=maximum()-minimum();
    if(!track_||span<=0||!lineEdit()||!lineEdit()->isReadOnly())return;
    const double filled=std::clamp((value()-minimum())/span,0.0,1.0);
    QStyleOptionSpinBox option;option.initFrom(this);
    QRect area=style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxEditField,this);
    if(area.isEmpty())area=rect().adjusted(1,1,-1,-1);
    // Two tones, the way a Blender slider reads: the track a step below the
    // field it sits in, the filled part a step above. One tone against the
    // field's own @Number@ background is there but cannot be seen at a glance.
    QPainter painter(this);
    painter.setPen(Qt::NoPen);
    painter.setClipRect(area);
    painter.setBrush(QColor(bts::theme::Border));
    painter.drawRoundedRect(area,bts::theme::RadiusPx,bts::theme::RadiusPx);
    painter.setBrush(QColor(bts::theme::OptionInner));
    QRect fill=area;fill.setWidth(int(std::lround(area.width()*filled)));
    painter.drawRoundedRect(fill,bts::theme::RadiusPx,bts::theme::RadiusPx);
    // A hairline where the two meet reads as an edge rather than a gradient,
    // which is what tells you the exact value at a glance.
    if(filled>0&&filled<1){
      painter.setPen(QPen(QColor(bts::theme::Muted),1));
      painter.drawLine(fill.right(),area.top()+1,fill.right(),area.bottom()-1);
    }
  }

 private:
  bool track_=false,pressed_=false,dragged_=false;
  QPoint press_;double press_value_=0;
};
}
