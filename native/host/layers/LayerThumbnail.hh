// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <QImage>
#include "ThumbnailMetadata.hh"
namespace bts {
QImage thumbnail_image(const ThumbnailMetadata &, const ThumbnailRequest &);
QImage mask_thumbnail(const LayerMask &,
                      const std::set<int> &tiles,
                      const MeshMap *id_map = nullptr,
                      const MeshMaps *maps = nullptr,
                      const TextureSet *context_set = nullptr,
                      const QUuid &layer_id = {});
QImage layer_thumbnail(const TextureLayer &, int channel, const std::set<int> &tiles);
}  // namespace bts
