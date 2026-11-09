#pragma once
#include "QuickMask.hh"
#include "TextureDocument.hh"
namespace bts {
inline TextureSet quick_mask_preview(const TextureSet &source,
                                     const QuickMaskField *mask,
                                     bool editing)
{
  auto set = source;
  if (source.instance_layers)
    set.layers = *source.instance_layers;
  set.instance_layers.reset();
  TextureLayer overlay;
  overlay.id = QUuid::createUuidV5(source.id, QByteArrayLiteral("BTS temporary quick mask"));
  overlay.name = "Temporary quick mask preview";
  overlay.channels = {true, false, false, false, false};
  overlay.ao_enabled = false;
  overlay.author_resolution = 256;
  auto view = std::make_shared<QuickMaskOverlay>();
  view->editing = editing;
  if (mask)
    view->field = std::make_shared<QuickMaskField>(*mask);
  overlay.quick_overlay = std::move(view);
  set.layers.push_back(std::move(overlay));
  return set;
}
}  // namespace bts
