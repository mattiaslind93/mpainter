// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlenderHost.hh"
#include "NativeMaterialInspectionDemo.hh"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <cassert>
#include <clocale>
#include <fstream>
#include <sys/resource.h>
using namespace bts;
static qint64 rchar(){std::ifstream file("/proc/self/io");std::string key;long long n;while(file>>key>>n)if(key=="rchar:")return n;return -1;}
int main(int argc,char **argv){QApplication app(argc,argv);QLocale::setDefault(QLocale::c());std::setlocale(LC_NUMERIC,"C");try{
 if(argc!=3)throw std::invalid_argument("Expected output directory and explicit cases.json");const QString output=argv[1];qputenv("BTS_NATIVE_MATERIAL_CASES",QByteArray(argv[2]));BlenderHost host("/tmp/mpainter-tests/host/bin/bts-host","");QFile manifest(argv[2]);assert(manifest.open(QIODevice::ReadOnly));auto cases=QJsonDocument::fromJson(manifest.readAll()).array();const auto path=cases.first().toObject()["file"].toString();QFileInfo before(path);QJsonArray timing;BlendAssetLibrary previous;
 for(int pass=0;pass<2;++pass){const auto read_before=rchar();QElapsedTimer clock;clock.start();auto library=host.inspect_asset_library(path,std::make_shared<std::stop_source>(),{}).get();const auto ms=clock.nsecsElapsed()/1e6;const auto bytes=rchar()-read_before;int previews=0;for(const auto &e:library.materials)previews+=!e.material.preview.isNull();assert(library.files==1&&library.materials.size()==110);if(pass){assert(previous.materials.size()==library.materials.size());for(size_t i=0;i<library.materials.size();++i){const auto &a=previous.materials[i].material,&b=library.materials[i].material;assert(a.name==b.name&&a.preview==b.preview&&a.tags==b.tags&&a.description==b.description&&a.catalog_id==b.catalog_id);}assert(bytes<before.size()/100);}else previous=library;struct rusage index_usage{};getrusage(RUSAGE_SELF,&index_usage);timing.append(QJsonObject{{"peak_rss_kib",qint64(index_usage.ru_maxrss)},{"pass",pass?"warm":"cold"},{"milliseconds",ms},{"process_read_bytes",bytes},{"materials",qint64(library.materials.size())},{"saved_previews",previews}});printf("INDEX %s ms=%.3f read_bytes=%lld materials=%zu saved_previews=%d\n",pass?"warm":"cold",ms,(long long)bytes,library.materials.size(),previews);fflush(stdout);}
 assert(QFileInfo(path).size()==before.size()&&QFileInfo(path).lastModified()==before.lastModified());run_native_material_inspection_demo(host,output);struct rusage usage{};getrusage(RUSAGE_SELF,&usage);QFile result(output+"/library-index.json");assert(result.open(QIODevice::WriteOnly));result.write(QJsonDocument(QJsonObject{{"timings",timing},{"source_bytes",before.size()},{"source_metadata_unchanged",true},{"peak_rss_kib",qint64(usage.ru_maxrss)},{"six_input_regressions",true},{"gpu",false}}).toJson());host.shutdown();return 0;
 }catch(const std::exception &e){fprintf(stderr,"AW_LIBRARY_FAIL %s\n",e.what());return 1;}}
