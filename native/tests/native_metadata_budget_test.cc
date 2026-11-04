#include "NativeJsonProperty.hh"
#include "TextureExport.hh"
#include "ImageColorMetadata.hh"
#include "SymmetryIO.hh"
#include "DisplayPostEffects.hh"
#include "Recovery.hh"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"
#include "BKE_main.hh"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cassert>
#include <climits>
#include <iostream>
using namespace bts;
using namespace blender;
namespace {
template<class F>bool rejects(F &&f){try{f();}catch(const std::exception &){return true;}return false;}
void write_file(const QString &path,const QByteArray &bytes){QFile file(path);assert(file.open(QIODevice::WriteOnly|QIODevice::Truncate));assert(file.write(bytes)==bytes.size());}
void property_corruption(IDProperty &property,const auto &read){
 const auto original=property;auto *data=static_cast<char *>(property.data.pointer);const char last=data[property.len-1],first=data[0];
 property.len=0;assert(rejects(read));property=original;property.len=-1;assert(rejects(read));property=original;
 property.totallen=property.len-1;assert(rejects(read));property=original;
 property.data.pointer=nullptr;assert(rejects(read));property=original;
 property.type=IDP_INT;property.data.pointer=reinterpret_cast<void *>(1);assert(rejects(read));property=original;
 property.subtype=IDP_STRING_SUB_BYTE;property.data.pointer=reinterpret_cast<void *>(1);assert(rejects(read));property=original;
 property.len=property.totallen=original.totallen+1024;assert(rejects(read));property=original;
 data[property.len-1]='x';assert(rejects(read));data[property.len-1]=last;
 data[0]='\0';assert(rejects(read));data[0]=first;
 read();
}
}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);assert(argc==2);QDir().mkpath(QString::fromLocal8Bit(argv[1]));
 assert(native_json_string_size(std::uint64_t(INT_MAX)-1,"Fixture")==INT_MAX-1);assert(rejects([]{native_json_string_size(INT_MAX,"Fixture");}));assert(rejects([]{native_json_string_size(UINT64_MAX,"Fixture");}));
 // Image color metadata shares the same checked native allocation span reader.
 auto *image_metadata=IDP_New(IDP_GROUP,nullptr,"metadata");const std::string color_space(4096,'c');
 IDP_AddToGroup(image_metadata,IDP_NewString(StringRef(color_space.data(),color_space.size()),"colorInteropID"));
 IDP_AddToGroup(image_metadata,IDP_NewString("","empty"));IDP_AddToGroup(image_metadata,IDP_NewInt(7,"non-string"));
 assert(image_color_metadata(image_metadata,"colorInteropID")==color_space);
 assert(image_color_metadata(image_metadata,"empty")==std::string{});
 assert(!image_color_metadata(nullptr,"missing")&&!image_color_metadata(image_metadata,"missing")&&!image_color_metadata(image_metadata,"non-string"));
 auto *color_property=IDP_GetPropertyFromGroup(image_metadata,"colorInteropID");
 // Non-string metadata is optional, whereas a malformed declared string rejects.
 property_corruption(*color_property,[&]{const auto value=image_color_metadata(image_metadata,"colorInteropID");if(!value)throw std::invalid_argument("Required image metadata fixture is missing");assert(*value==color_space);});
 {TextureRamQueryScope low([]()->std::uint64_t{return 0;});assert(!rejects([&]{image_color_metadata(image_metadata,"colorInteropID");}));assert(image_color_metadata(image_metadata,"empty")==std::string{});assert(!image_color_metadata(image_metadata,"non-string"));}
 IDP_FreeProperty(image_metadata);
 Scene scene{};SymmetrySettings symmetry;symmetry.enabled=true;symmetry.axis=2;symmetry.center={1,2,3};write_project_symmetry(scene,symmetry);
 const auto padded=[](const QJsonObject &value){return QJsonDocument(value).toJson(QJsonDocument::Compact)+QByteArray(65536,' ');};
 const auto symmetry_bytes=padded(symmetry_json(symmetry));IDP_ReplaceInGroup(scene.id.properties,IDP_NewString(StringRef(symmetry_bytes.constData(),symmetry_bytes.size()),"bts_symmetry"));
 assert(read_project_symmetry(scene).center==symmetry.center&&read_project_symmetry(scene).enabled);
 DisplayPostSettings display;display.brightness=1.25f;const auto display_bytes=padded(display.to_json());IDP_ReplaceInGroup(scene.id.properties,IDP_NewString(StringRef(display_bytes.constData(),display_bytes.size()),"bts_display_post_effects"));assert(display_post_settings(scene)==display);
 property_corruption(*IDP_GetPropertyFromGroup(scene.id.properties,"bts_symmetry"),[&]{read_project_symmetry(scene);});property_corruption(*IDP_GetPropertyFromGroup(scene.id.properties,"bts_display_post_effects"),[&]{display_post_settings(scene);});
 assert(rejects([]{new_native_json_property(QByteArray("{}\0suffix",9),"corrupt","Fixture");}));
 {
  TextureRamQueryScope low([]()->std::uint64_t{return 0;});assert(!rejects([&]{read_project_symmetry(scene);}));assert(!rejects([&]{display_post_settings(scene);}));assert(!rejects([&]{write_project_symmetry(scene,symmetry);}));
 }
 // The actual display writer stays inactive, so this needs no render context.
 auto *main=BKE_main_new();View3D view{};set_display_post_settings(*main,scene,view,display);assert(display_post_settings(scene)==display);BKE_main_free(main);IDP_FreeProperty(scene.id.properties);scene.id.properties=nullptr;
 QTemporaryDir directory(QString::fromLocal8Bit(argv[1])+"/recovery-XXXXXX");assert(directory.isValid());RecoveryStore store(directory.path());const auto id=QUuid::createUuid();const auto snapshot=store.next_path(id);write_file(snapshot,QByteArray(64,'B'));
 const QString source="/source/"+QString(65536,QChar('a'))+"/project.blend";store.record(id,snapshot,source);const auto manifest=snapshot+".json";QFile original_file(manifest);assert(original_file.open(QIODevice::ReadOnly));const auto original=original_file.readAll();original_file.close();assert(original.size()>32768);
 const auto check=[&](const QString &expected){const auto entries=store.entries();assert(entries.size()==1&&entries[0].project==id&&entries[0].path==snapshot&&entries[0].source==expected);};check(source);
 {TextureRamQueryScope low([]()->std::uint64_t{return 0;});check(source);assert(!rejects([&]{store.record(id,snapshot,source);}));}check(source);
 write_file(manifest,original+'\0');check({});write_file(manifest,"{ broken");check({});
 auto object=QJsonDocument::fromJson(original).object();object["source"]=QString("corrupt")+QChar::Null+"path";write_file(manifest,QJsonDocument(object).toJson());check({});
 for(const auto &key:{"project","snapshot"}){object=QJsonDocument::fromJson(original).object();object[key]="wrong identity";write_file(manifest,QJsonDocument(object).toJson());check({});}
 QTemporaryDir outside(QString::fromLocal8Bit(argv[1])+"/outside-XXXXXX");assert(outside.isValid());const auto outside_manifest=outside.path()+"/manifest.json";write_file(outside_manifest,original);assert(QFile::remove(manifest)&&QFile::link(outside_manifest,manifest));check({});assert(QFile::remove(manifest));write_file(manifest,original);
 const auto outside_snapshot=outside.path()+"/outside.blend";assert(QFile::rename(snapshot,outside_snapshot)&&QFile::link(outside_snapshot,snapshot));assert(store.entries().empty());
 // Planning large output sets and16K destinations allocates metadata, not pixels.
 {ExportRequest request;request.standard_outputs=false;request.mesh="fixture.obj";TextureSet set;request.tiles.push_back({set,1001,128});request.tiles.back().output_resolution=16384;for(int n=0;n<40;++n)request.outputs.push_back({"$mesh_map_"+std::to_string(n),ExportMapSource::Bump,false});const auto files=export_file_list(request);assert(files.size()==40);for(const auto &file:files)assert(file.resolution==16384);TextureRamQueryScope low(+[]()->std::uint64_t{return 0;});assert(!rejects([&]{export_file_list(request);}));}
 std::cout<<"Native metadata: full4096-byte image color metadata, absent/non-string/empty distinction, padded64KiB scene JSON, actual IDProperty spans/NUL/int limits, low-RAM admission, >32KiB recovery and full long source, corrupt/foreign manifest containment PASS\n";
}
