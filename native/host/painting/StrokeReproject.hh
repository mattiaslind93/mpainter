// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "UvTransfer.hh"
namespace bts {
bool spatial_history(const std::shared_ptr<const StrokeHistory> &, const StrokeRaster &);
std::pair<StrokeRaster, std::shared_ptr<const StrokeHistory>> transfer_stroke_history(
    const std::shared_ptr<const StrokeHistory> &,
    const StrokeRaster &,
    const UvTransferMap &,
    const std::set<int> &);
StrokeRaster transfer_stroke_raster(const StrokeRaster &,
                                    const UvTransferMap &,
                                    const std::set<int> &tiles);
void reproject_spatial_strokes(TextureDocument &,
                               const SceneSnapshot &previous,
                               const SceneSnapshot &replacement,
                               const std::map<std::uint32_t, std::uint32_t> &bindings,
                               bool preserve_world);
}  // namespace bts
