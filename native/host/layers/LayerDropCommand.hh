// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <set>
namespace bts {
// Storage is postorder: a folder follows all its descendants. Move whole
// subtrees atomically, retaining their internal IDs, effects and relative order.
inline void drop_layer_subtrees(
    TextureSet &set, const std::vector<QUuid> &ids, const QUuid &target, bool into, bool above)
{
  const auto index = [&](const QUuid &id) {
    for (int i = 0; i < int(set.layers.size()); ++i)
      if (set.layers[i].id == id)
        return i;
    throw std::invalid_argument("Drop layer no longer exists");
  };
  const auto start = [&](int end) {
    int begin = end;
    while (begin > 0 && set.layers[begin - 1].depth > set.layers[end].depth)
      --begin;
    return begin;
  };
  std::set<int> selected;
  for (const auto &id : ids)
    selected.insert(index(id));
  if (selected.empty())
    return;
  std::vector<std::pair<int, int>> ranges;
  for (int end : selected) {
    bool nested = false;
    for (int parent : selected)
      if (parent != end && start(parent) <= end && end < parent) {
        nested = true;
        break;
      }
    if (!nested)
      ranges.emplace_back(start(end), end);
  }
  const int destination = target.isNull() ? -1 : index(target);
  for (const auto &[first, last] : ranges)
    if (destination >= first && destination <= last)
      throw std::invalid_argument("Cannot drop a folder into itself or its descendants");
  if (into && (destination < 0 || set.layers[destination].kind != LayerKind::Folder))
    throw std::invalid_argument("Drop target must be a folder");
  auto candidate = set;
  candidate.layers.clear();
  std::vector<TextureLayer> block;
  QUuid parent;
  int depth = 0;
  if (destination >= 0) {
    parent = into ? target : set.layers[destination].parent;
    depth = set.layers[destination].depth + (into ? 1 : 0);
  }
  for (int i = 0; i < int(set.layers.size());) {
    const auto range = std::find_if(
        ranges.begin(), ranges.end(), [&](const auto &r) { return r.first == i; });
    if (range == ranges.end()) {
      candidate.layers.push_back(set.layers[i++]);
      continue;
    }
    const auto [first, last] = *range;
    const int delta = depth - set.layers[last].depth;
    for (int j = first; j <= last; ++j) {
      auto layer = set.layers[j];
      layer.depth += delta;
      if (j == last)
        layer.parent = parent;
      block.push_back(std::move(layer));
    }
    i = last + 1;
  }
  int insertion = above ? int(candidate.layers.size()) : 0;
  if (destination >= 0) {
    int position = 0;
    while (candidate.layers[position].id != target)
      ++position;
    insertion = position;
    if (!into) {
      if (above)
        ++insertion;
      else
        while (insertion > 0 &&
               candidate.layers[insertion - 1].depth > candidate.layers[position].depth)
          --insertion;
    }
  }
  candidate.layers.insert(candidate.layers.begin() + insertion, block.begin(), block.end());
  TextureDocument::validate_hierarchy(candidate);
  set = std::move(candidate);
}
}  // namespace bts
