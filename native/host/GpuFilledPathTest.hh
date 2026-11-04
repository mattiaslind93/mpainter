#pragma once
#include "GpuPaintPages.hh"
#include "MEM_guardedalloc.h"
namespace bts {
inline int verify_gpu_filled_path(GpuPaintPages *prepared=nullptr)
{
 using namespace blender;constexpr int size=128;
 using Image=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
 Image image(GPU_texture_create_2d("BTS filled contour oracle",size,size,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 GpuPaintPages owned;auto &pages=prepared?*prepared:owned;int cases=0;float maximum_error=0;
 const auto check=[&](const TilePaintPixels &authored,const TilePaintPixels &reference,int channel,int tile){
  const auto before=PaintPixels::recipe_materializations();const float clear[4]={};GPU_texture_clear(image.get(),GPU_DATA_FLOAT,clear);
  const auto &source=authored.tile(tile);const auto versions=source.page_versions();
  for(const auto &[key,version]:versions[channel])if(!pages.write(image.get(),source,channel,key))throw std::runtime_error("Filled path page unexpectedly deferred");
  if(PaintPixels::recipe_materializations()!=before)throw std::runtime_error("Filled path sampled CPU authoring pixels");
  auto *data=static_cast<float *>(GPU_texture_read(image.get(),GPU_DATA_FLOAT,0));float error=0;int mismatches=0;
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto expected=reference.sample(channel,x,y,tile);float local=0;for(int k=0;k<4;++k){if(!std::isfinite(data[4*(y*size+x)+k])||!std::isfinite(expected[k])){MEM_delete(data);throw std::runtime_error("Nonfinite GPU path oracle");}local=std::max(local,std::abs(data[4*(y*size+x)+k]-expected[k]));}error=std::max(error,local);
   if(local>2e-5&&mismatches++<6)std::fprintf(stderr,"BTS_FILLED_PATH_MISMATCH case=%d tile=%d channel=%d x=%d y=%d expected=%.9g,%.9g actual=%.9g,%.9g\n",cases,tile,channel,x,y,expected[0],expected[3],data[4*(y*size+x)],data[4*(y*size+x)+3]);}
  MEM_delete(data);maximum_error=std::max(maximum_error,error);
  if(error>2e-5)throw std::runtime_error("Filled-path GPU differs from CPU: case="+std::to_string(cases)+" tile="+std::to_string(tile)+" channel="+std::to_string(channel)+" error="+std::to_string(error));
  ++cases;
 };
 SurfacePath path;path.id="c0a80001-1234-4000-8000-000000000001";path.filled=path.closed=true;
 path.points={{{-.63,-.51,0},{0,0,1},{.185,.245},1},{{.61,-.51,0},{0,0,1},{.805,.245},1},{{.61,.53,0},{0,0,1},{.805,.765},1},{{-.63,.53,0},{0,0,1},{.185,.765},1}};
 path.brush.channels.fill(true);path.brush.values.base_color={1.4,.7,.2,1};path.brush.values.bump=-.3;
 path.brush.values.normal={.1,.3,.8};path.brush.ao=true;path.brush.values.ambient_occlusion=.4;
 path.brush.displacement=true;path.brush.displacement_value=-.0017;path.brush.users[0]=true;path.brush.user_values[0]=.7;
 const SurfacePathContact contact=[](const SurfacePathPoint &p,float diameter,bool,const BrushProjection &)->std::optional<SurfaceBrushSample>{return SurfaceBrushSample{{},{float(p.position[0]),float(p.position[1]),float(p.position[2])},diameter/100,1};};
 SceneSnapshot scene;
 for(int tile:{1001,1002,1011}){const float u=(tile-1001)%10,v=(tile-1001)/10;UvFaceTriangle a,b;
  a.corners={{{u,v},{u+1,v},{u+1,v+1}}};a.positions={{{-1,-1,0},{1,-1,0},{1,1,0}}};
  b.corners={{{u,v},{u+1,v+1},{u,v+1}}};b.positions={{{-1,-1,0},{1,1,0},{-1,1,0}}};scene.uv_triangles.push_back(a);scene.uv_triangles.push_back(b);
 }
 std::array<int,8> sizes;sizes.fill(size);
 for(int variant=0;variant<5;++variant){
  std::shared_ptr<QuickMaskField> mask;
  if(variant>=1){mask=std::make_shared<QuickMaskField>();mask->resolution=variant==4?256:64;mask->inverted=variant==2;
   PaintPixels::Stamp dab{{.5,.5},{.2,0},{0,.2}};dab.channels={false,false,true,false,false};dab.values.roughness=.15;
   {PaintRecordingScope recording;mask->pixels.stamp(mask->resolution,dab,{1001});}
  }
  if(variant==3){path.points[1].position[1]=-.23;path.points[2].position[0]=.2;scene.uv_triangles[1].positions[2][2]=.12;path.symmetry.enabled=true;path.symmetry.axis=0;path.symmetry.offset=.1;}
  const auto reference=raster_filled_path(path,2,size,sizes,{1001,1002,1011},contact,scene,0,mask);
  const auto before=PaintPixels::recipe_materializations();SurfacePathInk authored;
  {PaintRecordingScope recording;authored=raster_filled_path(path,2,size,sizes,{1001,1002,1011},contact,scene,0,mask);}
  if(PaintPixels::recipe_materializations()!=before)throw std::runtime_error("Filled-path recording materialized a CPU page");
  for(int tile:{1001,1002,1011}){for(int c=0;c<5;++c)check(authored.primary.paint,reference.primary.paint,c,tile);
   check(authored.primary.ao,reference.primary.ao,2,tile);check(authored.users[0],reference.users[0],2,tile);check(authored.displacement,reference.displacement,2,tile);
  }
 }
 std::printf("BTS_GPU_FILLED_PATH_VERIFIED cases=%d tiles=3 mask_resampling=1 signed_hdr=1 cpu_materializations=0 max_error=%.9g\n",cases,maximum_error);
 return cases;
}
}
