// SPDX-License-Identifier: GPL-3.0-or-later
#include "RibbonSettings.hh"
#include "RibbonRaster.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){RibbonSettings s;s.validate();assert(s.image->width==4*s.image->height);s.tiling=RibbonTiling::None;auto l=RibbonLayout::make(s,8,1,false);auto a=l.coordinates(s,0,0),b=l.coordinates(s,8,0);assert(a&&b&&(*a)[0]==0&&(*b)[0]==1);s.tiling=RibbonTiling::Custom;s.count=4;l=RibbonLayout::make(s,8,1,false);assert(std::abs((*l.coordinates(s,.5,0))[0]-.25)<1e-9);s.mirror=true;assert(std::abs((*l.coordinates(s,2.5,0))[0]-.75)<1e-9);s.flip=true;assert((*l.coordinates(s,.5,1))[1]==0);s.flip=s.mirror=false;
  s.offsets_only=true;s.start_offset=s.end_offset=.1;s.tiling=RibbonTiling::None;l=RibbonLayout::make(s,12,1,false);assert(std::abs((*l.coordinates(s,.2,0))[0]-.05)<1e-9);l=RibbonLayout::make(s,12,1,true);assert(std::abs((*l.coordinates(s,.2,0))[0]-.2/12)<1e-9);
  s.offsets_only=false;s.stretch=RibbonStretch::KeepAspect;s.tiling=RibbonTiling::None;s.alignment=RibbonAlignment::Center;l=RibbonLayout::make(s,8,1,false);assert(!l.coordinates(s,1,0)&&l.coordinates(s,2,0)&&l.coordinates(s,6,0)&&!l.coordinates(s,7,0));
  s.tiling=RibbonTiling::Auto;s.alignment=RibbonAlignment::Start;l=RibbonLayout::make(s,6,1,false);assert(l.count==2&&l.coordinates(s,5,0));s.remove_clipped=true;assert(!l.coordinates(s,5,0)&&l.coordinates(s,3,0));s.ratio=RibbonRatio::Length;l=RibbonLayout::make(s,6,1,false);assert(l.period==3&&l.width==.75&&l.coordinates(s,5,0));
  s.orientation=1;assert(s.image_aspect()==.25);s.image->validate();bool failed=false;s.start_offset=1;try{s.validate();}catch(const std::invalid_argument &){failed=true;}assert(failed);
  SurfacePath path;path.ribbon.enabled=true;path.id="c0a80001-1234-4000-8000-000000000001";path.points={{{-.6,0,0},{0,0,1},{.2,.5},1},{{.6,0,0},{0,0,1},{.8,.5},1}};path.brush.diameter=20;path.brush.channels={true,false,true,false,false};path.ribbon.tiling=RibbonTiling::None;std::array<int,8> sizes;sizes.fill(128);
  const SurfacePathContact contact=[](const SurfacePathPoint &p,float diameter,bool coverage,const BrushProjection &)->std::optional<SurfaceBrushSample>{PaintPixels::Stamp stamp{{(p.position[0]+1)*.5,(p.position[1]+1)*.5},{.1,0},{0,.1}};if(coverage){auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center=p.position;surface->radius=diameter/100;PaintPixels::SurfaceTriangle a,b;a.uv={{{0,0},{1,0},{1,1}}};a.position={{{-1,-1,0},{1,-1,0},{1,1,0}}};b.uv={{{0,0},{1,1},{0,1}}};b.position={{{-1,-1,0},{1,1,0},{-1,1,0}}};surface->triangles={a,b};stamp.surface=surface;}return SurfaceBrushSample{stamp,{float(p.position[0]),float(p.position[1]),0},diameter/100,1};};
  const auto ink=raster_ribbon_path(path,2,128,sizes,{1001},contact);assert(ink.primary.paint.sample(0,64,64)[3]>.9&&ink.primary.paint.sample(0,64,95)[3]==0);const auto repeated=ink.primary.paint.hash(0);path.brush.displacement=true;path.brush.displacement_value=-.5;assert(raster_ribbon_path(path,2,128,sizes,{1001},contact).displacement.sample(2,64,64)[0]<0);path.ribbon.tiling=RibbonTiling::Custom;path.ribbon.count=3;assert(raster_ribbon_path(path,2,128,sizes,{1001},contact).primary.paint.hash(0)!=repeated);
  path.ribbon.tiling=RibbonTiling::None;path.brush.displacement=false;path.brush.pressure_size=false;path.brush.pressure_flow=true;path.brush.minimum_flow=0;path.points[0].pressure=.25;path.points[1].pressure=1;const auto flow=raster_ribbon_path(path,2,128,sizes,{1001},contact);assert(flow.primary.paint.sample(0,100,64)[3]>flow.primary.paint.sample(0,35,64)[3]+.3f);
  path.brush.pressure_flow=false;path.brush.pressure_size=true;path.brush.minimum_size=5;const auto taper=raster_ribbon_path(path,2,128,sizes,{1001},contact);int left=0,right=0;for(int y=0;y<128;++y){left+=taper.primary.paint.sample(0,35,y)[3]>0;right+=taper.primary.paint.sample(0,100,y)[3]>0;}assert(right>left+5);
  path.brush.pressure_size=false;auto translucent=std::make_shared<RibbonImage>();translucent->width=translucent->height=1;translucent->name="Half alpha";translucent->pixels={{{.4,.2,.1,.5}}};path.ribbon.image=translucent;path.points={{{-.7,-.6,0},{0,0,1},{.15,.2},1},{{.7,.6,0},{0,0,1},{.85,.8},1},{{-.7,.6,0},{0,0,1},{.15,.8},1},{{.7,-.6,0},{0,0,1},{.85,.2},1}};path.ribbon.alpha=RibbonAlpha::Max;const auto maximum=raster_ribbon_path(path,2,128,sizes,{1001},contact).primary.paint.sample(0,64,64)[3];path.ribbon.alpha=RibbonAlpha::Normal;const auto normal=raster_ribbon_path(path,2,128,sizes,{1001},contact).primary.paint.sample(0,64,64)[3];path.ribbon.alpha=RibbonAlpha::Add;const auto add=raster_ribbon_path(path,2,128,sizes,{1001},contact).primary.paint.sample(0,64,64)[3];assert(std::abs(maximum-.5)<1e-5&&std::abs(normal-.75)<1e-5&&std::abs(add-1)<1e-5);

  auto overlap=path;overlap.brush.diameter=26;overlap.ribbon.corner=RibbonCorner::Round;overlap.ribbon.alpha=RibbonAlpha::Normal;
  overlap.points.resize(2);
  const auto continuous=raster_ribbon_path(overlap,2,128,sizes,{1001},contact);
  float max_alpha=0;int repeated_texels=0;
  for(int y=0;y<128;++y)for(int x=0;x<128;++x){const auto alpha=continuous.primary.paint.sample(0,x,y)[3];max_alpha=std::max(max_alpha,alpha);repeated_texels+=alpha>.500001;}
  std::cerr<<"Ribbon contiguous patch max_alpha="<<max_alpha<<" expected=.5 repeated="<<repeated_texels<<"\n";
  assert(repeated_texels==0&&std::abs(max_alpha-.5)<1e-6);
  overlap.closed=true;overlap.brush.diameter=20;
  overlap.points={{{-.7,-.7,0},{0,0,1},{.15,.15},1},{{.7,-.7,0},{0,0,1},{.85,.15},1},{{.7,.7,0},{0,0,1},{.85,.85},1},{{-.7,.7,0},{0,0,1},{.15,.85},1}};
  const auto closed=raster_ribbon_path(overlap,2,128,sizes,{1001},contact);int closed_pixels=0;
  for(int y=0;y<128;++y)for(int x=0;x<128;++x){const auto alpha=closed.primary.paint.sample(0,x,y)[3];if(alpha>0){++closed_pixels;if(std::abs(alpha-.5)>=1e-6)std::cerr<<"closed patch x="<<x<<" y="<<y<<" alpha="<<alpha<<"\n";assert(std::abs(alpha-.5)<1e-6);}}
  assert(closed_pixels>0);
  for(int corner=0;corner<4;++corner)for(int policy=0;policy<3;++policy){
   path.ribbon.corner=RibbonCorner(corner);path.ribbon.alpha=RibbonAlpha(policy);
   path.brush.channels.fill(true);path.brush.ao=path.brush.displacement=true;path.brush.users[0]=true;
   path.brush.displacement_value=-.25;path.brush.values.bump=-.2;path.brush.user_values[0]=.7;
   const auto reference=raster_ribbon_path(path,2,128,sizes,{1001},contact);
   const auto reads=PaintPixels::recipe_materializations();SurfacePathInk deferred;
   {PaintRecordingScope record;deferred=raster_ribbon_path(path,2,128,sizes,{1001},contact,{},0,true);}
   assert(PaintPixels::recipe_materializations()==reads);
   for(int c=0;c<5;++c)assert(deferred.primary.paint.hash(c)==reference.primary.paint.hash(c));
   assert(deferred.displacement.hash(2)==reference.displacement.hash(2));assert(deferred.users[0].hash(2)==reference.users[0].hash(2));assert(deferred.primary.ao.hash(2)==reference.primary.ao.hash(2));
  }
  std::cout<<"Ribbon lazy metadata: 12 corners/overlap policies, all channels, exact page-wise CPU replay and zero recording reads passed\n";
  RibbonImage image;image.width=2;image.height=1;image.name="transparent test";image.pixels={{{1,0,0,1}},{{0,0,1,0}}};image.validate();const auto color=image.sample(.5,.5);assert(std::abs(color[0]-1)<1e-6&&color[2]==0&&std::abs(color[3]-.5)<1e-6);std::cout<<"Ribbon stretch/tiling/mirroring/orientation/offset/closed/aspect/alignment/clipping and associated-alpha image sampling passed\n";
}
