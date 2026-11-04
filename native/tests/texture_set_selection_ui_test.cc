#include "InlineNameDelegate.hh"
#include "TextureSetRowSelection.hh"
#include <QApplication>
#include <QLabel>
#include <QHBoxLayout>
#include <cassert>
#include <cstdio>

static void click(QWidget *widget){
  const QPointF point=widget->rect().center();
  for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
    QMouseEvent event(type,point,widget->mapToGlobal(point.toPoint()),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(widget,&event);
  }
}
int main(int argc,char **argv){
  QApplication app(argc,argv);QListWidget list;list.resize(420,240);
  int selected=-1,changes=0;
  QObject::connect(&list,&QListWidget::currentRowChanged,[&](int row){selected=row;++changes;});
  for(int row=0;row<3;++row){
    auto *item=new QListWidgetItem(QString("Set %1").arg(row),&list);
    auto *container=new QWidget;auto *layout=new QHBoxLayout(container);
    auto *field=new bts::InlineNameField(container);field->synchronize(item->text());
    layout->addWidget(field);list.setItemWidget(item,container);item->setSizeHint(QSize(400,36));
  }
  list.show();app.processEvents();list.setCurrentRow(0);
  // AK through current: the read-only editor absorbs a real click. Establish
  // the failure before attaching exactly the forwarding used in production.
  auto field=[&](int row){return list.itemWidget(list.item(row))->findChild<QLineEdit *>();};
  click(field(1));assert(selected==0);
  for(int row=0;row<3;++row)new bts::TextureSetRowSelection(&list,field(row));
  for(int row:{2,1,0,2,0,1}){const int before=changes;click(field(row));assert(list.currentRow()==row&&selected==row&&changes==before+1);}
  QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);QCoreApplication::sendEvent(field(1),&down);assert(selected==2);
  QKeyEvent home(QEvent::KeyPress,Qt::Key_Home,Qt::NoModifier);QCoreApplication::sendEvent(field(2),&home);assert(selected==0);
  auto *edit=field(0);edit->setReadOnly(false);edit->setText("Editing");QKeyEvent end(QEvent::KeyPress,Qt::Key_End,Qt::NoModifier);QCoreApplication::sendEvent(edit,&end);assert(selected==0&&edit->cursorPosition()==7);
  std::puts("Texture Set GUI: AK click regression reproduced; 3-set mouse/navigation/edit isolation PASS");
}
