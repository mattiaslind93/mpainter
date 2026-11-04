#pragma once
#include "FlattenLayer.hh"
#include "BuiltinStandardMaterials.hh"
#include "BlendMaterialBindings.hh"
#include "FillTextureIO.hh"
#include "ImageSequence.hh"
#include "ResourceIO.hh"
#include "ProjectIO.hh"
#include "BKE_idprop.hh"
#include "BKE_material.hh"
#include "BKE_report.hh"
#include "BLO_readfile.hh"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"
#include <QJsonDocument>
#include <QScopeGuard>
#include "TextureExport.hh"
#include "BKE_main.hh"
#include "BKE_scene.hh"
#include "GPU_context.hh"
#include <OpenImageIO/imageio.h>
#include <QJsonArray>

namespace bts {
// Explicit CPU diagnostic/export oracle, never interactive authoring.
inline QJsonObject verify_sparse_udim_import_cpu(const QString &directory){
 using namespace blender;
 const auto check=[](bool ok,const char *message){if(!ok)throw std::runtime_error(std::string("Sparse UDIM regression: ")+message);};
 check(!blender::GPU_context_active_get(),"CPU oracle acquired a GPU context");
 check(QDir().mkpath(directory),"fixture directory");
 // An empty release library must not inject any material resources.
 const int micro_materials=0,micro_images=0;
 check(builtin_standard_material_assets().empty()&&builtin_standard_microtexture_assets().empty(),"release material library must be empty");

 const std::array<int,4> numbers{1001,1002,1011,2000};
 const std::array<std::array<int,2>,4> sizes{{{32,16},{16,32},{48,32},{32,48}}};
 for(int i=0;i<4;++i){
  const auto path=directory+QString("/source_%1.exr").arg(numbers[i]);auto writer=OIIO::ImageOutput::create(path.toStdString());check(bool(writer),"EXR writer");
  OIIO::ImageSpec spec(sizes[i][0],sizes[i][1],4,OIIO::TypeDesc::FLOAT);spec.attribute("oiio:ColorSpace","lin_ap1_scene");spec.attribute("colorInteropID","lin_ap1_scene");spec.attribute("compression","zip");
  std::vector<float> pixels(std::size_t(spec.width)*spec.height*4);for(std::size_t j=0;j<pixels.size();j+=4){pixels[j]=float(i+1)/8;pixels[j+1]=float(i+1)/16;pixels[j+2]=float(i+1)/32;pixels[j+3]=1;}
  check(writer->open(path.toStdString(),spec),"EXR open");check(writer->write_image(OIIO::TypeDesc::FLOAT,pixels.data()),"EXR pixels");check(writer->close(),"EXR close");
 }
 const auto resource=reference_image_resource(directory+"/source_<UDIM>.exr");check(resource.tiles==4&&resource.files.size()==4,"sparse pattern discovery");
 StudioAsset source;source.id=QUuid::createUuid();source.name="Sparse four-tile source";source.filename=resource.filename;source.path=resource.path;source.usages={"texture"};source.scope=AssetScope::Project;source.texture_files=resource.files;
 auto color=decode_fill_texture(source,false),raw=decode_fill_texture(source,true);check(color->tiles&&raw->tiles&&color->tiles->size()==4&&raw->tiles->size()==4,"decoded tile count");
 QJsonArray tile_proof;for(int i=0;i<4;++i){const auto &a=*color->tiles->at(numbers[i]),&b=*raw->tiles->at(numbers[i]);check(a.width==sizes[i][0]&&a.height==sizes[i][1],"source aspect/dimensions");check(b.width==a.width&&b.height==a.height&&b.raw&&b.input_space=="Non-Color/raw","data interpretation");check(a.sample(.4,.6)[0]==float(i+1)/8&&b.sample(.4,.6)[1]==float(i+1)/16,"tile identity/components");tile_proof.append(QJsonObject{{"tile",numbers[i]},{"source_width",a.width},{"source_height",a.height}});}
 std::unique_ptr<blender::Main,decltype(&blender::BKE_main_free)> main(blender::BKE_main_new(),blender::BKE_main_free);auto *scene=blender::BKE_scene_add(main.get(),"Sparse UDIM resources");auto *owner=blender::BKE_material_add(main.get(),"Sparse UDIM");const auto uid=owner->id.session_uid;
 SceneSnapshot snapshot;MaterialSummary material;material.session_id=uid;material.name="Sparse UDIM";material.uniform_editable=true;snapshot.materials={material};ImportSummary imported;imported.materials={{uid,"Sparse UDIM"}};
 NewProjectOptions options;options.uv_workflow=UvWorkflow::PreserveTiles;TextureDocument document(options,imported,snapshot);auto &model=document.set(uid);model.texture_resolution=128;model.texture_height=256;model.size_linked=false;model.tiles={numbers.begin(),numbers.end()};model.tile_resolutions[1002]=256;model.ao_channel=true;model.ao_mixing=AoMixing::Replace;model.precision.fill(ChannelPrecision::L32F);model.precision[0]=ChannelPrecision::RGB32F;model.channels[3]=false;
 for(int i=0;i<8;++i){TextureDocument::add_user_channel(model,i);}
 TextureDocument::add_displacement_channel(model);model.displacement.midlevel=0;
 // Cold document metadata must resolve action-local image descriptors through
 // the production project resource decoder, independently of layer Fill slots.
 {
  auto ordered=document;auto &set=ordered.set(uid);const std::vector<StudioAsset> resources{source};
  const auto expected=raw;
  LevelsEffect effect;effect.kind=ContentEffectKind::Fill;effect.channel=2;effect.fill_value={0,0,0};effect.bitmap_fill.emplace();effect.bitmap_fill->projection.texture=expected;effect.bitmap_fill->projection.scale={3,2};effect.bitmap_fill->projection.linked=false;effect.blending=LayerBlend::Overlay;effect.opacity=.37f;set.layers.front().effects.push_back(effect);
  const std::array<TextureSetBinding,1> binding{{{set.id,uid,set.name}}};auto cold=TextureDocument::from_json(ordered.to_json(),binding);resolve_fill_textures(cold,resources);
  const auto &restored=cold.set(uid).layers.front().effects.back();check(restored.bitmap_fill&&restored.bitmap_fill->projection.texture->pixels==expected->pixels&&restored.bitmap_fill->projection.scale==effect.bitmap_fill->projection.scale&&restored.blending==effect.blending&&restored.opacity==effect.opacity,"ordered bitmap project resource hydration changed source or mix");
  check(std::abs(TextureDocument::composite(cold.set(uid),7,3,1011).authored.roughness-TextureDocument::composite(set,7,3,1011).authored.roughness)<1e-6f,"ordered bitmap project reopen changed CPU evaluation");
 }
 TextureDocument::add_fill(model);const auto fill_id=model.selected_layer;auto &fill=TextureDocument::layer(model,fill_id);fill.channels[3]=false;fill.fill.mode=6;fill.displacement.enabled=true;for(auto &user:fill.users)user.enabled=true;for(int c=0;c<15;++c)if(c!=3)fill_channel_texture(fill.fill,c)=c==0?color:raw;
 TextureDocument::group_layers(model,{fill_id});const auto folder_id=model.selected_layer;auto &folder=TextureDocument::layer(model,folder_id);folder.mask.emplace();folder.mask->background=0;MaskEffect effect;effect.kind=MaskEffectKind::Fill;effect.mapping.projection.mode=6;effect.mapping.projection.texture=raw;effect.mapping.projection.alpha_mask[0]=false;folder.mask->effects.push_back(std::move(effect));
 const auto compare=[&](const TextureSet &a,const TextureSet &b){for(int tile:numbers)for(const auto uv:std::array<std::array<double,2>,3>{{{.2,.25},{.5,.5},{.8,.75}}}){const int x=int(uv[0]*a.width(tile)),y=int(uv[1]*a.rows(tile));const auto v=TextureDocument::evaluate(a,x,y,tile),w=TextureDocument::evaluate(b,x,y,tile);const auto near=[&](float p,float q){check(std::abs(p-q)<1e-6f,"roundtrip/flatten pixel mismatch");};for(int c=0;c<3;++c)near(v.material.base_color[c],w.material.base_color[c]);near(v.material.metallic,w.material.metallic);near(v.material.roughness,w.material.roughness);near(v.material.bump,w.material.bump);near(v.material.ambient_occlusion,w.material.ambient_occlusion);for(int c=0;c<8;++c){const auto p=TextureDocument::composite_user(a,c,x,y,tile),q=TextureDocument::composite_user(b,c,x,y,tile);near(p[0],q[0]);near(p[1],q[1]);}const auto p=TextureDocument::composite_displacement(a,x,y,tile),q=TextureDocument::composite_displacement(b,x,y,tile);near(p[0],q[0]);near(p[1],q[1]);}};
 for(int i=0;i<4;++i){const int tile=numbers[i];check(TextureDocument::fill_sample(model,TextureDocument::layer(model,fill_id),2,32,32,tile)[0]==float(i+1)/8,"data channel samples wrong tile");check(TextureDocument::mask_value(model,folder_id,32,32,tile)==float(i+1)/8,"folder mask samples wrong tile");for(int slot=0;slot<8;++slot){const auto value=TextureDocument::composite_user(model,slot,32,32,tile);check(value[0]==float(i+1)/8&&value[1]==float(i+1)/8,"custom channel data/coverage missing");}}
 check(TextureDocument::fill_sample(model,TextureDocument::layer(model,fill_id),2,32,32,1003)[3]==0,"sparse hole wrapped to another tile");
 std::atomic_bool stop=false;auto flat=flatten_layer_pixels(model,folder_id,stop);check(bool(flat),"flatten cancelled");
 for(int c=0;c<15;++c)if(c!=3){check(flat->images[c].size()==4,"flatten omitted channel/tile");const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();auto tiles=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(const auto &[tile,image]:flat->images[c]){check(image.width==model.width(tile)&&image.height==model.rows(tile),"flatten lost rectangular/per-tile size");auto texture=std::make_shared<FillTexture>();texture->asset=id;texture->hash=std::string(64,'f');texture->name="CPU flatten oracle";texture->width=image.width;texture->height=image.height;texture->raw=c!=0;texture->pixels=image.pixels;tiles->emplace(tile,std::move(texture));}auto sequence=std::make_shared<FillTexture>(*tiles->begin()->second);sequence->tiles=tiles;fill_channel_texture(flat->layer.fill,c)=std::move(sequence);}
 auto flattened=model;install_flattened_layer(flattened,std::move(flat->layer),folder_id);compare(model,flattened);
 ExportRequest output;output.directory=(directory+"/export").toStdString();check(QDir().mkpath(QString::fromStdString(output.directory)),"export directory");output.format=ExportFormat::Exr32;output.selected_channels=std::array<bool,9>{true,true,true,false,true,true,false,false,true};
 for(int tile:numbers){ExportTile item{model,tile,model.width(tile)};item.height=model.rows(tile);item.normal.resize(std::size_t(item.resolution)*item.height*4);output.tiles.push_back(std::move(item));}
 write_texture_export(output,stop,[](int,const std::string &){});int exports=0;
 for(const auto &file:export_file_list(output)){const auto descriptor=reference_image_resource(QString::fromStdString(output.directory+"/"+file.name),false);StudioAsset encoded;encoded.usages={"texture"};encoded.id=QUuid::createUuid();encoded.name=descriptor.name;encoded.filename=descriptor.filename;encoded.texture_files=descriptor.files;const auto pixels=decode_fill_texture(encoded,true);check(pixels->width==model.width(file.tile)&&pixels->height==model.rows(file.tile),"export dimensions/tile identity");const int x=model.width(file.tile)/4,y=model.rows(file.tile)/4;const auto expected=TextureDocument::composite(model,x,y,file.tile).material;const auto value=pixels->sample(.25,.25,false,false);const float scalar=file.channel==1?expected.metallic:file.channel==2?expected.roughness:file.channel==4?expected.bump:file.channel==5?expected.ambient_occlusion:TextureDocument::displacement_value(model,x,y,file.tile);if(file.channel==0){for(int c=0;c<3;++c)check(std::abs(value[c]-expected.base_color[c])<1e-6f,"export Color value");}else check(std::abs(value[0]-scalar)<1e-6f,"export scalar value");++exports;}
 check(exports==24,"six public export maps per tile; hidden Normal excluded");
 const auto probes=[&](const TextureSet &set){std::vector<float> values;for(int tile:numbers)for(const auto uv:std::array<std::array<double,2>,3>{{{.2,.25},{.5,.5},{.8,.75}}}){const int x=int(uv[0]*set.width(tile)),y=int(uv[1]*set.rows(tile));const auto composite=TextureDocument::evaluate(set,x,y,tile);const auto &v=composite.material;values.insert(values.end(),v.base_color.begin(),v.base_color.end());values.insert(values.end(),{v.metallic,v.roughness,v.bump,v.ambient_occlusion});values.insert(values.end(),composite.coverage.begin(),composite.coverage.end());values.insert(values.end(),{composite.ao_coverage,TextureDocument::mask_value(set,folder_id,x,y,tile)});for(int slot=0;slot<8;++slot){const auto u=TextureDocument::composite_user(set,slot,x,y,tile);values.insert(values.end(),u.begin(),u.end());}const auto d=TextureDocument::composite_displacement(set,x,y,tile);values.insert(values.end(),d.begin(),d.end());}return values;};
 const auto expected_probes=probes(model);const auto json=document.to_json();const auto set_id=model.id;std::array<std::array<int,2>,4> target_sizes;for(int i=0;i<4;++i)target_sizes[i]={model.width(numbers[i]),model.rows(numbers[i])};
 std::vector<std::weak_ptr<const std::vector<FillTexture::Pixel>>> previous_pixels;for(const auto &sequence:{color,raw})for(const auto &[tile,member]:*sequence->tiles)previous_pixels.push_back(member->pixels);
 pack_project_assets(*main,*scene,{source});const auto blend_path=directory+"/sparse-cold.blend";write_project(*main,*scene,document,blend_path.toStdString());check(QFileInfo(blend_path).size()>0,"actual .blend write");
 std::vector<QString> hidden;const auto restore_sources=qScopeGuard([&]{for(const auto &path:hidden)QFile::rename(path+".hidden",path);});for(const auto &file:source.texture_files){check(QFile::rename(file.path,file.path+".hidden"),"hide source");hidden.push_back(file.path);}
 // Retire writer IDs AND every decoded owner before opening the actual file.
 output.tiles.clear();flat.reset();flattened=TextureSet{};document=TextureDocument(options,imported,snapshot);color.reset();raw.reset();main.reset();scene=nullptr;owner=nullptr;
 for(const auto &pixels:previous_pixels)check(pixels.expired(),"disk restore still has warm decoded owners");for(const auto &file:source.texture_files)check(!QFileInfo::exists(file.path),"source still available before cold read");check(!blender::GPU_context_active_get(),"disk writer acquired a GPU context");
 blender::ReportList reports{};blender::BKE_reports_init(&reports,blender::RPT_STORE);const auto free_reports=qScopeGuard([&]{blender::BKE_reports_free(&reports);});blender::BlendFileReadReport report{};report.reports=&reports;
 std::unique_ptr<blender::BlendFileData,decltype(&blender::BLO_blendfiledata_free)> cold(blender::BLO_read_from_file(blend_path.toUtf8().constData(),blender::BLO_READ_SKIP_USERDEF,&report),blender::BLO_blendfiledata_free);check(cold&&cold->main,"actual .blend cold read");auto *cold_scene=static_cast<blender::Scene *>(cold->main->scenes.first);check(cold_scene,"cold scene missing");
 const auto *properties=blender::IDP_GetProperties(&cold_scene->id);check(properties,"cold scene metadata missing");const auto *saved=blender::IDP_GetPropertyFromGroup(properties,"bts_document");check(saved&&saved->type==blender::IDP_STRING,"cold document JSON missing");QJsonParseError parse_error;const auto saved_json=QJsonDocument::fromJson(QByteArray(IDP_string_get(saved)),&parse_error);check(parse_error.error==QJsonParseError::NoError&&saved_json.isObject()&&saved_json.object()==json,"actual saved document metadata changed");
 const auto *sets=blender::IDP_GetPropertyFromGroup(properties,"bts_sets");check(sets&&sets->type==blender::IDP_GROUP,"cold Texture Set bindings missing");const auto *reference=blender::IDP_GetPropertyFromGroup(sets,set_id.toString(QUuid::WithoutBraces).toStdString());check(reference&&reference->type==blender::IDP_ID,"cold material reference missing");auto *cold_id=IDP_ID_get(reference);check(cold_id&&GS(cold_id->name)==blender::ID_MA&&cold_id->session_uid!=0&&std::string(cold_id->name+2)==material.name,"cold material reference/name invalid");bool material_owned=false;for(const auto &candidate:cold->main->materials)material_owned|=&candidate.id==cold_id;check(material_owned,"cold material reference outside read Main");const std::array<TextureSetBinding,1> binding{{{set_id,cold_id->session_uid,cold_id->name+2}}};
 ProjectResourceImages resource_images;const auto packed=read_project_assets(*cold_scene,&resource_images);check(packed.size()==1&&packed[0].hash()==source.hash()&&resource_images.by_key.size()==4,"cold packed resource identity/member count");auto reopened=TextureDocument::from_json(saved_json.object(),binding);resolve_fill_textures(reopened,packed);auto &restored=reopened.set(cold_id->session_uid);check(restored.material_session_uid==cold_id->session_uid&&restored.name==material.name&&restored.id==set_id,"cold document material identity changed");const auto restored_probes=probes(restored);check(restored_probes.size()==expected_probes.size()&&restored_probes.size()==33*numbers.size()*3,"cold values/coverage probe count");for(std::size_t i=0;i<expected_probes.size();++i)check(std::abs(expected_probes[i]-restored_probes[i])<1e-6f,"cold disk channel/coverage mismatch");
 const auto &restored_fill=TextureDocument::layer(restored,fill_id);for(int i=0;i<4;++i){check(restored.width(numbers[i])==target_sizes[i][0]&&restored.rows(numbers[i])==target_sizes[i][1],"cold target aspect/override changed");for(int c:{0,2}){const auto &member=*fill_channel_texture(restored_fill.fill,c)->tiles->at(numbers[i]);check(member.width==sizes[i][0]&&member.height==sizes[i][1],"cold source rectangular dimensions changed");}}
 check(TextureDocument::mask_value(restored,folder_id,32,32,1011)==.375f,"cold mask tile1011");check(TextureDocument::fill_sample(restored,restored_fill,2,32,32,1003)[3]==0&&TextureDocument::mask_value(restored,folder_id,32,32,1003)==0,"cold sparse hole repeated a tile");
 check(!blender::GPU_context_active_get(),"CPU oracle left a GPU context active");
 return {{"ordered_bitmap_project_resource_roundtrip",true},{"standard_microstructure_materials",micro_materials},{"standard_microstructure_decoded_images",micro_images},{"tiles",tile_proof},{"channels",14},{"sample_points_per_tile",3},{"exported_files",exports},{"rectangular_and_per_tile_sizes",true},{"sparse_hole_transparent",true},{"folder_bitmap_mask",true},{"flatten_exact",true},{"packed_source_free_roundtrip",true},{"decoded_owners_released_before_restore",true},{"cold_disk",QJsonObject{{"actual_blend_write",true},{"actual_blend_cold_read",true},{"writer_Main_destroyed_before_read",true},{"source_files_hidden",4},{"decoded_pixel_owners_expired",int(previous_pixels.size())},{"metadata_from_saved_IDProperties",true},{"actual_saved_material_reference",true},{"packed_resource_members",4},{"sampled_values_and_coverage",int(restored_probes.size())},{"source_rectangles_target_sizes_preserved",true},{"sparse_hole_transparent",true},{"document_resource_hydration_exact",true},{"full_application_Open_tested",false},{"render_tested",false}}},{"custom_channels_exported",false},{"gpu_context_created",false}};
}
}
