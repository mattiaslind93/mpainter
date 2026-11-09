#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_signed_displacement_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
