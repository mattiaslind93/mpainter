// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
namespace blender {
struct Main;
struct Image;
struct ImBuf;
}  // namespace blender
namespace bts {
inline constexpr int native_material_decode_policy_version = 1;
struct NativeMaterialDecodedImage {
  std::unique_ptr<blender::ImBuf, void (*)(blender::ImBuf *)> buffer;
  QJsonObject metadata;
};
// Diagnostic only: no filesystem search or fallback; preserve the literal reference.
std::string native_material_image_source_error(const blender::Image &,
                                               std::string_view resolved_path,
                                               std::string_view reason,
                                               bool packed = false);
// Worker-only literal decode and named-space normalization. The returned buffer
// is independent of Image caches and process scene-linear role. No installation.
NativeMaterialDecodedImage decode_native_material_image(const blender::Main &,
                                                        blender::Image &,
                                                        const std::atomic_bool &);
NativeMaterialDecodedImage decode_native_material_image_tile(const blender::Main &,
                                                             const blender::Image &,
                                                             int tile,
                                                             const std::atomic_bool &);
}  // namespace bts
