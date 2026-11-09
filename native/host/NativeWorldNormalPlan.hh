// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "NativeMaterialParameters.hh"
#include "PaintRecording.hh"
#include <QHash>
#include <QSet>
#include <span>

namespace bts {
struct NativeWorldNormalLayer {
  QUuid layer;
  std::string resource;
  std::vector<QUuid> ancestors;
};
struct NativeWorldNormalPlan {
  QUuid layer;
  std::string resource;
  std::vector<QUuid> ancestors;  // Immediate parent first; GPU applies each mask/opacity once.
  QString reason;
  bool eligible = false;
  std::vector<NativeWorldNormalLayer> contributions;
};
// Admission metadata only. The caller supplies layer IDs whose immutable,
// actually inlined native Normal graph is perturbed, rather than every native
// Fill. No source import, paint hash, pixel sampling or materialization occurs.
inline NativeWorldNormalPlan native_world_normal_plan(
    const TextureSet &set, std::span<const QUuid> actually_perturbed_native_ids)
{
  PaintMetadataScope metadata;
  const auto &layers = evaluation_layers(set);
  QHash<QUuid, std::size_t> index;
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (layers[i].id.isNull() || index.contains(layers[i].id))
      throw std::invalid_argument("Invalid world Normal layer identity");
    index.insert(layers[i].id, i);
  }
  QSet<QUuid> perturbed;
  for (const auto &id : actually_perturbed_native_ids) {
    if (id.isNull() || perturbed.contains(id))
      throw std::invalid_argument("Invalid perturbed native layer identity");
    if (!index.contains(id)) {
      // Resolved instance copies have different IDs from the persistent stack.
      // Keep ordinary channels usable and report this unsupported Normal path;
      // never reinterpret a copy as an independent authored native layer.
      if (set.instance_layers &&
          std::any_of(layers.begin(),
                      layers.end(),
                      [](const auto &layer) { return bool(layer.instance); }))
      {
        const auto found = std::find_if(
            set.instance_layers->begin(), set.instance_layers->end(), [&](const auto &layer) {
              return layer.id == id && layer.kind == LayerKind::Fill && layer.fill.native_material;
            });
        if (found != set.instance_layers->end())
          return {found->id,
                  found->fill.material_resource,
                  {},
                  QStringLiteral("World Normal does not support expanded layer instances"),
                  false};
      }
      throw std::invalid_argument("Invalid perturbed native layer identity");
    }
    const auto &layer = layers[index.value(id)];
    if (layer.kind != LayerKind::Fill || !layer.fill.native_material)
      throw std::invalid_argument("Perturbed Normal metadata does not identify a native Fill");
    perturbed.insert(id);
  }
  struct State {
    std::size_t parent = 0, depth = 0;
    bool has_parent = false, visible = false;
    std::array<bool, 2> active{};
  };
  std::vector<State> states(layers.size());
  std::vector<QUuid> path;
  for (std::size_t r = layers.size(); r > 0; --r) {
    const auto i = r - 1;
    const auto &layer = layers[i];
    auto &state = states[i];
    if (!layer.parent.isNull()) {
      const auto found = index.constFind(layer.parent);
      if (found == index.cend() || found.value() <= i ||
          layers[found.value()].kind != LayerKind::Folder)
        throw std::invalid_argument("Invalid world Normal layer hierarchy");
      state.parent = found.value();
      state.has_parent = true;
      state.depth = states[state.parent].depth + 1;
    }
    if (state.depth && (state.depth > path.size() || path[state.depth - 1] != layer.parent))
      throw std::invalid_argument("World Normal folder children are not contiguous");
    path.resize(state.depth + 1);
    path[state.depth] = layer.id;
    state.visible = !set.disabled && layer.visible &&
                    (!state.has_parent || states[state.parent].visible);
    for (int k = 0; k < 2; ++k) {
      const int channel = k + 3;
      const float opacity = layer.opacity[channel];
      if (!std::isfinite(opacity) || opacity < 0 || opacity > 1 ||
          int(layer.blending[channel]) < 0 ||
          std::size_t(layer.blending[channel]) >= layer_blend_names.size())
        throw std::invalid_argument("Invalid world Normal channel metadata");
      state.active[k] = state.visible && set.channels[channel] && layer.channels[channel] &&
                        opacity > 0 && layer.blending[channel] != LayerBlend::Disabled &&
                        (!state.has_parent || states[state.parent].active[k]);
    }
  }
  NativeWorldNormalPlan result;
  for (std::size_t i = 0; i < layers.size(); ++i) {
    const auto &layer = layers[i];
    if (!states[i].active[1] || !perturbed.contains(layer.id) ||
        layer.fill.native_material->height_policy != NativeHeightPolicy::None)
      continue;
    if (layer.fill.material_resource.empty()) {
      result.reason = "Native Normal resource is missing";
      return result;
    }
    NativeWorldNormalLayer contribution{layer.id, layer.fill.material_resource, {}};
    for (auto parent = i; states[parent].has_parent;) {
      parent = states[parent].parent;
      contribution.ancestors.push_back(layers[parent].id);
    }
    result.contributions.push_back(std::move(contribution));
  }
  if (result.contributions.empty())
    return result;
  const auto &first = result.contributions.front();
  result.layer = first.layer;
  result.resource = first.resource;
  result.ancestors = first.ancestors;
  result.eligible = true;
  return result;
}
}  // namespace bts
