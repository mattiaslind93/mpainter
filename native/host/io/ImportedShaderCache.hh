#pragma once
#include "ShaderTreeCache.hh"
#include "ColdImBuf.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_lib_id.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "MEM_guardedalloc.h"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_ustring.hh"
#include "DNA_image_types.h"
#include "TextureDocument.hh"
#include <set>
namespace bts {
// Arbitrary imported Surface graphs are not editable layer stacks. Archive
// their native trees directly, retaining shared groups/images used elsewhere.
class ImportedShaderCache {
  struct Pixels {
    std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)> buffer{
        nullptr, blender::IMB_freeImBuf};
    ColdImBuf disk;
  };
  ShaderTreeCache trees_;
  std::vector<std::unique_ptr<Pixels>> pixels_;
  std::vector<blender::bNode *> wrapper_;
  std::vector<blender::bNodeLink *> edges_, original_links_;
  void unwrap(blender::Main &main, blender::bNodeTree &tree)
  {
    for (auto *node : wrapper_)
      blender::bke::node_remove_node(&main, tree, *node, true);
    wrapper_.clear();
    if (!original_links_.empty()) {
      tree.links.clear_no_delete();
      for (auto *link : original_links_)
        blender::BLI_addtail(&tree.links, link);
    }
    edges_.clear();
    original_links_.clear();
  }

 public:
  ~ImportedShaderCache()
  {
    for (auto *link : edges_)
      MEM_delete(link);
  }
  bool cold() const
  {
    return trees_.cold();
  }
  void restore(blender::Main &main, blender::Material &material)
  {
    // Disk errors retain the cache, so the user can retry without losing data.
    for (auto &p : pixels_)
      p->disk.restore(*p->buffer);
    unwrap(main, *material.nodetree);
    trees_.restore();
    pixels_.clear();
    blender::BKE_ntree_update_after_single_tree_change(main, *material.nodetree);
  }
  void change(blender::Main &main, blender::Material &material, const TextureSet &set)
  {
    using namespace blender;
    if (cold() && set.loaded_tiles().empty())
      return;
    restore(main, material);
    auto &tree = *material.nodetree;
    if (set.fully_loaded())
      return;
    if (set.loaded_tiles().empty()) {
      std::set<bNodeTree *> candidates{&tree};
      bool added = true;
      while (added) {
        added = false;
        const auto copy = candidates;
        for (auto *t : copy)
          for (auto &n : t->nodes)
            if (n.id && GS(n.id->name) == ID_NT)
              added |= candidates.insert(reinterpret_cast<bNodeTree *>(n.id)).second;
      }
      // Count only references from trees we can actually remove. Removing a shared
      // group from the candidate set also protects its transitive dependencies.
      for (bool changed = true; changed;) {
        changed = false;
        std::map<ID *, int> uses;
        for (auto *t : candidates)
          for (auto &n : t->nodes)
            if (n.id)
              ++uses[n.id];
        for (auto it = candidates.begin(); it != candidates.end();)
          if (*it != &tree && ((*it)->id.lib || (*it)->id.us > uses[&(*it)->id])) {
            it = candidates.erase(it);
            changed = true;
          }
          else
            ++it;
      }
      std::map<Image *, int> images;
      for (auto *t : candidates)
        for (auto &n : t->nodes)
          if (n.id && GS(n.id->name) == ID_IM)
            ++images[reinterpret_cast<Image *>(n.id)];
      std::vector<std::unique_ptr<Pixels>> buffers;
      for (auto [image, uses] : images)
        if (!image->id.lib && image->id.us <= uses && BKE_image_has_loaded_ibuf(image))
          for (auto &tile : image->tiles) {
            ImageUser user{};
            BKE_imageuser_default(&user);
            user.tile = tile.tile_number;
            auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
            if (!buffer)
              continue;
            auto p = std::make_unique<Pixels>();
            IMB_refImBuf(buffer);
            p->buffer.reset(buffer);
            BKE_image_release_ibuf(image, buffer, nullptr);
            buffers.push_back(std::move(p));
          }
      std::vector<Image *> exclusive_images;
      for (auto [image, uses] : images)
        if (!image->id.lib && image->id.us <= uses)
          exclusive_images.push_back(image);
      trees_.spill(main, {candidates.begin(), candidates.end()}, exclusive_images);
      // Trees have been safely committed to disk. A pixel-cache failure can be
      // retried; uncommitted buffers are retained by the original Image cache.
      pixels_ = std::move(buffers);
      for (auto [image, uses] : images)
        if (!image->id.lib && image->id.us <= uses)
          BKE_image_free_gputextures(image);
      try {
        for (auto &p : pixels_)
          p->disk.spill(*p->buffer);
      }
      catch (...) {
        restore(main, material);
        throw;
      }
    }
    // A temporary visibility graph also supports partial UDIM unload. The source
    // network is necessarily shared by remaining tiles and stays resident.
    const auto add = [&](const char *type) {
      auto *n = bke::node_add_node(nullptr, tree, UString(type));
      if (!n)
        throw std::runtime_error("Cannot create residency visibility node");
      wrapper_.push_back(n);
      return n;
    };
    const auto in = [](bNode *n, int index) -> bNodeSocket & {
      return *static_cast<bNodeSocket *>(BLI_findlink(&n->inputs, index));
    };
    const auto out = [](bNode *n, int index) -> bNodeSocket & {
      return *static_cast<bNodeSocket *>(BLI_findlink(&n->outputs, index));
    };
    const auto link = [&](bNode *a, int ai, bNode *b, int bi) {
      bke::node_add_link(tree, *a, out(a, ai), *b, in(b, bi));
    };
    const auto scalar = [&](bNode *n, int i, float v) {
      static_cast<bNodeSocketValueFloat *>(in(n, i).default_value)->value = v;
    };
    std::vector<bNode *> outputs;
    for (auto &n : tree.nodes)
      if (std::string(n.idname) == "ShaderNodeOutputMaterial")
        outputs.push_back(&n);
    if (outputs.empty())
      outputs.push_back(add("ShaderNodeOutputMaterial"));
    bNode *mask = nullptr;
    if (!set.loaded_tiles().empty()) {
      for (auto &link : tree.links)
        original_links_.push_back(&link);
      auto *uv = add("ShaderNodeTexCoord"), *split = add("ShaderNodeSeparateXYZ");
      link(uv, 2, split, 0);
      auto *x = add("ShaderNodeMath"), *y = add("ShaderNodeMath"), *number = add("ShaderNodeMath"),
           *sum = add("ShaderNodeMath");
      x->custom1 = y->custom1 = NODE_MATH_FLOOR;
      number->custom1 = NODE_MATH_MULTIPLY;
      sum->custom1 = NODE_MATH_ADD;
      link(split, 0, x, 0);
      link(split, 1, y, 0);
      link(y, 0, number, 0);
      scalar(number, 1, 10);
      link(x, 0, sum, 0);
      link(number, 0, sum, 1);
      for (int tile : set.tiles)
        if (!set.tile_loaded(tile)) {
          auto *test = add("ShaderNodeMath");
          test->custom1 = NODE_MATH_COMPARE;
          link(sum, 0, test, 0);
          scalar(test, 1, tile - 1001);
          scalar(test, 2, .1f);
          if (mask) {
            auto *join = add("ShaderNodeMath");
            join->custom1 = NODE_MATH_MAXIMUM;
            link(mask, 0, join, 0);
            link(test, 0, join, 1);
            mask = join;
          }
          else
            mask = test;
        }
    }
    for (auto *output : outputs) {
      auto *transparent = add("ShaderNodeBsdfTransparent");
      if (!mask) {
        link(transparent, 0, output, 0);
        continue;
      }
      auto *mix = add("ShaderNodeMixShader");
      for (auto &edge : tree.links)
        if (edge.tonode == output && edge.tosock == &in(output, 0)) {
          edges_.push_back(&edge);
          break;
        }
      if (!edges_.empty() && edges_.back()->tonode == output) {
        auto *e = edges_.back();
        BLI_remlink(&tree.links, e);
        bke::node_add_link(tree, *e->fromnode, *e->fromsock, *mix, in(mix, 1));
      }
      link(mask, 0, mix, 0);
      link(transparent, 0, mix, 2);
      link(mix, 0, output, 0);
    }
    BKE_ntree_update_after_single_tree_change(main, tree);
  }
};
}  // namespace bts
