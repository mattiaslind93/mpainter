#include "RequestedFeaturesProof.hh"
#include "ExportCpuDemo.hh"
#include "HeightNeutralExportTest.hh"
#include "CapturedExportCpuTest.hh"
#include "HybridBumpExportTest.hh"
#include "JpegExportCpuDemo.hh"
#include "DerivedExportCpuDemo.hh"
#include "UsdMeshExport.hh"
#include "ExportConfiguration.hh"
#include "AutomationExportPlan.hh"
#include "NativeCompositeJob.hh"
#include "UniformChannels.hh"
#include "ResourceIO.hh"
#include "MeshMapIO.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_scene.hh"
#include "BKE_idprop.hh"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"
#include <memory>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <cstring>
namespace bts {
QJsonObject verify_flattened_export_codec();
#ifdef BTS_WITH_MATERIALX
QJsonObject verify_openpbr_texture_export(const QString &);
#endif
QJsonObject verify_export_depths(const QString &);
QJsonObject verify_automation_export_plan(const TextureDocument &);
QJsonObject verify_python_extensions(const QString &);
namespace {
QJsonObject verify_unchanged_bake_reuse(){
 using namespace blender;std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);auto *scene=BKE_scene_add(main.get(),"Baked map encoding regression");
 SceneSnapshot description;MaterialSummary material;material.session_id=1;material.name="Bake";material.uniform_editable=true;description.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Bake"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,description);auto &set=document.set(1);
 constexpr int size=64;std::vector<float> values(size*size*4);for(std::size_t i=0;i<values.size();++i)values[i]=i%4==3?1.f:float(i%17)/17;auto pixels=std::make_shared<MeshMapPixels>(size,values);MeshMap map;map.name="AO";map.tiles[1001]={size,pixels->hash,pixels};set.mesh_maps[3]=map;
 pack_mesh_maps(*main,*scene,document);auto *image=static_cast<Image *>(main->images.first);if(!image||image!=main->images.last)throw std::runtime_error("Baked map image count changed");const auto *payload=static_cast<ImagePackedFile *>(image->packedfiles.first)->packedfile->data;
 BKE_image_free_buffers(image);pack_mesh_maps(*main,*scene,document);
 if(image->runtime->cache||static_cast<ImagePackedFile *>(image->packedfiles.first)->packedfile->data!=payload)throw std::runtime_error("Unchanged baked map was decoded/re-encoded");
 values[0]=.875f;pixels=std::make_shared<MeshMapPixels>(size,values);set.mesh_maps[3]->tiles[1001]={size,pixels->hash,pixels};pack_mesh_maps(*main,*scene,document);
 auto opened=document;opened.set(1).mesh_maps[3]->tiles[1001].pixels.reset();read_mesh_maps(*scene,opened);if(opened.set(1).mesh_maps[3]->tiles[1001].pixels->rgba!=values)throw std::runtime_error("Changed baked map was incorrectly reused");
 return {{"unchanged_payload_pointer_reused",true},{"unchanged_decoded",false},{"changed_pixels_exact",true}};
}
QJsonObject verify_legacy_resource_reuse(){
 using namespace blender;
 std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);
 auto *scene=BKE_scene_add(main.get(),"Resource packing regression");
 StudioAsset asset;asset.scope=AssetScope::Project;asset.name="Legacy UDIM payload";asset.filename="legacy.exr";asset.usages={"texture"};asset.bytes=QByteArray(1024*1024,'x');
 pack_project_assets(*main,*scene,{asset});
 auto *image=static_cast<Image *>(main->images.first);if(!image||image!=main->images.last)throw std::runtime_error("Initial resource is not shared");
 auto *properties=IDP_GetProperties(&image->id);auto *hint=properties?IDP_GetPropertyFromGroup(properties,"bts_source_sha256"):nullptr;if(!hint)throw std::runtime_error("Resource identity hint missing");IDP_FreeFromGroup(properties,hint);
 const auto *payload=static_cast<ImagePackedFile *>(image->packedfiles.first)->packedfile->data;
 pack_project_assets(*main,*scene,{asset});
 if(main->images.first!=image||main->images.last!=image||static_cast<ImagePackedFile *>(image->packedfiles.first)->packedfile->data!=payload)throw std::runtime_error("Legacy resource packing duplicated unchanged bytes");
 auto loaded=read_project_assets(*scene);if(loaded.size()!=1||loaded[0].bytes!=asset.bytes)throw std::runtime_error("Reused legacy payload changed");
 asset.bytes[0]='y';pack_project_assets(*main,*scene,{asset});
 if(main->images.first!=main->images.last)throw std::runtime_error("Changed resource left an orphan payload");
 loaded=read_project_assets(*scene);if(loaded.size()!=1||loaded[0].bytes!=asset.bytes)throw std::runtime_error("Changed resource was incorrectly reused");
 return {{"legacy_missing_identity_hint",true},{"unchanged_native_resource_image_count",1},{"packed_payload_pointer_reused",true},{"changed_bytes_roundtrip",true},{"orphan_payloads",0}};
}
}
// Studio through the automation plan: an asset painted only in color yields
// one color map; color+roughness+bump on two UDIM tiles yields exactly those.
QJsonObject verify_studio_export(const QString &directory){
 const auto require=[](bool value,const std::string &message){if(!value)throw std::runtime_error("Studio export: "+message);};
 const auto document_for=[&](const QString &folder,const std::vector<std::string> &names){SceneSnapshot scene;ImportSummary imported;for(std::size_t i=0;i<names.size();++i){MaterialSummary material;material.session_id=int(i+1);material.name=names[i];material.uniform_editable=true;scene.materials.push_back(material);imported.materials.push_back({std::uint32_t(i+1),names[i]});}
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.absolute_mesh_path=QDir(folder).absoluteFilePath("gnome.fbx").toStdString();return TextureDocument(options,imported,scene);};
 const auto paint=[](TextureSet &set,std::array<bool,5> channels,const std::set<int> &tiles,std::array<double,2> center={.5,.5}){PaintPixels::Stamp dab{center,{.15,0},{0,.15}};dab.values.base_color={1,0,0,1};dab.values.roughness=.8f;dab.values.bump=.7f;dab.channels=channels;dab.hardness=1;set.layers.front().pixels.stamp(128,dab,tiles);};
 QJsonArray projects;
 for(int variant=0;variant<2;++variant){
  const auto folder=QDir(directory).absoluteFilePath(variant?"color-roughness-bump":"color-only");QDir(folder).removeRecursively();QDir().mkpath(folder);
  auto document=document_for(folder,{"body"});auto &set=document.set(1);if(variant)set.tiles={1001,1002};paint(set,{true,false,variant==1,false,variant==1},{1001},variant?std::array<double,2>{.5,.5}:std::array<double,2>{.25,.75}); // UV upper-left: file rows are top-down
  auto plan=automation_export_plan(document,{{"directory",folder},{"preset","Studio"}});require(plan.request.studio&&!plan.usd,"automation did not select the Studio recipe");
  for(auto &tile:plan.request.tiles){tile.normal.assign(std::size_t(tile.resolution)*tile.rows()*4,1.f);for(std::size_t p=0;p<tile.normal.size();p+=4)tile.normal[p]=tile.normal[p+1]=.5f;}
  std::atomic_bool cancel=false;write_texture_export(plan.request,cancel,[](int,const std::string &){});
  const auto written=QDir(folder).entryList(QDir::Files|QDir::Hidden,QDir::Name);QStringList expected;
  if(!variant)expected={"gnome_color.exr"};else for(const char *channel:{"bump","color","roughness"})for(int tile:{1001,1002})expected<<QString("gnome_%1_%2.exr").arg(channel).arg(tile);
  expected.sort();require(written==expected,"unexpected files: "+written.join(",").toStdString());
  QJsonArray files;for(const auto &name:written)files.append(QDir(folder).filePath(name));projects.append(QJsonObject{{"project",variant?"color+roughness+bump (UDIM)":"color only"},{"files",files}});
 }
 // Several Texture Sets on one mesh keep unique names through $textureSet.
 auto shared=document_for(directory,{"body","hat"});for(int id:{1,2})paint(shared.set(id),{true,false,false,false,false},{1001});
 const auto plan=automation_export_plan(shared,{{"directory",QDir(directory).absolutePath()},{"preset",studio_preset_id.toString(QUuid::WithoutBraces)}});QStringList names;for(const auto &file:export_file_list(plan.request))names<<QString::fromStdString(file.name);names.sort();
 require(names==QStringList{"gnome_body_color.exr","gnome_hat_color.exr"},"multi-set names: "+names.join(",").toStdString());
 auto untouched=document_for(directory,{"body"});require(export_file_list(automation_export_plan(untouched,{{"directory",QDir(directory).absolutePath()},{"preset","studio"}}).request).empty(),"an unpainted asset exported maps");
 return {{"projects",projects},{"unpainted_files",0},{"multi_set_names",QJsonArray::fromStringList(names)}};
}
int run_export_cpu_demo(const QString &directory){
  if(qEnvironmentVariableIsSet("BTS_REQUESTED_FEATURES_PROOF"))return run_requested_features_proof(directory);
  if(qEnvironmentVariableIsSet("BTS_STUDIO_EXPORT_ONLY")){QDir().mkpath(directory);const auto proof=verify_studio_export(directory);QFile file(QDir(directory).filePath("studio-export.json"));if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write Studio proof");file.write(QJsonDocument(proof).toJson());return 0;}
  if(qEnvironmentVariableIsSet("BTS_USD_MESH_VERIFY_INPUT")){const auto proof=inspect_exported_usd(qEnvironmentVariable("BTS_USD_MESH_VERIFY_INPUT").toStdString());QDir().mkpath(directory);QFile file(QDir(directory).filePath("usd-inspected.json"));if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write USD inspection");file.write(QJsonDocument(proof).toJson());return 0;}
  if(qEnvironmentVariableIsSet("BTS_DERIVED_VERIFY_INPUT")){const auto proof=verify_derived_export_files(qEnvironmentVariable("BTS_DERIVED_VERIFY_INPUT"));QDir().mkpath(directory);QFile result(QDir(directory).filePath("verified-png16.json"));if(!result.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write native PNG16 verification");result.write(QJsonDocument(proof).toJson());return 0;}
  QDir().mkpath(directory);{QFile neutral(QDir(directory).filePath("height-neutral-export.json"));if(!neutral.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write neutral height proof");neutral.write(QJsonDocument(verify_neutral_height_exports(directory)).toJson());}{QFile proof(QDir(directory).filePath("legacy-resource-reuse.json"));if(!proof.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write resource regression proof");auto result=verify_legacy_resource_reuse();result["baked_maps"]=verify_unchanged_bake_reuse();proof.write(QJsonDocument(result).toJson());}
  const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="FixtureSurface";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"FixtureSurface"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);auto &set=document.set(1);TextureDocument::add_displacement_channel(set);TextureDocument::add_fill(set);set.layers.back().displacement.enabled=true;set.layers.back().displacement.value=.8;set.layers.back().values.bump=.25;set.ao_channel=true;
  ExportTile tile{set,1001,128};tile.normal.resize(128*128*4);for(std::size_t p=0;p<tile.normal.size();p+=4){tile.normal[p]=tile.normal[p+1]=.5;tile.normal[p+2]=tile.normal[p+3]=1;}
  ExportConfiguration config;config.directory=QDir(directory).absolutePath();config.selected.insert(set.id);ExportOutputPreset old;old.name="Legacy uppercase names";for(int c=0;c<9;++c)old.naming[c]="$mesh_$textureSet_"+std::string(export_channel_suffixes[c]);old.naming[0]="$mesh_$textureSet_Base_Color";old.naming[4]="$mesh_$textureSet_Height";old.naming[6]="$mesh_$textureSet_Normal_OpenGL";config.presets.push_back(old);auto legacy=export_configuration_json(config);legacy["version"]=1;auto presets=legacy["presets"].toArray();auto preset=presets[0].toObject();auto names=preset["names"].toArray();names.removeLast();names[4]="$mesh_$textureSet_Height";preset["names"]=names;presets[0]=preset;legacy["presets"]=presets;const std::array<QUuid,1> ids{set.id};const auto migrated=read_export_configuration(legacy,ids);
  QJsonArray proofs;const std::array<const char *,3> variants{"document-builtin","legacy-migrated","user-packed"};
  for(int variant=0;variant<3;++variant){ExportRequest request;request.directory=QDir(directory).absoluteFilePath(variants[variant]).toStdString();QDir().mkpath(QString::fromStdString(request.directory));request.tiles={tile};request.mesh="MixedCaseMesh.obj";if(variant==1)request.naming=migrated.presets.front().naming;if(variant==2){request.packed=PackedOutput{};request.packed->name="$mesh_$textureSet_Mixed_Custom_Map";request.packed->source={PackedSource::Bump,PackedSource::Displacement,PackedSource::One};}const auto files=export_file_list(request);std::atomic_bool cancel=false;write_texture_export(request,cancel,[](int,const std::string &){});QJsonArray output;bool bump=false,disp=false;
    for(const auto &file:files){const QString name=QString::fromStdString(file.name);require(!name.contains("Height")&&!name.contains("_Bump"),"Old suffix survived export migration");const int prefix=name.indexOf("FixtureSurface_");require(prefix>=0,"Texture Set case was changed");const auto suffix=name.mid(prefix+15).section('.',0,0);require(suffix==suffix.toLower(),"Uppercase export suffix");const QImage image(QDir(QString::fromStdString(request.directory)).filePath(name));require(!image.isNull(),"Export did not write a readable image");require(!name.contains("normal",Qt::CaseInsensitive),"Normal map export is disabled");if(name.endsWith("_bump.png")){bump=true;require(file.channels==1&&std::abs(image.pixelColor(64,64).redF()-.25)<.01,"Bump must stay one grayscale channel");}if(name.endsWith("_displacement.png")){disp=true;require(file.channels==1&&std::abs(image.pixelColor(64,64).redF()-.8)<.01,"Displacement must stay one grayscale channel");}if(name.endsWith("_mixed_custom_map.png")){auto rgb=image.pixelColor(64,64);require(std::abs(rgb.redF()-.25)<.01&&std::abs(rgb.greenF()-.8)<.01&&rgb.blueF()>.99,"Packed Bump/Displacement data changed");}output.append(name);}
    require(bump&&disp,"Separate Bump and Displacement outputs missing");proofs.append(QJsonObject{{"preset",variants[variant]},{"files",output}});
  }
  // Exact decoded-pixel oracle against the previous exporter, including signed
  // data, nonuniform fill mapping, paint, a mask and packed output components.
  set.layers.back().fill.atom=true;set.layers.back().fill.scale={2.3,.7};
  TextureDocument::add_paint(set);auto &paint=TextureDocument::layer(set,set.selected_layer);
  PaintPixels::Stamp dab{{.45,.55},{.2,0},{0,.13}};dab.values.base_color={.8,.04,.2,1};dab.values.bump=-.3;dab.values.normal={.2,.1,.97};
  paint.pixels.stamp(128,dab,set.tiles);paint.mask=LayerMask{};paint.mask->background=.67;paint.opacity[0]=.72;
  paint.displacement.enabled=true;auto displacement_dab=dab;displacement_dab.channels={false,false,true,false,false};displacement_dab.values.roughness=-.25;paint.displacement.pixels.stamp(128,displacement_dab,set.tiles);
  tile.set=set;
  QJsonArray parity;
  for(int format=0;format<4;++format){
    ExportRequest fast;fast.directory=QDir(directory).absoluteFilePath(QString("parity-%1-fast").arg(format)).toStdString();fast.tiles={tile};fast.format=ExportFormat(format);fast.packed=PackedOutput{};fast.packed->source={PackedSource::Bump,PackedSource::Displacement,PackedSource::Roughness};
    ExportRequest reference=fast;reference.directory=QDir(directory).absoluteFilePath(QString("parity-%1-reference").arg(format)).toStdString();reference.reference_composite=true;
    QDir().mkpath(QString::fromStdString(fast.directory));QDir().mkpath(QString::fromStdString(reference.directory));std::atomic_bool cancel=false;
    write_texture_export(fast,cancel,[](int,const std::string &){});write_texture_export(reference,cancel,[](int,const std::string &){});
    int checked=0;
    for(const auto &entry:export_file_list(fast)){
      const auto load=[&](const std::string &folder){QFile file(QDir(QString::fromStdString(folder)).filePath(QString::fromStdString(entry.name)));require(file.open(QIODevice::ReadOnly),"Export parity file missing");const auto bytes=file.readAll();return std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>(blender::IMB_load_image_from_memory(reinterpret_cast<const unsigned char *>(bytes.constData()),bytes.size(),blender::ImBufFlags::AlphaChannelPacked|blender::ImBufFlags::NoColorspaceConvert,entry.name.c_str()),blender::IMB_freeImBuf);};
      auto a=load(fast.directory),b=load(reference.directory);require(a&&b&&a->x==b->x&&a->y==b->y,"Export parity decode failed");const auto count=std::size_t(a->x)*a->y*4;
      if(a->float_data())require(b->float_data()&&std::memcmp(a->float_data(),b->float_data(),count*sizeof(float))==0,"Float export changed pixels");
      else require(a->byte_data()&&b->byte_data()&&std::memcmp(a->byte_data(),b->byte_data(),count)==0,"PNG export changed pixels");
      ++checked;
    }parity.append(QJsonObject{{"format",format},{"files",checked},{"max_pixel_difference",0}});
  }
  auto composite_set=set;composite_set.tiles={1001,1002};composite_set.texture_resolution=256;
  const auto serial_setting=qgetenv("BTS_COMPOSITE_SERIAL"),sampled_setting=qgetenv("BTS_COMPOSITE_SAMPLED");std::atomic_bool composite_cancel=false;
  qputenv("BTS_COMPOSITE_SERIAL","1");qputenv("BTS_COMPOSITE_SAMPLED","1");const auto serial_composite=native_composite_job(composite_set,true,nullptr,1,composite_cancel);
  qunsetenv("BTS_COMPOSITE_SERIAL");qunsetenv("BTS_COMPOSITE_SAMPLED");const auto parallel_composite=native_composite_job(composite_set,true,nullptr,1,composite_cancel);
  if(serial_setting.isNull())qunsetenv("BTS_COMPOSITE_SERIAL");else qputenv("BTS_COMPOSITE_SERIAL",serial_setting);
  if(sampled_setting.isNull())qunsetenv("BTS_COMPOSITE_SAMPLED");else qputenv("BTS_COMPOSITE_SAMPLED",sampled_setting);
  require(serial_composite&&parallel_composite&&serial_composite->tiles==parallel_composite->tiles,"Parallel multichannel/UDIM composite changed pixels");
  for(int test=0;test<5;++test){
    auto dominated=composite_set;TextureDocument::add_fill(dominated);auto &top=TextureDocument::layer(dominated,dominated.selected_layer);top.values.base_color={.15f,.7f,.2f,1};top.values.bump=-.12f;
    if(test==1)top.opacity[0]=.5f;
    if(test==2){top.mask=LayerMask{};top.mask->background=.6f;}
    if(test==3){top.fill.atom=true;top.fill.scale={.7,3};}
    if(test==4){dominated=TextureSet{};dominated.texture_resolution=256;dominated.tiles={1001,1002};dominated.layers.front().kind=LayerKind::Fill;dominated.layers.front().values.base_color={.2f,.3f,.7f,1};TextureDocument::add_paint(dominated);auto &erase=TextureDocument::layer(dominated,dominated.selected_layer);erase.blending[0]=LayerBlend::Replace;erase.opacity[0]=0;erase.mask=LayerMask{};PaintPixels::Stamp mask{{.5,.5},{.2,0},{0,.2}};mask.channels={false,false,true,false,false};mask.values.roughness=.2;erase.mask->pixels.stamp(256,mask,dominated.tiles);}
    require(uniform_channel(dominated,0,1001)==(test==0),"Uniform proof incorrectly includes a varying stack");
    qputenv("BTS_COMPOSITE_SERIAL","1");qputenv("BTS_COMPOSITE_SAMPLED","1");const auto sampled=native_composite_job(dominated,true,nullptr,1,composite_cancel);
    qunsetenv("BTS_COMPOSITE_SERIAL");qunsetenv("BTS_COMPOSITE_SAMPLED");const auto optimized=native_composite_job(dominated,true,nullptr,1,composite_cancel);
    require(sampled&&optimized&&sampled->tiles==optimized->tiles,"Uniform-channel shortcut changed composite pixels");
  }
  if(serial_setting.isNull())qunsetenv("BTS_COMPOSITE_SERIAL");else qputenv("BTS_COMPOSITE_SERIAL",serial_setting);
  if(sampled_setting.isNull())qunsetenv("BTS_COMPOSITE_SAMPLED");else qputenv("BTS_COMPOSITE_SAMPLED",sampled_setting);
  composite_cancel=true;require(!native_composite_job(composite_set,true,nullptr,2,composite_cancel),"Cancelled composite produced a result");
  SceneSnapshot export_geometry;export_geometry.materials.push_back(material);export_geometry.object_names[1]="ExportFixture";export_geometry.minimum={0,0,0};export_geometry.maximum={2,1,0};
  for(int t=0;t<2;++t)for(int triangle=0;triangle<2;++triangle){
    const std::array<std::array<float,2>,4> uv{{{.1f,.1f},{.9f,.1f},{.9f,.9f},{.1f,.9f}}};
    const std::array<int,3> ids=triangle?std::array<int,3>{0,2,3}:std::array<int,3>{0,1,2};UvFaceTriangle face;face.triangle_id=(1ULL<<32)|(t*2+triangle);face.boundary_edges=7;
    for(int k=0;k<3;++k){face.corners[k]={uv[ids[k]][0]+t,uv[ids[k]][1]};face.positions[k]={uv[ids[k]][0]+t,uv[ids[k]][1],0};face.vertex_ids[k]=1+t*4+ids[k];}export_geometry.uv_triangles.push_back(face);
  }
  auto deferred_set=set;deferred_set.tiles={1001,1002};auto export_padding=std::make_shared<TexturePadding>(export_geometry,1);CompositeChannels derived_channels{};derived_channels[3]=derived_channels[5]=true;std::atomic_bool export_cancel=false;
  auto native_source=native_composite_job(deferred_set,true,export_padding->clone(),0,export_cancel,false,derived_channels);require(bool(native_source),"Native export source cancelled");
  ExportRequest ready,deferred;ready.directory=QDir(directory).absoluteFilePath("ready-source").toStdString();deferred.directory=QDir(directory).absoluteFilePath("deferred-source").toStdString();ready.padding=deferred.padding=ExportPadding::Transparent;
  for(int number:deferred_set.tiles){ExportTile ready_tile{deferred_set,number,128,native_source->tiles.at(number)[3],native_source->tiles.at(number)[5]};ready_tile.coverage=export_padding->uv_coverage(128,number);ready.tiles.push_back(std::move(ready_tile));ExportTile later{deferred_set,number,128};later.deferred_padding=export_padding;deferred.tiles.push_back(std::move(later));}
  QDir().mkpath(QString::fromStdString(ready.directory));QDir().mkpath(QString::fromStdString(deferred.directory));write_texture_export(ready,export_cancel,[](int,const std::string &){});write_texture_export(deferred,export_cancel,[](int,const std::string &){});
  const auto deferred_files=export_file_list(ready);for(const auto &entry:deferred_files){const auto name=QString::fromStdString(entry.name);const QImage a(QDir(QString::fromStdString(ready.directory)).filePath(name)),b(QDir(QString::fromStdString(deferred.directory)).filePath(name));require(!a.isNull()&&a==b,"Deferred normal/AO/UV coverage changed an exported map");}
  const auto extension_proof=verify_python_extensions(QDir(directory).filePath("python-extensions"));
  const auto automation_proof=verify_automation_export_plan(document);
  const auto captured_proof=verify_captured_texture_export(directory);
  {QFile report(QDir(directory).filePath("hybrid-bump-export.json"));if(!report.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write hybrid export proof");report.write(QJsonDocument(verify_hybrid_bump_export(directory)).toJson());}
  const auto depth_proof=verify_export_depths(QDir(directory).filePath("depths"));
  const auto flatten_proof=verify_flattened_export_codec();
  const auto jpeg_proof=run_jpeg_export_cpu_demo(QDir(directory).filePath("jpeg"));
  const auto derived_proof=run_derived_export_cpu_demo(QDir(directory).filePath("derived"));
  {QFile studio(QDir(directory).filePath("studio-export.json"));require(studio.open(QIODevice::WriteOnly),"Studio proof unavailable");studio.write(QJsonDocument(verify_studio_export(QDir(directory).filePath("studio"))).toJson());}
  QJsonObject materialx_proof;
#ifdef BTS_WITH_MATERIALX
  materialx_proof=verify_openpbr_texture_export(QDir(directory).filePath("openpbr"));
#endif
  QFile result(QDir(directory).filePath("cpu-export.json"));require(result.open(QIODevice::WriteOnly),"CPU export proof unavailable");result.write(QJsonDocument(QJsonObject{{"captured_gpu_planes",captured_proof},{"openpbr_materialx",materialx_proof},{"python_extensions",extension_proof},{"automation_plan",automation_proof},{"depths",depth_proof},{"presets",proofs},{"legacy_config_version",1},{"new_config_version",7},{"derived_outputs",derived_proof},{"jpeg_outputs",jpeg_proof},{"flatten_codec",flatten_proof},{"gpu_required",false},{"cache_parity",parity},{"parallel_composite_bit_exact",true},{"uniform_composite_oracle_cases",5},{"composite_tiles",2},{"deferred_export_exact_files",int(deferred_files.size())}}).toJson());return 0;
}
}
