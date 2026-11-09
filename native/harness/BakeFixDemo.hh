#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_bake_fix_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
