// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AssetCatalog.hh"
#include <algorithm>
#include <set>
#include <span>
#include <stdexcept>

namespace bts {
// The reader/decoder has already verified these immutable originals. Prepare a
// catalog value only; publication with the document is a separate noexcept step.
// Never promote or rewrite a Builtin/User/Session resource on an Import collision.
inline AssetCatalog prepare_material_parameter_catalog(const AssetCatalog &catalog,
                                                       std::span<const StudioAsset> originals)
{
  auto prepared = catalog;
  std::set<QUuid> ids;
  for (const auto &incoming : originals) {
    if (incoming.scope != AssetScope::Project || !incoming.usages.contains("texture") ||
        incoming.texture_files.empty())
      throw std::invalid_argument("Parameter original must be a verified Project texture");
    if (!ids.insert(incoming.id).second)
      throw std::invalid_argument("Duplicate parameter resource identity");
    const auto found = std::find_if(prepared.assets().begin(),
                                    prepared.assets().end(),
                                    [&](const auto &item) { return item.id == incoming.id; });
    if (found == prepared.assets().end()) {
      prepared.add(incoming);
      continue;
    }
    if (found->scope != AssetScope::Project || !found->usages.contains("texture"))
      throw std::invalid_argument(
          "Parameter source identity collides with a resource outside this project's textures");
    const auto expected = incoming.hash();
    if (found->hash() != expected)
      throw std::invalid_argument("Parameter source identity collides with a different original");
    // The shared decoder uses this name as descriptor provenance. A conflicting
    // name is unsupported; neither decoded values nor another user's name changes.
    if (found->name != incoming.name)
      throw std::invalid_argument(
          "Parameter source identity collides with a different recorded source name");
    auto refreshed = *found;
    refreshed.filename = incoming.filename;
    refreshed.path = incoming.path;
    refreshed.bytes = incoming.bytes;
    refreshed.texture_files = incoming.texture_files;
    if (refreshed.filename != found->filename || refreshed.path != found->path ||
        refreshed.bytes != found->bytes || refreshed.texture_files != found->texture_files)
      if (!prepared.replace(std::move(refreshed), expected))
        throw std::runtime_error("Parameter original changed while preparing its catalog");
  }
  return prepared;
}
}  // namespace bts
