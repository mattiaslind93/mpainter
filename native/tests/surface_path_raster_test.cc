// SPDX-License-Identifier: GPL-3.0-or-later
#include "SurfacePathRaster.hh"
#include "PaintRecording.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
  SurfacePath path;path.id="c0a80001-1234-4000-8000-000000000001";path.points={{{-.6,0,0},{0,0,1},{.2,.5},1},{{.6,0,0},{0,0,1},{.8,.5},1}};path.brush.values.base_color={.1,.6,.9,1};path.brush.values.roughness=.8;path.brush.channels={true,false,true,false,false};path.brush.diameter=10;path.brush.spacing=20;path.brush.ao=true;path.brush.values.ambient_occlusion=.4;path.brush.users[0]=true;path.brush.user_values[0]=.7;
  const SurfacePathContact contact=[](const SurfacePathPoint &point,float diameter,bool,const BrushProjection &)->std::optional<SurfaceBrushSample>{if(std::abs(point.position[0])>1)return {};const auto uv=std::array{(point.position[0]+1)*.5,(point.position[1]+1)*.5};const double radius=diameter/200;return SurfaceBrushSample{PaintPixels::Stamp{uv,{radius,0},{0,radius}}, {float(point.position[0]),float(point.position[1]),0},float(diameter/100),1};};
  std::array<int,8> user_sizes;user_sizes.fill(128);const auto ink=raster_surface_path(path,2,128,user_sizes,{1001},contact);assert(ink.primary.paint.sample(0,64,64)[3]>.9);assert(ink.primary.paint.sample(0,64,64)[1]>.5);assert(ink.primary.ao.sample(2,64,64)[0]>.35);assert(ink.users[0].sample(2,64,64)[0]>.6);assert(ink.primary.paint.sample(0,64,95)[3]==0);
  path.brush.alpha.kind=AlphaKind::DynamicOutline;path.brush.alpha.dynamic.cycle=4;path.brush.spacing=100;auto dynamic=raster_surface_path(path,2,128,user_sizes,{1001},contact);assert(dynamic.primary.paint.hash(0)!=ink.primary.paint.hash(0));
  path.symmetry.enabled=true;path.symmetry.axis=1;path.symmetry.offset=.25;auto mirrored=raster_surface_path(path,2,128,user_sizes,{1001},contact);assert(mirrored.primary.paint.hash(0)!=dynamic.primary.paint.hash(0)&&mirrored.primary.paint.page_count(0)>=dynamic.primary.paint.page_count(0));
  // Authoring records immutable GPU work for every channel, including signed
  // displacement. CPU replay is deliberately only performed at this oracle boundary.
  path.brush.displacement=true;path.brush.displacement_value=-.35f;
  const auto reference=raster_surface_path(path,2,128,user_sizes,{1001},contact);
  const auto materializations=PaintPixels::recipe_materializations();SurfacePathInk recorded;
  {PaintRecordingScope gpu;recorded=raster_surface_path(path,2,128,user_sizes,{1001},contact);}
  assert(PaintPixels::recipe_materializations()==materializations);
  for(int c=0;c<5;++c)assert(recorded.primary.paint.hash(c)==reference.primary.paint.hash(c));
  assert(recorded.primary.ao.hash(2)==reference.primary.ao.hash(2));
  assert(recorded.users[0].hash(2)==reference.users[0].hash(2));
  assert(recorded.displacement.hash(2)==reference.displacement.hash(2));
  std::cout<<"Surface path contact-driven paint/AO/user raster, spacing, per-dab dynamic alpha and retained symmetry passed\n";
}
