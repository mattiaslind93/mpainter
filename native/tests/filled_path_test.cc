// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilledPathRaster.hh"
#include "SurfacePathIO.hh"
#include <cassert>
#include <iostream>
#include <chrono>
using namespace bts;
int main(){SurfacePath path;path.id="c0a80001-1234-4000-8000-000000000001";path.filled=path.closed=true;path.points={{{-.6,-.5,0},{0,0,1},{.2,.25},1},{{.6,-.5,0},{0,0,1},{.8,.25},1},{{.6,.5,0},{0,0,1},{.8,.75},1},{{-.6,.5,0},{0,0,1},{.2,.75},1}};path.brush.channels={true,true,true,false,true};path.brush.values.base_color={.2,.7,.3,1};path.brush.values.roughness=.8;path.brush.values.metallic=.4;path.brush.values.bump=-.2;path.brush.displacement=true;path.brush.displacement_value=-.5;path.brush.ao=true;path.brush.values.ambient_occlusion=.6;path.brush.users[0]=true;path.brush.user_values[0]=.9;std::array<int,8> sizes;sizes.fill(128);
 SceneSnapshot scene;UvFaceTriangle a,b;a.corners={{{0,0},{1,0},{1,1}}};a.positions={{{-1,-1,0},{1,-1,0},{1,1,0}}};b.corners={{{0,0},{1,1},{0,1}}};b.positions={{{-1,-1,0},{1,1,0},{-1,1,0}}};scene.uv_triangles={a,b};const SurfacePathContact contact=[](const SurfacePathPoint &p,float diameter,bool,const BrushProjection &)->std::optional<SurfaceBrushSample>{return SurfaceBrushSample{{},{float(p.position[0]),float(p.position[1]),float(p.position[2])},diameter/100,1};};
 auto ink=raster_filled_path(path,2,128,sizes,{1001},contact,scene,0);assert(ink.primary.paint.sample(0,64,64)==(PaintPixels::Pixel{.2f,.7f,.3f,1}));assert(ink.primary.paint.sample(0,64,64)[3]==1&&ink.primary.paint.sample(0,5,5)[3]==0);assert(std::abs(ink.primary.paint.sample(2,64,64)[0]-.8)<1e-6);assert(ink.primary.paint.sample(4,64,64)[0]==-.2f&&ink.displacement.sample(2,64,64)[0]==-.5f);assert(ink.primary.ao.sample(2,64,64)[0]==.6f&&ink.users[0].sample(2,64,64)[0]==.9f);

 const auto reads=PaintPixels::recipe_materializations();SurfacePathInk deferred;
 {PaintRecordingScope record;deferred=raster_filled_path(path,2,128,sizes,{1001},contact,scene,0);}
 assert(PaintPixels::recipe_materializations()==reads);
 for(int channel=0;channel<5;++channel)assert(deferred.primary.paint.hash(channel)==ink.primary.paint.hash(channel));
 assert(deferred.displacement.hash(2)==ink.displacement.hash(2));assert(deferred.primary.ao.hash(2)==ink.primary.ao.hash(2));assert(deferred.users[0].hash(2)==ink.users[0].hash(2));

 auto small=path;for(auto &point:small.points)for(int c=0;c<2;++c)point.position[c]=point.position[c]*.1-.5;
 std::array<int,8> small_sizes;small_sizes.fill(512);SurfacePathInk bounded;
 {PaintRecordingScope record;bounded=raster_filled_path(small,2,512,small_sizes,{1001},contact,scene,0);assert(bounded.primary.paint.page_count(0)<=4);}
 auto bounded_reference=raster_filled_path(small,2,512,small_sizes,{1001},contact,scene,0);
 for(int channel=0;channel<5;++channel)assert(bounded.primary.paint.hash(channel)==bounded_reference.primary.paint.hash(channel));

 auto large=path;for(auto &point:large.points)for(int c=0;c<2;++c)point.position[c]*=1.5;
 large.brush.channels={true,false,false,false,false};large.brush.ao=large.brush.displacement=false;large.brush.users.fill(false);
 SurfacePathInk large_ink;std::array<int,8> large_sizes;large_sizes.fill(2048);
 const auto large_start=std::chrono::steady_clock::now();
 {PaintRecordingScope record;large_ink=raster_filled_path(large,2,2048,large_sizes,{1001},contact,scene,0);}
 const auto large_pixels=large_ink.primary.paint.tile(1001).materialized();std::size_t covered=0;
 for(int y=0;y<2048;++y)for(int x=0;x<2048;++x){const auto pixel=large_pixels.sample(0,x,y);if(pixel[3]){++covered;assert(pixel==(PaintPixels::Pixel{.2f,.7f,.3f,1}));}}
 assert(covered>2ULL*1024*1024);assert(large_pixels.sample(0,0,0)[3]==0);
 std::cout<<"Filled large CPU export: covered="<<covered<<" temporary_map_max=4096 elapsed_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-large_start).count()<<"\n";
 path.brush.channels[0]=false;assert(raster_filled_path(path,2,128,sizes,{1001},contact,scene,0).primary.paint.page_count(0)==0);path.brush.channels[0]=true;
 const auto original=ink.primary.paint.hash(0);path.closed=false;assert(raster_filled_path(path,2,128,sizes,{1001},contact,scene,0).primary.paint.empty());path.closed=true;surface_path_reverse(path);assert(raster_filled_path(path,2,128,sizes,{1001},contact,scene,0).primary.paint.hash(0)==original);
 // Coplanar disconnected pieces separated by a UV seam, a shallow nonplanar
 // second surface, and a remote parallel sheet. Only the bounded front region fills.
 scene.uv_triangles[1].positions[1][2]=.2;scene.uv_triangles[1].positions[2][2]=.2;const auto folded=raster_filled_path(path,2,128,sizes,{1001},contact,scene,0);assert(folded.primary.paint.sample(0,45,75)[3]>0);
 auto remote=a;remote.corners={{{1,0},{2,0},{2,1}}};for(auto &p:remote.positions)p[2]=5;scene.uv_triangles.push_back(remote);const auto protected_ink=raster_filled_path(path,2,128,sizes,{1001,1002},contact,scene,0);assert(!protected_ink.primary.paint.tiles().contains(1002));
 const auto recipe=surface_path_json(path);assert(recipe["version"]==6&&recipe["filled"].toBool()&&read_surface_path(recipe)->filled);auto legacy=recipe;legacy["version"]=4;legacy.remove("filled");assert(!read_surface_path(legacy)->filled);
 path.ribbon.enabled=true;bool rejected=false;try{path.validate();}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 std::cout<<"Filled contour interior/exterior, channel flags/scalars/signed displacement/AO/users, open/reverse, nonplanar/seam surfaces, remote-sheet rejection and v5/v4 recipes passed\n";
}
