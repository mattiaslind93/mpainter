// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenPbrCompatibility.hh"
#include "MaterialValues.hh"
namespace bts {
struct OpenPbrImportConstants {
  OpenPbrParameters shader;
  MaterialValues base;
};
// Move the three layer-owned values into the material base; keep the original
// source parameters intact and validate the remaining portable preview subset.
inline OpenPbrImportConstants openpbr_import_constants(const OpenPbrParameters &source)
{
  source.validate();
  OpenPbrImportConstants result{source, {}};
  const auto color = source.get("base_color");
  result.base.base_color = {color[0], color[1], color[2], 1};
  result.base.metallic = source.get("base_metalness")[0];
  result.base.roughness = source.get("specular_roughness")[0];
  const OpenPbrParameters defaults;
  for (const auto *name : {"base_color", "base_metalness", "specular_roughness"})
    result.shader.set(name, defaults.get(name));
  validate_openpbr_compatibility(result.shader);
  return result;
}
}  // namespace bts
