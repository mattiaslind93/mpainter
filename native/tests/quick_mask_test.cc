#include "QuickMask.hh"
#include "QuickMaskOverlay.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
  auto mask=std::make_shared<QuickMaskField>();mask->resolution=128;
  PaintPixels::Stamp black{{.5,.5},{.08,0},{0,.35}};black.channels={false,false,true,false,false};black.values.roughness=0;black.hardness=1;mask->pixels.stamp(128,black);assert(mask->sample_uv(.5,.5)==0&&mask->sample_uv(.2,.5)==1);
  PaintPixels::Stamp ink{{.5,.5},{.4,0},{0,.08}};ink.values.base_color={1,1,1,1};ink.hardness=1;ink.channels={true,false,false,false,false};ink.coverage_mask=mask;TilePaintPixels gated;gated.stamp(128,ink);assert(gated.sample(0,64,64)[3]==0&&gated.sample(0,32,64)[3]>.99&&gated.sample(0,96,64)[3]>.99);
  mask->inverted=true;assert(mask->sample_uv(.5,.5)==1&&mask->sample_uv(.2,.5)==0);TilePaintPixels inside;inside.stamp(128,ink);assert(inside.sample(0,64,64)[3]>.99&&inside.sample(0,32,64)[3]==0);
  auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center={0,0,0};surface->radius=.8;surface->triangles={{{{{0,0},{1,0},{1,1}}},{{{-1,-1,0},{1,-1,0},{1,1,0}}},{}},{{{{0,0},{1,1},{0,1}}},{{{-1,-1,0},{1,1,0},{-1,1,0}}},{}}};ink.surface=surface;TilePaintPixels world;world.stamp(128,ink);assert(world.sample(0,64,64)[3]>.99&&world.sample(0,32,64)[3]==0);
  mask->inverted=false;black.center={1.5,.5};mask->pixels.stamp(128,black,{1002});ink.surface.reset();ink.center={1.5,.5};TilePaintPixels udim;udim.stamp(128,ink,{1002});assert(udim.sample(0,64,64,1002)[3]==0&&udim.sample(0,32,64,1002)[3]>.99);
  QuickMaskField clear;clear.inverted=true;assert(clear.sample_uv(.5,.5)==0);assert(clear.author_value(0)==1);clear.inverted=false;assert(clear.sample_uv(.5,.5)==1);
  // Independent old raster contract: virtual overlay must preserve each sample
  // while holding only a COW reference, not materializing 256x256 per tile.
  const std::set<int> tiles{1001,1002,1011};
  for(int mask_resolution:{128,256})for(bool editing:{false,true})for(bool inverted:{false,true}){
    QuickMaskField field;field.resolution=mask_resolution;field.inverted=inverted;
    auto stroke=black;stroke.center={1.45,.4};stroke.flow=.61;stroke.hardness=.37;field.pixels.stamp(mask_resolution,stroke,tiles);
    QuickMaskOverlay overlay;overlay.field=std::make_shared<QuickMaskField>(field);overlay.editing=editing;
    for(int tile:tiles){PaintPixels old;
      for(int py=0;py<4;++py)for(int px=0;px<4;++px){std::array<PaintPixels::Pixel,4096> page{};for(int y=0;y<64;++y)for(int x=0;x<64;++x){int ix=px*64+x,iy=py*64+y;float allowed=field.sample_uv((tile-1001)%10+(ix+.5)/256,(tile-1001)/10+(iy+.5)/256,256);float checker=editing?.14f:0,gray=((ix/8+iy/8)%2)?.3f:.12f;float alpha=1-allowed+checker*allowed,rgb=gray*checker*allowed;page[y*64+x]={rgb,rgb,rgb,alpha};}if(std::any_of(page.begin(),page.end(),[](const auto &pixel){return pixel[3]>0;}))old.assign_page(256,0,py*65536+px,page);}
      for(auto size:std::array<std::array<int,2>,4>{{{64,128},{200,203},{512,512},{2048,1024}}})for(int i=0;i<101;++i){int x=(i*71)%size[0],y=(i*43)%size[1];auto a=old.sample_resampled(0,x,y,size[0],size[1]),b=overlay.sample(x,y,size[0],size[1],tile);for(int c=0;c<4;++c)assert(std::abs(a[c]-b[c])<1e-6);}
    }
  }
  std::cout<<"Temporary quick mask: white default, black exclusion stripe, inversion, UV/world coverage gating, UDIM offsets and author-value inversion passed\n";
}
