// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
class QWidget;
namespace bts {
void start_gui_fix_demo(QWidget *workspace,QWidget *viewport,const QString &directory,const QString &mesh,int resolution,std::function<QJsonObject()> state);
}
