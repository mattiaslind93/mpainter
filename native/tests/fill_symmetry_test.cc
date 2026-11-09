// SPDX-License-Identifier: GPL-3.0-or-later
#include "FillSymmetry.hh"
#include "SpatialProjection.hh"
#include <cassert>
#include <iostream>
struct Surface{std::array<float,3> position{},normal{};bool valid=true;};
int main(){using namespace bts;
 FillSymmetry s;s.constraint.enabled=true;s.constraint.axis=0;s.constraint.offset=.3;
 const Surface a{{.7f,.2f,-.1f},{.8f,.4f,-.2f}},b{{-.1f,.2f,-.1f},{-.8f,.4f,-.2f}};
 auto x=fold_fill_surface(&s,a,std::array<double,3>{.7,0,0}),y=fold_fill_surface(&s,b,std::array<double,3>{.7,0,0});assert(x.copy==0&&y.copy==1);for(int c=0;c<3;++c){assert(std::abs(x.surface.position[c]-y.surface.position[c])<1e-6);assert(std::abs(x.surface.normal[c]-y.surface.normal[c])<1e-6);}
 auto negative=fold_fill_surface(&s,a,std::array<double,3>{-.5,0,0});assert(negative.copy==1&&std::abs(negative.surface.position[0]+.1)<1e-6);
 s.constraint.enabled=false;assert(fold_fill_surface(&s,b,std::array<double,3>{}).surface.position==b.position);s.constraint.enabled=true;
 s.constraint.mode=SymmetryMode::Radial;s.constraint.axis=2;s.constraint.copies=4;s.constraint.center={.3,.4,0};
 for(int n=0;n<4;++n){const double angle=n*std::numbers::pi/2;Surface point{{float(.3+.5*std::cos(angle)),float(.4+.5*std::sin(angle)),.2f},{float(std::cos(angle)),float(std::sin(angle)),0}};auto folded=fold_fill_surface(&s,point,std::array<double,3>{.8,.4,0});assert(folded.copy==n);assert(std::abs(folded.surface.position[0]-.8)<1e-6&&std::abs(folded.surface.position[1]-.4)<1e-6);assert(std::abs(folded.surface.normal[0]-1)<1e-6&&std::abs(folded.surface.normal[1])<1e-6);}
 s.constraint.copies=3;for(int n=0;n<3;++n){const double angle=n*2*std::numbers::pi/3;Surface point{{float(.3+.5*std::cos(angle)),float(.4+.5*std::sin(angle)),0},{}};auto folded=fold_fill_surface(&s,point,std::array<double,3>{.8,.4,0});assert(folded.copy==n);}
 s.constraint.copies=33;bool invalid=false;try{s.validate();}catch(const std::invalid_argument &){invalid=true;}assert(invalid);
 auto settings=std::make_shared<FillSymmetry>();settings->constraint.enabled=true;settings->constraint.axis=0;
 FillProjection mapping;mapping.symmetry=settings;mapping.mode=2;mapping.offset3={.25,0,0};mapping.scale3={.3,.3,.3};mapping.depth_culling=false;mapping.shape_crop=true;mapping.filtering=2;mapping.wrap=1;
 auto image=std::make_shared<FillTexture>();image->asset="c0a80001-1234-4000-8000-000000000001";image->hash=std::string(64,'a');image->name="Asymmetric";image->width=image->height=2;image->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(std::vector<FillTexture::Pixel>{{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1}});mapping.texture=image;
 FillSurfaceAtlas atlas;atlas.extent=2;FillSurfacePoint left,right;left.valid=right.valid=true;right.position={.65f,0,.12f};left.position={-.65f,0,.12f};right.normal=left.normal={0,-1,0};
 const auto sample=fill_spatial_sample(mapping,right,atlas);assert(sample[3]==1&&fill_spatial_sample(mapping,left,atlas)==sample);auto plain=mapping;plain.symmetry.reset();assert(fill_spatial_sample(plain,left,atlas)[3]==0);
 auto flip=std::make_shared<FillSymmetry>(*settings);flip->flip_u=true;mapping.symmetry=flip;assert(fill_spatial_sample(mapping,right,atlas)==sample&&fill_spatial_sample(mapping,left,atlas)!=sample);flip->flip_u=false;flip->flip_v=true;assert(fill_spatial_sample(mapping,left,atlas)!=sample);
 mapping.symmetry=settings;mapping.shape_crop=false;mapping.wrap=0;mapping.scale3={1,1,1};
 for(int mode:{2,3,4,5}){mapping.mode=mode;if(mode==5)mapping.warp=std::make_shared<const WarpGrid>();const auto aa=fill_spatial_sample(mapping,right,atlas),bb=fill_spatial_sample(mapping,left,atlas);for(int c=0;c<4;++c)assert(std::abs(aa[c]-bb[c])<1e-6);}
 mapping.mode=1;mapping.offset3={.5,0,0};const auto ta=fill_texture_triplanar_sample(mapping,right,atlas.extent),tb=fill_texture_triplanar_sample(mapping,left,atlas.extent);for(int c=0;c<4;++c)assert(std::abs(ta[c]-tb[c])<1e-6);
 mapping.mode=0;auto uv_mapping=mapping;const auto unchanged=fill_symmetric_surface(uv_mapping,left,{0,0,0});assert(unchanged.position==left.position&&uv_mapping.scale==mapping.scale);
 std::cout<<"Fill symmetry world mirror/radial folding, normals, source side, off and copy parity PASS\n";
}
