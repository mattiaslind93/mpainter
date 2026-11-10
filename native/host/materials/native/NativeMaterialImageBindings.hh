// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QStringList>
#include <atomic>
#include <vector>

namespace blender {
struct Main;
struct Material;
struct bNode;
struct bNodeTree;
}  // namespace blender
namespace bts {
// Identifiers are Blender's persisted node identifiers, never list offsets.
// The group name is an additional source check, not a fallback lookup key.
struct NativeImageGroupStep {
  int node_identifier = 0;
  QString group;
  bool operator==(const NativeImageGroupStep &) const = default;
};
// A binding names one original Image Texture output -> consumer socket edge.
// Other fanout edges, shared Image IDs and other group occurrences are separate
// bindings. The owning source asset SHA is checked by the variant request.
struct NativeImageBinding {
  std::vector<NativeImageGroupStep> groups;
  int node_identifier = 0, consumer_identifier = 0;
  QString output, input, consumer_type, image;
  // Native source properties are retained literally and checked against the
  // resolved source; they are never cast into application sampler ordinals.
  QJsonObject source_settings;
  QString key() const;
  bool operator==(const NativeImageBinding &) const = default;
};
struct NativeImageBindingInventory {
  std::vector<NativeImageBinding> bindings;
  QStringList diagnostics;
  // Raw source reachability is not prepared AOV eligibility. Never enable an
  // Apply button from this inventory alone; the detached candidate proves use.
  bool supported_aov_usage_verified = false;
};
inline QJsonObject native_image_binding_json(const NativeImageBinding &);
inline NativeImageBinding read_native_image_binding(const QJsonObject &);
// Worker-only, before AOV inlining. No pixels, GPU objects, cache writes or live
// source mutations. Traversal distinguishes each nested group occurrence.
NativeImageBindingInventory native_material_image_bindings(const blender::Material &,
                                                           const std::atomic_bool &);
// Resolve and validate the complete path, source fingerprint and exact link.
// The returned pointers belong only to the caller's detached source Main.
struct ResolvedNativeImageBinding {
  blender::bNodeTree *tree = nullptr;
  blender::bNode *image_node = nullptr, *consumer = nullptr;
};
ResolvedNativeImageBinding resolve_native_image_binding(blender::Material &,
                                                        const NativeImageBinding &);
}  // namespace bts

#include "NativeMaterialImageBindingMetadata.inl"
