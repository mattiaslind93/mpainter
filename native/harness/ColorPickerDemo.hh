#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_color_picker_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
