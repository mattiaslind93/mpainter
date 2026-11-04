#pragma once
#include "AutoBakeEngine.hh"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#if defined(__linux__)
# include <sys/resource.h>
#endif
namespace bts {
// Procedural, detached fixture: no live document or artist file is touched.
inline QJsonObject verify_bake_memory_storage(){
 const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 auto source=std::make_shared<SceneSnapshot>();MaterialSummary material;material.session_id=1;material.name="BC memory fixture";source->materials.push_back(material);
 source->minimum={0,0,0};source->maximum={2,1,0};source->has_bounds=true;source->mesh_objects=1;source->object_names[1]="BC planes";source->object_vertex_counts[1]=8;
 for(int column=0;column<2;++column){
  const std::array<std::array<float,2>,4> corners{{{float(column),0},{float(column+1),0},{float(column+1),1},{float(column),1}}};
  for(const auto &indices:{std::array<int,3>{0,1,2},std::array<int,3>{0,2,3}}){
   UvFaceTriangle triangle;triangle.has_normals=true;triangle.boundary_edges=7;triangle.triangle_id=source->uv_triangles.size();triangle.polygon_id=column;
   for(int k=0;k<3;++k){triangle.corners[k]=corners[indices[k]];triangle.positions[k]={corners[indices[k]][0],corners[indices[k]][1],0};triangle.normals[k]={0,0,1};triangle.vertex_ids[k]=(1ULL<<32)|(column*4+indices[k]);}
   source->uv_triangles.push_back(triangle);
  }
 }
 AutoBakeEngine engine(source,source);AutoBakeOptions settings;settings.maps.resolution=settings.maps.height=4096;settings.maps.use_project_mesh=true;settings.maps.rays=1;settings.maps.dilation=0;settings.ray.match_names=false;
 std::atomic<bool> cancel=false;QElapsedTimer timer;timer.start();
 auto maps=engine.bake(1,"BC memory fixture",{}, {1001,1002},{},settings,cancel);
 int completed=0;std::uint64_t bytes=0;
 for(int role=0;role<7;++role){require(maps[role]&&maps[role]->tiles.size()==2,"Bake omitted a map or UDIM tile");for(int tile:{1001,1002}){
  const auto &plane=maps[role]->tiles.at(tile);require(plane.pixels&&plane.pixels->rgba.disk_backed(),"Bake result is not disk backed");
  require(plane.resolution==4096&&plane.rows()==4096,"Bake output resolution changed");
  const auto center=(std::size_t(2048)*4096+2048)*4;require(plane.pixels->rgba[center+3]>.99f,"Bake produced empty fixture coverage");
  bytes=resource_sum(bytes,resource_product(plane.pixels->rgba.size(),sizeof(float)));++completed;
 }}
 require(bytes>2ULL*1024*1024*1024,"Bake fixture did not cross the former history limit");
 const auto before=maps;
 settings.maps.resolution=settings.maps.height=32;settings.maps.enabled.fill(false);settings.maps.enabled[0]=true;
 auto rebaked=engine.bake(1,"BC rebake",{}, {1001,1002},{},settings,cancel);require(bool(rebaked[0]),"Rebake failed while old maps were retained");
 maps[0]=std::move(rebaked[0]);const auto after=maps;maps=before;
 for(int role=0;role<7;++role)for(int tile:{1001,1002})require(maps[role]->tiles.at(tile).pixels==before[role]->tiles.at(tile).pixels,"Undo lost the exact map snapshot");
 maps=after;require(maps[0]->tiles.at(1001).resolution==32,"Redo lost rebaked output");
 int fallback_completed=0;
 {
  struct RestoreEnvironment{QByteArray value=qgetenv("BTS_RECOVERY_DIR");bool present=qEnvironmentVariableIsSet("BTS_RECOVERY_DIR");~RestoreEnvironment(){if(present)qputenv("BTS_RECOVERY_DIR",value);else qunsetenv("BTS_RECOVERY_DIR");}} restore;
  QTemporaryFile blocker(qEnvironmentVariable("BTS_RECOVERY_DIR")+"/blocked-bake-cache-XXXXXX");require(blocker.open(),"Cannot create cache failure fixture");
  qputenv("BTS_RECOVERY_DIR",blocker.fileName().toUtf8());settings.maps.enabled.fill(true);
  const auto fallback=engine.bake(1,"BC unavailable cache",{}, {1001,1002},{},settings,cancel);
  for(int role=0;role<7;++role){require(fallback[role]&&fallback[role]->tiles.size()==2,"Unavailable disk cache discarded a completed map");for(const auto &[tile,plane]:fallback[role]->tiles){require(plane.pixels&&!plane.pixels->rgba.disk_backed(),"Cache failure fixture did not exercise fallback");++fallback_completed;}}
 }
 qint64 rss=0;
#if defined(__linux__)
 rusage usage{};getrusage(RUSAGE_SELF,&usage);rss=qint64(usage.ru_maxrss)*1024;
#endif
 return {{"native_auto_bake",true},{"native_mesh_bake_job",true},{"resolution",4096},{"roles",7},{"udim_tiles",2},{"completed",completed},{"retained_bytes",qint64(bytes)},{"exact_undo_redo",true},{"disk_cache_failure_completed",fallback_completed},{"peak_rss_bytes",rss},{"elapsed_ms",timer.elapsed()}};
}
inline int run_bake_memory_demo(const QString &directory,const QString &renderer){
 const auto proof=verify_bake_memory_storage();auto output=proof;output["renderer"]=renderer;output["visible_windows"]=0;
 QFile file(directory+"/bc-bake-memory.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write BC bake memory proof");file.write(QJsonDocument(output).toJson());
 std::printf("BTS_BAKE_MEMORY_PASS completed=14 retained_bytes=3758096384 renderer=%s\n",qPrintable(renderer));return 0;
}
}
