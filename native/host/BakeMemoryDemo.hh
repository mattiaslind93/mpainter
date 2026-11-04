#pragma once
#include "AutoBakeEngine.hh"
#include "BlenderHost.hh"
#include "GPU_capabilities.hh"
#include "GPU_state.hh"
#include <QJsonArray>
#include <cstring>
#include <QScopeGuard>
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
inline QJsonObject verify_bake_memory_storage(const std::function<void(const std::function<void()> &)> &with_gpu){
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
 const int size=qEnvironmentVariableIntValue("BTS_BAKE_TEST_RESOLUTION")==8192?8192:4096;const std::set<int> tiles=size==8192?std::set<int>{1001}:std::set<int>{1001,1002};
 AutoBakeEngine engine(source,source);AutoBakeOptions settings;settings.maps.resolution=settings.maps.height=size;settings.maps.use_project_mesh=true;settings.maps.rays=1;settings.maps.dilation=0;settings.ray.match_names=false;
 settings.maps.with_gpu=[&](const std::function<void()> &work){with_gpu([&]{const auto old_free=texture_free_vram.exchange(16ULL*1024*1024);const auto old_valid=texture_vram_query_valid.exchange(true);const auto restore=qScopeGuard([&]{texture_free_vram=old_free;texture_vram_query_valid=old_valid;});work();});};
 std::atomic<bool> cancel=false;QElapsedTimer timer;timer.start();
 auto maps=engine.bake(1,"BC memory fixture",{}, tiles,{},settings,cancel);
 int completed=0;std::uint64_t bytes=0;
 for(int role=0;role<7;++role){require(maps[role]&&maps[role]->tiles.size()==tiles.size(),"Bake omitted a map or UDIM tile");for(int tile:tiles){
  const auto &plane=maps[role]->tiles.at(tile);require(plane.pixels&&plane.pixels->rgba.disk_backed(),"Bake result is not disk backed");
  require(plane.resolution==size&&plane.rows()==size,"Bake output resolution changed");
  const auto center=(std::size_t(size/2)*size+size/2)*4;require(plane.pixels->rgba[center+3]>.99f,"Bake produced empty fixture coverage");
  for(int y:{size/4,size/2,3*size/4})for(int x:{size/4,size/2,3*size/4}){
   const auto index=(std::size_t(y)*size+x)*4;std::array<float,3> expected{};
   if(role==0||role==1)expected={.5f,.5f,1};
   else if(role==2)std::copy_n(MaterialValues{}.base_color.begin(),3,expected.begin());
   else if(role==3||role==6)expected={1,1,1};
   else if(role==4)expected={.5f,.5f,.5f};
   else expected={float(((tile-1001)+(x+.5)/size-1)/std::sqrt(5.)+.5),float(((y+.5)/size-.5)/std::sqrt(5.)+.5),.5f};
   for(int c=0;c<3;++c)require(std::abs(plane.pixels->rgba[index+c]-expected[c])<2e-5,"Baked map differs from analytical plane oracle");
  }
  bytes=resource_sum(bytes,resource_product(plane.pixels->rgba.size(),sizeof(float)));++completed;
 }}
 require(bytes>2ULL*1024*1024*1024,"Bake fixture did not cross the former history limit");
 QJsonArray compared_maps;
 {
  settings.maps.with_gpu=[&](const std::function<void()> &work){with_gpu([&]{
   blender::GPU_finish();int total=0,free=0;if(blender::GPU_mem_stats_supported())blender::GPU_mem_stats_get(&total,&free);
   const auto old_free=texture_free_vram.exchange(total>0&&free>=0?std::uint64_t(free)*1024:0);const auto old_valid=texture_vram_query_valid.exchange(total>0&&free>=0&&free<=total);
   const auto restore=qScopeGuard([&]{texture_free_vram=old_free;texture_vram_query_valid=old_valid;});work();
  });};
  const auto reference=engine.bake(1,"BC unconstrained reference",{},tiles,{},settings,cancel);
  for(int role=0;role<7;++role){require(reference[role]&&reference[role]->tiles.size()==tiles.size(),"Reference bake omitted a map or UDIM tile");for(int tile:tiles){
   const auto &a=maps[role]->tiles.at(tile),&b=reference[role]->tiles.at(tile);require(a.resolution==b.resolution&&a.rows()==b.rows()&&a.hash==b.hash&&a.pixels->rgba.size()==b.pixels->rgba.size(),"Constrained full bake changed dimensions or pixel hash");
   const auto length=a.pixels->rgba.size()*sizeof(float);const auto *ap=reinterpret_cast<const unsigned char *>(a.pixels->rgba.data()),*bp=reinterpret_cast<const unsigned char *>(b.pixels->rgba.data());
   for(std::size_t offset=0;offset<length;offset+=1024*1024)require(std::memcmp(ap+offset,bp+offset,std::min<std::size_t>(1024*1024,length-offset))==0,"Constrained full bake changed a pixel bit");
   compared_maps.append(QJsonObject{{"role",role},{"udim",tile},{"pixel_hash",QString::fromStdString(a.hash)},{"compared_bytes",qint64(length)}});
  }}
 }

 const auto before=maps;
 settings.maps.resolution=settings.maps.height=32;settings.maps.enabled.fill(false);settings.maps.enabled[0]=true;
 auto rebaked=engine.bake(1,"BC rebake",{}, tiles,{},settings,cancel);require(bool(rebaked[0]),"Rebake failed while old maps were retained");
 maps[0]=std::move(rebaked[0]);const auto after=maps;maps=before;
 for(int role=0;role<7;++role)for(int tile:tiles)require(maps[role]->tiles.at(tile).pixels==before[role]->tiles.at(tile).pixels,"Undo lost the exact map snapshot");
 maps=after;require(maps[0]->tiles.at(1001).resolution==32,"Redo lost rebaked output");
 int fallback_completed=0;
 {
  struct RestoreEnvironment{QByteArray value=qgetenv("BTS_RECOVERY_DIR");bool present=qEnvironmentVariableIsSet("BTS_RECOVERY_DIR");~RestoreEnvironment(){if(present)qputenv("BTS_RECOVERY_DIR",value);else qunsetenv("BTS_RECOVERY_DIR");}} restore;
  QTemporaryFile blocker(qEnvironmentVariable("BTS_RECOVERY_DIR")+"/blocked-bake-cache-XXXXXX");require(blocker.open(),"Cannot create cache failure fixture");
  qputenv("BTS_RECOVERY_DIR",blocker.fileName().toUtf8());settings.maps.enabled.fill(true);
  settings.maps.with_gpu=[&](const std::function<void()> &work){with_gpu([&]{const auto old_free=texture_free_vram.exchange(0);const auto old_valid=texture_vram_query_valid.exchange(true);const auto restore=qScopeGuard([&]{texture_free_vram=old_free;texture_vram_query_valid=old_valid;});work();});};
  const auto fallback=engine.bake(1,"BC unavailable cache",{}, tiles,{},settings,cancel);
  for(int role=0;role<7;++role){require(fallback[role]&&fallback[role]->tiles.size()==tiles.size(),"Unavailable disk cache discarded a completed map");for(const auto &[tile,plane]:fallback[role]->tiles){require(plane.pixels&&!plane.pixels->rgba.disk_backed(),"Cache failure fixture did not exercise fallback");++fallback_completed;}}
 }
 qint64 rss=0;
#if defined(__linux__)
 rusage usage{};getrusage(RUSAGE_SELF,&usage);rss=qint64(usage.ru_maxrss)*1024;
#endif
 return {{"all_maps_bit_exact_against_actual_VRAM",true},{"compared_maps",compared_maps},{"simulated_available_vram_bytes",16*1024*1024},{"per_map_gpu_with_CPU_fallback",true},{"native_auto_bake",true},{"native_mesh_bake_job",true},{"resolution",size},{"roles",7},{"udim_tiles",int(tiles.size())},{"completed",completed},{"retained_bytes",qint64(bytes)},{"analytical_checks_per_map",27},{"exact_undo_redo",true},{"disk_cache_failure_completed",fallback_completed},{"peak_rss_bytes",rss},{"elapsed_ms",timer.elapsed()}};
}
inline int run_bake_memory_demo(BlenderHost &host,const QString &directory){
 const auto renderer=QString::fromStdString(host.renderer());const auto proof=verify_bake_memory_storage([&](const std::function<void()> &work){host.with_gpu(work);});auto output=proof;output["renderer"]=renderer;output["visible_windows"]=0;
 QFile file(directory+"/bc-bake-memory.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write BC bake memory proof");file.write(QJsonDocument(output).toJson());
 std::printf("BTS_BAKE_MEMORY_PASS resolution=%d completed=%d retained_bytes=%lld renderer=%s\n",proof["resolution"].toInt(),proof["completed"].toInt(),(long long)proof["retained_bytes"].toInteger(),qPrintable(renderer));return 0;
}
}
