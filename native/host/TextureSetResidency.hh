#pragma once
#include "TextureDocument.hh"
#include "HistoryMemory.hh"
#include <functional>
namespace bts {
// This view is used only for native preview allocations. Persistent dimensions,
// authored pixels, exports and the per-tile choices remain in the document.
inline TextureSet resident_texture_view(const TextureSet &source){
 auto view=source;view.residency_view=true;
 if(view.loaded_tiles().empty()){
  view.layers.clear();TextureLayer placeholder;placeholder.id=view.selected_layer;
  placeholder.channels.fill(false);placeholder.ao_enabled=false;
  view.layers.push_back(std::move(placeholder));view.instance_layers.reset();
  view.mesh_maps={};view.projection_geometry.reset();view.projection_tiles.clear();
  view.geometry_ownership.clear();
 }
 for(auto &map:view.mesh_maps)if(map)std::erase_if(map->tiles,[&](const auto &entry){return !view.tile_loaded(entry.first);});
 return view;
}
inline void visit_set_textures(const TextureSet &set,const std::function<void(const FillTexture &)> &visit){
 const auto projection=[&](const FillProjection &fill){for(int c=0;c<15;++c)if(const auto &texture=fill_channel_texture(fill,c))visit(*texture);};
 for(const auto &material:set.material_layering.materials)for(const auto &texture:material.textures)if(texture)visit(*texture);
 for(const auto &layer:evaluation_layers(set)){
  projection(layer.fill);
  for(const auto &effect:layer.effects)if(effect.bitmap_fill)projection(effect.bitmap_fill->projection);
  if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))projection(effect.mapping.projection);
 }
}
inline void spill_unloaded_textures(const TextureDocument &document){
 std::set<const void *> protected_pixels;
 std::vector<const FillTexture *> candidates;
 for(const auto &set:document.texture_sets())visit_set_textures(set,[&](const FillTexture &texture){
  if(texture.tiles){for(const auto &[tile,image]:*texture.tiles){if(set.tile_loaded(tile))protected_pixels.insert(image->pixels.identity());else candidates.push_back(image.get());}}
  else if(!set.loaded_tiles().empty())protected_pixels.insert(texture.pixels.identity());else candidates.push_back(&texture);
 });
 // Complete every write before eviction. A failed write keeps that payload.
 for(const auto *texture:candidates)if(!protected_pixels.contains(texture->pixels.identity()))texture->pixels.spill();
 for(const auto &set:document.texture_sets()){
  for(const auto &map:set.mesh_maps)if(map)for(const auto &[tile,plane]:map->tiles)if(!set.tile_loaded(tile)&&plane.pixels)plane.pixels->rgba.spill();
  for(auto i=set.projection_tiles.begin();i!=set.projection_tiles.end();)if(!set.tile_loaded(i->first))i=set.projection_tiles.erase(i);else ++i;
  for(auto i=set.geometry_ownership.begin();i!=set.geometry_ownership.end();)if(!set.tile_loaded(i->first))i=set.geometry_ownership.erase(i);else ++i;
  if(set.loaded_tiles().empty())set.projection_geometry.reset();
 }
 if(history_spill_pending())wait_history_spill();
 request_history_spill(0);
 if(const auto result=wait_history_spill();result&&!result->error.empty())throw std::runtime_error(result->error);
}
}
