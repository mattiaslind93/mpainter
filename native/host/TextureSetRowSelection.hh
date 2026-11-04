#pragma once
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>

namespace bts {
// The inline name is a persistent child widget: Qt does not deliver its mouse
// events to the item view. Keep normal list selection and tile reset semantics.
class TextureSetRowSelection final : public QObject {
 public:
  TextureSetRowSelection(QListWidget *list,QLineEdit *field)
      :QObject(field),list_(list),field_(field){field->installEventFilter(this);}
 protected:
  bool eventFilter(QObject *,QEvent *event) override {
    if(!field_->isReadOnly())return false;
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease){
      const auto *mouse=static_cast<QMouseEvent *>(event);
      const auto point=field_->mapTo(list_->viewport(),mouse->position());
      QMouseEvent forwarded(event->type(),point,mouse->globalPosition(),mouse->button(),mouse->buttons(),mouse->modifiers(),mouse->pointingDevice());
      QCoreApplication::sendEvent(list_->viewport(),&forwarded);
    }else if(event->type()==QEvent::MouseButtonDblClick){
      // A double click may also be sent directly by accessibility/test clients.
      const auto *mouse=static_cast<QMouseEvent *>(event);
      if(auto *item=list_->itemAt(field_->mapTo(list_->viewport(),mouse->position()).toPoint()))list_->setCurrentItem(item);
    }else if(event->type()==QEvent::KeyPress){
      switch(static_cast<QKeyEvent *>(event)->key()){
        case Qt::Key_Up:case Qt::Key_Down:case Qt::Key_Home:case Qt::Key_End:
        case Qt::Key_PageUp:case Qt::Key_PageDown:
          QCoreApplication::sendEvent(list_,event);return true;
        default:break;
      }
    }
    return false;
  }
 private:
  QListWidget *list_;
  QLineEdit *field_;
};
}
