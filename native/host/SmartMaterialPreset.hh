// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <QByteArray>
namespace bts {
// Detached, immutable authoring data. Baked maps and scene geometry are never
// part of a material preset: generators resolve against the destination set.
struct SmartMaterialPreset {
  TextureSet set;
};
bool material_preset_has_generators(const SmartMaterialPreset &);
SmartMaterialPreset capture_material_folder(const TextureSet &, const QUuid &folder);
QByteArray write_material_preset(const SmartMaterialPreset &);
SmartMaterialPreset read_material_preset(const QByteArray &);
QUuid apply_material_preset(TextureSet &, const SmartMaterialPreset &);
}  // namespace bts
