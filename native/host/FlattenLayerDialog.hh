#pragma once
#include "AssetCatalog.hh"
#include "FlattenLayer.hh"
#include "LayerCapture.hh"
#include <functional>
class QWidget;
namespace bts {
class TexturePadding;
struct PreparedFlatten {
  FlattenedLayer bake;
  std::vector<StudioAsset> assets;
};
struct LayerCaptureBridge {
  std::function<std::uint64_t(const TextureSet &, const LayerCaptureRequest &)> request;
  std::function<std::optional<LayerCaptureResult>()> result;
};
std::optional<PreparedFlatten> choose_layer_flatten(QWidget *,
                                                    TextureSet,
                                                    const QUuid &,
                                                    LayerCaptureBridge);
}  // namespace bts
