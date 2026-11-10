// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BKE_node.hh"
#include "BLI_string.h"
#include "DNA_node_types.h"
#include <set>
#include <stdexcept>
#include <string>

namespace bts {
struct NativeBumpCapability {
  bool neutral = false;
  int no_height_nodes = 0;
  std::string reason;
};
// Inspect only the Normal path in an already inlined private tree. Blender's
// gpu_shader_bump() explicitly bypasses a Bump node when Height is unlinked;
// its unlinked Normal then means the original surface normal. Active Height,
// normal maps and arbitrary vector expressions are never inferred as height.
inline NativeBumpCapability native_bump_capability(const blender::bNodeSocket &normal)
{
  using namespace blender;
  NativeBumpCapability result;
  const bNodeSocket *socket = &normal;
  std::set<const bNode *> visited;
  bool custom_normal = false;
  while (socket->link) {
    auto *node = socket->link->fromnode;
    if (!node || !visited.insert(node).second) {
      result.reason = "cyclic or invalid Normal chain";
      return result;
    }
    if (STREQ(node->idname, "NodeReroute")) {
      socket = static_cast<const bNodeSocket *>(node->inputs.first);
      if (!socket || !socket->link) {
        result.reason = "unconnected Normal reroute has no proven original surface normal";
        return result;
      }
      continue;
    }
    if (STREQ(node->idname, "ShaderNodeBump")) {
      const auto *height = bke::node_find_enabled_input_socket(*node, "Height");
      const auto *input_normal = bke::node_find_enabled_input_socket(*node, "Normal");
      if (!height || !input_normal) {
        result.reason = "Bump source is missing its Height or Normal input";
        return result;
      }
      if (height->link) {
        result.reason =
            input_normal->link ?
                "active Bump Height and a custom Normal chain cannot be represented as grayscale Bump" :
                "active Bump Height requires native surface derivatives and Strength/Filter Width semantics";
        return result;
      }
      ++result.no_height_nodes;
      custom_normal = input_normal->link != nullptr;
      socket = input_normal;
      continue;
    }
    if (STREQ(node->idname, "ShaderNodeNormalMap"))
      result.reason = custom_normal ? "custom Normal Map chain has no authored scalar Height" :
                                      "Normal Map source has no authored scalar Height";
    else
      result.reason = std::string(custom_normal ? "custom Normal chain is not a proven no-op: " :
                                                  "Normal source has no proven scalar Height: ") +
                      node->idname;
    return result;
  }
  result.neutral = true;
  return result;
}
}  // namespace bts
