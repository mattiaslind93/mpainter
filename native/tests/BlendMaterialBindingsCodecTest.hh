// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BlendMaterialBindings.hh"
#include <cassert>
#include <limits>

namespace bj_mapping_test {
using namespace bts;
template<class F>void rejects(F &&run){bool failed=false;try{run();}catch(const std::exception &){failed=true;}assert(failed);}
inline std::shared_ptr<const FillTexture> image(bool raw){
 auto result=std::make_shared<FillTexture>();result->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();result->hash=std::string(64,'a');result->name="BJ immutable image";result->width=result->height=2;result->raw=raw;
 result->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{.2f,.3f,.4f,1});return result;
}
inline void same_mapping(const FillProjection &a,const FillProjection &b){
 assert(a.scale==b.scale&&a.offset==b.offset&&a.rotation==b.rotation&&a.linked==b.linked&&a.manipulator==b.manipulator);
 assert(a.mode==b.mode&&a.hardness==b.hardness&&a.offset3==b.offset3&&a.rotation3==b.rotation3&&a.scale3==b.scale3&&a.linked3==b.linked3);
 assert(a.gizmo==b.gizmo&&a.gizmo_size==b.gizmo_size&&a.filtering==b.filtering&&a.wrap==b.wrap);
 assert(a.physical==b.physical&&a.custom_size==b.custom_size&&a.asset_size_cm==b.asset_size_cm&&a.custom_size_cm==b.custom_size_cm);
 assert(a.shape_crop==b.shape_crop&&a.depth_culling==b.depth_culling&&a.depth_hardness==b.depth_hardness&&a.backface_culling==b.backface_culling&&a.backface_angle==b.backface_angle&&a.backface_hardness==b.backface_hardness&&a.cylinder_angle==b.cylinder_angle);
 assert(warp_settings_json(a)==warp_settings_json(b)&&fill_symmetry_json(a.symmetry)==fill_symmetry_json(b.symmetry));
}
inline FillProjection spatial(){
 FillProjection p;p.scale={-2,.75};p.offset={.125,-.375};p.rotation=31;p.linked=false;p.manipulator=false;
 p.offset3={.2,-.4,.6};p.rotation3={13,-27,41};p.scale3={-.75,1.5,2};p.hardness=.72;p.gizmo=2;p.gizmo_size=.8;
 p.physical=true;p.custom_size=false;p.asset_size_cm={7,11,17};p.custom_size_cm={3,5,9};
 p.shape_crop=true;p.depth_culling=false;p.depth_hardness=.4;p.backface_culling=true;p.backface_angle=67;p.backface_hardness=.2;p.cylinder_angle=213;
 auto grid=std::make_shared<WarpGrid>(2,2);grid->nodes[0].position[2]=.15;p.warp=std::move(grid);p.warp_depth=2;p.warp_edit=true;p.warp_surface=false;p.warp_handle_size=9;p.warp_color={.1f,.2f,.3f};
 auto symmetry=std::make_shared<FillSymmetry>();symmetry->constraint.enabled=true;symmetry->constraint.axis=1;symmetry->constraint.offset=.25;symmetry->flip_u=true;p.symmetry=std::move(symmetry);
 p.texture=image(false);fill_channel_texture(p,1)=image(true);return p;
}
inline void codec(){
 // Literal v1 shape, independent of the new writer. All new settings must use
 // the old FillProjection defaults, including source alpha disabled for PBR.
 const auto color=image(false);const QJsonArray images{fill_texture_json(color),QJsonValue(QJsonValue::Null),QJsonValue(QJsonValue::Null)};
 for(int mode:{0,6})for(int filter=0;filter<3;++filter)for(int wrap=0;wrap<3;++wrap){
  const QJsonObject old{{"version",1},{"mode",mode},{"filter",filter},{"wrap",wrap},{"images",images}};
  const auto decoded=read_blend_material_bindings(old);FillProjection defaults;defaults.mode=mode;defaults.filtering=filter;defaults.wrap=wrap;same_mapping(decoded,defaults);
  assert(decoded.texture&&decoded.texture->asset==color->asset&&decoded.texture->hash==color->hash&&!decoded.alpha_mask[0]&&!decoded.alpha_mask[1]&&!decoded.alpha_mask[2]);
 }
 auto source=spatial();const auto immutable=blend_material_mapping_json(source);
 for(int mode=0;mode<=6;++mode)for(int wrap=0;wrap<=3;++wrap){
  source.mode=mode;source.wrap=wrap;source.filtering=mode%3;const auto encoded=blend_material_bindings_json(source);
  assert(encoded["version"]==2&&encoded["mapping"].toObject()["scale"].toArray()==QJsonArray({-2,.75}));
  const auto decoded=read_blend_material_bindings(encoded);same_mapping(decoded,source);assert(blend_material_bindings_json(decoded)==encoded);
  assert(decoded.texture->asset==source.texture->asset&&decoded.texture->hash==source.texture->hash&&fill_channel_texture(decoded,1)->raw);
  TextureLayer target;target.kind=LayerKind::Fill;target.fill.native_per_uv={3,7};target.fill.cm_per_native=100;target.fill.bump_bias=-.5f;const auto bump=image(true);fill_channel_texture(target.fill,4)=bump;target.fill.alpha_mask[4]=true;target.channels[4]=true;target.values.bump=.7f;
  MaterialValues values;values.base_color={.1f,.2f,.3f,1};values.metallic=.4f;values.roughness=.6f;
  const auto original_pixels=source.texture->pixels;apply_blend_material_channels(target,values,&source,false);same_mapping(target.fill,source);
  assert(target.fill.texture==source.texture&&target.fill.texture->pixels==original_pixels&&fill_channel_texture(target.fill,4)==bump&&target.fill.alpha_mask[4]&&target.fill.bump_bias==-.5f);
  assert((target.fill.native_per_uv==std::array<double,2>{3,7})&&target.fill.cm_per_native==100&&target.channels[4]&&target.values.bump==.7f&&!target.fill.alpha_mask[0]&&!target.fill.alpha_mask[1]);
 }
 source.mode=0;source.wrap=0;source.filtering=0;assert(blend_material_mapping_json(source)==immutable);
 const auto valid=blend_material_bindings_json(source);
 for(int mode:{1,5}){QJsonObject legacy{{"version",1},{"mode",mode},{"filter",0},{"wrap",0},{"images",images}};rejects([&]{read_blend_material_bindings(legacy);});}
 {QJsonObject legacy{{"version",1},{"mode",0},{"filter",0},{"wrap",3},{"images",images}};rejects([&]{read_blend_material_bindings(legacy);});}
 for(const char *field:{"version","mode","filter","wrap"})for(const QJsonValue value:{QJsonValue(-1),QJsonValue(7.5),QJsonValue("1"),QJsonValue(true)}){auto bad=valid;bad[field]=value;rejects([&]{read_blend_material_bindings(bad);});}
 for(const char *field:{"mapping","images"}){auto bad=valid;bad.remove(field);rejects([&]{read_blend_material_bindings(bad);});}
 for(int mutation=0;mutation<8;++mutation){auto bad=valid;auto mapping=bad["mapping"].toObject();auto space=mapping["spatial"].toObject();
  if(mutation==0)mapping["scale"]=QJsonArray{0,1};
  if(mutation==1)mapping["offset"]=QJsonArray{1};
  if(mutation==2)mapping["linked"]=true;
  if(mutation==3)mapping["gizmo"]=1.5;
  if(mutation==4)space["scale3"]=QJsonArray{0,1,1};
  if(mutation==5)space["mode"]=6; // Contradicts top-level mode0.
  if(mutation==6)space["asset_size_cm"]=QJsonArray{0,1,1};
  if(mutation==7)space.remove("warp");
  mapping["spatial"]=space;bad["mapping"]=mapping;rejects([&]{read_blend_material_bindings(bad);});
 }
 auto bad=valid;bad["images"]=QJsonArray{QJsonValue(QJsonValue::Null),QJsonValue(QJsonValue::Null),QJsonValue(QJsonValue::Null)};rejects([&]{read_blend_material_bindings(bad);});
 bad=valid;bad["images"]=QJsonArray{fill_texture_json(image(true)),QJsonValue(QJsonValue::Null),QJsonValue(QJsonValue::Null)};rejects([&]{read_blend_material_bindings(bad);});
 bad=valid;bad["version"]=1;rejects([&]{read_blend_material_bindings(bad);}); // v2 mapping cannot masquerade as v1.
 auto invalid=source;invalid.scale[0]=std::numeric_limits<double>::infinity();rejects([&]{blend_material_bindings_json(invalid);});
 TextureLayer untouched;untouched.values.metallic=.8f;untouched.fill.rotation=12;const auto saved=blend_material_mapping_json(untouched.fill);rejects([&]{apply_blend_material_channels(untouched,MaterialValues{},&invalid,false);});assert(untouched.values.metallic==.8f&&blend_material_mapping_json(untouched.fill)==saved);
}
inline void native_modes(){
 NativeMaterialDescription description;description.material="BJ source";description.group="Group";description.surface_type="ShaderNodeGroup";
 auto asset=native_material_asset("BJ native",QByteArray("immutable archive"),description);asset.search_terms.append("native-channel:0");const auto unchanged=asset.bytes;
 for(int mode=0;mode<=6;++mode)for(int wrap=0;wrap<=3;++wrap){TextureLayer layer;layer.kind=LayerKind::Fill;layer.fill=spatial();layer.fill.mode=mode;layer.fill.wrap=wrap;const auto expected=layer.fill;const bool shading_gate=layer.channels[4];const auto source_before=native_material_description_json(*asset.fill_preset->native_material);apply_native_material_bindings(layer,asset,true);same_mapping(layer.fill,expected);assert(layer.fill.native_material==asset.fill_preset->native_material&&!layer.channels[3]&&layer.channels[4]==shading_gate&&!native_material_channel_supported(asset,4)&&native_material_description_json(*layer.fill.native_material)==source_before);}
 for(int mutation=0;mutation<5;++mutation){TextureLayer layer;layer.kind=LayerKind::Fill;layer.fill.rotation=17;
  if(mutation==0)layer.fill.mode=7;if(mutation==1)layer.fill.wrap=4;if(mutation==2)layer.fill.scale3[0]=0;if(mutation==3)layer.kind=LayerKind::Paint;if(mutation==4)layer.instance=LayerInstance{};
  const auto before=blend_material_mapping_json(layer.fill);const auto channels=layer.channels;rejects([&]{apply_native_material_bindings(layer,asset,true);});assert(blend_material_mapping_json(layer.fill)==before&&layer.channels==channels&&!layer.fill.native_material);
 }
 auto unsupported=asset;unsupported.search_terms.removeAll("native-channel:0");TextureLayer layer;layer.kind=LayerKind::Fill;rejects([&]{apply_native_material_bindings(layer,unsupported,true);});assert(!layer.fill.native_material&&asset.bytes==unchanged);
}
inline void run(){codec();native_modes();}
}
