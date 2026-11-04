#include "ResourceAvailability.hh"
#include "TextureExtent.hh"
#include "ExportConfiguration.hh"
#include "ExportNaming.hh"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <map>
#include <set>
namespace bts {
namespace {
[[noreturn]] void fail(){throw std::invalid_argument("Invalid project export configuration");}
int integer(const QJsonValue &value,int minimum,int maximum){if(!value.isDouble())fail();const auto number=value.toDouble();if(!std::isfinite(number)||number<minimum||number>maximum||std::floor(number)!=number)fail();return int(number);}
QString text(const QJsonValue &value,qsizetype maximum=std::numeric_limits<qsizetype>::max()){if(!value.isString())fail();auto result=value.toString();if(result.size()>maximum||result.contains(QChar(0)))fail();return result;}
bool boolean(const QJsonValue &value){if(!value.isBool())fail();return value.toBool();}
QUuid uuid(const QJsonValue &value){const auto result=QUuid(text(value,38));if(result.isNull())fail();return result;}
QJsonValue format_json(std::optional<ExportFormat> value){return value?QJsonValue(int(*value)):QJsonValue(QJsonValue::Null);}
std::optional<ExportFormat> read_format(const QJsonValue &value){if(value.isNull())return {};return ExportFormat(integer(value,0,4));}
int size(const QJsonValue &value,bool inherited){const int result=integer(value,inherited?0:128,sparse_texture_max_side);if(result&&((result&(result-1))||result<128))fail();return result;}
}
ExportOutputPreset material_id_preset(){ExportOutputPreset p;p.id=QUuid("6e15bcb2-191a-4e19-b6d1-5db2bea50005");p.name="Material ID";p.standard_outputs=false;p.outputs={{"$mesh_$textureSet_material_id",ExportMapSource::MaterialId,false,ExportFormat::Png8}};return p;}
void set_default_export_formats(ExportOutputPreset &p){p.formats[0]=ExportFormat::Png8;p.formats[4]=ExportFormat::Png16;p.formats[8]=ExportFormat::Exr32;}
ExportOutputPreset converted_specular_glossiness_preset(){ExportOutputPreset p;p.id=QUuid("6e15bcb2-191a-4e19-b6d1-5db2bea50001");p.name="PBR Specular Glossiness (converted from Metallic Roughness)";p.standard_outputs=false;p.outputs={{"$mesh_$textureSet_diffuse",ExportMapSource::Diffuse,true},{"$mesh_$textureSet_specular",ExportMapSource::Specular,false},{"$mesh_$textureSet_glossiness",ExportMapSource::Glossiness,false},{"$mesh_$textureSet_bump",ExportMapSource::Bump,false},{"$mesh_$textureSet_emissive",ExportMapSource::Black,false}};p.outputs[0].format=ExportFormat::Png8;p.outputs[3].format=ExportFormat::Png16;return p;}
ExportOutputPreset usd_metal_rough_preset(){ExportOutputPreset p;p.id=QUuid("6e15bcb2-191a-4e19-b6d1-5db2bea50002");p.name="USD PBR Metal Roughness";p.kind=ExportPresetKind::UsdMetalRough;for(int c=0;c<9;++c)p.naming[c]="$mesh_$textureSet_"+std::string(export_channel_suffixes[c]);return p;}
ExportOutputPreset studio_preset(){ExportOutputPreset p;p.id=studio_preset_id;p.name="Studio";p.kind=ExportPresetKind::Studio;for(int c=0;c<9;++c){p.naming[c]="$mesh_"+std::string(export_channel_suffixes[c]);p.formats[c]=ExportFormat::Exr32;}return p;}
namespace {
// Page metadata only (including deferred recipes); never evaluates pixels.
bool has_pages(const TilePaintPixels &pixels,int channel){bool found=false;for(const auto &[number,tile]:pixels.tiles())if(!found)tile.visit_page_versions(channel,[&](std::uint32_t,std::uint64_t){found=true;});return found;}
bool has_scalar_pages(const TilePaintPixels &pixels){for(int c=0;c<5;++c)if(has_pages(pixels,c))return true;return false;}
// Layer channel index: 0-4 material channels, 14 displacement.
bool layer_passes(const TextureLayer &layer,int channel){
 if(!layer.visible)return false;
 if(channel==14)return layer.displacement.enabled&&layer.displacement.opacity>0&&layer.displacement.blending!=LayerBlend::Disabled;
 return layer.channels[channel]&&layer.opacity[channel]>0&&layer.blending[channel]!=LayerBlend::Disabled;
}
bool layer_has_content(const TextureLayer &layer,int channel){
 if(layer.kind==LayerKind::Fill||layer.instance)return true; // A fill always writes its enabled channels.
 if(channel==0&&layer.quick_overlay)return true;
 if(channel==14?has_scalar_pages(layer.displacement.pixels):has_pages(layer.pixels,channel))return true;
 for(const auto &effect:layer.effects){
  if(effect.kind==ContentEffectKind::Generator||effect.kind==ContentEffectKind::Anchor)return true;
  if(effect.kind==ContentEffectKind::Paint&&(channel==14?has_scalar_pages(effect.paint.displacement):has_pages(effect.paint.pixels,channel)))return true;
 }
 return false;
}
}
bool export_channel_used(const TextureSet &set,int channel){
 if(channel<0||channel>8||channel==3||channel==6)return false;
 if(channel==5||channel==7)return false; // Occlusion is not part of the Studio map set.
 const int layer_channel=channel==8?14:channel;
 if(channel==8){if(!set.displacement.channel||!set.displacement.enabled)return false;}
 else if(!set.channels[channel])return false;
 // Texture Set base values are not authoring: imports seed them from the source
 // material (e.g. Principled roughness 0.5), so they alone never export a map.
 if(set.material_layering.enabled)return true;
 const auto &layers=evaluation_layers(set);std::map<QUuid,const TextureLayer *> by_id;for(const auto &layer:layers)by_id[layer.id]=&layer;
 for(const auto &layer:layers){
  if(layer.kind==LayerKind::Folder||!layer_passes(layer,layer_channel)||!layer_has_content(layer,layer_channel))continue;
  bool visible=true;std::set<QUuid> seen;for(auto parent=layer.parent;visible&&!parent.isNull()&&seen.insert(parent).second;){const auto found=by_id.find(parent);if(found==by_id.end())break;visible=found->second->visible; // Folder channel masks vary by schema; only hidden folders are certain.
  parent=found->second->parent;}
  if(visible)return true;
 }
 return false;
}
void apply_export_preset(ExportRequest &request,const ExportOutputPreset &preset){
 request.studio=false;
 request.naming=preset.naming;request.standard_outputs=preset.standard_outputs;request.packed=preset.packed;request.outputs=preset.outputs;request.selected_channels.reset();request.formats=preset.formats;request.dither_normals=false;
 std::erase_if(request.outputs,[](const ExportMapOutput &map){return export_map_normal(map.source);});
 if(preset.kind==ExportPresetKind::UsdMetalRough){if(!preset.standard_outputs||preset.packed||!request.outputs.empty())throw std::invalid_argument("USD preset exports color, metallic, roughness and occlusion");request.selected_channels=std::array<bool,9>{true,true,true,false,false,false,false,true,false};for(int c:{0,1,2,7})request.formats[c]=ExportFormat::Jpeg8;request.format=ExportFormat::Png8;}
 if(preset.kind==ExportPresetKind::Studio){if(!preset.standard_outputs||preset.packed||!request.outputs.empty())throw std::invalid_argument("Studio exports only the used standard channels");request.selected_channels=std::array<bool,9>{true,true,true,false,true,false,false,false,true};request.formats.fill(ExportFormat::Exr32);request.format=ExportFormat::Exr32;request.studio=true;}
}
QJsonObject export_configuration_json(const ExportConfiguration &config){
  if(int(config.naming_convention)<0||int(config.naming_convention)>1||(config.openpbr_materialx&&(config.format!=ExportFormat::Exr32||!config.preset.isNull())))throw std::invalid_argument("MaterialX requires EXR32 Document channels and a valid naming convention");
  QJsonArray selected,sizes,presets;for(const auto &id:config.selected)selected.append(id.toString(QUuid::WithoutBraces));for(const auto &[id,value]:config.sizes)sizes.append(QJsonObject{{"id",id.toString(QUuid::WithoutBraces)},{"size",value}});
  for(const auto &preset:config.presets){QJsonArray formats;for(const auto &format:preset.formats)formats.append(format_json(format));QJsonArray names;for(const auto &name:preset.naming)names.append(canonical_export_pattern(QString::fromStdString(name)));QJsonValue packed;if(preset.packed){QJsonArray sources;for(auto source:preset.packed->source)sources.append(int(source));packed=QJsonObject{{"name",canonical_export_pattern(QString::fromStdString(preset.packed->name))},{"sources",sources},{"format",format_json(preset.packed->format)}};}QJsonArray outputs;for(const auto &map:preset.outputs){validate_export_map(map);outputs.append(QJsonObject{{"name",canonical_export_pattern(QString::fromStdString(map.name))},{"source",int(map.source)},{"alpha",map.alpha},{"format",format_json(map.format)}});}presets.append(QJsonObject{{"formats",formats},{"outputs",outputs},{"id",preset.id.toString(QUuid::WithoutBraces)},{"name",preset.name},{"names",names},{"standard_outputs",preset.standard_outputs},{ "kind",int(preset.kind)},{"packed",packed}});}
  return {{"version",8},{"naming_convention",int(config.naming_convention)},{"openpbr_materialx",config.openpbr_materialx},{"directory",config.directory},{"format",int(config.format)},{"selected",selected},{"preset",config.preset.isNull()?QString():config.preset.toString(QUuid::WithoutBraces)},{"size",config.size},{"sizes",sizes},{"padding",int(config.padding)},{"padding_width",config.padding_width},{"presets",presets},{"mesh_displacement",config.mesh_displacement}};
}
ExportConfiguration read_export_configuration(const QJsonObject &root,std::span<const QUuid> sets){
  ExportConfiguration result;result.directory=QDir::homePath();const std::set<QUuid> known(sets.begin(),sets.end());if(root.isEmpty()){result.selected=known;result.presets={studio_preset()};result.preset=studio_preset_id;return result;}
  const ResourceAvailability budget;const int version=integer(root["version"],1,8);result.directory=text(root["directory"]);const bool drive=result.directory.size()>=3&&result.directory[0].isLetter()&&result.directory[1]==':'&&(result.directory[2]=='/'||result.directory[2]=='\\');if(!QDir::isAbsolutePath(result.directory)&&!drive&&!result.directory.startsWith("\\\\"))fail();result.format=ExportFormat(integer(root["format"],0,version>=4?4:3));result.size=size(root["size"],true);result.padding=ExportPadding(integer(root["padding"],0,4));result.padding_width=integer(root["padding_width"],0,256);if(version>=5)result.mesh_displacement=boolean(root["mesh_displacement"]);
  if(version>=7){result.naming_convention=ExportNamingConvention(integer(root["naming_convention"],0,1));result.openpbr_materialx=boolean(root["openpbr_materialx"]);}
  if(!root["selected"].isArray()||!root["sizes"].isArray()||!root["presets"].isArray())fail();for(const auto &entry:root["selected"].toArray()){const auto id=uuid(entry);if(!known.contains(id)||!result.selected.insert(id).second)fail();}
  for(const auto &entry:root["sizes"].toArray()){if(!entry.isObject())fail();const auto object=entry.toObject();const auto id=uuid(object["id"]);if(!known.contains(id)||result.sizes.contains(id))fail();result.sizes[id]=size(object["size"],false);}
  const auto presets=root["presets"].toArray();std::set<QUuid> ids;for(const auto &entry:presets){if(!entry.isObject())fail();const auto object=entry.toObject();ExportOutputPreset preset;preset.id=uuid(object["id"]);if(!ids.insert(preset.id).second)fail();preset.name=text(object["name"]);if(preset.name.trimmed().isEmpty())fail();preset.standard_outputs=boolean(object["standard_outputs"]);if(version>=5)preset.kind=ExportPresetKind(integer(object["kind"],0,version>=8?2:1));if(!object["names"].isArray()||object["names"].toArray().size()!=(version==1?8:9))fail();for(int c=0;c<(version==1?8:9);++c)preset.naming[c]=canonical_export_pattern(text(object["names"].toArray()[c])).toStdString();
    if(version>=6){if(!object["formats"].isArray()||object["formats"].toArray().size()!=9)fail();for(int c=0;c<9;++c)preset.formats[c]=read_format(object["formats"].toArray()[c]);}
    if(version==1)preset.naming[8]="$textureSet_displacement";
    const auto packed=object["packed"];if(!packed.isNull()){if(!packed.isObject())fail();const auto data=packed.toObject();PackedOutput output;output.name=canonical_export_pattern(text(data["name"])).toStdString();if(!data["sources"].isArray()||data["sources"].toArray().size()!=3)fail();for(int c=0;c<3;++c)output.source[c]=PackedSource(integer(data["sources"].toArray()[c],0,version==1?6:7));if(version>=6)output.format=read_format(data["format"]);preset.packed=output;}if(version>=3){if(!object["outputs"].isArray())fail();for(const auto &entry:object["outputs"].toArray()){if(!entry.isObject())fail();const auto o=entry.toObject();if(o.size()!=(version>=6?4:3))fail();ExportMapOutput map{canonical_export_pattern(text(o["name"])).toStdString(),ExportMapSource(integer(o["source"],0,int(export_map_labels.size())-1)),boolean(o["alpha"])};if(version>=6)map.format=read_format(o["format"]);validate_export_map(map);preset.outputs.push_back(std::move(map));}}std::erase_if(preset.outputs,[](const ExportMapOutput &map){return export_map_normal(map.source);});ExportRequest validated;apply_export_preset(validated,preset);result.presets.push_back(std::move(preset));
  }
  const auto preset=text(root["preset"],38);if(!preset.isEmpty()){result.preset=QUuid(preset);if(result.preset.isNull()||!ids.contains(result.preset))fail();}
  // Schema 8 stores an explicit Document channels choice as "". Older files
  // could not distinguish it from "never chosen", so they adopt Studio once.
  else if(version<8&&!result.openpbr_materialx){if(!ids.contains(studio_preset_id)){result.presets.push_back(studio_preset());}result.preset=studio_preset_id;}if(result.openpbr_materialx&&(result.format!=ExportFormat::Exr32||!result.preset.isNull()))fail();return result;
}
}
