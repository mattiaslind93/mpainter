// SPDX-License-Identifier: GPL-3.0-or-later
#include "SmartMaterial.hh"
namespace bts {
// Legacy descriptors must not recreate a removed bundled material recipe.
bool worn_metal_ready(const TextureSet &)
{
  return false;
}
QUuid apply_worn_metal(TextureSet &, int)
{
  throw std::invalid_argument("Import a user-owned Material Preset before applying it");
}
}  // namespace bts
