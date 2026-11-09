// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialHeightResource.hh"
#include <QUuid>
#include <memory>
#include <optional>

namespace bts {
class TextureDocument;
struct NativeHeightProjectSource {
  QUuid id;
  QString hash;
  std::shared_ptr<NativeMaterialGraph> graph;
  std::optional<NativeMaterialHeightResource> height;
};
// Empty for ordinary projects. A raw-policy project validates all its used
// native instances against actual detached sources before the live Main moves.
std::vector<NativeHeightProjectSource> prepare_native_height_project(
    const TextureDocument &, const std::vector<StudioAsset> &, const std::atomic_bool &);
bool native_height_project(const TextureDocument &);
}  // namespace bts
