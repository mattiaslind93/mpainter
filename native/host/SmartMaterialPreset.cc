#include "ResourceAvailability.hh"
#include "SmartMaterialPreset.hh"
#include "EffectPaintIO.hh"
#include "FillTexturePool.hh"
#include "HistoryArchiveCodec.hh"
#include "MaskPresetTextures.hh"
#include "PaintRecording.hh"
#include "SparsePresetIO.hh"
#include <QJsonDocument>
#include <algorithm>
#include <map>
namespace bts {
namespace {
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid smart-material preset");}
std::array<TilePaintPixels *,11> planes(TextureLayer &layer){std::array<TilePaintPixels *,11> result{&layer.pixels,&layer.ao_pixels};for(int i=0;i<8;++i)result[2+i]=&layer.users[i].pixels;result[10]=&layer.displacement.pixels;return result;}
std::uint64_t preset_budget(){const auto available=texture_available_ram_bytes();return std::numeric_limits<std::uint64_t>::max();}
void set_stream(QDataStream &stream){stream.setVersion(QDataStream::Qt_6_8);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);}
TextureDocument preset_document(const TextureSet &source){
 NewProjectOptions options;options.resolution=DocumentResolution(source.texture_resolution);options.uv_workflow=UvWorkflow::PreserveTiles;
 ImportSummary imported;imported.materials={{1,"Material Preset",source.tiles}};
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Material Preset";material.uniform_editable=true;scene.materials.push_back(material);
 TextureDocument document(options,imported,scene);auto &set=document.set(1);const auto id=set.id;set=source;set.id=id;set.material_session_uid=1;set.name="Material Preset";return document;
}
unsigned generator_required_maps(const MeshGeneratorSettings &settings){if(settings.optional_inputs)return 0;unsigned result=0;for(int slot=0;slot<generator_input_count(settings.generator);++slot)if(settings.input_anchors[slot].source.isNull())result|=1u<<generator_input_role(settings.generator,slot);return result;}
void local_references(const TextureSet &set){
 std::set<QUuid> anchors;for(const auto &layer:set.layers){if(layer.instance)throw std::invalid_argument("Make layer instances unique before creating a material preset");if(layer.quick_overlay)throw std::invalid_argument("Finish the current preview before creating a material preset");for(const auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Anchor)anchors.insert(effect.id);if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Anchor)anchors.insert(effect.id);}
 for(const auto &layer:set.layers)if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(!effect.anchor_source.isNull()&&!anchors.contains(effect.anchor_source))throw std::invalid_argument("Material preset references an anchor outside its folder");
 const auto check_generator=[&](const MeshGeneratorSettings &e){for(const auto &ref:e.input_anchors)if(!ref.source.isNull()&&!anchors.contains(ref.source))throw std::invalid_argument("Material preset references an anchor outside its folder");};
 for(const auto &layer:set.layers){for(const auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Generator)check_generator(effect);if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Generator)check_generator(effect);}
 for(const auto &layer:set.layers)for(const auto &ref:layer.fill.anchors)if(!ref.source.isNull()&&!anchors.contains(ref.source))throw std::invalid_argument("Material preset references an anchor outside its folder");
}
void clear_runtime(TextureSet &set){set.mesh_maps={};set.material_layering={};set.skew={};set.instance_layers.reset();set.instance_hierarchies.clear();set.instance_problems.clear();set.geometry_names.clear();set.geometry_ownership.clear();set.projection_geometry.reset();set.projection_tiles.clear();}
}
SmartMaterialPreset capture_material_folder(const TextureSet &source,const QUuid &folder){
 PaintMetadataScope metadata;const auto end=std::find_if(source.layers.begin(),source.layers.end(),[&](const auto &layer){return layer.id==folder;});
 if(end==source.layers.end()||end->kind!=LayerKind::Folder)throw std::invalid_argument("Select a folder to create a material preset");
 auto begin=end;while(begin!=source.layers.begin()&&(begin-1)->depth>end->depth)--begin;
 SmartMaterialPreset result{source};auto &set=result.set;set.layers.assign(begin,end+1);set.layers.back().parent=QUuid();set.selected_layer=folder;set.selected_mask=false;set.selected_geometry=false;set.selected_effect=QUuid();clear_runtime(set);TextureDocument::validate_hierarchy(set);local_references(set);return result;
}
QByteArray write_material_preset(const SmartMaterialPreset &preset){
 GpuPersistenceScope no_cpu_recipes;local_references(preset.set);auto document=preset_document(preset.set);const auto &set=document.set(1);
 LayerMask bank;QJsonArray targets;
 for(const auto &layer:set.layers){
  for(int channel=0;channel<15;++channel){const auto &texture=fill_channel_texture(layer.fill,channel);if(!texture)continue;MaskEffect entry;entry.kind=MaskEffectKind::Fill;entry.mapping.projection.texture=texture;targets.append(QJsonObject{{"binding",entry.id.toString()},{"layer",layer.id.toString()},{"channel",channel}});bank.effects.push_back(std::move(entry));}
  for(const auto &effect:layer.effects)if(effect.bitmap_fill){MaskEffect entry;entry.kind=MaskEffectKind::Fill;entry.id=effect.id;entry.mapping=*effect.bitmap_fill;bank.effects.push_back(entry);targets.append(QJsonObject{{"binding",entry.id.toString()},{"layer",layer.id.toString()},{"content_effect",entry.id.toString()}});}
  if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Fill&&effect.mapping.projection.texture){auto entry=effect;entry.kind=MaskEffectKind::Fill;bank.effects.push_back(entry);targets.append(QJsonObject{{"binding",entry.id.toString()},{"layer",layer.id.toString()},{"effect",entry.id.toString()}});}
 }
 const MaskPresetTextures textures(bank);QJsonObject root{{"version",1},{"document",document.to_json()},{"texture_targets",targets},{"texture_resources",textures.descriptors},{"texture_bindings",textures.bindings}};
 const auto json=QJsonDocument(root).toJson(QJsonDocument::Compact);
 HistoryArchiveWriter archive;QDataStream stream(&archive);set_stream(stream);stream<<quint32(0x42534d31)<<json;
 std::uint64_t decoded=std::uint64_t(json.size());const auto budget=preset_budget();
 for(auto layer:set.layers){for(const auto *plane:planes(layer))write_preset_plane(stream,*plane,decoded,budget);if(layer.mask)write_preset_plane(stream,layer.mask->pixels,decoded,budget);for(const auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Paint)write_effect_paint(stream,effect.paint,decoded,budget);if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Paint)write_effect_paint(stream,effect.paint,decoded,budget);}
 textures.write(stream,decoded,budget);if(stream.status()!=QDataStream::Ok)throw std::runtime_error("Cannot encode material preset");return archive.finish();
}
SmartMaterialPreset read_material_preset(const QByteArray &bytes){
 if(!bytes.startsWith(QByteArray(history_archive::magic,8)))invalid();
 HistoryArchiveReader archive(bytes);QDataStream stream(&archive);set_stream(stream);quint32 magic;stream>>magic;if(magic!=0x42534d31)invalid();
 // Validate the length before allowing QDataStream to allocate untrusted metadata.
 quint32 length;stream>>length;if(qint64(length)>archive.bytesAvailable())invalid();QByteArray json(qsizetype(length),Qt::Uninitialized);if(stream.readRawData(json.data(),length)!=qint64(length))invalid();
 QJsonParseError error;const auto root=QJsonDocument::fromJson(json,&error).object();if(error.error!=QJsonParseError::NoError||root["version"].toDouble()!=1)invalid();
 const auto metadata=root["document"].toObject();const auto descriptions=metadata["texture_sets"].toArray();if(descriptions.size()!=1)invalid();const auto description=descriptions[0].toObject();TextureSetBinding binding{QUuid(description["id"].toString()),1,description["name"].toString().toStdString()};
 auto document=TextureDocument::from_json(metadata,std::span(&binding,1));auto &set=document.set(1);
 std::uint64_t decoded=std::uint64_t(json.size());const auto budget=preset_budget();
 for(auto &layer:set.layers){auto output=planes(layer);for(int p=0;p<11;++p)*output[p]=read_preset_plane(stream,set.tiles,decoded,budget,p>0);
  if(layer.mask)layer.mask->pixels=read_preset_plane(stream,set.tiles,decoded,budget,true,true);
  for(auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Paint)read_effect_paint(stream,effect.paint,set.tiles,decoded,budget);
  if(layer.mask)for(auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Paint)read_effect_paint(stream,effect.paint,set.tiles,decoded,budget,true);
 }
 if(!root["texture_targets"].isArray()||root["texture_targets"].toArray().size()!=root["texture_bindings"].toArray().size())invalid();
 LayerMask bank;std::set<QUuid> bindings;for(const auto &target:root["texture_targets"].toArray()){MaskEffect entry;entry.kind=MaskEffectKind::Fill;entry.id=QUuid(target.toObject()["binding"].toString());if(entry.id.isNull()||!bindings.insert(entry.id).second)invalid();bank.effects.push_back(entry);}MaskPresetTextures::read(stream,bank,root,decoded,budget,bank.effects.size());
 for(const auto &target:root["texture_targets"].toArray()){const auto value=target.toObject();auto &layer=TextureDocument::layer(set,QUuid(value["layer"].toString()));const auto entry=std::find_if(bank.effects.begin(),bank.effects.end(),[&](const auto &e){return e.id==QUuid(value["binding"].toString());});if(entry==bank.effects.end()||!entry->mapping.projection.texture)invalid();
  if(value.contains("channel")){const int channel=value["channel"].toInt(-1);if(channel<0||channel>=15)invalid();auto &slot=fill_channel_texture(layer.fill,channel);if(!slot)invalid();slot=bind_fill_texture_descriptor(*slot,*entry->mapping.projection.texture);}
  else if(value.contains("content_effect")){auto effect=std::find_if(layer.effects.begin(),layer.effects.end(),[&](const auto &candidate){return candidate.id==QUuid(value["content_effect"].toString());});if(effect==layer.effects.end()||!effect->bitmap_fill||!effect->bitmap_fill->projection.texture)invalid();auto &slot=effect->bitmap_fill->projection.texture;slot=bind_fill_texture_descriptor(*slot,*entry->mapping.projection.texture);}
  else{if(!layer.mask)invalid();MaskEffect *effect=nullptr;for(auto &candidate:mask_effects_recursive(layer.mask->effects))if(candidate.id==QUuid(value["effect"].toString())){if(effect)invalid();effect=&candidate;}if(!effect||!effect->mapping.projection.texture)invalid();effect->mapping.projection.texture=bind_fill_texture_descriptor(*effect->mapping.projection.texture,*entry->mapping.projection.texture);}
 }
 const auto resolved=[](const std::shared_ptr<const FillTexture> &texture){if(!texture)return;if(!texture->pixels)invalid();if(texture->tiles)for(const auto &[tile,member]:*texture->tiles)if(!member->pixels)invalid();};
 for(const auto &layer:set.layers){for(int channel=0;channel<15;++channel)resolved(fill_channel_texture(layer.fill,channel));for(const auto &effect:layer.effects)if(effect.bitmap_fill)resolved(effect.bitmap_fill->projection.texture);if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))resolved(effect.mapping.projection.texture);}
 // Format 1 predates optional generator settings. Validate its original schema
 // first, then compare raster hashes against canonical metadata with defaults.
 const auto upgraded_metadata=document.to_json();auto expected=metadata;expected["schema"]=texture_document_schema;
 auto sets=expected["texture_sets"].toArray();for(int si=0;si<sets.size();++si){auto description=sets[si].toObject();auto layers=description["layers"].toArray();for(int li=0;li<layers.size();++li){auto layer=layers[li].toObject();if(metadata["schema"].toInt()<79&&!layer.contains("material_id"))layer["material_id"]=upgraded_metadata["texture_sets"].toArray()[si].toObject()["layers"].toArray()[li].toObject()["material_id"];if(layer["mask"].isObject()){auto mask=layer["mask"].toObject();auto effects=mask["effects"].toArray();for(int ei=0;ei<effects.size();++ei){auto effect=effects[ei].toObject();if(effect["type"]=="generator"){if(!effect.contains("dirt_curvature"))effect["dirt_curvature"]=.5;if(!effect.contains("light_direction"))effect["light_direction"]=QJsonArray{0,0,1};effects[ei]=effect;}}mask["effects"]=effects;layer["mask"]=mask;}layers[li]=layer;}description["layers"]=layers;sets[si]=description;}expected["texture_sets"]=sets;
 if(stream.status()!=QDataStream::Ok||!archive.complete())invalid();if(document.to_json()!=expected)invalid();local_references(set);clear_runtime(set);return {set};
}
QUuid apply_material_preset(TextureSet &destination,const SmartMaterialPreset &preset){
 PaintMetadataScope metadata;local_references(preset.set);auto block=preset.set.layers;if(block.empty()||block.back().kind!=LayerKind::Folder||!block.back().parent.isNull())invalid();
 for(const auto &layer:block)for(const auto &effect:layer.effects)if(effect.enabled&&effect.kind==ContentEffectKind::Generator){const auto required=generator_required_maps(effect);for(int role=0;role<7;++role)if(required&(1u<<role))for(int tile:destination.tiles)if(!destination.mesh_maps[role]||!destination.mesh_maps[role]->tiles.contains(tile)||!destination.mesh_maps[role]->tiles.at(tile).pixels)throw std::invalid_argument("Material preset requires "+std::string(mesh_map_names[role])+" for tile "+std::to_string(tile)+"; Bake Mesh Maps (Ctrl+Shift+B)");}
 for(const auto &layer:block)if(layer.mask&&layer.mask->enabled)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.enabled){const auto required=effect.kind==MaskEffectKind::Generator?generator_required_maps(effect):effect.kind==MaskEffectKind::ColorSelection&&effect.use_id_map&&!effect.colors.empty()?1u<<2:0u;for(int role=0;role<7;++role)if(required&(1u<<role))for(int tile:destination.tiles)if(!destination.mesh_maps[role]||!destination.mesh_maps[role]->tiles.contains(tile)||!destination.mesh_maps[role]->tiles.at(tile).pixels)throw std::invalid_argument("Material preset requires "+std::string(mesh_map_names[role])+" for tile "+std::to_string(tile)+"; Bake Mesh Maps (Ctrl+Shift+B)");}
 auto candidate=destination;const auto selected=std::find_if(candidate.layers.begin(),candidate.layers.end(),[&](const auto &l){return l.id==candidate.selected_layer;});if(selected==candidate.layers.end())invalid();
 const auto parent=selected->kind==LayerKind::Folder?selected->id:selected->parent;const auto insertion=std::size_t(selected-candidate.layers.begin())+(selected->kind==LayerKind::Folder?0:1);
 std::map<QUuid,QUuid> ids,anchors;for(const auto &layer:block){ids[layer.id]=QUuid::createUuid();for(const auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Anchor)anchors[effect.id]=QUuid::createUuid();if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Anchor)anchors[effect.id]=QUuid::createUuid();}
 for(auto &layer:block){for(auto &ref:layer.fill.anchors)if(!ref.source.isNull())ref.source=anchors.at(ref.source);layer.id=ids.at(layer.id);layer.parent=layer.parent.isNull()?parent:ids.at(layer.parent);for(auto &effect:layer.effects){effect.id=anchors.contains(effect.id)?anchors.at(effect.id):QUuid::createUuid();for(auto &ref:effect.input_anchors)if(!ref.source.isNull())ref.source=anchors.at(ref.source);}if(layer.mask)for(auto &effect:mask_effects_recursive(layer.mask->effects)){effect.id=anchors.contains(effect.id)?anchors.at(effect.id):QUuid::createUuid();effect.levels.id=effect.id;for(auto &ref:effect.input_anchors)if(!ref.source.isNull())ref.source=anchors.at(ref.source);if(!effect.anchor_source.isNull())effect.anchor_source=anchors.at(effect.anchor_source);}
  const auto check=[&](const TilePaintPixels &pixels){for(const auto &[tile,plane]:pixels.tiles())if(!destination.tiles.contains(tile))throw std::invalid_argument("Smart-material paint uses a UV tile missing from the destination Texture Set");};check(layer.pixels);check(layer.ao_pixels);check(layer.displacement.pixels);for(const auto &user:layer.users)check(user.pixels);if(layer.mask)check(layer.mask->pixels);for(const auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Paint)for(int p=0;p<11;++p)check(effect.paint.plane(p));if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Paint)check(effect.paint.pixels);
 }
 for(int i=0;i<8;++i)if(preset.set.users[i].present){if(candidate.users[i].present&&(candidate.users[i].name!=preset.set.users[i].name||candidate.users[i].scattering!=preset.set.users[i].scattering))throw std::invalid_argument("Smart-material custom channel conflicts with this Texture Set");if(!candidate.users[i].present)candidate.users[i]=preset.set.users[i];}
 if(preset.set.displacement.channel&&!candidate.displacement.channel)candidate.displacement=preset.set.displacement;
 candidate.ao_channel|=preset.set.ao_channel;candidate.selected_layer=block.back().id;candidate.selected_mask=false;candidate.selected_effect=QUuid();candidate.selected_geometry=false;candidate.layers.insert(candidate.layers.begin()+insertion,block.begin(),block.end());TextureDocument::validate_hierarchy(candidate);const auto root=candidate.selected_layer;destination=std::move(candidate);return root;
}
}
