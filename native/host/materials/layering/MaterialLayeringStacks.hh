// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <map>
namespace bts {
inline std::map<QUuid, int> material_stack_membership(const TextureSet &set)
{
  std::map<QUuid, int> result;
  std::array<int, 18> ancestry;
  ancestry.fill(-1);
  for (auto it = set.layers.rbegin(); it != set.layers.rend(); ++it) {
    const auto &layer = *it;
    int group = layer.depth > 0 ? ancestry.at(layer.depth) : -1;
    for (int i = 0; i < 3; ++i)
      if (layer.id == set.material_layering.mask_roots[i])
        group = i;
    result[layer.id] = group;
    if (layer.kind == LayerKind::Folder)
      ancestry.at(layer.depth + 1) = group;
  }
  return result;
}
inline bool material_mask_channel(const TextureSet &set, int slot)
{
  return set.material_layering.allocated() &&
         std::find(set.material_layering.mask_slots.begin(),
                   set.material_layering.mask_slots.end(),
                   slot) != set.material_layering.mask_slots.end();
}
inline void select_material_substack(TextureSet &set, int which)
{
  auto &model = set.material_layering;
  if (which < -1 || which > 2 || (which >= 0 && !model.enabled))
    throw std::invalid_argument("Material opacity substack is unavailable");
  const auto groups = material_stack_membership(set);
  if (const auto old = groups.find(set.selected_layer); old != groups.end()) {
    if (old->second < 0)
      model.main_selection = set.selected_layer;
    else if (set.selected_layer != model.mask_roots[old->second])
      model.mask_selections[old->second] = set.selected_layer;
  }
  model.active_mask = which;
  QUuid selected = which < 0 ? model.main_selection : model.mask_selections[which];
  if (!groups.contains(selected) || groups.at(selected) != which) {
    selected = QUuid();
    for (auto it = set.layers.rbegin(); it != set.layers.rend(); ++it)
      if (groups.at(it->id) == which && (which < 0 || it->id != model.mask_roots[which])) {
        selected = it->id;
        break;
      }
    if (selected.isNull())
      selected = model.mask_roots[std::max(0, which)];
  }
  set.selected_layer = selected;
  set.selected_mask = false;
  set.selected_effect = QUuid();
  set.selected_geometry = false;
}
inline void prepare_material_layer_insert(const TextureSet &set, TextureLayer &layer)
{
  if (!set.material_layering.allocated())
    return;
  const int which = set.material_layering.active_mask;
  if (which < 0) {
    for (int slot : set.material_layering.mask_slots)
      layer.users[slot].enabled = false;
    return;
  }
  layer.channels.fill(false);
  layer.ao_enabled = false;
  layer.displacement.enabled = false;
  for (auto &user : layer.users)
    user.enabled = false;
  auto &opacity = layer.users[set.material_layering.mask_slots[which]];
  opacity.enabled = true;
  if (layer.kind == LayerKind::Folder)
    opacity.blending = LayerBlend::Passthrough;
  if (set.selected_layer == set.material_layering.mask_roots[which]) {
    layer.parent = set.selected_layer;
    layer.depth = 1;
  }
}
inline void configure_material_layering(TextureSet &set, bool enabled)
{
  auto candidate = set;
  auto &model = candidate.material_layering;
  if (enabled && !model.allocated()) {
    std::vector<int> available;
    for (int i = 0; i < custom_channel_count; ++i)
      if (!candidate.users[i].allocated)
        available.push_back(i);
    if (available.size() < 3)
      throw std::invalid_argument("Material layering requires three unused scalar-channel slots");
    model.main_selection = candidate.selected_layer;
    model.materials[0].values = candidate.base_values;
    for (int i = 0; i < 3; ++i) {
      const int slot = available[i];
      model.mask_slots[i] = slot;
      auto &channel = candidate.users[slot];
      channel.name = "Layering mask " + std::to_string(i + 1);
      channel.precision = ChannelPrecision::L8;
      TextureDocument::add_user_channel(candidate, slot);
      for (auto &layer : candidate.layers)
        layer.users[slot].enabled = false;
      TextureLayer root;
      root.kind = LayerKind::Folder;
      root.name = "Opacity mask " + std::to_string(i + 1);
      root.channels.fill(false);
      root.ao_enabled = false;
      root.displacement.enabled = false;
      for (auto &user : root.users)
        user.enabled = false;
      root.users[slot].enabled = true;
      root.users[slot].blending = LayerBlend::Passthrough;
      model.mask_roots[i] = root.id;
      TextureLayer paint;
      paint.name = "Layer 1";
      paint.parent = root.id;
      paint.depth = 1;
      paint.channels.fill(false);
      paint.ao_enabled = false;
      paint.displacement.enabled = false;
      for (auto &user : paint.users)
        user.enabled = false;
      paint.users[slot].enabled = true;
      model.mask_selections[i] = paint.id;
      candidate.layers.push_back(std::move(paint));
      candidate.layers.push_back(std::move(root));
    }
  }
  if (!enabled && model.allocated())
    select_material_substack(candidate, -1);
  model.enabled = enabled;
  if (model.allocated()) {
    for (int slot : model.mask_slots)
      candidate.users[slot].present = enabled;
    if (enabled)
      select_material_substack(candidate, std::max(0, model.active_mask));
  }
  model.validate();
  set = std::move(candidate);
}
inline void validate_material_substacks(const TextureSet &set)
{
  const auto &model = set.material_layering;
  model.validate();
  if (!model.allocated())
    return;
  const auto groups = material_stack_membership(set);
  for (int i = 0; i < 3; ++i) {
    const auto root = std::find_if(set.layers.begin(), set.layers.end(), [&](const auto &layer) {
      return layer.id == model.mask_roots[i];
    });
    if (root == set.layers.end() || root->kind != LayerKind::Folder || root->depth != 0 ||
        !root->parent.isNull())
      throw std::invalid_argument("Shader-defined opacity roots cannot be removed or reparented");
    const auto &channel = set.users[model.mask_slots[i]];
    if (!channel.allocated || channel.present != model.enabled || channel.scattering ||
        channel.name != "Layering mask " + std::to_string(i + 1))
      throw std::invalid_argument("Material opacity channels are defined by the shader");
  }
  for (const auto &layer : set.layers) {
    const int which = groups.at(layer.id);
    if (which < 0) {
      for (int slot : model.mask_slots)
        if (layer.users[slot].enabled)
          throw std::invalid_argument("Ordinary layers cannot write shader-owned opacity stacks");
      continue;
    }
    if (layer.instance ||
        std::any_of(layer.channels.begin(), layer.channels.end(), [](bool on) { return on; }) ||
        layer.ao_enabled || layer.displacement.enabled)
      throw std::invalid_argument("Material substacks contain only their opacity channel");
    for (int slot = 0; slot < custom_channel_count; ++slot)
      if (slot != model.mask_slots[which] && layer.users[slot].enabled)
        throw std::invalid_argument("Opacity stack writes another channel");
  }
}
inline TextureSet material_layering_display(const TextureSet &set)
{
  auto result = set;
  const auto &model = set.material_layering;
  if (!model.allocated())
    return result;
  const auto groups = material_stack_membership(set);
  const int which = model.active_mask;
  std::erase_if(result.layers, [&](const auto &layer) {
    return groups.at(layer.id) != which || (which >= 0 && layer.id == model.mask_roots[which]);
  });
  if (which >= 0) {
    for (auto &layer : result.layers) {
      --layer.depth;
      if (layer.parent == model.mask_roots[which])
        layer.parent = QUuid();
    }
    result.channels.fill(false);
    result.ao_channel = false;
    result.displacement.channel = false;
    for (int slot = 0; slot < custom_channel_count; ++slot) {
      result.users[slot].present = slot == model.mask_slots[which];
      if (result.users[slot].present)
        result.users[slot].name = "Opacity";
    }
  }
  else
    for (int slot : model.mask_slots)
      result.users[slot].present = false;
  result.material_layering = {};
  result.instance_layers.reset();
  return result;
}
}  // namespace bts
