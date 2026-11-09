#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_tablet_performance_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
