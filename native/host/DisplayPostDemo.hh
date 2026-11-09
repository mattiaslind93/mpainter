#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
class QWidget;
namespace bts {
void start_display_post_demo(
    QWidget *, QWidget *, const QString &, const QString &, int, std::function<QJsonObject()>);
}
