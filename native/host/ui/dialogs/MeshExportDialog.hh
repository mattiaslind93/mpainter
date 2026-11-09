// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MeshExport.hh"
#include <functional>
class QWidget;
namespace bts {
void choose_mesh_export(QWidget *,
                        const QString &mesh_path,
                        bool displacement_available,
                        const std::function<std::shared_ptr<NativeMeshExport>(bool, bool)> &);
}
