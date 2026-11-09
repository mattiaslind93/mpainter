// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QJsonObject>
#include <functional>
class QWidget;
namespace bts {
void start_auto_bake_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
