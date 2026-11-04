#include "OpenPbrUsdResources.hh"
#include "OpenPbrUsd.hh"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
namespace {
void write(const QString &path,const QByteArray &bytes){assert(QDir().mkpath(QFileInfo(path).absolutePath()));QFile file(path);assert(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size());}
QByteArray read(const QString &path){QFile file(path);assert(file.open(QIODevice::ReadOnly));return file.readAll();}
}
int main(int argc,char **argv){using namespace bts;QCoreApplication app(argc,argv);QTemporaryDir directory;assert(directory.isValid());const QDir root(directory.path());const QString cache=root.filePath("cache");const QByteArray first("encoded fixture tile 1001\0raw",29),second("encoded fixture tile 1002\0raw",29);write(root.filePath("maps/color_1001.exr"),first);write(root.filePath("maps/color_1002.exr"),second);
 OpenPbrMaterialX request;request.images["base_color"]={"maps/color_<UDIM>.exr",OpenPbrImageSpace::AcesCg};request.tiles={1001,1002};const auto xml=openpbr_materialx_document(request);write(root.filePath("material.mtlx"),xml);write(root.filePath("material.usda"),openpbr_usd_document(xml,"material.mtlx"));write(root.filePath("OpenPBR-LICENSE.txt"),openpbr_materialx_license());std::atomic_bool cancel=false;
 const auto profiles=read_openpbr_usd_profiles(root.filePath("material.usda"));assert(!profiles.empty());auto source=profiles.front().images.at("base_color");int progress=0;const auto loose=prepare_openpbr_usd_resource(source,{1001,1002},cache,cancel,[&](int,int,const QString &){++progress;});assert(QFileInfo(loose.path).isAbsolute());assert(progress==3&&loose.texture_files.size()==2&&loose.bytes.isEmpty()&&loose.texture_files[0].tile==1001&&loose.texture_files[1].tile==1002);assert(read(loose.texture_files[0].path)==first&&read(loose.texture_files[1].path)==second);
 package_openpbr_usd_files(root.filePath("material.usda"),root.filePath("material.usdz"),{"material.mtlx","OpenPBR-LICENSE.txt","maps/color_1001.exr","maps/color_1002.exr"},cancel);const auto packaged=read_openpbr_usd_profiles(root.filePath("material.usdz"));assert(!packaged.empty());const auto packed=prepare_openpbr_usd_resource(packaged.front().images.at("base_color"),{1001,1002},cache,cancel);assert(QFileInfo(packed.path).isAbsolute());assert(packed.bytes.isEmpty()&&packed.id==loose.id&&packed.texture_files.size()==2);assert(read(packed.texture_files[0].path)==first&&read(packed.texture_files[1].path)==second);assert(packed.texture_files[0].path!=loose.texture_files[0].path);const auto stamp=QFileInfo(packed.texture_files[0].path).lastModified();const auto again=prepare_openpbr_usd_resource(packaged.front().images.at("base_color"),{1001,1002},cache,cancel);assert(again.texture_files==packed.texture_files&&QFileInfo(again.texture_files[0].path).lastModified()==stamp);
 bool missing=false;try{prepare_openpbr_usd_resource(source,{1001,1002,1011},cache,cancel);}catch(const std::invalid_argument &error){missing=std::string(error.what()).find("1011")!=std::string::npos;}assert(missing);cancel=true;bool cancelled=false;try{prepare_openpbr_usd_resource(source,{1001},cache,cancel);}catch(const std::runtime_error &error){cancelled=std::string(error.what()).find("cancelled")!=std::string::npos;}assert(cancelled);cancel=false;
 // A single source is not converted into a numbered UDIM or copied into a bundle.
 source.binding.filename="maps/color_1001.exr";const auto single=prepare_openpbr_usd_resource(source,{1001,1002},cache,cancel);assert(single.texture_files.size()==1&&single.texture_files[0].tile==0&&single.bytes.isEmpty());assert(read(root.filePath("maps/color_1001.exr"))==first&&read(root.filePath("maps/color_1002.exr"))==second);
 std::cout<<"USD encoded resources: loose references, two UDIM package members, exact streamed bytes/SHA, shared IDs/cache reuse, missing tile/cancel, no decode or aggregate blob passed\n";
}
