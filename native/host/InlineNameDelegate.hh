#include "Theme.hh"
#pragma once
#include <limits>
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QListWidget>
#include <functional>
namespace bts {
// Commit through the document, never through Qt's itemChanged/check-state path.
class InlineNameDelegate:public QStyledItemDelegate {
 public:
 using QStyledItemDelegate::QStyledItemDelegate;
 static constexpr int name_role=Qt::UserRole+1003;
 std::function<void(const QModelIndex &,const QString &)> renamed;
 QWidget *createEditor(QWidget *parent,const QStyleOptionViewItem &,const QModelIndex &)const override{auto *edit=new QLineEdit(parent);edit->setObjectName("inlineNameEditor");edit->setMaxLength(std::numeric_limits<int>::max());return edit;}
 void setEditorData(QWidget *editor,const QModelIndex &index)const override{auto *edit=static_cast<QLineEdit *>(editor);const auto name=index.data(name_role);edit->setText(name.isValid()?name.toString():index.data(Qt::DisplayRole).toString().trimmed());edit->selectAll();}
 void setModelData(QWidget *editor,QAbstractItemModel *,const QModelIndex &index)const override{const auto name=static_cast<QLineEdit *>(editor)->text().trimmed();const auto old=index.data(name_role).isValid()?index.data(name_role).toString():index.data().toString().trimmed();if(!name.isEmpty()&&!name.contains(QChar::Null)&&name!=old&&renamed)renamed(index,name);}
};
}

namespace bts {
// Texture-set rows contain a live shader button, so Qt treats the whole row as
// a persistent editor. A small inline field handles its name independently.
class InlineNameField final:public QLineEdit {
 public:
 std::function<void(const QString &)> renamed;
 explicit InlineNameField(QWidget *parent=nullptr):QLineEdit(parent){
   setReadOnly(true);setFrame(false);setMaxLength(std::numeric_limits<int>::max());
   setStyleSheet(bts::theme::qss("QLineEdit{background:transparent;border:0;} QLineEdit:focus{border:1px solid @Accent@;}"));
   // Item widgets are persistent editors to Qt; the list cannot open its
   // normal delegate on F2. Route that key to the current row's name instead.
   for(auto *ancestor=parent;ancestor;ancestor=ancestor->parentWidget())
     if(auto *list=qobject_cast<QListWidget *>(ancestor)){list->installEventFilter(this);break;}
 }
 void synchronize(const QString &name){if(!editing_){name_=name;setText(name);}}
 void begin_edit(){if(editing_)return;editing_=true;setReadOnly(false);setFocus();selectAll();}
 protected:
 bool eventFilter(QObject *object,QEvent *event)override{
   if(event->type()==QEvent::KeyPress){auto *key=static_cast<QKeyEvent *>(event);
     if(key->key()==Qt::Key_F2)if(auto *list=qobject_cast<QListWidget *>(object))
       if(auto *row=list->itemWidget(list->currentItem());row&&row->isAncestorOf(this)){begin_edit();key->accept();return true;}
   }
   return QLineEdit::eventFilter(object,event);
 }
 void mouseDoubleClickEvent(QMouseEvent *event)override{if(event->button()==Qt::LeftButton){begin_edit();event->accept();return;}QLineEdit::mouseDoubleClickEvent(event);}
 void keyPressEvent(QKeyEvent *event)override{
   if(event->key()==Qt::Key_F2&&!editing_){begin_edit();event->accept();return;}
   if(editing_&&(event->key()==Qt::Key_Escape||event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)){
     finish(event->key()==Qt::Key_Escape);event->accept();return;
   }
   QLineEdit::keyPressEvent(event);
 }
 void focusOutEvent(QFocusEvent *event)override{finish(false);QLineEdit::focusOutEvent(event);}
 private:
 QString name_;bool editing_=false;
 void finish(bool cancel){
   if(!editing_)return;
   // Capture before changing readOnly/text: Qt may emit editingFinished while
   // changing focus, and rejected/cancelled edits must never reach history.
   const auto value=text().trimmed();editing_=false;setReadOnly(true);setText(name_);
   if(!cancel&&!value.isEmpty()&&!value.contains(QChar::Null)&&value!=name_&&renamed){
     const auto callback=renamed;
     // Refreshing a document may delete the row; own delivery by the window.
     QTimer::singleShot(0,window(),[callback,value]{callback(value);});
   }
 }
};
}
