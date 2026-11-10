// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "CompositeChannels.hh"
#include "PaintRecording.hh"
#include "NativeMaterialParameters.hh"
#include <QJsonArray>
#include <map>
namespace bts {
inline QJsonObject edit_set_state(const QJsonObject &document, const QUuid &id)
{
  for (const auto &entry : document["texture_sets"].toArray()) {
    const auto set = entry.toObject();
    if (QUuid(set["id"].toString()) == id)
      return set;
  }
  throw std::logic_error("Edit Texture Set metadata missing");
}
inline bool layer_edit_compatible(QJsonObject before, QJsonObject after)
{
  for (const char *key : {"material_layering",
                          "layers",
                          "selected_effect",
                          "selected_geometry",
                          "selected_layer",
                          "selected_mask",
                          "name",
                          "next_fill_number",
                          "next_folder_number",
                          "next_paint_number",
                          "displacement",
                          "skew_correction",
                          "channels",
                          "ao_channel",
                          "ao_mixing",
                          "normal_mixing",
                          "bump_method"})
  {
    before.remove(key);
    after.remove(key);
  }
  return before == after;
}
inline CompositeChannels layer_edit_channels(const TextureSet &before,
                                             const TextureSet &after,
                                             const QJsonObject &old_state,
                                             const QJsonObject &new_state)
{
  PaintMetadataScope
      metadata;  // Dirty detection must never evaluate deferred authoring pixels, including Undo.
  bool raw_inputs_changed = false;
  CompositeChannels channels{};
  for (int c = 0; c < 5; ++c)
    if (before.channels[c] != after.channels[c])
      channels[c] = true;
  if (before.ao_channel != after.ao_channel || before.ao_mixing != after.ao_mixing)
    channels[5] = true;
  if (before.normal_mixing != after.normal_mixing || before.bump_method != after.bump_method)
    channels[3] = true;
  if (old_state["displacement"] != new_state["displacement"])
    channels[16] = true;
  const auto include = [&](const TextureSet &set, const TextureLayer &layer) {
    raw_inputs_changed = true;
    const bool fill = layer.kind != LayerKind::Paint,
               complex = !layer.effects.empty() || bool(layer.instance);
    for (int c = 0; c < 5; ++c)
      if (set.channels[c] && layer.channels[c] && (fill || complex || layer.pixels.page_count(c)))
        channels[c] = true;
    if (set.ao_channel && layer.ao_enabled && (fill || complex || !layer.ao_pixels.empty()))
      channels[5] = true;
    for (int slot = 0; slot < 8; ++slot)
      if (set.users[slot].present && layer.users[slot].enabled &&
          (fill || complex || !layer.users[slot].pixels.empty()))
        channels[slot + 7] = true;
    if (set.displacement.channel && layer.displacement.enabled &&
        (fill || complex || !layer.displacement.pixels.empty()))
      channels[16] = true;
  };
  std::map<QUuid, QJsonObject> old_layers, new_layers;
  for (auto entry : old_state["layers"].toArray()) {
    auto value = entry.toObject();
    value.remove("name");
    old_layers[QUuid(value["id"].toString())] = value;
  }
  for (auto entry : new_state["layers"].toArray()) {
    auto value = entry.toObject();
    value.remove("name");
    new_layers[QUuid(value["id"].toString())] = value;
  }
  std::vector<QUuid> old_order, new_order;
  for (const auto &layer : before.layers)
    if (new_layers.contains(layer.id))
      old_order.push_back(layer.id);
  for (const auto &layer : after.layers)
    if (old_layers.contains(layer.id))
      new_order.push_back(layer.id);
  const bool reordered = old_order != new_order;
  const auto image_channel = [](int c) { return c == 14 ? 16 : c >= 6 ? c + 1 : c; };
  const auto mark = [&](const TextureLayer &old, const TextureLayer &next, int c) {
    raw_inputs_changed = true;
    const bool active = c < 5   ? ((before.channels[c] && old.channels[c]) ||
                                 (after.channels[c] && next.channels[c])) :
                        c == 5  ? ((before.ao_channel && old.ao_enabled) ||
                                  (after.ao_channel && next.ao_enabled)) :
                        c == 14 ? ((before.displacement.channel && old.displacement.enabled) ||
                                   (after.displacement.channel && next.displacement.enabled)) :
                                  ((before.users[c - 6].present && old.users[c - 6].enabled) ||
                                   (after.users[c - 6].present && next.users[c - 6].enabled));
    if (active)
      channels[image_channel(c)] = true;
  };
  for (const auto &old : before.layers) {
    if (reordered || !new_layers.contains(old.id)) {
      include(before, old);
      continue;
    }
    const auto &next = *std::find_if(after.layers.begin(),
                                     after.layers.end(),
                                     [&](const auto &layer) { return layer.id == old.id; });
    const auto &a = old_layers.at(old.id), &b = new_layers.at(old.id);
    if (a == b)
      continue;
    auto structural_a = a, structural_b = b;
    for (const char *key : {"values",
                            "channel_opacity",
                            "blending",
                            "channels",
                            "ao",
                            "users",
                            "displacement",
                            "fill_projection"})
    {
      structural_a.remove(key);
      structural_b.remove(key);
    }
    if (structural_a != structural_b) {
      include(before, old);
      include(after, next);
      continue;
    }
    const auto old_values = a["values"].toObject(), new_values = b["values"].toObject();
    const std::array<const char *, 6> keys{
        "color", "metallic", "roughness", "normal", "bump", "ao"};
    for (int c = 0; c < 6; ++c)
      if (old_values[keys[c]] != new_values[keys[c]])
        mark(old, next, c);
    for (const char *key : {"channel_opacity", "blending", "channels"}) {
      const auto av = a[key].toArray(), bv = b[key].toArray();
      for (int c = 0; c < 5; ++c)
        if (av[c] != bv[c])
          mark(old, next, c);
    }
    if (a["ao"] != b["ao"])
      mark(old, next, 5);
    const auto old_users = a["users"].toArray(), new_users = b["users"].toArray();
    for (int slot = 0; slot < 8; ++slot)
      if (old_users[slot] != new_users[slot])
        mark(old, next, slot + 6);
    if (a["displacement"] != b["displacement"])
      mark(old, next, 14);
    const auto old_fill = a["fill_projection"].toObject(),
               new_fill = b["fill_projection"].toObject();
    if (old_fill["texture"] != new_fill["texture"] || old_fill["atom"] != new_fill["atom"])
      mark(old, next, 0);
    const auto old_textures = old_fill["channel_textures"].toArray(),
               new_textures = new_fill["channel_textures"].toArray();
    for (int c = 1; c < 15; ++c)
      if (old_textures[c - 1] != new_textures[c - 1])
        mark(old, next, c);
    for (int c = 0; c < 15; ++c)
      if (old.fill.anchors[c] != next.fill.anchors[c])
        mark(old, next, c);
    const auto old_alpha = old_fill["alpha_mask"].toArray(),
               new_alpha = new_fill["alpha_mask"].toArray();
    for (int c = 0; c < 15; ++c)
      if (old_alpha[c] != new_alpha[c])
        mark(old, next, c);
    auto old_mapping = old_fill, new_mapping = new_fill;
    for (const char *key : {"texture",
                            "channel_textures",
                            "anchors",
                            "alpha_mask",
                            "manipulator",
                            "gizmo",
                            "gizmo_size",
                            "linked",
                            "linked3"})
    {
      old_mapping.remove(key);
      new_mapping.remove(key);
    }
    if (old.fill.native_material && next.fill.native_material &&
        old.fill.native_material->height_policy != next.fill.native_material->height_policy)
    {
      auto old_native = *old.fill.native_material, new_native = *next.fill.native_material;
      old_native.height_policy = new_native.height_policy = NativeHeightPolicy::None;
      old_native.channels.clear();
      new_native.channels.clear();
      if (native_material_description_json(old_native) ==
          native_material_description_json(new_native))
      {
        // Only the selected scalar interpretation/capability snapshot changed.
        // Keep ordinary native channels cached; derived Normal follows Bump below.
        mark(old, next, 4);
        old_mapping.remove("native_material");
        new_mapping.remove("native_material");
      }
    }
    for (auto *mapping : {&old_mapping, &new_mapping}) {
      auto warp = (*mapping)["warp"].toObject();
      for (const char *key : {"edit", "surface", "handle_size", "color"})
        warp.remove(key);
      (*mapping)["warp"] = warp;
    }
    for (auto *mapping : {&old_mapping, &new_mapping}) {
      auto symmetry = (*mapping)["symmetry"].toObject();
      if (!symmetry.isEmpty()) {
        symmetry.remove("manipulator");
        symmetry.remove("intersection");
        (*mapping)["symmetry"] = symmetry;
      }
    }
    auto old_volume = old_mapping, new_volume = new_mapping;
    for (const char *key : {"scale",
                            "offset",
                            "rotation",
                            "hardness",
                            "atom",
                            "invert",
                            "border_dots",
                            "middle_dot",
                            "border_width"})
    {
      old_volume.remove(key);
      new_volume.remove(key);
    }
    auto old_options = old_volume["projection_options"].toObject(),
         new_options = new_volume["projection_options"].toObject();
    for (const char *key : {"filtering", "wrap"}) {
      old_options.remove(key);
      new_options.remove(key);
    }
    if (!old.fill.shape_crop && !next.fill.shape_crop) {
      old_options.remove("cylinder_angle");
      new_options.remove("cylinder_angle");
    }
    old_volume["projection_options"] = old_options;
    new_volume["projection_options"] = new_options;
    const bool volume_changed = (fill_spatial_projection(old.fill) ||
                                 fill_spatial_projection(next.fill)) &&
                                old_volume != new_volume;
    // Native graph inputs are authored in the descriptor, without bitmap/atom
    // bindings. Their edits still need an artist preview/Undo GPU publication.
    // The prepared graph owns fine-grained parameter dependencies; this metadata
    // diff conservatively schedules every active supported native channel.
    const auto native_supports = [](const FillProjection &fill, int channel) {
      if (!fill.native_material)
        return false;
      if (channel == 4)
        return true;  // Native shading switch, even when scalar H is unavailable.
      const auto &supported = fill.native_material->channels;
      if (supported.empty())
        return channel == 0 || channel == 1 || channel == 2 || channel == 4 || channel == 5 ||
               channel == 14;
      return std::find(supported.begin(), supported.end(), channel) != supported.end();
    };
    if (old_mapping != new_mapping)
      for (int c = 0; c < 15; ++c)
        if (fill_channel_texture(old.fill, c) || fill_channel_texture(next.fill, c) ||
            (c == 0 && (old.fill.atom || next.fill.atom)) || volume_changed ||
            native_supports(old.fill, c) || native_supports(next.fill, c))
          mark(old, next, c);
  }
  for (const auto &layer : after.layers)
    if (reordered || !old_layers.contains(layer.id))
      include(after, layer);
  const auto coupled = [](const TextureSet &set) {
    return std::any_of(evaluation_layers(set).begin(),
                       evaluation_layers(set).end(),
                       [](const auto &layer) { return TextureDocument::anchor_dependent(layer); });
  };
  if ((raw_inputs_changed ||
       std::any_of(channels.begin(), channels.end(), [](bool on) { return on; })) &&
      (coupled(before) || coupled(after)))
  {
    for (const auto &layer : before.layers)
      include(before, layer);
    for (const auto &layer : after.layers)
      include(after, layer);
    channels[15] = true;
  }
  if (channels[3])
    channels[6] = true;
  if (channels[4])
    channels[3] = true;
  return channels;
}
}  // namespace bts
