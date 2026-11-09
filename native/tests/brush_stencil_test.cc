// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaintPixels.hh"
#include "TilePaintPixels.hh"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){using namespace bts;
  BrushStencil settings;assert(settings.sample({123,-456})==1);
  auto bitmap=std::make_shared<AlphaBitmap>();bitmap->width=bitmap->height=2;bitmap->pixels={0,1,0,1};bitmap->name="Half mask";
  BrushAlpha image;image.kind=AlphaKind::Bitmap;image.bitmap=bitmap;settings.resource=image;
  assert(settings.sample({-.9,0})==0&&settings.sample({.9,0})==1&&settings.sample({3,0})==0);
  for(int mode=0;mode<4;++mode){settings.tiling=mode;assert((settings.sample({2.9,0})>0)==bool(mode&1));assert((settings.sample({.9,2.5})>0)==bool(mode&2));}
  settings.tiling=0;settings.angle=90;assert(settings.sample({0,.9})==1);settings.angle=0;settings.offset={.5,0};assert(settings.sample({0,0})==0);
  settings.offset={};auto mapping=std::make_shared<StencilMapping>();mapping->stencil=settings;
  PaintPixels::Stamp stamp{{.5,.5},{.5,0},{0,.5}};stamp.alpha.kind=AlphaKind::Uniform;stamp.channels={false,true,true,false,false};stamp.values.metallic=.3;stamp.values.roughness=.7;stamp.flow=.4;stamp.stencil=mapping;
  PaintPixels masked;masked.stamp(128,stamp);
  for(int y=0;y<128;++y)for(int x=0;x<128;++x){const double expected=settings.sample({2*(x+.5)/128-1,2*(y+.5)/128-1})*.4;const auto m=masked.sample(1,x,y),r=masked.sample(2,x,y);assert(std::abs(r[3]-expected)<1e-7&&std::abs(r[0]-.7*expected)<1e-7&&std::abs(m[0]-.3*expected)<1e-7);}
  assert(masked.sample(2,8,64)[3]==0&&std::abs(masked.sample(2,120,64)[3]-.4)<1e-6);
  auto world=std::make_shared<StencilMapping>(*mapping);world->world=true;world->projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center={0,0,0};surface->radius=1;
  PaintPixels::SurfaceTriangle left{{{{0,0},{1,0},{1,1}}},{{{-1,-1,0},{1,-1,0},{1,1,0}}},{}};
  PaintPixels::SurfaceTriangle right{{{{0,0},{1,1},{0,1}}},{{{-1,-1,0},{1,1,0},{-1,1,0}}},{}};surface->triangles={left,right};stamp.surface=surface;stamp.stencil=world;
  PaintPixels spatial;spatial.stamp(128,stamp);assert(spatial.hash(1)==masked.hash(1)&&spatial.hash(2)==masked.hash(2));
  for(const auto &triangle:surface->triangles)world->uv_world.push_back({triangle.uv,triangle.position});stamp.surface.reset();PaintPixels aligned;aligned.stamp(128,stamp);assert(aligned.hash(2)==masked.hash(2));
  world->projection[14]=1;world->projection[15]=2;assert(std::abs(world->sample_world({.8,0,2})-settings.sample({.2,0}))<1e-7);assert(world->sample_world({0,0,-3})==0);
  auto tile_mapping=std::make_shared<StencilMapping>();tile_mapping->stencil.resource=BrushAlpha{};tile_mapping->stencil.resource->kind=AlphaKind::Uniform;tile_mapping->stencil.offset={2,0};
  stamp.stencil=tile_mapping;stamp.center={1.5,.5};TilePaintPixels tiles;tiles.stamp(128,stamp,{1001,1002});assert(tiles.tile(1001).empty()&&std::abs(tiles.sample(2,64,64,1002)[3]-.4)<1e-7);
  const auto before=tiles.hash(2);tile_mapping->stencil.scale=0;bool rejected=false;try{tiles.stamp(128,stamp,{1001,1002});}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&tiles.hash(2)==before);
  std::cout<<"Stencil screen transforms/tiling, per-texel multichannel UV/world equivalence, UV-aligned lookup, perspective, UDIM offset and atomic rejection passed\n";
{
 bts::StencilMapping mapping;mapping.world=true;mapping.viewport_aspect=2;mapping.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};const auto neutral=mapping.screen_world({.3,.2,0});assert(neutral&&std::abs((*neutral)[0]-.6)<1e-10);mapping.lens={true,.2,0,0};const auto warped=mapping.screen_world({.3,.2,0});assert(warped&&*warped!=*neutral);const auto restored=mapping.lens.to_source({((*warped)[0]/2+1)*.5,((*warped)[1]+1)*.5});assert(restored&&std::abs((*restored)[0]-.65)<1e-10&&std::abs((*restored)[1]-.6)<1e-10);
 }
}
