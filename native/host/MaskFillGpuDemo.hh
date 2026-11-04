#pragma once
#include <functional>
#include <QJsonObject>
class QWidget;class QString;
namespace bts {void start_mask_fill_gpu_demo(QWidget *,QWidget *,const QString &,const QString &,const QString &,std::function<QJsonObject()>);}
