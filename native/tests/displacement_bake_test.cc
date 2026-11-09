// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef BTS_NATIVE_DISPLACEMENT_ONLY
#include "MeshBaker.hh"
#include "CLG_log.h"
#include "IMB_imbuf.hh"
#include <QCoreApplication>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace bts;
int main(int argc,char **argv){
  QCoreApplication app(argc,argv);CLG_init();blender::IMB_init();
  auto snapshot=std::make_shared<SceneSnapshot>();snapshot->materials.resize(1);snapshot->materials[0].session_id=7;snapshot->uv_triangles.resize(1);
  auto &triangle=snapshot->uv_triangles.front();triangle.positions={{{0,0,0},{1,0,0},{0,1,0}}};triangle.corners={{{0,0},{1,0},{0,1}}};triangle.normals={{{0,0,1},{0,0,1},{0,0,1}}};triangle.has_normals=true;const auto original_positions=triangle.positions;
  auto atlas=std::make_shared<FillSurfaceAtlas>();atlas->resolution=atlas->height=32;atlas->tiles[1001].resize(32*32);for(auto &point:atlas->tiles[1001])point.valid=true;
  MeshBakeSettings settings;settings.resolution=settings.height=32;settings.use_project_mesh=true;settings.enabled.fill(false);settings.enabled[0]=true;settings.dilation=0;settings.displacement[7].strength=1;settings.displacement[7].subdivision=3;
  const std::vector<MeshBakeInput> inputs{{7,"Displaced",{},atlas}};
  // Working pixels fit; the expanded geometry must be rejected before copying.
  {TextureRamQueryScope low(+[]()->std::uint64_t{return 1024*1024;});bool denied=false;try{MeshBakeJob job(snapshot,inputs,settings);}catch(const std::length_error &error){denied=std::string(error.what()).find("Expanded displacement bake geometry")!=std::string::npos;}assert(!denied);}
  settings.displacement[7].subdivision=9;
  {MeshBakeJob job(snapshot,inputs,settings);while(!job.finished.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(job.error.empty()&&job.completed==1&&job.result.size()==1);const auto &plane=job.result[0].maps[0]->tiles.at(1001);assert(plane.pixels&&plane.pixels->rgba.size()==32*32*4);}
  assert(snapshot->uv_triangles.size()==1&&snapshot->uv_triangles.front().positions==original_positions);
  blender::IMB_exit();CLG_exit();std::cout<<"Native MeshBakeJob: level3 low-RAM admission; level9 262144 triangles to completed normal map; immutable source PASS\n";
}
#else
#include "DisplacementBake.hh"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
using namespace bts;
static BakeTri plane(std::array<std::array<float,2>,3> uv){
  BakeTri tri;for(int i=0;i<3;++i){tri.corners[i].uv=uv[i];tri.corners[i].normal={0,0,1};tri.corners[i].position={uv[i][0]*2-1,uv[i][1]*2-1,0};}return tri;
}
int main(){
  const std::vector<BakeTri> flat{plane({{{0,0},{1,0},{1,1}}}),plane({{{0,0},{1,1},{0,1}}})};
  BakeDisplacement off;off.strength=0;off.subdivision=2;const auto unchanged=displace_bake_mesh(flat,off);
  assert(unchanged.size()==flat.size());
  for(std::size_t i=0;i<flat.size();++i)for(int k=0;k<3;++k)for(int a=0;a<3;++a)assert(unchanged[i].corners[k].position[a]==flat[i].corners[k].position[a]);
  BakeDisplacement bump;bump.subdivision=2;bump.width=bump.rows=33;bump.midlevel=0;bump.strength=1;bump.samples[1001].assign(33*33,0);
  for(int y=0;y<33;++y)for(int x=0;x<33;++x){const float u=x/32.f,v=y/32.f,d=(u-.5f)*(u-.5f)+(v-.5f)*(v-.5f);bump.samples[1001][y*33+x]=std::exp(-d/.02f);}
  const auto displaced=displace_bake_mesh(flat,bump);
  assert(displaced.size()==flat.size()*16);
  bool lifted=false;for(const auto &tri:displaced)for(const auto &corner:tri.corners){const float u=corner.uv[0],v=corner.uv[1];if((u-.5f)*(u-.5f)+(v-.5f)*(v-.5f)<.002f){assert(corner.position[2]>0.4f);lifted=true;}}
  assert(lifted);
  std::vector<std::array<std::array<float,3>,3>> raised,original;
  for(const auto &tri:displaced)raised.push_back({tri.corners[0].position,tri.corners[1].position,tri.corners[2].position});
  for(const auto &tri:flat)original.push_back({tri.corners[0].position,tri.corners[1].position,tri.corners[2].position});
  const std::array<float,3> beside{-.5f,0,0},up{0,0,1};const auto open=bake_cosine_occlusion(original,beside,up,32,4,7),shadowed16=bake_cosine_occlusion(raised,beside,up,16,4,7),shadowed=bake_cosine_occlusion(raised,beside,up,32,4,7),shadowed128=bake_cosine_occlusion(raised,beside,up,128,4,7),shadowed256=bake_cosine_occlusion(raised,beside,up,256,4,7),shadowed512=bake_cosine_occlusion(raised,beside,up,512,4,7);
  assert(open>.98f&&shadowed16<open-.05f&&shadowed<open-.05f&&shadowed128<open-.05f&&shadowed256<open-.05f&&shadowed512<open-.05f&&std::abs(shadowed512-shadowed256)>1e-6f);
  const float peak=bake_cavity(bump,.5f,.5f),ring=bake_cavity(bump,.72f,.5f),far=bake_cavity(bump,.05f,.05f),flat_cavity=bake_cavity(off,.5f,.5f);
  assert(peak>0.05f&&ring<0&&std::abs(far)<1e-4f&&flat_cavity==0);
  // Native levels 0..11 retain their exact four-way subdivision count.
  for(int level=0;level<=11;++level){const auto plan=bake_subdivision_plan(1,level);const auto count=std::uint64_t(1)<<(2*level);assert(plan.triangles==count);assert(plan.peak_bytes==(count+(level?count/4:0))*sizeof(BakeTri));}
  for(int invalid:{-1,12}){bool rejected=false;try{bake_subdivision_plan(1,invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);}
  {bool overflow=false;try{bake_subdivision_plan(std::numeric_limits<std::uint64_t>::max()/4+1,1);}catch(const std::overflow_error &){overflow=true;}assert(overflow);}
  assert(bake_ray_tree_nodes(1)==6);assert(bake_ray_tree_bytes(1000000)>1000000*128ULL);
  {bool native_bound=false;try{bake_ray_tree_nodes(std::numeric_limits<int>::max());}catch(const std::length_error &){native_bound=true;}assert(native_bound);}
  BakeDisplacement detailed;detailed.strength=1;detailed.subdivision=9;
  {const auto dense=displace_bake_mesh({flat.front()},detailed);assert(dense.size()==262144);}
  // Also execute the largest native level; this used to silently become 6.
  detailed.subdivision=11;{const auto dense=displace_bake_mesh({flat.front()},detailed);assert(dense.size()==4194304);}
  {TextureRamQueryScope small(+[]()->std::uint64_t{return 1024;});bool denied=false;try{displace_bake_mesh({flat.front()},detailed);}catch(const std::length_error &error){denied=std::string(error.what()).find("Displacement bake subdivision buffers")!=std::string::npos;}assert(!denied);}
  std::cout<<"Displacement bake: plane AO "<<open<<" -> "<<shadowed<<", cavity peak "<<peak<<" ring "<<ring<<"\n";
}

#endif
