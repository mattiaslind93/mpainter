#pragma once
#include <QFileDialog>
#include <QTimer>
#include <QLineEdit>
#include <QFileInfo>
#include <cstdio>
namespace bts {
// Native GUI demos must wait for QFileDialog's asynchronous filesystem model.
// Retrying acceptance leaves the production chooser and import path untouched.
inline void select_gui_test_file(QWidget *owner,const QString &name,const QString &path){
 auto *retry=new QTimer(owner);retry->setInterval(100);
 QObject::connect(retry,&QTimer::timeout,owner,[owner,name,path,retry,attempts=0]()mutable{
  if(++attempts>100)qFatal("GUI file selection timed out: %s",qPrintable(path));
  auto *dialog=owner->findChild<QFileDialog *>(name);if(!dialog||!dialog->isVisible())return;
  if(auto *filename=dialog->findChild<QLineEdit *>("fileNameEdit"))filename->setText(QFileInfo(path).absoluteFilePath());else dialog->selectFile(path);
  if(attempts==1||attempts==10)std::fprintf(stderr,"BTS_GUI_FILE_PICK attempt=%d visible=%d exists=%d selected=%s\n",attempts,int(dialog->isVisible()),int(QFileInfo::exists(path)),qPrintable(dialog->selectedFiles().join(";")));
  QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
  if(!dialog->isVisible()){retry->stop();retry->deleteLater();}
 });retry->start();
}
}
