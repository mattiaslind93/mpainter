#pragma once
#include "TextureDocument.hh"
namespace bts {
inline bool layer_channel_enabled(const TextureLayer &layer, int channel)
{
  if (channel < 0 || channel > 14)
    throw std::invalid_argument("Invalid layer channel");
  return channel < 5   ? layer.channels[channel] :
         channel == 5  ? layer.ao_enabled :
         channel == 14 ? layer.displacement.enabled :
                         layer.users[channel - 6].enabled;
}
inline void clear_history_raster_channel(StrokeRaster &raster, int channel)
{
  if (channel == 5)
    raster.ao = {};
  else
    raster.paint.clear_channel(channel);
}
inline std::shared_ptr<const SurfacePath> clear_history_path_channel(
    const std::shared_ptr<const SurfacePath> &source, int channel)
{
  if (!source)
    return {};
  auto path = std::make_shared<SurfacePath>(*source);
  if (channel == 5)
    path->brush.ao = false;
  else
    path->brush.channels[channel] = false;
  return path;
}
inline std::shared_ptr<const StrokeHistory> clear_history_channel(
    const std::shared_ptr<const StrokeHistory> &source, int channel, const StrokeRaster &final)
{
  if (!source)
    return {};
  if (!source->loaded)
    throw std::runtime_error("Wait for paint history to finish loading before removing a channel");
  auto history = std::make_shared<StrokeHistory>(*source);
  if (source->baseline) {
    auto baseline = std::make_shared<StrokeRaster>(*source->baseline);
    clear_history_raster_channel(*baseline, channel);
    history->baseline = std::move(baseline);
  }
  for (auto &entry : history->operations) {
    auto op = std::make_shared<StrokeOperation>(*entry);
    if (op->ink) {
      auto ink = std::make_shared<StrokeRaster>(*op->ink);
      clear_history_raster_channel(*ink, channel);
      op->ink = std::move(ink);
    }
    op->path = clear_history_path_channel(op->path, channel);
    entry = std::move(op);
  }
  for (auto &[id, path] : history->declared_paths)
    path = clear_history_path_channel(path, channel);
  history->final_pixels = std::make_shared<StrokeRaster>(final);
  return history;
}
inline void clear_layer_channel_content(TextureLayer &layer, int channel, float midlevel)
{
  if (channel < 5)
    layer.pixels.clear_channel(channel);
  else if (channel == 5)
    layer.ao_pixels = {};
  else {
    auto &scalar = channel == 14 ? layer.displacement : layer.users.at(channel - 6);
    scalar.pixels = {};
    scalar.strokes.reset();
    scalar.author_resolution = 0;
    scalar.value = channel == 14 ? midlevel : 1;
  }
  if (channel <= 5)
    layer.strokes = clear_history_channel(layer.strokes, channel, {layer.pixels, layer.ao_pixels});
  for (auto &effect : layer.effects)
    if (effect.kind == ContentEffectKind::Paint) {
      auto &paint = effect.paint;
      if (channel < 5)
        paint.pixels.clear_channel(channel);
      else if (channel == 5)
        paint.ao = {};
      else if (channel == 14) {
        paint.displacement = {};
        paint.displacement_strokes.reset();
      }
      else {
        paint.users[channel - 6] = {};
        paint.user_strokes[channel - 6].reset();
      }
      if (channel <= 5)
        paint.strokes = clear_history_channel(paint.strokes, channel, {paint.pixels, paint.ao});
    }
  fill_channel_texture(layer.fill, channel).reset();
  layer.fill.anchors[channel] = {};
  if (channel == 0) {
    layer.fill.atom = false;
    layer.values.base_color = new_base_values().base_color;
    // The explicit raw interpretation belongs to the Color source being
    // destroyed. Undo keeps the complete COW layer, including this boundary.
    std::erase_if(layer.effects, [](const auto &effect) {
      return effect.kind == ContentEffectKind::LinearColorTransform;
    });
  }
  else if (channel == 1)
    layer.values.metallic = new_base_values().metallic;
  else if (channel == 2)
    layer.values.roughness = new_base_values().roughness;
  else if (channel == 3)
    layer.values.normal = {0, 0, 1};
  else if (channel == 4)
    layer.values.bump = 0;
  else if (channel == 5)
    layer.values.ambient_occlusion = 1;
  if (channel < 3)
    layer.fill.material_resource.clear();
}
// One destructive channel transition and one document Undo transaction. GPU
// cache retirement is separate and runs on the Blender owner context.
inline bool set_layer_channel(TextureSet &set, const QUuid &id, int channel, bool enabled)
{
  if (channel < 0 || channel > 14)
    throw std::invalid_argument("Choose a valid layer channel");
  auto &layer = TextureDocument::layer(set, id);
  if (layer.instance || layer.kind == LayerKind::Folder)
    throw std::invalid_argument("Channel resources belong to editable paint/fill layers");
  const bool unavailable = enabled && (channel < 5   ? !set.channels[channel] :
                                       channel == 5  ? !set.ao_channel :
                                       channel == 14 ? (!set.displacement.channel ||
                                                        !set.displacement.allocated) :
                                                       !set.users[channel - 6].present);
  if (enabled && channel >= 6 && channel < 14 && !set.users[channel - 6].present)
    throw std::invalid_argument("User channel is not in this texture set");
  if (layer_channel_enabled(layer, channel) == enabled && !unavailable)
    return false;
  const bool removed_selection =
      channel == 0 &&
      std::any_of(layer.effects.begin(), layer.effects.end(), [&](const auto &effect) {
        return effect.kind == ContentEffectKind::LinearColorTransform &&
               effect.id == set.selected_effect;
      });
  auto edited = layer;
  clear_layer_channel_content(edited, channel, set.displacement.midlevel);
  if (enabled) {
    if (channel < 5)
      set.channels[channel] = true;
    else if (channel == 5)
      set.ao_channel = true;
    else if (channel == 14 && unavailable) {
      // Preserve other paint layers when allocating the texture-set channel.
      std::vector<bool> previous;
      for (const auto &item : set.layers)
        previous.push_back(item.displacement.enabled);
      TextureDocument::add_displacement_channel(set);
      for (std::size_t i = 0; i < set.layers.size(); ++i)
        if (set.layers[i].kind != LayerKind::Folder)
          set.layers[i].displacement.enabled = previous[i];
    }
  }
  if (channel < 5)
    edited.channels[channel] = enabled;
  else if (channel == 5)
    edited.ao_enabled = enabled;
  else if (channel == 14)
    edited.displacement.enabled = enabled;
  else
    edited.users[channel - 6].enabled = enabled;
  layer = std::move(edited);
  if (removed_selection)
    set.selected_effect = QUuid();
  return true;
}
}  // namespace bts
