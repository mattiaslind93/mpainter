// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialParameters.hh"
#include "BKE_node.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_colorband_types.h"
#include "DNA_node_types.h"
#include "GPU_material_hosted.hh"
#include <map>
namespace bts {
inline blender::ColorBand native_material_ramp(const QJsonObject &value)
{
  using namespace blender;
  const auto integer = [&](const char *name, int maximum) {
    const auto v = value[name];
    const int n = v.toInt(-1);
    if (!v.isDouble() || v.toDouble() != n || n < 0 || n > maximum)
      throw std::invalid_argument("Invalid native ColorRamp mode");
    return n;
  };
  ColorBand band;
  band.color_mode = eColorBand_ColorMode(integer("color_mode", 2));
  band.ipotype = eColorBand_Interp(integer("interpolation", 4));
  band.ipotype_hue = eColorBand_HueInterp(integer("hue_interpolation", 3));
  const auto stops = value["stops"].toArray();
  if (!value["stops"].isArray() || stops.isEmpty() || stops.size() > 32)
    throw std::invalid_argument("Native ColorRamp requires 1–32 stops");
  band.tot = short(stops.size());
  double previous = -1;
  for (int i = 0; i < stops.size(); ++i) {
    const auto stop = stops[i].toObject();
    const auto position = stop["position"];
    const double p = position.toDouble(-1);
    const auto color = stop["color"].toArray();
    if (!position.isDouble() || !std::isfinite(p) || p < previous || p < 0 || p > 1 ||
        color.size() != 4)
      throw std::invalid_argument("Invalid native ColorRamp stop");
    std::array<float, 4> rgba;
    for (int c = 0; c < 4; ++c) {
      if (!color[c].isDouble() || !std::isfinite(color[c].toDouble()) ||
          !std::isfinite(float(color[c].toDouble())))
        throw std::invalid_argument("Non-finite native ColorRamp color");
      rgba[c] = float(color[c].toDouble());
    }
    band.data[i] = {rgba[0], rgba[1], rgba[2], rgba[3], float(p), i};
    previous = p;
  }
  return band;
}
inline bool native_material_ramp_compatible(const NativeMaterialParameter &value,
                                            const NativeMaterialParameter &source)
{
  if (source.ramp.isEmpty())
    return value.ramp.isEmpty();
  if (value.ramp.isEmpty())
    return false;
  native_material_ramp(value.ramp);
  // The native lookup's sampling mode is static; its stop table is dynamic.
  for (const char *key : {"color_mode", "interpolation", "hue_interpolation"})
    if (value.ramp[key] != source.ramp[key])
      return false;
  return true;
}
// Only the owning runtime copy is passed here. The encoded source stays intact.
inline void bind_native_material_ramps(blender::bNodeTree &tree,
                                       blender::bNode &top,
                                       const NativeMaterialDescription &description)
{
  using namespace blender;
  std::map<bNode *, QJsonObject> changed;
  for (std::size_t index = 0; index < description.parameters.size(); ++index) {
    const auto &parameter = description.parameters[index];
    if (parameter.ramp.isEmpty())
      continue;
    bNodeSocket *socket = nullptr;
    for (auto &candidate : top.inputs)
      if (parameter.identifier == candidate.identifier) {
        socket = &candidate;
        break;
      }
    auto *source = socket && socket->link ? socket->link->fromnode : nullptr;
    if (!source || !STREQ(source->idname, "ShaderNodeValToRGB") || !source->storage ||
        parameter.source_node != "ShaderNodeValToRGB")
      throw std::invalid_argument("Native ColorRamp source binding changed");
    if (auto [found, fresh] = changed.try_emplace(source, parameter.ramp); !fresh)
      throw std::invalid_argument(
          "Shared ColorRamp inputs require grouped parameter editing (not supported yet)");
    // Keep editable ramps from being constant-folded by the native inliner.
    // This attribute becomes literal zero only during GPU code generation.
    bNodeSocket *factor = nullptr;
    for (auto &candidate : source->inputs)
      if (STREQ(candidate.identifier, "Fac")) {
        factor = &candidate;
        break;
      }
    if (!factor)
      throw std::runtime_error("Native ramp factor is missing");
    auto *zero = bke::node_add_node(nullptr, tree, UString("ShaderNodeAttribute"));
    auto *add = bke::node_add_node(nullptr, tree, UString("ShaderNodeMath"));
    if (!zero || !add)
      throw std::runtime_error("Cannot preserve editable ramp factor");
    STRNCPY(static_cast<NodeShaderAttribute *>(zero->storage)->name, "BTS_Dynamic_Zero");
    add->custom1 = NODE_MATH_ADD;
    auto *first = static_cast<bNodeSocket *>(add->inputs.first);
    auto *second = first->next;
    if (factor->link) {
      auto *link = factor->link;
      bke::node_add_link(tree, *link->fromnode, *link->fromsock, *add, *first);
      bke::node_remove_link(&tree, *link);
    }
    else
      static_cast<bNodeSocketValueFloat *>(first->default_value)->value =
          static_cast<bNodeSocketValueFloat *>(factor->default_value)->value;
    bNodeSocket *fac = nullptr, *value = nullptr;
    for (auto &candidate : zero->outputs)
      if (STREQ(candidate.identifier, "Fac"))
        fac = &candidate;
    for (auto &candidate : add->outputs)
      if (STREQ(candidate.identifier, "Value"))
        value = &candidate;
    if (!fac || !value)
      throw std::runtime_error("Native ramp factor adapter output is missing");
    bke::node_add_link(tree, *zero, *fac, *add, *second);
    bke::node_add_link(tree, *add, *value, *source, *factor);
    *static_cast<ColorBand *>(source->storage) = native_material_ramp(parameter.ramp);
    source->custom1 = short(index + 1);
    source->custom2 = GPU_HOSTED_RAMP_TAG;
  }
}
}  // namespace bts
