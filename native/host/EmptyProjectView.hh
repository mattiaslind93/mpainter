#pragma once
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class EmptyProjectView final:public QFrame {
 public:
  std::function<void()> new_requested,open_requested;
  explicit EmptyProjectView(QWidget *parent):QFrame(parent){setObjectName("emptyProjectView");setStyleSheet("#emptyProjectView{background:#303030;border:1px solid #505050}");auto *layout=new QVBoxLayout(this);auto *label=new QLabel("No project – File → New (Ctrl+N) / File → Open",this);label->setObjectName("emptyProjectMessage");label->setWordWrap(true);label->setAlignment(Qt::AlignCenter);layout->addWidget(label);auto *buttons=new QHBoxLayout;auto *create=new QPushButton("New project…",this),*open=new QPushButton("Open project…",this);create->setObjectName("emptyProjectNew");open->setObjectName("emptyProjectOpen");buttons->addWidget(create);buttons->addWidget(open);layout->addLayout(buttons);connect(create,&QPushButton::clicked,this,[this]{if(new_requested)new_requested();});connect(open,&QPushButton::clicked,this,[this]{if(open_requested)open_requested();});}
};
}
