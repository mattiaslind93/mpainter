// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureExport.hh"
#include "UsdTextureBindings.hh"
#include <span>
namespace bts {
std::map<std::uint32_t, std::string> openpbr_usd_material_libraries(const ExportRequest &);
std::vector<UsdTextureSetExport> openpbr_usd_preview_textures(const ExportRequest &);
void validate_openpbr_texture_package(const ExportRequest &);
std::vector<std::string> openpbr_texture_package_files(const ExportRequest &);
// Called on the existing export worker after image encoding. No image readback/composition.
void write_openpbr_texture_package(const ExportRequest &,
                                   std::span<const ExportFile>,
                                   const std::atomic_bool &,
                                   const std::function<void(int, const std::string &)> &);
}  // namespace bts
