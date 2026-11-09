#pragma once
#include "AdjustmentPaintSource.hh"
#include "GpuLayerChannel.hh"
namespace bts {
inline bool adjustment_channel(const TextureSet &set, const TextureLayer &layer, int channel)
{
  const auto info = gpu_layer_channel(layer, channel);
  return layer.kind == LayerKind::Paint && !layer.instance && gpu_channel_enabled(set, channel) &&
         info.enabled && info.blend == LayerBlend::Passthrough;
}
inline bool adjustment_layer(const TextureLayer &layer)
{
  if (layer.kind != LayerKind::Paint || layer.instance)
    return false;
  for (int c : {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16})
    if (gpu_layer_channel(layer, c).blend == LayerBlend::Passthrough)
      return true;
  return false;
}
inline void set_adjustment_layer(TextureLayer &layer, bool enabled)
{
  if (layer.kind != LayerKind::Paint || layer.instance)
    throw std::invalid_argument("Adjustment mode requires an editable Paint layer");
  const auto blend = enabled ? LayerBlend::Passthrough : LayerBlend::Normal;
  layer.blending.fill(blend);
  layer.ao_blending = blend;
  for (auto &user : layer.users)
    user.blending = blend;
  layer.displacement.blending = blend;
}
inline AdjustmentPaintSource adjustment_main_source(
    const TextureSet &set,
    const TextureLayer &layer,
    std::shared_ptr<const AdjustmentPaintSnapshot> snapshot)
{
  AdjustmentPaintSource result;
  result.snapshot = std::move(snapshot);
  for (int c = 0; c < 5; ++c) {
    const int logical = c == 3 ? 6 : c;
    if (adjustment_channel(set, layer, logical))
      result.channels[c] = logical;
  }
  return result;
}
inline AdjustmentPaintSource adjustment_scalar_source(
    const TextureSet &set,
    const TextureLayer &layer,
    int channel,
    std::shared_ptr<const AdjustmentPaintSnapshot> snapshot)
{
  AdjustmentPaintSource result;
  result.snapshot = std::move(snapshot);
  if (adjustment_channel(set, layer, channel))
    result.channels[2] = channel;
  return result;
}
}  // namespace bts
