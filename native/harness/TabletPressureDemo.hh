// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_tablet_pressure_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
