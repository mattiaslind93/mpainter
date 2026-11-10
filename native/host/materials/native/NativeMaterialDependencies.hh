// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialMapping.hh"
#include "BKE_node.hh"
#include "NativeMaterialSourcePolicy.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_node_types.h"
#include "GPU_material_hosted.hh"
#include <charconv>
#include <string_view>
#include <stdexcept>
#include <set>
#include <vector>
namespace bts {
struct NativeMaterialDependencies {
  std::vector<std::size_t> parameters;
  bool surface = false, object = false, view = false, ao = false, uv = false, facing = false;
};
inline NativeMaterialDependencies native_material_dependencies(const blender::bNodeTree &tree,
                                                               std::size_t count)
{
  NativeMaterialDependencies result;
  std::set<const blender::bNode *> visited;
  std::set<std::size_t> parameters;
  const auto visit = [&](auto &&self, const blender::bNode &node) -> void {
    if (!visited.insert(&node).second)
      return;
    const std::string_view type(node.idname);
    const auto implicit = native_material_node_context(type);
    result.surface |= implicit.surface;
    result.view |= implicit.view;
    if (type == "ShaderNodeValToRGB" && node.custom2 == blender::GPU_HOSTED_RAMP_TAG &&
        node.custom1 > 0)
    {
      const auto index = std::size_t(node.custom1 - 1);
      if (index >= count)
        throw std::invalid_argument("Invalid native ramp dependency");
      parameters.insert(index);
    }
    if (type == "ShaderNodeAttribute" && node.storage) {
      const std::string_view name(
          static_cast<const blender::NodeShaderAttribute *>(node.storage)->name);
      if (const auto mapping = read_native_mapping_attribute(name, count)) {
        parameters.insert(mapping->parameter);
      }
      else if (name.starts_with("BTS_Param")) {
        std::size_t index = 0;
        const auto suffix = name.substr(9);
        const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
        if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() ||
            index >= count)
          throw std::invalid_argument("Native material parameter dependency is invalid");
        parameters.insert(index);
      }
      else {
        const auto context = native_material_attribute_context(name);
        result.surface |= context.surface;
        result.object |= context.object;
        result.view |= context.view;
        result.ao |= context.ao;
        result.uv |= context.uv;
        result.facing |= context.facing;
      }
    }
    if (type.starts_with("ShaderNodeTex"))
      for (const auto &socket : node.inputs)
        if (STREQ(socket.identifier, "Vector") && !socket.link) {
          if (type == "ShaderNodeTexImage")
            result.uv = true;
          else
            result.object = result.surface = true;
        }
    for (const auto &socket : node.inputs)
      if (socket.link && socket.link->fromnode)
        self(self, *socket.link->fromnode);
  };
  for (const auto &node : tree.nodes)
    if (STREQ(node.idname, "ShaderNodeOutputAOV"))
      visit(visit, node);
  result.parameters.assign(parameters.begin(), parameters.end());
  return result;
}
}  // namespace bts
