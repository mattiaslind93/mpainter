// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
namespace bts {
// Called on the candidate inside the existing document/Undo transaction.
inline void constrain_asset_insertion(TextureSet &changed,
                                      const TextureSet &previous,
                                      const std::string &mesh,
                                      QUuid relative = {},
                                      bool above = true)
{
  std::vector<QUuid> added;
  for (auto &layer : changed.layers)
    if (std::none_of(previous.layers.begin(),
                     previous.layers.end(),
                     [&](const auto &old) { return old.id == layer.id; }))
    {
      added.push_back(layer.id);
      if (!mesh.empty()) {
        layer.geometry.type = 0;
        layer.geometry.default_include = false;
        layer.geometry.meshes.clear();
        layer.geometry.set(mesh, true);
      }
    }
  if (!relative.isNull() && added.size() == 1)
    TextureDocument::move_layer_relative(changed, added.front(), relative, above);
}
}  // namespace bts
