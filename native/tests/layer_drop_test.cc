// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayerDropCommand.hh"
#include <cassert>
#include <cstdio>
int main()
{
  using namespace bts;
  TextureSet set;
  set.layers.clear();
  TextureLayer a, b, c, d;
  a.name = "A";
  b.name = "B";
  b.kind = LayerKind::Folder;
  c.name = "C";
  d.name = "D";
  d.kind = LayerKind::Folder;
  a.effects.push_back(LevelsEffect{});
  const auto effect = a.effects[0].id;
  set.layers = {a, b, c, d};
  drop_layer_subtrees(set, {a.id}, b.id, true, false);
  assert(TextureDocument::layer(set, a.id).parent == b.id);
  assert(TextureDocument::layer(set, a.id).effects[0].id == effect);
  drop_layer_subtrees(set, {a.id}, d.id, true, false);
  assert(TextureDocument::layer(set, a.id).parent == d.id);
  drop_layer_subtrees(set, {a.id}, {}, false, false);
  assert(TextureDocument::layer(set, a.id).parent.isNull() && set.layers.front().id == a.id);
  drop_layer_subtrees(set, {a.id, c.id}, b.id, true, false);
  assert(TextureDocument::layer(set, a.id).parent == b.id &&
         TextureDocument::layer(set, c.id).parent == b.id);
  drop_layer_subtrees(set, {b.id, a.id}, d.id, true, false);
  assert(TextureDocument::layer(set, b.id).parent == d.id &&
         TextureDocument::layer(set, a.id).depth == 2);
  const auto before = set;
  bool rejected = false;
  try {
    drop_layer_subtrees(set, {d.id}, b.id, true, false);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected && set.layers.size() == before.layers.size());
  TextureDocument::validate_hierarchy(
      set);  // Deep hierarchy no longer depends on the former fixed 17-entry stack.
  TextureSet deep;
  deep.layers.clear();
  TextureLayer leaf;
  leaf.kind = LayerKind::Fill;
  leaf.values.base_color = {.25f, .5f, .75f, 1};
  deep.layers.push_back(leaf);
  for (int i = 0; i < 48; ++i) {
    TextureLayer folder;
    folder.kind = LayerKind::Folder;
    folder.blending.fill(LayerBlend::Passthrough);
    deep.layers.back().parent = folder.id;
    deep.layers.push_back(std::move(folder));
  }
  TextureDocument::validate_hierarchy(deep);
  assert(deep.layers.front().depth == 48);
  const auto pixel = TextureDocument::composite(deep, 1, 1).material.base_color;
  assert(std::abs(pixel[0] - .25f) < 1e-6f && std::abs(pixel[2] - .75f) < 1e-6f);
  std::puts("Layer drop: root/across/nested/multiple/subtree effects and cycle rejection PASS");
}
