// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AssetCatalog.hh"
#include <atomic>
namespace blender {
struct Image;
struct Main;
}  // namespace blender
namespace bts {
// Worker-only immutable encoded snapshot. No image decoding or re-encoding.
StudioAsset snapshot_native_image_source(const blender::Image &,
                                         const QString &library,
                                         const std::atomic_bool &);
StudioAsset snapshot_native_image_source(const blender::Main &,
                                         const blender::Image &,
                                         const std::atomic_bool &);
}  // namespace bts
