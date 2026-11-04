#pragma once
#include "TextureDocument.hh"
#include "TextureLoadController.hh"
#include "MaskEffectPath.hh"
#include "OrdinaryColorSource.hh"
namespace bts {
inline const TextureLayer &mask_texture_layer(const TextureSet &set,const QUuid &id){
 const auto found=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &layer){return layer.id==id;});
 if(found==set.layers.end())throw std::runtime_error("Selected layer is missing");return *found;
}
// Resolve without changing the document: failed/cancelled decodes create no mask or Undo.
inline TextureLoadTarget texture_load_target(const QUuid &document,const TextureSet &set,int channel,bool insert,std::uint64_t revision){
 if(!set.uniform_editable||set.disabled)throw std::runtime_error("Select an editable Texture Set with a Principled BSDF surface");
 const auto &layer=mask_texture_layer(set,set.selected_layer);
 const bool mask=set.selected_mask||layer.kind==LayerKind::Folder;
 TextureLoadTarget target;target.document=document;target.set=set.id;target.layer=layer.id;target.tiles=set.tiles;target.channel=mask?0:channel;
 if(mask){
  if(layer.instance)throw std::invalid_argument("Edit instance masks at their source");
  const auto *effect=layer.mask?find_mask_effect(layer.mask->effects,set.selected_effect):nullptr;
  if(set.selected_mask&&effect&&effect->kind==MaskEffectKind::Fill&&effect->anchor_source.isNull())target.effect=effect->id;
  else {target.create_mask_fill=true;target.effect=QUuid::createUuid();target.revision=revision;target.selection_mask=set.selected_mask;target.selection_geometry=set.selected_geometry;target.selection_effect=set.selected_effect;}
 }else{
  if(!insert&&(layer.instance||set.selected_geometry||!set.selected_effect.isNull()))throw std::runtime_error("Select an editable Fill or Paint layer. Load image creates a fill layer above Paint.");
  if(!insert)require_ordinary_color_source_replacement(layer,channel);
  target.create_fill=insert||layer.kind!=LayerKind::Fill;if(insert)target.insertion=QUuid::createUuid();
 }
 return target;
}
inline bool mask_texture_target_current(const TextureSet &set,const TextureLoadTarget &target,std::uint64_t revision){
 if(set.id!=target.set||set.selected_layer!=target.layer)return false;
 const auto &layer=mask_texture_layer(set,target.layer);
 if(layer.instance||set.disabled||!set.uniform_editable)return false;
 if(target.create_mask_fill)return target.revision==revision&&set.selected_mask==target.selection_mask&&set.selected_geometry==target.selection_geometry&&set.selected_effect==target.selection_effect;
 const auto *effect=layer.mask?find_mask_effect(layer.mask->effects,target.effect):nullptr;
 return set.selected_mask&&set.selected_effect==target.effect&&effect&&effect->kind==MaskEffectKind::Fill&&effect->anchor_source.isNull();
}
inline MaskEffect &install_mask_texture_target(TextureSet &set,const TextureLoadTarget &target){
 auto &layer=TextureDocument::layer(set,target.layer);if(layer.instance)throw std::invalid_argument("Edit instance masks at their source");
 if(target.create_mask_fill){
  if(!layer.mask)layer.mask=LayerMask{};
  require_texture_ram(resource_product(resource_sum(layer.mask->effects.size(),1),sizeof(MaskEffect)),"Mask effects");
  MaskEffect effect;effect.kind=MaskEffectKind::Fill;effect.id=target.effect;layer.mask->effects.push_back(std::move(effect));
 }
 if(!layer.mask)throw std::runtime_error("Mask is missing");
 auto *effect=edit_mask_effect(layer.mask->effects,target.effect,MaskEffectKind::Fill);if(!effect)throw std::runtime_error("Mask fill is missing");
 set.selected_layer=target.layer;set.selected_mask=true;set.selected_geometry=false;set.selected_effect=target.effect;return *effect;
}
inline void add_bitmap_mask_target(TextureSet &set){
 auto &layer=TextureDocument::layer(set,set.selected_layer);if(layer.instance)throw std::invalid_argument("Edit instance masks at their source");
 TextureLoadTarget target;target.layer=layer.id;target.effect=QUuid::createUuid();target.create_mask_fill=true;install_mask_texture_target(set,target);
}
}
