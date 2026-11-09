#pragma once
#include "TextureDocument.hh"
#include "FlattenLayerDialog.hh"
class QWidget;
namespace bts {
class TexturePadding;
void choose_flattened_export(QWidget *,
                             TextureSet,
                             const std::vector<QUuid> &,
                             std::shared_ptr<TexturePadding>,
                             bool mask_only,
                             LayerCaptureBridge);
}  // namespace bts
