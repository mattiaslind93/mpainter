// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaskEffect.hh"
#include <optional>
#include <utility>
namespace bts {
using MaskEffectPath = std::vector<std::size_t>;
struct MaskEffectEntry {
  const MaskEffect *effect = nullptr;
  MaskEffectPath path;
  std::size_t depth = 0;
};
namespace mask_effect_path_detail {
inline std::uint64_t traversal_bytes(std::size_t depth)
{
  return resource_product(resource_sum(depth, 1), sizeof(void *) * 2 + sizeof(std::size_t) * 6);
}
inline bool has_nonempty_group(const std::vector<MaskEffect> &effects)
{
  for (const auto &effect : effects)
    if (effect.kind == MaskEffectKind::Group && effect.group_effects &&
        !effect.group_effects->empty())
      return true;
  return false;
}
template<class Visit>
void visit(const std::vector<MaskEffect> &effects, bool reverse, Visit &&apply)
{
  struct Frame {
    const std::vector<MaskEffect> *effects;
    std::size_t next, depth;
  };

  std::vector<Frame> stack{{&effects, 0, 0}};
  MaskEffectPath path;
  while (!stack.empty()) {
    auto &frame = stack.back();
    if (frame.next == frame.effects->size()) {
      stack.pop_back();
      continue;
    }

    path.resize(frame.depth);
    const auto index = reverse ? frame.effects->size() - 1 - frame.next : frame.next;
    ++frame.next;
    path.push_back(index);
    const auto &effect = frame.effects->at(index);
    if (!apply(effect, path))
      return;
    if (effect.kind == MaskEffectKind::Group && effect.group_effects &&
        !effect.group_effects->empty())
    {

      stack.push_back({effect.group_effects.get(), 0, path.size()});
    }
  }
}
inline std::optional<MaskEffectPath> find_path(const std::vector<MaskEffect> &effects,
                                               const QUuid &id)
{
  if (id.isNull())
    return {};
  std::optional<MaskEffectPath> result;
  visit(effects, false, [&](const MaskEffect &effect, const MaskEffectPath &path) {
    if (effect.id != id)
      return true;

    result = path;
    return false;
  });
  return result;
}
}  // namespace mask_effect_path_detail
// Dependency classifiers skip disabled/zero-opacity Group subtrees. The
// operation owns one budget snapshot and never detaches immutable children.
template<class Predicate>
bool any_active_mask_effect(const std::vector<MaskEffect> &effects, Predicate &&predicate)
{
  // Flat lookups need no traversal storage or RAM snapshot. This path is also
  // used by per-pixel reference evaluation; nested trees retain the guarded DFS.
  if (!mask_effect_path_detail::has_nonempty_group(effects)) {
    for (const auto &effect : effects)
      if (effect.enabled && predicate(effect))
        return true;
    return false;
  }
  struct Frame {
    const std::vector<MaskEffect> *effects;
    std::size_t next;
  };
  std::vector<Frame> stack{{&effects, 0}};
  while (!stack.empty()) {
    auto &frame = stack.back();
    if (frame.next == frame.effects->size()) {
      stack.pop_back();
      continue;
    }
    const auto &effect = frame.effects->at(frame.next++);
    if (!effect.enabled)
      continue;
    if (predicate(effect))
      return true;
    if (effect.kind == MaskEffectKind::Group && effect.blending != LayerBlend::Disabled &&
        effect.opacity > 0 && effect.group_effects && !effect.group_effects->empty())
    {
      stack.push_back({effect.group_effects.get(), 0});
    }
  }
  return false;
}
inline std::optional<MaskEffectPath> find_mask_effect_path(const std::vector<MaskEffect> &effects,
                                                           const QUuid &id)
{
  return mask_effect_path_detail::find_path(effects, id);
}
inline const MaskEffect *find_mask_effect(const std::vector<MaskEffect> &effects, const QUuid &id)
{
  if (id.isNull())
    return nullptr;
  // Preserve DFS preorder whenever children exist, including duplicate-ID
  // diagnostic fixtures. Empty and flat stacks allocate nothing.
  if (!mask_effect_path_detail::has_nonempty_group(effects)) {
    for (const auto &effect : effects)
      if (effect.id == id)
        return &effect;
    return nullptr;
  }

  const MaskEffect *result = nullptr;
  mask_effect_path_detail::visit(
      effects, false, [&](const MaskEffect &effect, const MaskEffectPath &) {
        if (effect.id != id)
          return true;
        result = &effect;
        return false;
      });
  return result;
}
inline std::vector<MaskEffectEntry> mask_effect_entries(const std::vector<MaskEffect> &effects,
                                                        bool reverse_siblings = false)
{

  std::vector<MaskEffectEntry> result;
  std::uint64_t bytes = 0;
  mask_effect_path_detail::visit(
      effects, reverse_siblings, [&](const MaskEffect &effect, const MaskEffectPath &path) {
        bytes = resource_sum(bytes,
                             resource_sum(sizeof(MaskEffectEntry) * 2,
                                          resource_product(path.size(), sizeof(std::size_t) * 2)));
        result.push_back({&effect, path, path.size() - 1});
        return true;
      });
  return result;
}
// Resolve the siblings owning a selected path. Validate the whole path before
// detaching, then copy only ancestors; every unrelated Group owner stays shared.
namespace mask_effect_path_detail {
inline std::vector<MaskEffect> *edit_siblings(std::vector<MaskEffect> &effects,
                                              const MaskEffectPath &path)
{
  if (path.empty())
    return nullptr;
  std::uint64_t bytes = resource_product(path.size(), sizeof(std::size_t) * 2);
  const auto *checked = &std::as_const(effects);
  for (std::size_t level = 0; level < path.size(); ++level) {
    if (path[level] >= checked->size())
      return nullptr;
    if (level + 1 < path.size()) {
      const auto &parent = checked->at(path[level]);
      if (parent.kind != MaskEffectKind::Group || !parent.group_effects)
        return nullptr;
      bytes = resource_sum(
          bytes,
          resource_sum(resource_product(parent.group_effects->size(), sizeof(MaskEffect) * 2),
                       sizeof(void *) * 8));
      checked = parent.group_effects.get();
    }
  }

  auto *siblings = &effects;
  for (std::size_t level = 0; level + 1 < path.size(); ++level) {
    auto &parent = siblings->at(path[level]);
    auto children = std::make_shared<std::vector<MaskEffect>>(*parent.group_effects);
    parent.group_effects = children;
    siblings = children.get();
  }
  return siblings;
}
}  // namespace mask_effect_path_detail
inline std::vector<MaskEffect> *edit_mask_effect_siblings(std::vector<MaskEffect> &effects,
                                                          const MaskEffectPath &path)
{
  return mask_effect_path_detail::edit_siblings(effects, path);
}
inline MaskEffect *edit_mask_effect(std::vector<MaskEffect> &effects, const QUuid &id)
{
  const auto path = mask_effect_path_detail::find_path(std::as_const(effects), id);
  if (!path)
    return nullptr;
  return &mask_effect_path_detail::edit_siblings(effects, *path)->at(path->back());
}
inline MaskEffect *edit_mask_effect(std::vector<MaskEffect> &effects,
                                    const QUuid &id,
                                    MaskEffectKind kind)
{
  const auto path = mask_effect_path_detail::find_path(std::as_const(effects), id);
  if (!path)
    return nullptr;
  const auto *siblings = &std::as_const(effects);
  for (std::size_t i = 0; i + 1 < path->size(); ++i)
    siblings = siblings->at(path->at(i)).group_effects.get();
  if (siblings->at(path->back()).kind != kind)
    return nullptr;
  return &mask_effect_path_detail::edit_siblings(effects, *path)->at(path->back());
}
// Bulk mutable visitors (archive restore/reprojection) need stable leaf
// references: detach each required ancestor once, before yielding any leaf.
namespace mask_effect_path_detail {
struct MutableTree {
  std::vector<MaskEffect *> effects;
  std::vector<MaskEffectPath> paths;
  std::map<MaskEffectPath, std::vector<MaskEffect> *> owners;
};
inline MutableTree edit_tree(std::vector<MaskEffect> &effects, std::optional<MaskEffectKind> kind)
{

  std::vector<MaskEffectPath> leaves;
  std::set<MaskEffectPath> ancestors;
  std::uint64_t bytes = 0;
  mask_effect_path_detail::visit(
      std::as_const(effects), false, [&](const MaskEffect &effect, const MaskEffectPath &path) {
        if (kind && effect.kind != *kind)
          return true;
        bytes = resource_sum(bytes,
                             resource_sum(sizeof(MaskEffectPath) * 2,
                                          resource_product(path.size(), sizeof(std::size_t) * 2)));

        leaves.push_back(path);

        auto parent = path;
        parent.pop_back();
        while (!parent.empty()) {
          if (!ancestors.contains(parent)) {
            bytes = resource_sum(
                bytes,
                resource_sum(sizeof(MaskEffectPath) + 8 * sizeof(void *),
                             resource_product(parent.size(), sizeof(std::size_t) * 2)));

            ancestors.insert(parent);
          }
          parent.pop_back();
        }
        return true;
      });
  bytes = resource_sum(bytes, sizeof(MaskEffectPath) + 8 * sizeof(void *));

  std::map<MaskEffectPath, std::vector<MaskEffect> *> owners{{{}, &effects}};
  for (const auto &path : ancestors) {
    const auto path_bytes = resource_product(path.size(), sizeof(std::size_t) * 2);

    auto parent = path;
    const auto index = parent.back();
    parent.pop_back();
    auto &group = owners.at(parent)->at(index);
    bytes = resource_sum(
        bytes,
        resource_sum(resource_product(group.group_effects->size(), sizeof(MaskEffect) * 2),
                     resource_sum(sizeof(MaskEffectPath) + sizeof(void *) * 16, path_bytes)));

    auto children = std::make_shared<std::vector<MaskEffect>>(*group.group_effects);
    group.group_effects = children;
    owners.emplace(path, children.get());
  }
  bytes = resource_sum(bytes, resource_product(leaves.size(), sizeof(MaskEffect *)));

  std::vector<MaskEffect *> result;
  result.reserve(leaves.size());
  for (const auto &leaf : leaves) {
    auto path = leaf;
    const auto index = path.back();
    path.pop_back();
    result.push_back(&owners.at(path)->at(index));
  }
  return {std::move(result), std::move(leaves), std::move(owners)};
}
}  // namespace mask_effect_path_detail
inline std::vector<MaskEffect *> edit_mask_effects_of_kind(std::vector<MaskEffect> &effects,
                                                           MaskEffectKind kind)
{
  return mask_effect_path_detail::edit_tree(effects, kind).effects;
}
inline std::vector<MaskEffect *> edit_mask_effects(std::vector<MaskEffect> &effects)
{
  return mask_effect_path_detail::edit_tree(effects, {}).effects;
}
inline void erase_mask_effects_of_kind(std::vector<MaskEffect> &effects, MaskEffectKind kind)
{
  auto edited = mask_effect_path_detail::edit_tree(effects, kind);
  // Reverse preorder removes children first and higher sibling indices first.
  for (auto it = edited.paths.rbegin(); it != edited.paths.rend(); ++it) {
    const auto index = it->back();
    it->pop_back();
    auto *siblings = edited.owners.at(*it);
    siblings->erase(siblings->begin() + index);
  }
}
}  // namespace bts
