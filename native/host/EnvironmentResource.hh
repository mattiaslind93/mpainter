// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <QJsonObject>
namespace blender {
struct StudioLight;
}
namespace bts {
QJsonObject pack_environment_resource(const QString &name);
QString unpack_environment_resource(const QJsonObject &);
// Register immutable, content-addressed HDR/EXR bytes. Existing names must match their content.
blender::StudioLight *register_environment_bytes(const QByteArray &, const QString &name);
blender::StudioLight *register_environment_file(const QString &absolute_path);
}  // namespace bts
