// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GuiFixDemo.hh"
namespace bts {
void start_clipping_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
