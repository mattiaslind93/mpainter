#pragma once
#include "FillTextureJSON.hh"
#include "FillSpatialJSON.hh"
#include "TextureDocument.hh"
namespace bts {
// Channel presets carry references to ordinary image resources. Encoded source
// files remain owned and packed by the resource catalog, once per content hash.
inline QJsonObject blend_material_mapping_json(const FillProjection &fill){
 return {{"scale",QJsonArray{fill.scale[0],fill.scale[1]}},{"offset",QJsonArray{fill.offset[0],fill.offset[1]}},{"rotation",fill.rotation},{"linked",fill.linked},{"manipulator",fill.manipulator},{"gizmo",fill.gizmo},{"gizmo_size",fill.gizmo_size},{"spatial",fill_spatial_json(fill)}};
}
inline void read_blend_material_mapping(FillProjection &fill,const QJsonValue &value){
 const auto fail=[]{throw std::invalid_argument("Invalid Blender material mapping");};if(!value.isObject())fail();const auto data=value.toObject();
 const auto number=[&](const QJsonValue &v){if(!v.isDouble()||!std::isfinite(v.toDouble()))fail();return v.toDouble();};
 const auto boolean=[&](const char *key){if(!data[key].isBool())fail();return data[key].toBool();};
 auto candidate=fill;
 for(const auto key:{"scale","offset"}){if(!data[key].isArray()||data[key].toArray().size()!=2)fail();auto &target=QStringView(u"scale")==QLatin1StringView(key)?candidate.scale:candidate.offset;for(int c=0;c<2;++c)target[c]=number(data[key].toArray()[c]);}
 candidate.rotation=number(data["rotation"]);candidate.linked=boolean("linked");candidate.manipulator=boolean("manipulator");
 const auto gizmo=number(data["gizmo"]);if(gizmo!=std::floor(gizmo)||gizmo<0||gizmo>2)fail();candidate.gizmo=int(gizmo);candidate.gizmo_size=number(data["gizmo_size"]);
 if(!data["spatial"].isObject())fail();read_fill_spatial(candidate,data["spatial"]);validate_fill_projection(candidate);fill=std::move(candidate);
}
// Mapping is shared by channels, but source resources, alpha interpretation,
// Bump binding bias and the destination's runtime units keep their identities.
inline void copy_blend_material_mapping(FillProjection &to,const FillProjection &from){
 to.scale=from.scale;to.offset=from.offset;to.rotation=from.rotation;to.linked=from.linked;to.manipulator=from.manipulator;
 to.mode=from.mode;to.hardness=from.hardness;to.offset3=from.offset3;to.rotation3=from.rotation3;to.scale3=from.scale3;to.linked3=from.linked3;to.gizmo=from.gizmo;to.gizmo_size=from.gizmo_size;
 to.filtering=from.filtering;to.wrap=from.wrap;to.shape_crop=from.shape_crop;to.depth_culling=from.depth_culling;to.depth_hardness=from.depth_hardness;to.backface_culling=from.backface_culling;to.backface_angle=from.backface_angle;to.backface_hardness=from.backface_hardness;to.cylinder_angle=from.cylinder_angle;
 to.physical=from.physical;to.custom_size=from.custom_size;to.asset_size_cm=from.asset_size_cm;to.custom_size_cm=from.custom_size_cm;to.symmetry=from.symmetry;
 to.warp=from.warp;to.warp_geometry=from.warp_geometry;to.warp_fit.reset();to.warp_depth=from.warp_depth;to.warp_edit=from.warp_edit;to.warp_surface=from.warp_surface;to.warp_handle_size=from.warp_handle_size;to.warp_color=from.warp_color;
}
inline QJsonObject blend_material_bindings_json(const FillProjection &fill){
 validate_fill_projection(fill);
 const bool extended=fill_channel_texture(fill,3)||fill_channel_texture(fill,4)||fill_channel_texture(fill,5);
 QJsonArray images;for(int channel=0;channel<(extended?6:3);++channel)images.append(fill_texture_json(fill_channel_texture(fill,channel)));
 return {{"version",extended?3:2},{"mode",fill.mode},{"filter",fill.filtering},{"wrap",fill.wrap},{"mapping",blend_material_mapping_json(fill)},{"images",images}};
}
inline FillProjection read_blend_material_bindings(const QJsonValue &value){
 const auto fail=[]{throw std::invalid_argument("Invalid Blender material image bindings");};if(!value.isObject())fail();const auto data=value.toObject();
 const auto integer=[&](const char *key){const auto v=data[key];const int result=v.toInt(-1);if(!v.isDouble()||v.toDouble()!=result)fail();return result;};
 const int version=integer("version");if((version!=1&&version!=2&&version!=3)||!data["images"].isArray()||data["images"].toArray().size()!=(version==3?6:3))fail();
 FillProjection fill;fill.mode=integer("mode");fill.filtering=integer("filter");fill.wrap=integer("wrap");if(fill.mode<0||fill.mode>6||fill.filtering<0||fill.filtering>2||fill.wrap<0||fill.wrap>3)fail();
 if(version==1){if((fill.mode!=0&&fill.mode!=6)||fill.wrap>2||data.contains("mapping"))fail();}
 else {read_blend_material_mapping(fill,data["mapping"]);if(fill.mode!=integer("mode")||fill.filtering!=integer("filter")||fill.wrap!=integer("wrap"))fail();}
 bool bound=false;const auto images=data["images"].toArray();for(int channel=0;channel<images.size();++channel){auto texture=read_fill_texture_json(images[channel]);if(texture){if(texture->raw!=(channel!=0))fail();bound=true;fill_channel_texture(fill,channel)=std::move(texture);}}if(!bound)fail();fill.alpha_mask.fill(false);validate_fill_projection(fill);return fill;
}
inline std::shared_ptr<const FillTexture> rebind_material_texture(const FillTexture &source,const QUuid &asset){
 auto result=std::make_shared<FillTexture>(source);result->asset=asset.toString(QUuid::WithoutBraces).toStdString();
 if(source.tiles){auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(const auto &[tile,member]:*source.tiles)members->emplace(tile,rebind_material_texture(*member,asset));result->tiles=std::move(members);}result->validate();return result;
}
inline void apply_blend_material_channels(TextureLayer &layer,const MaterialValues &values,const FillProjection *mapping,bool fresh){
 auto fill=layer.fill;if(mapping)copy_blend_material_mapping(fill,*mapping);
 for(int channel:{0,1,2}){fill_channel_texture(fill,channel)=mapping?fill_channel_texture(*mapping,channel):nullptr;fill.alpha_mask[channel]=false;}
 fill.atom=false;validate_fill_projection(fill);
 layer.values.base_color=values.base_color;layer.values.metallic=values.metallic;layer.values.roughness=values.roughness;
 const bool normal=mapping&&bool(fill_channel_texture(*mapping,3));
 if(normal){fill_channel_texture(fill,3)=fill_channel_texture(*mapping,3);fill.alpha_mask[3]=false;layer.values.normal=values.normal;layer.channels[3]=true;}
 const bool bump=mapping&&bool(fill_channel_texture(*mapping,4));
 if(bump){fill_channel_texture(fill,4)=fill_channel_texture(*mapping,4);fill.alpha_mask[4]=false;fill.bump_bias=mapping->bump_bias;layer.values.bump=values.bump;layer.channels[4]=true;}
 validate_fill_projection(fill);layer.fill=std::move(fill);if(fresh){layer.channels={true,true,true,normal,bump};layer.ao_enabled=false;layer.displacement.enabled=false;}
}
inline void apply_blend_material_channels(TextureSet &set,TextureLayer &layer,const MaterialValues &values,const FillProjection *mapping,bool fresh){
 apply_blend_material_channels(layer,values,mapping,fresh);
 if(mapping&&fill_channel_texture(*mapping,3))set.channels[3]=true;
}

}
