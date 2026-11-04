#include "MaterialLayerParameterIO.hh"
#include "MaterialLayeringIO.hh"
#include "MaterialLayeringAuthoring.hh"
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <limits>

namespace bts {
namespace {
[[noreturn]] void invalid(const char *why){throw std::invalid_argument(why);}
void check_cancel(const std::atomic_bool &cancel){if(cancel.load())throw std::runtime_error("Material parameter export cancelled");}
QString id(const QUuid &value){return value.toString(QUuid::WithoutBraces);}
constexpr auto shader_id="5cfc01b9-9a57-40e7-91ef-f43b79dc75f5";
QJsonObject shader_json(const ShaderParameters &value){
 value.validate();if(value.openpbr)invalid("Layering parameter packages require the ordinary Principled workflow");
 ShaderInstances library;const QUuid key(shader_id);library.default_instance=key;library.instances.emplace(key,ShaderInstance{key,"Parameters",value});
 return library.to_json()["instances"].toArray().first().toObject()["parameters"].toObject();
}
ShaderParameters read_shader(const QJsonValue &value){
 const auto key=QString::fromLatin1(shader_id);const QJsonObject root{{"version",3},{"default",key},{"instances",QJsonArray{QJsonObject{{"id",key},{"name","Parameters"},{"shader","principled-metallic-roughness"},{"parameters",value}}}},{"texture_sets",QJsonObject{}}};
 return ShaderInstances::from_json(root).instances.begin()->second.parameters;
}
int integer(const QJsonValue &value){if(!value.isDouble()||!std::isfinite(value.toDouble())||value.toDouble()<std::numeric_limits<int>::min()||value.toDouble()>std::numeric_limits<int>::max()||value.toDouble()!=value.toInt())invalid("Invalid material parameter integer");return value.toInt();}
void context(const QString &name,const std::array<double,9> &m){
 if(name.isEmpty()||name.contains(QChar(0)))invalid("Missing scene-working color identity");for(double value:m)if(!std::isfinite(value))invalid("Nonfinite scene-working matrix");
 const double determinant=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
 if(!std::isfinite(determinant)||determinant==0)invalid("Singular scene-working matrix");
}
using OriginalIndex=std::map<std::string,const MaterialParameterOriginal *>;
OriginalIndex validate(const MaterialLayerParameters &v){
 context(v.working_space,v.scene_linear_to_xyz);shader_json(v.shader);
 if(v.name.isEmpty()||v.name.contains(QChar(0))||v.tiles.empty())invalid("Missing parameter package identity/tiles");
 require_texture_ram(resource_sum(resource_product(v.name.size()+v.working_space.size(),8),resource_product(v.originals.size(),sizeof(MaterialParameterOriginal))),"Layering parameter metadata");
 std::set<int> tiles;for(const auto &[tile,size]:v.tiles){if(tile<1001||tile>9999||size[0]<1||size[1]<1)invalid("Invalid parameter destination tile");tiles.insert(tile);}
 OriginalIndex originals;std::map<QString,qint64> content;std::uint64_t file_bytes=0;for(const auto &original:v.originals){file_bytes=resource_sum(file_bytes,resource_product(original.files.size(),sizeof(MaterialParameterFile)+128));for(const auto &file:original.files)file_bytes=resource_sum(file_bytes,resource_product(resource_sum(file.descriptor.filename.size(),file.descriptor.path.size()),16));}require_texture_ram(file_bytes,"Parameter original descriptors");
 for(const auto &original:v.originals){if(original.asset.isNull()||!originals.emplace(id(original.asset).toStdString(),&original).second)invalid("Duplicate/invalid parameter resource identity");std::vector<TextureSourceFile> files;
  for(const auto &file:original.files){files.push_back(file.descriptor);if(!file.bytes.isEmpty()&&file.bytes.size()!=file.descriptor.size)invalid("Original byte size differs from its descriptor");const auto [at,fresh]=content.emplace(file.descriptor.sha256,file.descriptor.size);if(!fresh&&at->second!=file.descriptor.size)invalid("Conflicting original size for one checksum");}validate_texture_sources(files);
 }
 std::set<std::string> used;
 const auto resource=[&](const std::shared_ptr<const FillTexture> &source){if(!source)return;source->validate();const auto found=originals.find(source->asset);if(found==originals.end())invalid("Parameter resource is not included in package");used.insert(source->asset);std::vector<TextureSourceFile> files;for(const auto &file:found->second->files)files.push_back(file.descriptor);
  if(texture_sources_hash(files).toStdString()!=source->hash)invalid("Parameter source checksum differs from original members");
  if(source->tiles)require_material_layer_udim_originals(*source,files);else if(files.size()!=1||files[0].tile!=0)invalid("Ordinary source requires one ordinary original");
 };
 for(int i=0;i<4;++i){const auto &input=v.materials[i];input.validate();require_material_layer_udim_coverage(input,tiles);
  if(input.textures[3]||input.values.normal!=std::array<float,3>{0,0,1}||input.values.bump!=.5f)invalid("Parameter packages preserve hidden Normal and neutral uniform Bump restrictions");
  if(const auto &color=input.textures[0]){
   if(color->tiles){if(!v.color_processors[i].isEmpty()||v.color_members[i].size()!=color->tiles->size())invalid("Color UDIM parameters require an exact per-member processor table");for(const auto &[tile,member]:*color->tiles){const auto at=v.color_members[i].find(tile);if(at==v.color_members[i].end()||at->second.input_space.isEmpty()||at->second.processor.isEmpty()||at->second.input_space.contains(QChar(0))||at->second.processor.contains(QChar(0)))invalid("Invalid Color UDIM processor/input-space identity");}}
   else if(color->source_space.empty()||v.color_processors[i].isEmpty()||!v.color_members[i].empty())invalid("Ordinary Color parameters require their effective space and one processor");
  }else if(!v.color_processors[i].isEmpty()||!v.color_members[i].empty())invalid("Unexpected parameter Color processor");
  for(const auto &source:input.textures)resource(source);
 }
 for(const auto &mask:v.masks){if(!mask||!mask->raw||!mask->tiles||mask->tiles->size()!=tiles.size())invalid("Each opacity stack requires raw members for every destination tile");resource(mask);for(const auto &[tile,size]:v.tiles){const auto at=mask->tiles->find(tile);if(at==mask->tiles->end()||at->second->width!=size[0]||at->second->height!=size[1])invalid("Opacity member dimensions/tiles differ from destination");}}
 if(used.size()!=originals.size())invalid("Unreferenced original in material parameter package");return originals;
}
std::map<QString,QString> payload_layout(const MaterialLayerParameters &v){
 std::map<QString,QString> result;
 for(const auto &original:v.originals)for(const auto &file:original.files){auto suffix=QFileInfo(file.descriptor.filename).suffix().toLower();if(suffix.isEmpty()||std::any_of(suffix.begin(),suffix.end(),[](QChar c){return !c.isLetterOrNumber();}))suffix="image";const QString path="payloads/"+file.descriptor.sha256+"."+suffix;auto [at,fresh]=result.emplace(file.descriptor.sha256,path);if(!fresh&&path<at->second)at->second=path;}
 return result;
}
QString contained_file(const QDir &root,const QString &relative){
 if(relative.isEmpty()||QDir::isAbsolutePath(relative)||(relative.size()>1&&relative[0].isLetter()&&relative[1]==':')||relative.startsWith("//")||relative.contains('\\')||relative.contains(QChar(0))||QDir::cleanPath(relative)!=relative||relative==".."||relative.startsWith("../"))invalid("Parameter payload path escapes package");
 const QFileInfo file(root.filePath(relative));const auto canonical=file.canonicalFilePath();const auto parent=root.canonicalPath();if(parent.isEmpty()||canonical.isEmpty()||!file.isFile()||!canonical.startsWith(parent+'/'))invalid("Parameter payload is missing or outside package");return canonical;
}
void copy_original(const MaterialParameterFile &source,const QString &path,const std::atomic_bool &cancel){
 QBuffer buffer;QFile input(source.descriptor.path);QIODevice *stream=&input;if(!source.bytes.isEmpty()){buffer.setData(source.bytes);stream=&buffer;}
 if(!stream->open(QIODevice::ReadOnly))invalid("Cannot read original parameter source");if(stream->size()!=source.descriptor.size)invalid("Original parameter source changed size");
 QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))invalid("Cannot stage parameter payload");QCryptographicHash hash(QCryptographicHash::Sha256);qint64 total=0;
 require_texture_ram(4*1024*1024,"Parameter source transfer chunk");
 while(!stream->atEnd()){check_cancel(cancel);const auto chunk=stream->read(4*1024*1024);if(chunk.isEmpty())invalid("Cannot read complete parameter source");hash.addData(chunk);if(output.write(chunk)!=chunk.size())invalid("Cannot write parameter payload");total+=chunk.size();}
 if(total!=source.descriptor.size||QString::fromLatin1(hash.result().toHex())!=source.descriptor.sha256)invalid("Original parameter checksum changed");check_cancel(cancel);if(!output.commit())invalid("Cannot commit staged parameter payload");
}
}
QJsonObject material_layer_parameters_json(const MaterialLayerParameters &v){
 validate(v);const auto paths=payload_layout(v);QJsonArray materials,masks,tiles,originals,matrix;
 bool version2=false;
 for(int i=0;i<4;++i){QJsonValue processor=v.color_processors[i];if(!v.color_members[i].empty()){version2=true;QJsonArray members;for(const auto &[tile,member]:v.color_members[i])members.append(QJsonObject{{"tile",tile},{"input_space",member.input_space},{"processor",member.processor}});processor=QJsonObject{{"contract","linear-color-udim-v1"},{"members",members}};}materials.append(QJsonObject{{"input",i},{"material",layer_material_json(v.materials[i])},{"color_processor",processor}});}
 for(int i=0;i<3;++i)masks.append(QJsonObject{{"stack",i},{"signal","clamp(shader-image-R,0,1)"},{"image",fill_texture_json(v.masks[i])}});
 for(const auto &[tile,size]:v.tiles)tiles.append(QJsonObject{{"tile",tile},{"width",size[0]},{"height",size[1]}});
 for(double value:v.scene_linear_to_xyz)matrix.append(value);
 std::map<QString,const MaterialParameterOriginal *> ordered;for(const auto &original:v.originals)ordered.emplace(id(original.asset),&original);
 for(const auto &[asset,original]:ordered){QJsonArray files;for(const auto &file:original->files){QJsonObject row{{"tile",file.descriptor.tile},{"filename",file.descriptor.filename},{"sha256",file.descriptor.sha256},{"size",file.descriptor.size},{"path",paths.at(file.descriptor.sha256)}};files.append(row);}originals.append(QJsonObject{{"asset",asset},{"files",files}});}
 return {{"kind","bts-material-layering-parameters"},{"version",version2?2:1},{"source_members_version",version2?4:3},{"name",v.name},{"enabled",v.enabled},{"working_space",v.working_space},{"scene_linear_to_xyz",matrix},{"shader_parameters",shader_json(v.shader)},{"materials",materials},{"opacity_stacks",masks},{"tiles",tiles},{"originals",originals}};
}
MaterialLayerParameters read_material_layer_parameters_json(const QJsonValue &value){
 if(!value.isObject())invalid("Invalid material parameter manifest");const auto o=value.toObject();if(o.size()!=12||o["kind"]!="bts-material-layering-parameters"||!((o["version"]==1&&o["source_members_version"]==3)||(o["version"]==2&&o["source_members_version"]==4))||!o["name"].isString()||!o["enabled"].isBool()||!o["working_space"].isString()||!o["scene_linear_to_xyz"].isArray()||o["scene_linear_to_xyz"].toArray().size()!=9||!o["materials"].isArray()||o["materials"].toArray().size()!=4||!o["opacity_stacks"].isArray()||o["opacity_stacks"].toArray().size()!=3||!o["tiles"].isArray()||!o["originals"].isArray())invalid("Unsupported material parameter manifest");
 MaterialLayerParameters v;v.name=o["name"].toString();v.enabled=o["enabled"].toBool();v.working_space=o["working_space"].toString();v.shader=read_shader(o["shader_parameters"]);
 for(int i=0;i<9;++i){const auto n=o["scene_linear_to_xyz"].toArray()[i];if(!n.isDouble())invalid("Invalid scene-working matrix element");v.scene_linear_to_xyz[i]=n.toDouble();}
 const auto inputs=o["materials"].toArray(),masks=o["opacity_stacks"].toArray();
 for(int i=0;i<4;++i){const auto row=inputs[i].toObject();if(row.size()!=3||integer(row["input"])!=i)invalid("Invalid parameter input order");v.materials[i]=read_layer_material(row["material"]);const auto processor=row["color_processor"];
  if(processor.isString())v.color_processors[i]=processor.toString();else{const auto table=processor.toObject();if(o["version"]!=2||table.size()!=2||table["contract"]!="linear-color-udim-v1"||!table["members"].isArray())invalid("Invalid versioned Color UDIM processor contract");for(const auto &value:table["members"].toArray()){const auto member=value.toObject();if(member.size()!=3||!member["input_space"].isString()||!member["processor"].isString()||!v.color_members[i].emplace(integer(member["tile"]),MaterialParameterColorMember{member["input_space"].toString(),member["processor"].toString()}).second)invalid("Invalid or duplicate Color UDIM processor member");}}
 }
 for(int i=0;i<3;++i){const auto row=masks[i].toObject();if(row.size()!=3||integer(row["stack"])!=i||row["signal"]!="clamp(shader-image-R,0,1)")invalid("Invalid opacity stack order/signal");v.masks[i]=read_fill_texture_json(row["image"]);}
 for(const auto &value:o["tiles"].toArray()){const auto row=value.toObject();if(row.size()!=3||!v.tiles.emplace(integer(row["tile"]),std::array<int,2>{integer(row["width"]),integer(row["height"])}).second)invalid("Duplicate parameter destination tile");}
 for(const auto &value:o["originals"].toArray()){const auto row=value.toObject();const QUuid asset(row["asset"].toString());if(row.size()!=2||!row["asset"].isString()||id(asset)!=row["asset"].toString())invalid("Invalid parameter source UUID");MaterialParameterOriginal original;original.asset=asset;for(const auto &file:read_texture_sources(row["files"]))original.files.push_back({file,{}});v.originals.push_back(std::move(original));}
 validate(v);if(material_layer_parameters_json(v)!=o)invalid("Noncanonical parameter manifest or payload layout");return v;
}
void write_material_layer_parameter_package(const QString &directory,const MaterialLayerParameters &v,const std::atomic_bool &cancel){
 const auto manifest=material_layer_parameters_json(v);check_cancel(cancel);QFileInfo destination(directory);if(!destination.isAbsolute()||destination.exists()||destination.fileName().isEmpty())invalid("Choose a new absolute parameter package directory");QDir parent(destination.absolutePath());if(!parent.exists())invalid("Parameter package parent is missing");
 QTemporaryDir staging(parent.filePath(".bts-parameters-XXXXXX"));if(!staging.isValid()||!QDir(staging.path()).mkdir("payloads"))invalid("Cannot create parameter staging directory");const auto paths=payload_layout(v);std::set<QString> written;
 for(const auto &original:v.originals)for(const auto &file:original.files)if(written.insert(file.descriptor.sha256).second)copy_original(file,QDir(staging.path()).filePath(paths.at(file.descriptor.sha256)),cancel);
 const auto bytes=QJsonDocument(manifest).toJson(QJsonDocument::Indented);require_texture_ram(resource_product(bytes.size(),3),"Parameter manifest encoding");QSaveFile json(QDir(staging.path()).filePath("parameters.json"));if(!json.open(QIODevice::WriteOnly)||json.write(bytes)!=bytes.size()||!json.commit())invalid("Cannot write parameter manifest");check_cancel(cancel);
 if(!parent.rename(staging.path(),destination.absoluteFilePath()))invalid("Cannot publish parameter package");staging.setAutoRemove(false);
}
MaterialLayerParameters read_material_layer_parameter_package(const QString &directory,const std::atomic_bool &cancel){
 QDir root(directory);if(!root.isAbsolute()||root.canonicalPath().isEmpty())invalid("Parameter package directory is missing");const auto path=contained_file(root,"parameters.json");QFile input(path);if(!input.open(QIODevice::ReadOnly))invalid("Cannot read parameter manifest");require_texture_ram(resource_product(input.size(),8),"Parameter manifest decode");QJsonParseError error;const auto json=QJsonDocument::fromJson(input.readAll(),&error);if(error.error!=QJsonParseError::NoError||!json.isObject())invalid("Invalid parameter JSON");auto result=read_material_layer_parameters_json(json.object());
 std::map<QString,QString> checked;
 for(auto &original:result.originals)for(auto &file:original.files){check_cancel(cancel);const auto path=contained_file(root,file.descriptor.path);if(QFileInfo(path).size()!=file.descriptor.size)invalid("Parameter payload size differs");auto [at,fresh]=checked.emplace(file.descriptor.sha256,path);if(!fresh&&at->second!=path)invalid("One parameter encoding has multiple payloads");if(fresh&&hash_texture_file(path,&cancel)!=file.descriptor.sha256)invalid("Parameter payload checksum differs");file.descriptor.path=path;}
 return result;
}
void require_material_parameter_main(const TextureSet &source){
 require_uniform_material_layering(source);if(source.ao_channel||source.mesh_maps[3]||source.displacement.channel)invalid("Parameter package excludes retained Main AO/displacement contributions");
 for(int slot=0;slot<custom_channel_count;++slot)if(source.users[slot].present&&source.users[slot].scattering)invalid("Parameter package excludes retained Main Subsurface scattering channel");
}
TextureSet reconstruct_material_layer_parameters(const TextureSet &source,const MaterialLayerParameters &v,const QString &space,const std::array<double,9> &matrix,const MaterialParameterDecoder &decode,const MaterialParameterProcessor &processor,const MaterialParameterTargetValidator &validate_target,const MaterialParameterColorSequenceValidator &validate_sequence){
 const auto originals=validate(v);if(space!=v.working_space||matrix!=v.scene_linear_to_xyz)invalid("Parameter package uses another scene-working color context");if(!decode)invalid("Parameter reconstruction requires the actual shared decoder");
 if(source.material_layering.allocated())invalid("Reconstruct parameters into a fresh compatible Texture Set");require_material_parameter_main(source);if(!validate_target)invalid("Parameter reconstruction requires actual native surface validation");validate_target(source);
 for(const auto &input:v.materials)if(input.textures[0]&&input.textures[0]->tiles&&(!validate_sequence||!processor))invalid("Color UDIM reconstruction requires native linear admission before decode");
 std::set<int> tiles;for(const auto &[tile,size]:v.tiles){tiles.insert(tile);if(!source.tiles.contains(tile)||source.width(tile)!=size[0]||source.rows(tile)!=size[1])invalid("Parameter target dimensions differ");}if(tiles!=source.tiles)invalid("Parameter target tiles differ");
 const auto hydrate=[&](const std::shared_ptr<const FillTexture> &descriptor,bool mask){auto result=decode(*descriptor,*originals.at(descriptor->asset),mask);if(!result||fill_texture_json(result)!=fill_texture_json(descriptor))invalid("Decoded parameter source differs from manifest");if(result->tiles){for(const auto &[tile,member]:*result->tiles)if(!member->pixels)invalid("Unresolved parameter member");}else if(!result->pixels)invalid("Unresolved parameter image");return result;};
 auto candidate=source;auto inputs=v.materials;for(int i=0;i<4;++i)for(int c=0;c<6;++c)if(inputs[i].textures[c]){inputs[i].textures[c]=hydrate(inputs[i].textures[c],false);if(c==0){const auto &color=*inputs[i].textures[c];if(color.tiles){if(!validate_sequence||!processor)invalid("Color UDIM reconstruction requires actual native linear processor admission");validate_sequence(color);for(const auto &[tile,member]:*color.tiles){const auto &expected=v.color_members[i].at(tile);if(QString::fromStdString(member->input_space)!=expected.input_space||processor(*member)!=expected.processor)invalid("Color UDIM member processor/input space differs from validated original");}}else if(!processor||processor(color)!=v.color_processors[i])invalid("Parameter Color processor differs from validated original");}}
 std::array<std::shared_ptr<const FillTexture>,3> masks;for(int i=0;i<3;++i){masks[i]=hydrate(v.masks[i],true);for(const auto &[tile,image]:*masks[i]->tiles)for(const auto &pixel:*image->pixels)if(!std::isfinite(pixel[0])||pixel[0]<0||pixel[0]>1||pixel[1]!=0||pixel[2]!=0||pixel[3]!=1)invalid("Parameter opacity image is not normalized raw R/alpha1");}
 configure_material_layering(candidate,true);candidate.material_layering.materials=inputs;candidate.shader_parameters=v.shader;
 for(int i=0;i<3;++i){const auto root=candidate.material_layering.mask_roots[i];std::erase_if(candidate.layers,[&](const TextureLayer &layer){return layer.parent==root;});TextureLayer layer;layer.kind=LayerKind::Fill;layer.name="Imported opacity "+std::to_string(i+1);layer.parent=root;layer.depth=1;layer.channels.fill(false);layer.ao_enabled=false;layer.displacement.enabled=false;for(auto &user:layer.users)user.enabled=false;const int slot=candidate.material_layering.mask_slots[i];layer.users[slot].enabled=true;layer.users[slot].value=1;candidate.users[slot].precision=ChannelPrecision::L32F;layer.fill.mode=6;layer.fill.filtering=2;fill_channel_texture(layer.fill,6+slot)=masks[i];layer.fill.alpha_mask[6+slot]=false;candidate.material_layering.mask_selections[i]=layer.id;
  const auto at=std::find_if(candidate.layers.begin(),candidate.layers.end(),[&](const auto &value){return value.id==root;});candidate.layers.insert(at,std::move(layer));}
 select_material_substack(candidate,0);if(!v.enabled)configure_material_layering(candidate,false);TextureDocument::validate_hierarchy(candidate);validate_material_substacks(candidate);require_uniform_material_layering(candidate);return candidate;
}
}
