// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuBrushRaster.hh"
#include "GpuPolygonRaster.hh"
#include "GpuTextureProbe.hh"
#include "GpuHistogram.hh"
#include "GpuThumbnail.hh"
#include "GpuLayerCapture.hh"
#include "GpuMaterialSample.hh"
#include "GpuRangeSummary.hh"
#include "GpuMarginExecutor.hh"
#include "GPU_capabilities.hh"
#include "MEM_guardedalloc.h"
#include <QElapsedTimer>
#include <QThread>
#include <numeric>
#include <cmath>
namespace bts {
inline void metal_require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class Predicate> void metal_wait(Predicate complete){
 QElapsedTimer timer;timer.start();
 while(!complete()){
  if(timer.elapsed()>10000)throw std::runtime_error("Metal asynchronous result timed out");
  QThread::msleep(1);
 }
}
inline void verify_metal_metadata(){
 using namespace blender;
 using Image=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
 const float color[4]={.125f,.25f,.375f,.5f};
 Image source(GPU_texture_create_2d("Metal metadata fixture",32,32,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 Image owners(GPU_texture_create_2d("Metal owner fixture",32,32,1,gpu::TextureFormat::UINT_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 Image selection(GPU_texture_create_2d("Metal selection fixture",1,1,1,gpu::TextureFormat::UINT_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 GPU_texture_clear(source.get(),GPU_DATA_FLOAT,color);
 const unsigned owner[4]={1,1,0,0};GPU_texture_clear(owners.get(),GPU_DATA_UINT,owner);GPU_texture_update(selection.get(),GPU_DATA_UINT,owner);
 {
  GpuThumbnail thumbnail;thumbnail.update({},{{source.get(),32,32}},17);
  bool rejected=false;try{thumbnail.update({},{{source.get(),32,32}},18);}catch(const std::logic_error &){rejected=true;}
  metal_require(rejected,"Thumbnail accepted overwrite while in flight");
  metal_wait([&]{thumbnail.poll();return !thumbnail.pending();});
  auto result=thumbnail.get();metal_require(result&&result->ticket==17,"Thumbnail ticket mismatch");
  for(auto pixel:result->pixels)for(int c=0;c<4;++c)metal_require(pixel[c]==color[c],"Thumbnail value mismatch");
 }
 {
  GpuLayerCapture capture;LayerCaptureRequest request;request.width=7;request.height=9;request.canvas_width=request.canvas_height=32;request.x=3;request.y=5;request.contextual=true;
  capture.update(request,{source.get(),owners.get(),selection.get(),32,32,2},23);
  metal_wait([&]{capture.poll();return !capture.pending();});auto result=capture.get();
  metal_require(result&&result->ticket==23&&result->pixels->size()==63,"Layer capture dimensions/ticket mismatch");
  for(auto pixel:*result->pixels)for(int c=0;c<4;++c)metal_require(pixel[c]==(c<3?color[c]*2:color[c]),"Unpremultiplied layer capture mismatch");
 }
 {
  GpuMaterialSample sample;std::array<GpuMaterialSample::Input,6> inputs{};
  for(auto &input:inputs)input={source.get(),{.25f,.25f,.25f}};
  sample.update({},inputs,31);metal_wait([&]{sample.poll();return !sample.pending();});auto result=sample.get();
  metal_require(result&&result->ticket==31&&result->material.base_color[0]==.25f&&result->material.roughness==.25f,"Material pick mismatch");
 }
 {
  GpuHistogram histogram;GpuHistogram::Input input{source.get(),source.get(),source.get()};input.width=input.height=32;input.compare=false;input.component=1;input.owners=owners.get();input.selection=selection.get();input.inclusion=2;
  histogram.update("fixture",input,41);
  // Revision coalescing must discard the earlier ticket without stalling.
  histogram.update("fixture",input,42);
  metal_wait([&]{histogram.poll();return !histogram.pending();});auto result=histogram.get("fixture");
  metal_require(result&&result->ticket==42,"Histogram coalesced ticket mismatch");
  metal_require(std::accumulate(result->bins[0].begin(),result->bins[0].end(),0u)==256,"Histogram sample count mismatch");
  metal_require(result->minimum==color[0]&&result->maximum==color[0],"Histogram range mismatch");
 }
 {
  GpuRangeSummary range;const GpuRangeSummary::Source input{1001,32,32,source.get(),1,{}};
  range.update(7,std::span(&input,1),9);std::vector<GpuRangeSummary::Result> results;
  metal_wait([&]{auto batch=range.poll();results.insert(results.end(),batch.begin(),batch.end());return !range.pending();});
  metal_require(results.size()==1&&results[0].present&&results[0].minimum==.25f&&results[0].maximum==.25f&&results[0].epoch==9,"Range reduction mismatch");
 }
 // Read the old submitted allocation after update() replaces the live one.
 {
  const std::array<unsigned,4> original{1,2,3,4},replacement{5,6,7,8};
  auto *buffer=GPU_storagebuf_create_ex(16,original.data(),GPU_USAGE_DYNAMIC,"Metal revision lifetime");
  GPU_storagebuf_bind(buffer,0);GPU_storagebuf_unbind(buffer);GpuReadbackFence fence;fence.submit(buffer);
  GPU_storagebuf_update(buffer,replacement.data());std::array<unsigned,4> values{};
  metal_wait([&]{return fence.read(buffer,values.data());});metal_require(values==original,"Readback lost submitted allocation");
  GPU_storagebuf_free(buffer);
 }
 std::puts("MPAINTER_METAL_METADATA_PASS thumbnail=1 capture=1 pick=1 histogram=1 range=1 revision_lifetime=1");
}
inline void verify_metal_brush(){
 using namespace blender;using Image=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
 constexpr int size=128;const float empty[4]={};int cases=0;float maximum_error=0,allowed_error=2e-5;
 const auto make=[](const char *name){return Image(GPU_texture_create_2d(name,size,size,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);};
 auto ink_image=make("BTS test GPU ink"),before_image=make("BTS test GPU before"),result_image=make("BTS test GPU stroke result");
 const auto check=[&](gpu::Texture *image,const PaintPixels &pixels,int channel){
  auto *values=static_cast<float *>(GPU_texture_read(image,GPU_DATA_FLOAT,0));float error=0;
  for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto expected=pixels.sample(channel,x,y);for(int c=0;c<4;++c)error=std::max(error,std::abs(values[(y*size+x)*4+c]-expected[c]));}
  MEM_delete(values);maximum_error=std::max(maximum_error,error);
  if(error>allowed_error)throw std::runtime_error("GPU brush raster differs from CPU: "+std::to_string(error));++cases;
 };
 GpuBrushRaster gpu;
 for(int variant=0;variant<20;++variant){
  PaintPixels::Stamp stamp{{.517,.473},{.312,.071},{-.093,.257}};
  stamp.values.base_color={.8,.2,.45,1};stamp.values.metallic=.7;stamp.values.roughness=.19;stamp.values.normal={.2,-.3,.9};stamp.values.bump=-.75;
  stamp.alpha.kind=AlphaKind(variant%5);stamp.alpha.uniform=variant==2?-.2f:variant==7?1.7f:.37f;stamp.alpha.inverted=variant%2;stamp.hardness=variant%4==0?1:.37;stamp.flow=.13+.04*variant;
  stamp.alpha.dynamic.phase=double(variant)/20;stamp.alpha.dynamic.roundness=.37;stamp.alpha.dynamic.width=.16;
  if(stamp.alpha.kind==AlphaKind::Bitmap){auto bitmap=std::make_shared<AlphaBitmap>();bitmap->width=13;bitmap->height=7;for(int y=0;y<7;++y)for(int x=0;x<13;++x)bitmap->pixels.push_back(float((x*3+y*7)%17)/16);stamp.alpha.bitmap=bitmap;}
  allowed_error=stamp.alpha.kind==AlphaKind::DynamicOutline?5e-5f:2e-5f; // Sharp procedural edge: float GPU coordinates vs double CPU reference.
  if(variant>=10){
   auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center={.517,.473,0};surface->radius=.31;surface->projected=variant%2;surface->unwrapped=variant%3==0;surface->planar_fade=variant%3==1;surface->cull=true;
   const std::array<std::array<double,2>,4> uv{{{.07,.09},{.93,.09},{.93,.91},{.07,.91}}};
   for(const auto indices:{std::array{0,1,2},std::array{0,2,3}}){PaintPixels::SurfaceTriangle t;t.seam_edges={true,true,true};for(int v=0;v<3;++v){const auto point=uv[indices[v]];t.uv[v]=point;t.position[v]={point[0],point[1],.1*(point[0]-.5)};t.paint_coordinates[v]={point[0]-.517,point[1]-.473};}surface->triangles.push_back(t);}stamp.surface=surface;
  }
  for(int channel=0;channel<5;++channel){
   GPU_texture_clear(ink_image.get(),GPU_DATA_FLOAT,empty);PaintPixels ink;
   for(int dab=0;dab<3;++dab){auto sample=stamp;sample.flow*=float(dab+1)/3;ink.stamp(size,sample);if(!gpu.stamp(ink_image.get(),channel,sample))throw std::runtime_error("GPU stamp unsupported");}
   check(ink_image.get(),ink,channel);
   std::array<PaintPixels::Stamp,3> batch;std::array<const PaintPixels::Stamp *,3> commands;
   for(int dab=0;dab<3;++dab){batch[dab]=stamp;batch[dab].flow*=float(dab+1)/3;commands[dab]=&batch[dab];}
   GPU_texture_clear(ink_image.get(),GPU_DATA_FLOAT,empty);
   if(!gpu.stamps(ink_image.get(),channel,commands))throw std::runtime_error("GPU batch stamp unsupported");check(ink_image.get(),ink,channel);
   PaintPixels before;auto base=stamp;base.surface.reset();base.flow=.47;base.values.base_color={.1,.9,.3,1};base.values.bump=.5;before.stamp(size,base);
   std::vector<float> baseline(size*size*4);for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto p=before.sample(channel,x,y);std::copy(p.begin(),p.end(),baseline.begin()+(y*size+x)*4);}
   GPU_texture_update(before_image.get(),GPU_DATA_FLOAT,baseline.data());
   for(bool erase:{false,true}){
    GPU_texture_update(result_image.get(),GPU_DATA_FLOAT,baseline.data());auto result=before;const auto bounds=GpuBrushRaster::bounds(size,stamp);
    result.over_stroke(before,ink,.61,bounds,erase,true);
    if(!gpu.over(result_image.get(),before_image.get(),ink_image.get(),.61,bounds,erase,true))throw std::runtime_error("GPU whole-stroke opacity failed");
    check(result_image.get(),result,channel);
   }
  }
 }
 std::printf("BTS_GPU_BRUSH_RASTER_VERIFIED cases=%d channels=5 uv_surface=1 alpha_shapes=5 flow=1 whole_stroke_opacity_erase=1 max_error=%.9g production_readback=0\n",cases,maximum_error);
 if(gpu.metadata_allocations()>5)throw std::runtime_error("Brush metadata reallocated between batch and stroke-opacity commands");
 std::printf("BTS_GPU_BRUSH_BUFFERS_VERIFIED cases=400 allocations=%llu growing_capacity=1\n",(unsigned long long)gpu.metadata_allocations());

}
}

#include "GpuProjectReadback.hh"
namespace bts {
inline void verify_metal_project_readback(){
 using namespace blender;using Image=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
 constexpr int width=1024,height=521,layers=2;
 std::vector<std::array<float,4>> values(width*height*layers);
 for(int z=0;z<layers;++z)for(int y=0;y<height;++y)for(int x=0;x<width;++x)
  values[(z*height+y)*width+x]={float(x)/width,float(y)/height,float(z),.5f};
 Image source(GPU_texture_create_2d_array("Metal save array",width,height,layers,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,values.front().data()),GPU_texture_free);
 metal_require(bool(source),"Save fixture allocation failed");
 {
  GpuProjectReadback readback;readback.submit(1,1002,source.get(),17,11,1,997,509);
  readback.submit_native(2,1001,source.get(),0,0,0,width,height);
  auto *live=source.release();metal_require(GpuProjectReadback::protect_for_write(&live),"Native save did not protect source");source.reset(live);
  const float changed[4]={9,8,7,6};GPU_texture_clear(source.get(),GPU_DATA_FLOAT,changed);
  metal_wait([&]{return readback.poll();});
  for(const auto &image:readback.images()){
   const int z=image.uid==1?1:0,ox=image.uid==1?17:0,oy=image.uid==1?11:0;
   metal_require(image.completed_rows==image.height&&!image.uniform,"Save completion mismatch");
   for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)for(int c=0;c<4;++c)
    metal_require(image.pixels[(y*image.width+x)*4+c]==values[(z*height+y+oy)*width+x+ox][c],"Frozen project snapshot mismatch");
  }
  metal_require(!GpuProjectReadback::protect_for_write(&live),"Completed save kept a native pin");
  readback.release_cpu();for(const auto &image:readback.images())metal_require(!image.pixels,"CPU release failed");
 }
 // Destroy a pending transfer and reuse the adapter without a synchronous wait.
 {
  GpuProjectReadback canceled;canceled.submit(3,1001,source.get(),0,0,0,width,height);canceled.poll();canceled.clear();
  canceled.submit(4,1001,source.get(),0,0,1,13,7);metal_wait([&]{return canceled.poll();});
  metal_require(canceled.images()[0].uniform&&canceled.images()[0].pixels[0]==9,"Canceled uniform readback reuse failed");
 }
 // The ordinary 2D/half-float path is distinct from the array snapshot kernel.
 {
  const float value[4]={.125f,.25f,.5f,1};
  Image image(GPU_texture_create_2d("Metal half float save",32,16,1,gpu::TextureFormat::SFLOAT_16_16_16_16,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
  GPU_texture_clear(image.get(),GPU_DATA_FLOAT,value);GpuProjectReadback readback;
  bool rejected=false;try{readback.submit(5,1001,image.get(),31,0,0,2,1);}catch(const std::invalid_argument &){rejected=true;}
  metal_require(rejected&&readback.images().empty(),"Invalid save region was accepted");
  readback.submit(5,1001,image.get(),3,4,0,17,9);metal_wait([&]{return readback.poll();});
  metal_require(readback.images()[0].uniform,"Half float uniform capture was not compacted");
  for(int i=0;i<4;++i)metal_require(readback.images()[0].pixels[i]==value[i],"2D half float save mismatch");
 }
 GpuProjectReadback::free_program();
 std::puts("MPAINTER_METAL_PROJECT_READBACK_PASS array=1 half_float_2d=1 crop=1 multi_packet=1 frozen_revision=1 native_cow=1 cancel=1");
}
}

namespace bts {
inline void verify_metal_margin_tables(){
 using namespace blender;
 using Image=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
 const int max_size=GPU_max_texture_size(),max_layers=GPU_max_texture_layers();
 metal_require(max_size>=1024&&max_layers>=1,"GPU 2D texture limits are unavailable");
 const auto small_shape=GpuMarginExecutor::packed_table_shape(2000,max_size,max_layers);
 metal_require(small_shape.valid&&small_shape.width==1024&&small_shape.height==2&&small_shape.layers==1,
               "Small margin table packing changed");
 const auto split_shape=GpuMarginExecutor::packed_table_shape(std::size_t(1024)*std::size_t(max_size)+1,max_size,max_layers);
 metal_require(split_shape.valid&&split_shape.width==1024&&split_shape.height==max_size&&split_shape.layers==2,
               "Oversized margin table was not split across 2D array layers");
 const auto refused=GpuMarginExecutor::packed_table_shape(std::size_t(1024)*std::size_t(max_size)*2+1,max_size,1);
 metal_require(!refused.valid,"Margin table larger than max 2D array layers was not refused");
 std::uint32_t small_first=0,small_last=0;int small_w=0,small_h=0,small_z=0;
 metal_require(GpuMarginExecutor::debug_upload_uint_table(2000,&small_first,&small_last,&small_w,&small_h,&small_z),
               "Small margin table upload failed");
 metal_require(small_w==1024&&small_h==2&&small_z==1&&small_w<=max_size&&small_h<=max_size,
               "Small margin table exceeded device 2D limits");
 const std::size_t large_count=std::size_t(1024)*std::size_t(max_size+8);
 std::uint32_t large_first=0,large_last=0;int large_w=0,large_h=0,large_z=0;
 metal_require(GpuMarginExecutor::debug_upload_uint_table(large_count,&large_first,&large_last,&large_w,&large_h,&large_z),
               "Oversized margin table upload aborted or failed");
 metal_require(large_w==1024&&large_h==max_size&&large_z==2&&large_w<=max_size&&large_h<=max_size&&large_z<=max_layers,
               "Oversized margin table was not packed within Metal 2D limits");
 metal_require(large_first==1u&&large_last==std::uint32_t((large_count-1)*2654435761u+1u),
               "Oversized margin table readback mismatch");
 std::vector<unsigned char> coverage(32*32,1);for(int x=0;x<32;++x)coverage[x]=0;
 const auto plan=std::make_shared<const GpuMarginPlan>(GpuMarginPlan::compile(32,32,coverage));
 metal_require(!plan->operations.empty(),"Small margin plan compiled no operations");
 Image image(GPU_texture_create_2d("Metal margin small target",32,32,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 metal_require(bool(image),"Small margin target allocation failed");
 std::vector<float> pixels(32*32*4);for(std::size_t i=0;i<pixels.size();++i)pixels[i]=float((i*13)%971)/400.f-.7f;
 GPU_texture_update(image.get(),GPU_DATA_FLOAT,pixels.data());
 GpuMarginExecutor executor;std::vector<std::uint32_t> dirty(1,1);
 metal_require(executor.apply("metal-small",plan,image.get(),{0,0,0},dirty,true),"Small GPU margin dispatch failed");
 auto expected=pixels;plan->evaluate_reference(expected);
 auto *actual=static_cast<float *>(GPU_texture_read(image.get(),GPU_DATA_FLOAT,0));
 metal_require(actual,"Small GPU margin readback failed");
 double error=0;for(std::size_t i=0;i<expected.size();++i)error=std::max(error,double(std::abs(actual[i]-expected[i])));
 MEM_delete(actual);
 metal_require(error<=1e-5,"Small GPU margin results changed");
 std::printf("MPAINTER_METAL_MARGIN_TABLE_PASS max_2d=%d max_layers=%d small=%dx%dx%d large=%dx%dx%d large_count=%zu max_error=%.9g\n",
             max_size,max_layers,small_w,small_h,small_z,large_w,large_h,large_z,large_count,error);
}
inline void verify_metal_polygon(){
 using namespace blender;
 using Image=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
 const auto texture=[](const char *name,int width,int height,gpu::TextureFormat format){
  return Image(GPU_texture_create_2d(name,width,height,1,format,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 };
 int cases=0;
#include "GpuPolygonRasterTests.inc"
 metal_require(cases==60,"Polygon case count mismatch");
}
}
