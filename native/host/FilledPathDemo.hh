#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_filled_path_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
