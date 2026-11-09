// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QImage>
#include <functional>
namespace bts {
class BlenderHost;
QJsonObject verify_native_host_material_queue(BlenderHost &,
                                              const std::function<void()> &frame,
                                              const std::function<QImage()> &capture);
}  // namespace bts
