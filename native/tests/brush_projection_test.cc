// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaintPixels.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
using bts::BrushFaceFrame;
using bts::PaintPixels;
int main()
{
  BrushFaceFrame flat;
  const auto folded=flat.neighbor({0,-1,0},{0,1,0},{-.5,0,0},{0,0,.5},{1,0,0});
  const auto edge_a=folded.map(BrushFaceFrame::V3{0,-1,0}),edge_b=folded.map(BrushFaceFrame::V3{0,1,0});
  assert(edge_a[0]==0&&edge_a[1]==-1&&edge_b[0]==0&&edge_b[1]==1);
  const auto center=folded.map(BrushFaceFrame::V3{0,0,.75});assert(std::abs(center[0]-.75)<1e-6&&center[1]==0);
  assert(bts::accepts_brush_normal({0,0,1},{0,0,-1},0));
  assert(!bts::accepts_brush_normal({.8660254,0,.5},{0,0,-1},10));
  assert(bts::accepts_brush_normal({.8660254,0,.5},{0,0,-1},90));
  assert(!bts::accepts_brush_normal({0,0,-1},{0,0,-1},90));
  assert(bts::accepts_brush_normal({0,0,-1},{0,0,-1},180));
  auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center={0,0,0};surface->radius=1;surface->projected=true;
  PaintPixels::SurfaceTriangle a,b;a.uv={{{0,0},{1,0},{1,1}}};b.uv={{{0,0},{1,1},{0,1}}};
  a.position={{{0,-1,0},{0,-1,1},{0,1,1}}};b.position={{{0,-1,0},{0,1,1},{0,1,0}}};a.normal=b.normal={1,0,0};
  for(auto *tri:{&a,&b})for(int i=0;i<3;++i)tri->paint_coordinates[i]=folded.map(tri->position[i]);surface->triangles={a,b};
  PaintPixels::Stamp stamp{{.5,.5},{.5,0},{0,.5},{}};stamp.channels={false,false,true,false,false};stamp.alpha.kind=bts::AlphaKind::Uniform;
  stamp.surface=surface;surface->unwrapped=true;PaintPixels wrap;wrap.stamp(128,stamp);assert(wrap.sample(2,96,64)[3]==1);
  surface->unwrapped=false;surface->planar_fade=true;PaintPixels planar;planar.stamp(128,stamp);assert(std::abs(planar.sample(2,96,64)[3]-(1-96.5/128))<1e-6);
  surface->planar_fade=false;surface->cull=true;surface->cull_angle=10;PaintPixels rejected;rejected.stamp(128,stamp);assert(rejected.empty());
  surface->cull=false;PaintPixels admitted;admitted.stamp(128,stamp);assert(admitted.sample(2,96,64)[3]==1);
  std::puts("projection: edge-preserving 90-degree unfolding, full Wrap vs depth-faded Planar, angle thresholds, backface rejection and disabled-cull admission PASS");
}
