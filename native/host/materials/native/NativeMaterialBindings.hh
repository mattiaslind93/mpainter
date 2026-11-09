#pragma once
#include "NativeMaterialAsset.hh"
#include "TextureDocument.hh"
namespace bts {
inline bool native_material_channel_supported(const StudioAsset &asset, int channel)
{
  return asset.search_terms.contains("native-channel:" + QString::number(channel));
}
inline void apply_native_material_bindings(TextureLayer &layer,
                                           const StudioAsset &asset,
                                           bool fresh)
{
  if (layer.kind != LayerKind::Fill || layer.instance || !is_native_material_asset(asset) ||
      !asset.fill_preset || !asset.fill_preset->native_material)
    throw std::invalid_argument(
        "Native material requires an editable fill layer and prepared source");
  if (!native_material_channel_supported(asset, 0))
    throw std::invalid_argument("Native material has no supported Color channel");
  auto fill = layer.fill;
  fill.native_material = asset.fill_preset->native_material;
  fill.material_resource = asset.id.toString(QUuid::WithoutBraces).toStdString();
  fill.texture.reset();
  fill.channel_textures.reset();
  fill.atom = false;
  fill.bump_bias = 0;
  fill.invert = false;
  fill.legacy_alpha_policy = false;
  fill.anchors = {};
  fill.alpha_mask.fill(false);
  // Projection capability is supplied by the prepared GPU adapter. Validate
  // authored mapping here before changing the live layer or its channel flags.
  validate_fill_projection(fill);
  layer.fill = std::move(fill);
  if (fresh) {
    layer.values = MaterialValues{};
    layer.channels = {true, true, true, false, true};
    layer.ao_enabled = false;
    layer.displacement.enabled = false;
  }
  // Bump4 is also the explicit native WORLD-shading switch. Its scalar source
  // remains independently admitted; hidden Normal3 is never enabled here.
  for (int channel : {0, 1, 2})
    layer.channels[channel] = layer.channels[channel] &&
                              native_material_channel_supported(asset, channel);
  layer.channels[3] = false;
  if (asset.search_terms.contains("native-normal:unsupported") &&
      !native_material_channel_supported(asset, 4))
    layer.channels[4] = false;
  if (!native_material_channel_supported(asset, 5))
    layer.ao_enabled = false;
  if (!native_material_channel_supported(asset, 14))
    layer.displacement.enabled = false;
}
}  // namespace bts
