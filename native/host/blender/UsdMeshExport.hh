// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <string>
#include <map>
namespace blender {
struct Main;
struct Depsgraph;
}  // namespace blender
namespace bts {
void write_native_usd_mesh(blender::Main &,
                           blender::Depsgraph &,
                           const std::string &,
                           bool package,
                           bool triangulate,
                           const std::map<std::string, std::string> &occlusion = {},
                           const std::map<std::string, std::string> &material_libraries = {});
void package_native_usd(const std::string &layer, const std::string &package);
QJsonObject inspect_exported_usd(const std::string &);
}  // namespace bts
