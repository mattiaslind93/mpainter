// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialBindings.hh"
namespace bts {
// Called inside the document edit transaction after source preparation and target
// validation. Never owns Blender IDs, starts GPU work, or mutates the catalog.
inline QUuid apply_native_material_asset(TextureSet &set,
                                         const StudioAsset &asset,
                                         QUuid target = {},
                                         bool above = true,
                                         bool replace = false)
{
  if (!target.isNull())
    TextureDocument::layer(set, target);  // Validate before adding.
  if (replace) {
    const auto id = target.isNull() ? set.selected_layer : target;
    auto &layer = TextureDocument::layer(set, id);
    apply_native_material_bindings(layer, asset, false);
    set.selected_layer = id;
    set.selected_mask = false;
    set.selected_effect = QUuid();
    return id;
  }
  TextureDocument::add_fill(set);
  const auto id = set.selected_layer;
  auto &layer = TextureDocument::layer(set, id);
  apply_native_material_bindings(layer, asset, true);
  layer.name = asset.name.toStdString();
  if (!target.isNull())
    TextureDocument::move_layer_relative(set, id, target, above);
  return id;
}
}  // namespace bts
