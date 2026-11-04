#include "DiagnosticSpatialFilter.hh"
#include <cstdio>
#include <functional>
#include <string>

using namespace bts;
using namespace bts::diagnostic_spatial;
namespace {
int checks=0;
void require(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,const char *message){require(std::isfinite(actual)&&std::abs(actual-expected)<2e-6f,message);}
Plane scalar(int width,int height,std::initializer_list<float> values){
  Plane plane{width,height,{}};for(float v:values)plane.pixels.push_back({v,v,v,.37f});return plane;
}
Plane mask_plane(int width,int height,std::initializer_list<float> values){
  Plane p=scalar(width,height,values);for(auto &pixel:p.pixels)pixel={pixel[0],0,0,1};return p;
}
FilterSettings settings(int kind){FilterSettings value;value.resource=kind;return value;}
void kernels(){
  const auto source=scalar(3,2,{-1,0,2,3,5,8});
  const auto blur=filter(source,settings(7),Domain::Scalar);
  // Closed-form clamped-edge multiplicities; no reference convolution loop.
  const float expected[]={7.f/9,2,29.f/9,20.f/9,11.f/3,46.f/9};
  for(int i=0;i<6;++i){for(int c=0;c<3;++c)near(blur.pixels[i][c],expected[i],"Box kernel/edge mismatch");require(blur.pixels[i][3]==.37f,"Blur changed center coverage");}
  auto radius=settings(7);radius.radius=1.49f;require(filter(source,radius,Domain::Scalar).pixels==blur.pixels,"Radius rounding below half changed");
  radius.radius=1.5f;near(filter(source,radius,Domain::Scalar).at(0,0)[0],41.f/25,"Radius rounding to two failed");
  radius.radius=4;near(filter(source,radius,Domain::Scalar).at(0,0)[0],181.f/81,"Radius-four edge multiplicities failed");
  const auto sharp=filter(source,settings(8),Domain::Scalar);
  for(int i=0;i<6;++i)near(sharp.pixels[i][0],2*source.pixels[i][0]-expected[i],"Sharpen differs from independent 2C-box formula");
  require(sharp.at(0,0)[0]<0&&sharp.at(2,1)[0]>1,"Sharpen clipped working values");
  const auto mixed=filter(source,settings(8),Domain::Scalar,LayerBlend::Multiply,.25f);
  for(int i=0;i<6;++i){const float v=source.pixels[i][0];near(mixed.pixels[i][0],v*.75f+(2*v-expected[i])*v*.25f,"Filter blend/opacity order mismatch");}
  require(filter(source,settings(7),Domain::Scalar,LayerBlend::Disabled).pixels==source.pixels,"Disabled filter changed source");
  require(filter(source,settings(7),Domain::Scalar,LayerBlend::Normal,0).pixels==source.pixels,"Zero-opacity filter changed source");
  const auto mask=filter(source,settings(8),Domain::Mask);
  for(int i=0;i<6;++i)require(mask.pixels[i][1]==0&&mask.pixels[i][2]==0&&mask.pixels[i][3]==1,"Raw mask stage layout mismatch");
  const auto slope=filter(source,settings(10),Domain::Mask);
  near(slope.at(0,0)[0],6.2f,"Slope corner clamping/five samples mismatch");near(slope.at(2,0)[0],6.8f,"Slope opposite corner mismatch");
  // Each tile is an independent plane: adjacent data cannot enter its stencil.
  const auto other=scalar(2,3,{100,100,100,100,100,100});
  const auto independent=filter(other,settings(7),Domain::Scalar);
  for(const auto &p:independent.pixels)near(p[0],100,"Tile boundary sampled another tile");
  require(filter(source,settings(7),Domain::Scalar).pixels==blur.pixels,"Other tile contaminated source evaluation");
}
void rgb_and_warp(){
  Plane rgb{13,9,{}};
  for(int y=0;y<9;++y)for(int x=0;x<13;++x)rgb.pixels.push_back({-.4f+.11f*x+.07f*y,1.4f+.03f*x-.02f*y,.2f-.04f*x+.05f*y,.2f+.05f*(x%3)});
  const auto blur=filter(rgb,settings(7),Domain::RGB);
  const auto mixed=filter(rgb,settings(8),Domain::RGB,LayerBlend::Multiply,.25f);
  const float step_x[3]={.11f,.03f,-.04f},step_y[3]={.07f,-.02f,.05f};
  for(int y:{0,8})for(int x:{0,12}){
    const auto p=rgb.at(x,y);
    for(int c=0;c<3;++c){const float average=p[c]+(x==0?step_x[c]:-step_x[c])/3+(y==0?step_y[c]:-step_y[c])/3;
      near(blur.at(x,y)[c],average,"RGB corner box mismatch");
      near(mixed.at(x,y)[c],p[c]*.75f+(2*p[c]-average)*p[c]*.25f,"RGB signed/HDR mixed corner mismatch");}
    require(mixed.at(x,y)[3]==p[3],"Mixed RGB filter changed coverage");
  }
  const auto slope=filter(rgb,settings(10),Domain::RGB);const auto center=rgb.at(4,3);
  // Interior luminance gradient gives x offsets [0,0,1,1,1], y all zero.
  near(slope.at(4,3)[0],center[0]+.066f,"RGB slope R/luminance mismatch");
  near(slope.at(4,3)[1],center[1]+.018f,"RGB slope G mismatch");
  near(slope.at(4,3)[2],center[2]-.024f,"RGB slope B mismatch");
  require(slope.at(4,3)[3]==center[3],"RGB slope changed coverage");
  const auto mask=filter(rgb,settings(10),Domain::Mask);
  // Red-only gradient gives x offsets [0,1,1,2,3], y [0,0,1,1,2].
  near(mask.at(4,3)[0],center[0]+.11f*1.4f+.07f*.8f,"Mask slope used RGB luminance");
  auto warp=settings(9);warp.radius=1.3f;warp.hue=.17f;warp.value_scale=.7f;
  const auto displaced=filter(rgb,warp,Domain::RGB);
  // Frozen coordinate witnesses calculated separately with Python integer hash
  // and double interpolation. None is near a half-integer displacement tie.
  const int positions[][4]={{0,0,0,0},{12,0,12,0},{0,8,2,8},{12,8,9,8},{6,4,3,4},{3,7,0,6}};
  for(const auto &p:positions){for(int c=0;c<3;++c)near(displaced.at(p[0],p[1])[c],rgb.at(p[2],p[3])[c],"Warp noise/rotation/nearest-coordinate witness mismatch");require(displaced.at(p[0],p[1])[3]==rgb.at(p[0],p[1])[3],"Warp transported neighbor coverage");}
  warp.value_scale=0;require(filter(rgb,warp,Domain::RGB).pixels==rgb.pixels,"Zero Warp displacement changed source");
  const auto singleton=scalar(1,1,{-2});
  for(int kind=7;kind<=10;++kind)near(filter(singleton,settings(kind),Domain::Scalar).at(0,0)[0],-2,"Singleton boundary changed constant");
}
void ordering_and_groups(){
  const auto source=scalar(3,2,{-1,0,2,3,5,8});
  // Point Clamp and spatial Blur do not commute on signed/HDR working values.
  const auto clamp=[](Plane p){for(auto &pixel:p.pixels)for(int c=0;c<3;++c)pixel[c]=std::clamp(pixel[c],0.f,1.f);return p;};
  const auto before=filter(clamp(source),settings(7),Domain::Scalar);
  const auto after=clamp(filter(source,settings(7),Domain::Scalar));
  near(before.at(0,0)[0],1.f/3,"Ordered prefix source mismatch");
  near(after.at(0,0)[0],7.f/9,"Ordered filter result mismatch");
  require(std::abs(before.at(0,0)[0]-after.at(0,0)[0])>.4f,"Noncommuting order lost witness");
  const auto parent=mask_plane(2,2,{.4f,.4f,.4f,.4f});
  const auto raw_child=mask_plane(2,2,{-1,.2f,1.3f,.8f});
  const auto composed=group(parent,raw_child,LayerBlend::Multiply,.5f);
  const float expected[]={.2f,.24f,.4f,.36f};
  for(int i=0;i<4;++i)near(composed.pixels[i][0],expected[i],"Group resolve clamp/opacity/blend mismatch");
  const auto zero=mask_plane(2,2,{0,0,0,0});
  const auto child=group(zero,raw_child,LayerBlend::Normal,.5f);
  const auto nested=group(parent,child,LayerBlend::Overlay,.25f);
  const float nested_expected[]={.3f,.32f,.4f,.38f};
  for(int i=0;i<4;++i)near(nested.pixels[i][0],nested_expected[i],"Nested group background/order mismatch");
  require(group(parent,raw_child,LayerBlend::Disabled,.5f).pixels==parent.pixels,"Disabled group changed parent");
  require(source.pixels==scalar(3,2,{-1,0,2,3,5,8}).pixels,"Filter mutated immutable prefix");
}
void rejection(){
  const auto invalid=[&](const std::function<void()> &run){bool failed=false;try{run();}catch(const std::invalid_argument &){failed=true;}require(failed,"Invalid diagnostic input accepted");};
  invalid([&]{filter(Plane{2,2,{}},settings(7),Domain::RGB);});
  auto p=scalar(1,1,{1});auto bad=settings(7);bad.resource=6;invalid([&]{filter(p,bad,Domain::Scalar);});
  invalid([&]{filter(p,settings(7),Domain::Scalar,LayerBlend::Normal,1.1f);});
  invalid([&]{group(p,scalar(2,1,{0,0}),LayerBlend::Normal,1);});
  p.pixels[0][1]=2;invalid([&]{filter(p,settings(7),Domain::Scalar);});
}
}
int main(){try{kernels();rgb_and_warp();ordering_and_groups();rejection();std::printf("DIAGNOSTIC_SPATIAL_FILTER PASS checks=%d explicit_diagnostic_only=1 full_stack_parity=0\n",checks);return 0;}catch(const std::exception &e){std::fprintf(stderr,"DIAGNOSTIC_SPATIAL_FILTER FAILED: %s\n",e.what());return 1;}}
