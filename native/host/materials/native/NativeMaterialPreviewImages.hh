// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <atomic>
namespace bts {
struct NativeMaterialGraph;
// Preview worker only. Private float caches use the graph's encoded space;
// no shared source buffers or mutable scene-linear role are changed.
QJsonArray prepare_native_material_preview_images(NativeMaterialGraph &, const std::atomic_bool &);
}  // namespace bts
