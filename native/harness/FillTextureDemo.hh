#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_fill_texture_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
