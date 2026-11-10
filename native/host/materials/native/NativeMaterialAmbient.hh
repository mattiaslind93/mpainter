// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include <set>
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_node_types.h"
#include <stdexcept>
#include <vector>
namespace bts {
// Renderer-context adapter, not an AO ray evaluator. A project bake supplies AO;
// the caller must expose this substitution and its bake-distance policy in UI.
inline bool bind_native_material_ambient(blender::bNodeTree &tree)
{
  using namespace blender;
  BKE_ntree_update_without_main(tree);
  std::set<const bNode *> reachable;
  std::vector<bNode *> order;
  const auto visit = [&](auto &&self, const bNode &node) -> void {
    if (!reachable.insert(&node).second)
      return;
    for (const auto &socket : node.inputs)
      if (socket.link && socket.link->fromnode)
        self(self, *socket.link->fromnode);
    if (STREQ(node.idname, "ShaderNodeAmbientOcclusion"))
      order.push_back(const_cast<bNode *>(&node));
  };
  for (const auto &node : tree.nodes)
    if (STREQ(node.idname, "ShaderNodeOutputAOV"))
      visit(visit, node);

  const auto output = [](bNode &node, const char *id) -> bNodeSocket & {
    for (auto &socket : node.outputs)
      if (STREQ(socket.identifier, id))
        return socket;
    throw std::runtime_error("Native AO adapter output missing");
  };
  const auto input = [](bNode &node, const char *id) -> bNodeSocket & {
    auto *socket = bke::node_find_enabled_input_socket(node, id);
    if (!socket)
      throw std::runtime_error("Native AO adapter input missing");
    return *socket;
  };
  for (auto *source : order) {
    std::vector<bNodeLink *> links;
    for (auto &link : tree.links)
      if (link.fromnode == source)
        links.push_back(&link);
    for (auto *replacement : links) {
      auto &link = *replacement;
      auto &ao = *link.fromnode;
      if (ao.custom2 & (SHD_AO_INSIDE | SHD_AO_LOCAL))
        throw std::runtime_error(
            "Native Inside/Only Local AO requires a matching bake and is not supported yet");
      auto *attribute = bke::node_add_node(nullptr, tree, UString("ShaderNodeAttribute"));
      STRNCPY(static_cast<NodeShaderAttribute *>(attribute->storage)->name, "BTS_Mesh_AO");
      if (STREQ(link.fromsock->identifier, "AO"))
        bke::node_add_link(
            tree, *attribute, output(*attribute, "Fac"), *link.tonode, *link.tosock);
      else if (STREQ(link.fromsock->identifier, "Color")) {
        auto *multiply = bke::node_add_node(nullptr, tree, UString("ShaderNodeVectorMath"));
        multiply->custom1 = NODE_VECTOR_MATH_SCALE;
        auto &color = input(ao, "Color"), &vector = input(*multiply, "Vector");
        if (color.link)
          bke::node_add_link(
              tree, *color.link->fromnode, *color.link->fromsock, *multiply, vector);
        else {
          const auto *rgba = static_cast<const bNodeSocketValueRGBA *>(color.default_value);
          auto *value = static_cast<bNodeSocketValueVector *>(vector.default_value);
          for (int c = 0; c < 3; ++c)
            value->value[c] = rgba->value[c];
        }
        bke::node_add_link(
            tree, *attribute, output(*attribute, "Fac"), *multiply, input(*multiply, "Scale"));
        bke::node_add_link(
            tree, *multiply, output(*multiply, "Vector"), *link.tonode, *link.tosock);
      }
      else
        throw std::runtime_error("Unknown native AO output");
      bke::node_remove_link(&tree, link);
    }
    BKE_ntree_update_without_main(tree);
  }
  return !order.empty();
}
}  // namespace bts
