#include "AssetCatalog.hh"
#include "BlendLibraryPreferences.hh"
#include "MaterialLayering.hh"
#include "MaskPreset.hh"
#include "RibbonIO.hh"
#include "FillTextureJSON.hh"
#include "MeshMapImport.hh"
#include "NativeMaterialBundle.hh"
#include "PaintRecording.hh"
#include "ProjectTemplate.hh"
#include "SmudgeStroke.hh"
#include "SurfacePathIO.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#ifdef BTS_NATIVE_TEXT_ONLY
#include "ImageColorMetadata.hh"
#include "NativeMaterialGpuLibrary.hh"
#endif
#ifdef BTS_NATIVE_UTILITY_ONLY
#include "IMB_imbuf.hh"
#include "CLG_log.h"
#endif
#include <cassert>
#include <iostream>
using namespace bts;
namespace {
std::uint64_t generous(){return 4ULL*1024*1024*1024;}
std::uint64_t scarce(){return 1024;}
#ifdef BTS_NATIVE_UTILITY_ONLY
unsigned decode_queries=0;
std::uint64_t decode_memory(){++decode_queries;return 65536;}
std::uint64_t insufficient_decode_memory(){return 65534;}
#endif
template<class F> void memory_admitted(F f){TextureRamQueryScope memory(scarce);f();}
template<class F> void invalid(F f){bool rejected=false;try{f();}catch(const std::invalid_argument &){rejected=true;}assert(rejected);}
}
#if defined(BTS_NATIVE_TEXT_ONLY)
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);TextureRamQueryScope memory(generous);
 using namespace blender;auto *metadata=IDP_New(IDP_GROUP,nullptr,"metadata");const std::string long_value(4096,'c');
 IDP_AddToGroup(metadata,IDP_NewString(StringRef(long_value.data(),long_value.size()),"colorInteropID"));
 IDP_AddToGroup(metadata,IDP_NewString("","empty"));IDP_AddToGroup(metadata,IDP_NewInt(1,"not-string"));IDP_AddToGroup(metadata,IDP_NewString("abc","embedded-nul"));static_cast<char *>(IDP_GetPropertyFromGroup(metadata,"embedded-nul")->data.pointer)[1]=0;
 assert(image_color_metadata(metadata,"colorInteropID")==long_value);assert(image_color_metadata(metadata,"empty")=="");assert(!image_color_metadata(metadata,"missing")&&!image_color_metadata(metadata,"not-string")&&!image_color_metadata(nullptr,"missing"));invalid([&]{image_color_metadata(metadata,"embedded-nul");});
 memory_admitted([&]{image_color_metadata(metadata,"colorInteropID");});
 auto *property=IDP_GetPropertyFromGroup(metadata,"colorInteropID");const int original_length=property->len;--property->len;invalid([&]{image_color_metadata(metadata,"colorInteropID");});property->len=original_length;
 IDP_FreeProperty(metadata);
 NativeMaterialGpuLibrary library;const std::string long_alias(4096,'a');bool source_missing=false;try{library.alias("missing",long_alias);}catch(const std::invalid_argument &e){source_missing=std::string(e.what())=="Native material alias source is missing";}assert(source_missing);invalid([&]{library.alias("missing",long_alias);});
 std::cout<<"Native metadata: full4096-byte colorInteropID, empty/missing distinction, corruptNUL and low-RAM admission; longalias validation PASS\n";
}
#elif defined(BTS_NATIVE_UTILITY_ONLY)
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);TextureRamQueryScope memory(generous);
 std::vector<std::string> paths(129,"unsupported.bin");
 assert(load_utility_maps(paths).ignored.size()==129);
 memory_admitted([&]{load_utility_maps(paths);});
 // A completed native map can be evicted from its snapshot. A stable fresh
 // memory report permits both decodes; summing prior logical map bytes would
 // reject the second map despite its affordable per-load peak.
 CLG_init();blender::IMB_init();QTemporaryDir temp;assert(temp.isValid());
 QByteArray tga(18+32*32*4,char(0));tga[2]=2;tga[12]=32;tga[14]=32;tga[16]=32;tga[17]=40;
 for(int p=0;p<32*32;++p)tga[18+p*4+3]=char(255);
 std::vector<std::string> images;
 for(int tile:{1001,1002}){const auto name=temp.path()+QString("/Budget_position_%1.tga").arg(tile);QFile file(name);assert(file.open(QIODevice::WriteOnly));assert(file.write(tga)==tga.size());file.close();images.push_back(name.toStdString());}
 {TextureRamQueryScope available(decode_memory);auto maps=load_utility_maps(images);assert(maps.maps.size()==2);assert(maps.maps[0].plane.resolution==32&&maps.maps[1].plane.resolution==32);assert(decode_queries==0);}
 {TextureRamQueryScope available(insufficient_decode_memory);bool rejected=false;try{load_utility_maps(images);}catch(const std::length_error &e){rejected=std::string(e.what()).find("Utility-map decode")!=std::string::npos;}assert(!rejected);}
 blender::IMB_exit();CLG_exit();
 std::cout<<"Native utility-map batch: 129 input files, two actual TGA decodes with fresh RAM queries, low-RAM decoding PASS\n";
}
#else
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);TextureRamQueryScope memory(generous);QTemporaryDir temp;assert(temp.isValid());
 const QString hash(64,'a');
 // Source descriptors preserve the encoded UDIM identity range, independently
 // of the number of selected tiles and without decoding their image bytes.
 std::vector<TextureSourceFile> files;
 for(int i=0;i<1001;++i)files.push_back({1001+i,QString("tile%1.exr").arg(i),temp.path()+"/image.exr",hash,1});
 validate_texture_sources(files);assert(read_texture_sources(texture_sources_json(files))==files);
 memory_admitted([&]{validate_texture_sources(files);});
 auto bad=files;bad.back().tile=files.front().tile;invalid([&]{validate_texture_sources(bad);});bad=files;bad.back().tile=10000;invalid([&]{validate_texture_sources(bad);});
 auto leaf=std::make_shared<FillTexture>();leaf->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();leaf->hash=hash.toStdString();leaf->name="Tile";leaf->width=leaf->height=1;
 auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(const auto &file:files)(*members)[file.tile]=leaf;
 FillTexture sequence=*leaf;sequence.tiles=members;sequence.validate();auto loaded=read_fill_texture_json(fill_texture_json(std::make_shared<FillTexture>(sequence)));assert(loaded->tiles->size()==1001);
 memory_admitted([&]{sequence.validate();});memory_admitted([&]{read_fill_texture_json(fill_texture_json(std::make_shared<FillTexture>(sequence)));});
 // App labels, aliases and persisted offline paths are metadata strings.
 const QString long_label(100000,QChar(0x03bb)),offline_path="/offline/"+QString(20000,'p');
 StudioAsset text_asset;text_asset.name=long_label;text_asset.filename=QString(400,'f')+".exr";text_asset.path=offline_path;text_asset.usages={"texture"};text_asset.search_terms={QString(5000,'t')};text_asset.texture_files={{0,text_asset.filename,offline_path,hash,1}};
 auto text_roundtrip=asset_metadata_read(asset_metadata(text_asset));assert(text_roundtrip.name==long_label&&text_roundtrip.path==offline_path&&text_roundtrip.search_terms==text_asset.search_terms);validate_texture_sources(text_asset.texture_files);memory_admitted([&]{asset_metadata_read(asset_metadata(text_asset));});
 AssetCatalog text_catalog(temp.path()+"/text-catalog");text_catalog.replace_project({text_asset});text_catalog.rename(text_asset.id,long_label+" renamed");assert(text_catalog.project_assets().front().name==long_label+" renamed");
 QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,temp.path()+"/config");save_material_library_paths({offline_path});assert(material_library_paths()==QStringList{offline_path});memory_admitted([&]{material_library_paths();});
 auto text_texture=std::make_shared<FillTexture>(*leaf);text_texture->name=long_label.toStdString();text_texture->source_space=std::string(5000,'c');text_texture->input_space=std::string(6000,'r');text_texture->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(1,FillTexture::Pixel{0,0,0,1});text_texture->validate();assert(read_fill_texture_json(fill_texture_json(text_texture))->name==text_texture->name);memory_admitted([&]{text_texture->validate();});
 LayerMaterialInput input;input.name=long_label.toStdString();input.resource=std::string(5000,'k');input.validate();memory_admitted([&]{input.validate();});
 RibbonImage ribbon;ribbon.width=ribbon.height=1;ribbon.name=long_label.toStdString();ribbon.pixels={{{1,0,0,1}}};const auto ribbon_bytes=ribbon_io::image_bytes(ribbon);assert(ribbon_io::read_image(QString::fromLatin1(qCompress(ribbon_bytes,1).toBase64()),ribbon_io::hash(ribbon_bytes))->name==ribbon.name);memory_admitted([&]{ribbon.validate();});
 LayerMask text_mask;MaskEffect fill;fill.kind=MaskEffectKind::Fill;fill.mapping.projection.texture=text_texture;text_mask.effects={fill};const auto packed_text_mask=write_mask_preset(text_mask);const auto restored_text_mask=read_mask_preset(packed_text_mask);assert(restored_text_mask.effects.front().mapping.projection.texture->name==text_texture->name&&restored_text_mask.effects.front().mapping.projection.texture->input_space==text_texture->input_space);memory_admitted([&]{write_mask_preset(text_mask);});
 // Native archive lengths remain 64-bit; valid ASCII manifests exceed
 // the former 4 MiB cutoff, with more than 4096 native image references.
 std::vector<NativeMaterialImageReference> images;const QString suffix(240,'x');
 for(int i=0;i<8000;++i)images.push_back({QString::number(i)+suffix,hash,suffix+".exr"});
 const auto bundle=native_material_bundle_bytes("native-node-bytes",images);assert(qFromBigEndian<quint64>(bundle.constData()+8)>4*1024*1024);
 const auto decoded=read_native_material_bundle(bundle);assert(decoded.images.size()==8000&&decoded.blend=="native-node-bytes");
 memory_admitted([&]{native_material_bundle_bytes("native-node-bytes",images);});memory_admitted([&]{read_native_material_bundle(bundle);});
 auto corrupt=bundle;corrupt[8]=char(0x7f);invalid([&]{read_native_material_bundle(corrupt);});
 // Catalog descriptor counts and JSON file length share the RAM admission used
 // for source payloads. This fixture loads metadata only, without image I/O.
 AssetCatalog catalog(temp.path()+"/library");std::vector<StudioAsset> resources;QJsonArray metadata;
 const QString long_path=temp.path()+"/"+QString(1500,'x');
 for(int i=0;i<5000;++i){StudioAsset asset;asset.name=QString("Resource %1").arg(i);asset.filename="source.exr";asset.path=long_path;asset.usages={"texture"};asset.texture_files={{0,"source.exr",long_path,hash,1}};metadata.append(asset_metadata(asset));resources.push_back(std::move(asset));}
 catalog.replace_project(resources);assert(catalog.project_assets().size()==5000);memory_admitted([&]{catalog.replace_project(resources);});assert(catalog.project_assets().size()==5000);
 const auto library=QJsonDocument(QJsonObject{{"version",1},{"resources",metadata}}).toJson();assert(library.size()>4*1024*1024);QFile library_file(temp.path()+"/library/library.json");assert(QDir().mkpath(temp.path()+"/library")&&library_file.open(QIODevice::WriteOnly));assert(library_file.write(library)==library.size());library_file.close();
 AssetCatalog reopened(temp.path()+"/library");reopened.load_user_library();assert(reopened.assets().size()>=5000);memory_admitted([&]{reopened.load_user_library();});
 // Paths retain their UUID/type validation while points use resource admission.
 SurfacePath path;path.name=long_label.toStdString();path.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();path.points.resize(513);path.validate();assert(read_surface_path(surface_path_json(path))->points.size()==513&&read_surface_path(surface_path_json(path))->name==path.name);memory_admitted([&]{path.validate();});
 // Unsupported utility files are deliberately reported/ignored, even in a
 // large batch; known utility image formats are decoded by the native reader.
#ifdef BTS_NATIVE_UTILITY_MAP_REGRESSION
 std::vector<std::string> paths(129,"unsupported.bin");assert(load_utility_maps(paths).ignored.size()==129);memory_admitted([&]{load_utility_maps(paths);});
#endif
 auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->radius=1;surface->triangles.resize(200001);PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1}};stamp.surface=surface;TilePaintPixels empty;SmudgeStroke stroke(empty,128);
 {PaintRecordingScope recording;assert(stroke.dab(stamp,{1001},.1).empty());memory_admitted([&]{stroke.dab(stamp,{1001},.1);});}
 // The parser validates a checksum-correct packed environment larger than the
 // former 16 MiB cutoff. Actual HDR decoding is a separate native boundary.
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Surface";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Surface"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);
 const QByteArray environment(17*1024*1024,'e');QJsonObject resource{{"name","large.hdr"},{"data",QString::fromLatin1(environment.toBase64())},{"sha256",QString::fromLatin1(QCryptographicHash::hash(environment,QCryptographicHash::Sha256).toHex())}};
 auto project=make_project_template(document,1,{{"view_transform","Standard"}},{{"rotation",0},{"exposure",0},{"opacity",100},{"blur",0},{"alignment","World"}},resource,{});validate_project_template(project);memory_admitted([&]{validate_project_template(project);});
#ifdef BTS_NATIVE_UTILITY_MAP_REGRESSION
 std::cout<<"Native utility-map batch: 129 input files and low-low-RAM admission PASS\n";
#endif
 std::cout<<"Resource caps: 5000 catalog descriptors/8000 native image references; >4 MiB manifests; 1001 source descriptors; 513 path points; 200001 smudge triangles; 17 MiB environment; low-RAM admission and encoded identity checks PASS\n";
}

#endif
