// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshMaps.hh"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);
 const auto root=qEnvironmentVariable("BTS_RECOVERY_DIR",QDir::homePath()+"/bts/build/bc-bake-memory");
 assert(QDir().mkpath(root));QTemporaryDir isolated(root+"/storage-XXXXXX");assert(isolated.isValid());
 qputenv("BTS_RECOVERY_DIR",isolated.path().toUtf8());
 const bool large=argc>1&&std::strcmp(argv[1],"--large")==0;
 const int resolution=large?4096:32;
 const auto bytes=bts::resource_product(bts::resource_product(resolution,resolution),16);

 std::uint64_t retained=0;int completed=0;
 {
  bts::MeshMaps maps;
  for(int role=0;role<7;++role){
   bts::MeshMap map;map.name=bts::mesh_map_names[role];
   for(int tile:{1001,1002}){
    const float value=float(role*2+tile-1001)/16;
    std::vector<float> data(std::size_t(resolution)*resolution*4,value);
    for(std::size_t at=3;at<data.size();at+=4)data[at]=1;
    auto pixels=std::make_shared<bts::MeshMapPixels>(resolution,std::move(data));
    assert(pixels->rgba.disk_backed());
    map.tiles.emplace(tile,bts::MeshMapPlane{resolution,pixels->hash,pixels,resolution});
    retained+=bytes;++completed;
   }
   maps[role]=std::move(map);
  }
  assert(completed==14);if(large)assert(retained>2ULL*1024*1024*1024);
  // Document history holds immutable snapshots: rebake, undo, redo keep exact
  // shared addresses and float bits without copying any retained tile.
  const auto before=maps;
  auto replacement=std::make_shared<bts::MeshMapPixels>(32,std::vector<float>(32*32*4,1));
  maps[0]->tiles.at(1001)={32,replacement->hash,replacement,32};
  const auto after=maps;maps=before;
  for(int role=0;role<7;++role)for(int tile:{1001,1002}){
   const auto &pixels=maps[role]->tiles.at(tile).pixels;
   assert(pixels==before[role]->tiles.at(tile).pixels);
   const float value=float(role*2+tile-1001)/16;
   for(std::size_t at:{std::size_t(0),pixels->rgba.size()/2,pixels->rgba.size()-4}){
    assert(pixels->rgba[at]==value&&pixels->rgba[at+3]==1);
   }
  }
  maps=after;assert(maps[0]->tiles.at(1001).pixels==replacement);
 }
 assert(QDir(isolated.path()+"/mesh-maps").entryList(QDir::Files).empty());
 // A failed disk cache preserves the finished buffer instead of failing bake.
 QFile blocker(isolated.path()+"/not-a-directory");assert(blocker.open(QIODevice::WriteOnly));blocker.close();
 qputenv("BTS_RECOVERY_DIR",blocker.fileName().toUtf8());
 bts::MeshMaps fallback_maps;int fallback_completed=0;
 for(int role=0;role<7;++role){
  bts::MeshMap map;
  for(int tile:{1001,1002}){
   auto fallback=std::make_shared<bts::MeshMapPixels>(32,std::vector<float>(32*32*4,.5f));
   assert(!fallback->rgba.disk_backed()&&fallback->rgba[0]==.5f);
   map.tiles.emplace(tile,bts::MeshMapPlane{32,fallback->hash,fallback,32});++fallback_completed;
  }
  fallback_maps[role]=std::move(map);
 }
 assert(fallback_completed==14);
 std::printf("BC baked-map storage PASS resolution=%d roles=7 udim_tiles=2 completed=%d retained_bytes=%llu exact_undo_redo=1 disk_failure_completed=%d\n",resolution,completed,static_cast<unsigned long long>(retained),fallback_completed);
}
