#pragma once
#include "TextureDocument.hh"
#include "MaskEffectPath.hh"
namespace bts {
inline QString checked_name(const QString &value){require_texture_ram(resource_product(value.size(),8),"Edited name");const auto name=value.trimmed();if(name.isEmpty()||name.contains(QChar::Null))throw std::invalid_argument("Name must contain nonempty text without NUL");return name;}
inline void rename_layer_item(TextureSet &set,const QUuid &layer_id,const QUuid &effect_id,const QString &value){const auto name=checked_name(value);auto &layer=TextureDocument::layer(set,layer_id);if(effect_id.isNull()){layer.name=name.toStdString();return;}for(auto &effect:layer.effects)if(effect.id==effect_id){if(effect.kind==ContentEffectKind::Anchor)effect.anchor_name=name;else effect.name=name.toStdString();return;}if(layer.mask)if(auto *effect=edit_mask_effect(layer.mask->effects,effect_id)){if(effect->kind==MaskEffectKind::Anchor)effect->anchor_name=name;else effect->name=name.toStdString();return;}throw std::invalid_argument("Named layer item no longer exists");}
}
