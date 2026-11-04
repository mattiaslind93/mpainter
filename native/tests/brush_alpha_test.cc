#include "PaintPixels.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
using bts::PaintPixels;
int main()
{
  PaintPixels::Stamp stamp{{.5,.5},{.25,0},{0,.25},{}};stamp.channels={false,false,true,false,false};stamp.values.roughness=1;stamp.alpha.kind=bts::AlphaKind::Plus;
  PaintPixels plus,inverse;plus.stamp(128,stamp);stamp.alpha.inverted=true;inverse.stamp(128,stamp);
  for(int y=0;y<128;++y)for(int x=0;x<128;++x)assert(plus.sample(2,x,y)[3]+inverse.sample(2,x,y)[3]==(x>=32&&x<96&&y>=32&&y<96?1:0));
  assert(plus.sample(2,64,64)[3]==1&&plus.sample(2,80,80)[3]==0);
  stamp.alpha={};stamp.alpha.kind=bts::AlphaKind::Uniform;stamp.alpha.uniform=.25;stamp.flow=.4;
  PaintPixels uniform,composed;const auto bounds=uniform.stamp(128,stamp);composed.over_stroke({},uniform,.5,bounds);assert(std::abs(composed.sample(2,80,80)[3]-.05)<1e-6);
  auto bitmap=std::make_shared<bts::AlphaBitmap>();bitmap->width=bitmap->height=2;bitmap->pixels={0,.25,.5,1};
  assert(bitmap->sample(.5,.5)==.4375&&bitmap->sample(0,0)==0&&bitmap->sample(1,1)==1&&bitmap->sample(.5,.5,false)==1);
  stamp.alpha.kind=bts::AlphaKind::Bitmap;stamp.alpha.bitmap=bitmap;stamp.flow=1;
  PaintPixels image;image.stamp(128,stamp);assert(std::abs(image.sample(2,64,64)[3]-bitmap->sample(.5078125,.5078125))<1e-6);
  stamp.alpha.kind=bts::AlphaKind::Uniform;stamp.alpha.uniform=1;stamp.axis_x={.2,.2};stamp.axis_y={-.2,.2};PaintPixels rotated;rotated.stamp(128,stamp);assert(rotated.sample(2,64,110)[3]==1); // square corner outside old circular bounding box
  stamp.axis_x={.5,0};stamp.axis_y={0,.5};stamp.alpha.kind=bts::AlphaKind::Plus;
  auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center={0,0,0};surface->radius=1;
  PaintPixels::SurfaceTriangle a{{{{0,0},{1,0},{1,1}}},{{{-1,-1,0},{1,-1,0},{1,1,0}}},{}};
  PaintPixels::SurfaceTriangle b{{{{0,0},{1,1},{0,1}}},{{{-1,-1,0},{1,1,0},{-1,1,0}}},{}};surface->triangles={a,b};
  PaintPixels uv,world;uv.stamp(128,stamp);stamp.surface=surface;world.stamp(128,stamp);assert(uv.hash(2)==world.hash(2));
  stamp.alpha.kind=bts::AlphaKind::Uniform;PaintPixels square;square.stamp(128,stamp);assert(square.sample(2,120,120)[3]==1);
  for(auto &triangle:surface->triangles)for(auto &p:triangle.position)p[2]=2;PaintPixels deep;deep.stamp(128,stamp);assert(deep.empty());
  std::puts("alpha: complementary plus/inversion, uniform flow/opacity, raw bilinear bitmap, rotated square bounds, tangent/UV equivalence, square corners and depth rejection PASS");
}
