// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenPbrParameters.hh"
#include <QByteArray>
#include <QString>
#include <map>
#include <set>
namespace bts {
enum class OpenPbrImageSpace { Raw, AcesCg, Srgb, LinearRec709 };
struct OpenPbrImageBinding {
  QString filename;
  OpenPbrImageSpace space = OpenPbrImageSpace::Raw;
};
struct OpenPbrMaterialX {
  OpenPbrParameters parameters;
  QString material_name = "StudioMaterial";
  // Parameter identifiers, plus scalar "bump" and "displacement". No normal image input.
  std::map<std::string, OpenPbrImageBinding> images;
  std::set<int> tiles{1001};
  float bump_scale = .001f, displacement_scale = 1, displacement_midlevel = .5f;
  bool displacement_enabled = true;
};
// Pure metadata export: no image decode/composition or filesystem writes.
// Embed the pinned ASWF graph; distribute the returned license alongside the document.
QByteArray openpbr_materialx_document(const OpenPbrMaterialX &);
QByteArray openpbr_materialx_license();
}  // namespace bts
