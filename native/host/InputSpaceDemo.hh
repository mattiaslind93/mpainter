#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
class QWidget;
namespace bts {void start_input_space_demo(QWidget *,QWidget *,const QString &,const QString &,int,std::function<QJsonObject()>);}
