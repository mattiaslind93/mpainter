#pragma once
#include "GpuPaintPages.hh"
#include "GpuFilledPathTest.hh"
#include "GpuRibbonPathTest.hh"
#include "SurfacePathRaster.hh"
#include "PolygonFill.hh"
#include "MEM_guardedalloc.h"

namespace bts {
inline int verify_gpu_path_raster(GpuPaintPages *prepared=nullptr)
{
  using namespace blender;
  constexpr int size=128;
  using Image=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
  Image image(GPU_texture_create_2d("BTS GPU path test",size,size,1,
      gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
  GpuPaintPages owned;auto &pages=prepared?*prepared:owned;
  int cases=0;
  float maximum_error=0,allowed_error=2e-5f;
  const auto check=[&](const TilePaintPixels &authored,const TilePaintPixels &reference,int channel){
    const auto materializations=PaintPixels::recipe_materializations();
    const float clear[4]={};
    GPU_texture_clear(image.get(),GPU_DATA_FLOAT,clear);
    if(const auto tile=authored.tiles().find(1001);tile!=authored.tiles().end()){
      const auto versions=tile->second.page_versions();
      for(const auto &[key,version]:versions[channel])
        if(!pages.write(image.get(),tile->second,channel,key))
          throw std::runtime_error("GPU path page unexpectedly deferred");
    }
    if(PaintPixels::recipe_materializations()!=materializations)
      throw std::runtime_error("GPU path materialized CPU source");
    auto *pixels=static_cast<float *>(GPU_texture_read(image.get(),GPU_DATA_FLOAT,0));
    float error=0;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
      const auto expected=reference.sample(channel,x,y);
      for(int c=0;c<4;++c){if(!std::isfinite(pixels[4*(y*size+x)+c])||!std::isfinite(expected[c])){MEM_delete(pixels);throw std::runtime_error("Nonfinite GPU path oracle");}error=std::max(error,std::abs(pixels[4*(y*size+x)+c]-expected[c]));}
    }
    MEM_delete(pixels);maximum_error=std::max(maximum_error,error);
    if(error>allowed_error)throw std::runtime_error("GPU path differs from CPU at case "+
      std::to_string(cases)+" channel "+std::to_string(channel)+": "+std::to_string(error));
    ++cases;
  };
  const SurfacePathContact contact=[](const SurfacePathPoint &point,float diameter,bool,const BrushProjection &)
      ->std::optional<SurfaceBrushSample>{
    const auto uv=std::array{(point.position[0]+1)*.5,(point.position[1]+1)*.5};
    const double radius=diameter/200;
    return SurfaceBrushSample{PaintPixels::Stamp{uv,{radius,0},{0,radius}},
      {float(point.position[0]),float(point.position[1]),0},float(diameter/100),1};
  };
  SurfacePath path;path.id="c0a80001-1234-4000-8000-000000000001";
  path.points={{{-.6,0,0},{0,0,1},{.2,.5},.3f},
               {{0,.25,0},{0,0,1},{.5,.625},.7f},
               {{.6,0,0},{0,0,1},{.8,.5},1}};
  path.brush.channels.fill(true);path.brush.values.base_color={.1,.6,.9,1};
  path.brush.values.roughness=.8;path.brush.values.bump=-.4;
  path.brush.diameter=18;path.brush.spacing=30;path.brush.ao=true;
  path.brush.values.ambient_occlusion=.4;path.brush.users[0]=true;path.brush.user_values[0]=.7;
  path.brush.displacement=true;path.brush.displacement_value=-.35;
  std::array<int,8> user_sizes;user_sizes.fill(size);
  for(int variant=0;variant<4;++variant){
    path.points[1].smooth=variant>=1;
    path.brush.alpha.kind=variant>=2?AlphaKind::DynamicOutline:AlphaKind::Shape;
    // Several overlapping sharp procedural outlines amplify the FP32 edge
    // difference measured by the single-dab test (5e-5). Keep the accumulated
    // path below 1e-4 in float32, well below one 8-bit channel step.
    allowed_error=variant>=2?1e-4f:2e-5f;
    path.brush.alpha.dynamic.cycle=4;
    path.symmetry.enabled=variant==3;path.symmetry.axis=1;path.symmetry.offset=.2;
    const auto reference=raster_surface_path(path,2,size,user_sizes,{1001},contact);
    SurfacePathInk authored;
    {PaintRecordingScope record;authored=raster_surface_path(path,2,size,user_sizes,{1001},contact);}
    for(int c=0;c<5;++c)check(authored.primary.paint,reference.primary.paint,c);
    check(authored.primary.ao,reference.primary.ao,2);
    check(authored.users[0],reference.users[0],2);
    check(authored.displacement,reference.displacement,2);
  }
  SceneSnapshot scene;
  for(const auto uv:{std::array<std::array<float,2>,3>{{{.1,.1},{.9,.1},{.9,.9}}},
                     std::array<std::array<float,2>,3>{{{.1,.1},{.9,.9},{.1,.9}}}}){
    UvFaceTriangle triangle;triangle.corners=uv;scene.uv_triangles.push_back(triangle);
  }
  TilePaintPixels authored,reference;
  allowed_error=1e-6f; // Full-coverage polygon fills have no procedural edge.
  const std::array<std::size_t,2> both{0,1};
  for(int step=0;step<3;++step){
    std::span<const std::size_t> selection=both;
    if(step==0)selection=selection.first(1);
    const auto stamp=polygon_fill_stamp(scene,selection,step==2?0:1);
    reference.stamp(size,stamp);
    {PaintRecordingScope record;authored.stamp(size,stamp);}
    check(authored,reference,2);
  }
  std::printf("BTS_GPU_PATH_RASTER_VERIFIED cases=%d polygon_black_overwrite=1 curved_pressure_symmetry=1 ao_user_signed_displacement=1 cpu_materializations=0 max_error=%.9g\n",cases,maximum_error);
  return cases+verify_gpu_filled_path(prepared)+verify_gpu_ribbon_path(prepared);
}
}
