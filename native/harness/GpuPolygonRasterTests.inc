// SPDX-License-Identifier: GPL-3.0-or-later
// Included only by the explicit owner-context GPU self-test. Production bucket
// rendering never reads pixel data back or invokes this CPU oracle.
{
 GpuPolygonRaster raster;if(!raster.prepare())throw std::runtime_error("Polygon pipelines unavailable");
 constexpr int size=128;auto image=texture("BTS polygon raster oracle",size,size,gpu::TextureFormat::SFLOAT_32_32_32_32);
 const std::array<float,4> before{.2f,.1f,.05f,.5f};
 const std::array<std::uint32_t,4> all_pages{0,1,65536,65537};
 const std::array<std::uint32_t,1> one_page{0};
 double maximum=0;int polygon_cases=0;
 for(int shape=0;shape<3;++shape){
  auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center={0,0,0};surface->radius=1;PaintPixels::SurfaceTriangle triangle{};
  if(shape==0)triangle.uv={{{.125,.125},{.875,.125},{.125,.875}}};
  if(shape==1)triangle.uv={{{-1,.125},{.875,.125},{.875,1.25}}};
  if(shape==2)triangle.uv={{{4.5/128,4.5/128},{120.5/128,4.5/128},{4.5/128,120.5/128}}};
  surface->triangles.push_back(triangle);auto degenerate=triangle;degenerate.uv[1]=degenerate.uv[0];surface->triangles.push_back(degenerate);
  PaintPixels::Stamp stamp{};stamp.surface=surface;stamp.alpha.kind=AlphaKind::Uniform;stamp.alpha.uniform=1;stamp.values.base_color={2,.3f,-.2f,1};stamp.values.metallic=.7;stamp.values.roughness=.23;stamp.values.bump=-1.3;stamp.channels.fill(true);
  PaintPixels reference;reference.stamp(size,stamp);
  for(int channel=0;channel<5;++channel)for(float opacity:{1.f,.37f})for(bool partial:{false,true}){
   GPU_texture_clear(image.get(),GPU_DATA_FLOAT,before.data());const std::span<const std::uint32_t> pages=partial?std::span<const std::uint32_t>(one_page):std::span<const std::uint32_t>(all_pages);
   auto undo=raster.capture(image.get(),pages);raster.paint(image.get(),stamp,channel,size,pages,opacity);
   auto *data=static_cast<float *>(GPU_texture_read(image.get(),GPU_DATA_FLOAT,0));
   for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto ink=reference.sample(channel,x,y);for(int component=0;component<4;++component){const float expected=ink[3]>0&&(!partial||(x<64&&y<64))?ink[component]*opacity+before[component]*(1-opacity):before[component];const auto delta=std::abs(double(data[(y*size+x)*4+component])-expected);maximum=std::max(maximum,delta);if(delta>2e-5){MEM_delete(data);throw std::runtime_error("GPU polygon boundary/value differs from CPU oracle");}}}MEM_delete(data);
   raster.restore(image.get(),undo);data=static_cast<float *>(GPU_texture_read(image.get(),GPU_DATA_FLOAT,0));for(int i=0;i<size*size*4;++i)if(data[i]!=before[i%4]){MEM_delete(data);throw std::runtime_error("GPU polygon dirty Undo changed source");}MEM_delete(data);++polygon_cases;
  }
 }
 cases+=polygon_cases;std::printf("BTS_GPU_POLYGON_VERIFIED cases=%d channels=5 opacity=2 dirty_pages=1 clipped_boundaries=1 undo_exact=1 max_error=%.9g production_readback=0\n",polygon_cases,maximum);
}
