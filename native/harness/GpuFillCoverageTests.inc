// SPDX-License-Identifier: GPL-3.0-or-later
// Actual common Fill producer and the same bounded capture used by Flatten.
// Explicit diagnostic CPU source-sampler parity plus independent constant
// signed/HDR expectation; no CPU composition or interactive fallback. The
// sequential Float32 stencil has the established relative 1e-6 CPU/GPU bound;
// ideal constant comparison uses the existing BJ 2e-6 absolute bound.
{
 constexpr int side=64;int coverage_cases=0;std::uint64_t coverage_texels=0;double coverage_error=0,coverage_absolute_error=0,ideal_error=0;
 for(int mode:{0,1})for(float alpha:{1.f,.37f}){
  TextureSet model;model.texture_resolution=side;model.precision[0]=ChannelPrecision::RGB32F;model.precision[4]=ChannelPrecision::L32F;
  auto &fill=model.layers.front();fill.kind=LayerKind::Fill;fill.values.base_color={1,1,1,1};fill.fill.mode=mode;fill.fill.hardness=.5;fill.fill.linked=fill.fill.linked3=false;fill.fill.scale={1.37,.81};fill.fill.scale3={1.26,.73,1.11};fill.fill.rotation3={17,-11,46};fill.fill.offset3={.226,.13379,0};
  auto image=std::make_shared<FillTexture>();image->width=image->height=2;image->raw=true;image->input_space="Non-Color/raw";image->hash="fill-coverage-"+std::to_string(mode)+"-"+std::to_string(alpha);image->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{-.75f,2.5f,.37f,alpha});fill.fill.texture=image;fill_channel_texture(fill.fill,4)=image;fill.fill.alpha_mask[0]=fill.fill.alpha_mask[4]=true;
  auto atlas=std::make_shared<FillSurfaceAtlas>();atlas->resolution=atlas->height=side;atlas->extent=2.3;auto &points=atlas->tiles[1001];points.resize(side*side);
  for(int y=0;y<side;++y)for(int x=0;x<side;++x){auto &point=points[y*side+x];point.position={float(x)/side-.5f,float(y)/side-.5f,float(x+y)/(2*side)-.5f};point.normal={.13f+float(x)/side,.29f+float(y)/side,.47f+float(x+y)/(2*side)};point.footprint=.019f;point.valid=true;}model.projection_geometry=atlas;
  GpuLayerPreview coverage_preview;auto output=texture("BTS regular Fill strict coverage",side,side,gpu::TextureFormat::SFLOAT_32_32_32_32);
  for(int channel:{0,4}){
   coverage_preview.begin_frame();if(coverage_preview.update(model,channel,output.get())!=GpuPreviewUpdate::Updated)throw std::runtime_error("Regular Fill coverage update deferred");
   auto *pixels=static_cast<float *>(GPU_texture_read(output.get(),GPU_DATA_FLOAT,0));if(!pixels)throw std::runtime_error("Regular Fill coverage update read unavailable");bool valid=true;
   for(int i=0;i<side*side;++i){for(int c=0;c<4;++c)valid&=std::isfinite(pixels[i*4+c]);valid&=pixels[i*4+3]>=0&&pixels[i*4+3]<=1;}MEM_delete(pixels);if(!valid)throw std::runtime_error("Regular Fill update coverage outside strict [0,1]");
   LayerCaptureRequest request;request.layer=fill.id;request.channel=channel;request.width=request.height=request.canvas_width=request.canvas_height=side;const auto ticket=std::uint64_t(++coverage_cases);
   if(coverage_preview.capture_layer(model,request,ticket)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Regular Fill coverage capture deferred");glFinish();coverage_preview.impl_->layer_capture.poll();const auto packet=coverage_preview.layer_capture_result();
   if(!packet||packet->ticket!=ticket||!packet->valid||!packet->pixels||packet->pixels->size()!=side*side)throw std::runtime_error("Regular Fill coverage capture packet unavailable");
   for(const auto &pixel:*packet->pixels){if(!std::isfinite(pixel[3])||pixel[3]<0||pixel[3]>1)throw std::runtime_error("Regular Fill captured coverage outside strict [0,1]");const auto i=&pixel-packet->pixels->data();const auto cpu=TextureDocument::fill_sample(model,fill,channel,int(i%side),int(i/side));const float a=std::clamp(cpu[3],0.f,1.f);const std::array<float,4> expected=channel==0?std::array<float,4>{cpu[0],cpu[1],cpu[2],a}:std::array<float,4>{cpu[0],cpu[0],cpu[0],a};const std::array<float,4> ideal=channel==0?std::array<float,4>{-.75f,2.5f,.37f,alpha}:std::array<float,4>{-.75f,-.75f,-.75f,alpha};for(int c=0;c<4;++c){const auto error=std::abs(double(pixel[c])-expected[c]),scaled=error/std::max(1.,std::abs(double(expected[c]))),constant_error=std::abs(double(pixel[c])-ideal[c]);coverage_error=std::max(coverage_error,scaled);coverage_absolute_error=std::max(coverage_absolute_error,error);ideal_error=std::max(ideal_error,constant_error);if(!std::isfinite(pixel[c])||scaled>1e-6||constant_error>2e-6){std::fprintf(stderr,"BTS_GPU_FILL_COVERAGE_DIFFERENCE mode=%d source_alpha=%.12g channel=%d x=%lld y=%lld component=%d actual=%.12g expected=%.12g error=%.12g rgba=%.12g,%.12g,%.12g,%.12g\n",mode,alpha,channel,(long long)(i%side),(long long)(i/side),c,pixel[c],expected[c],error,pixel[0],pixel[1],pixel[2],pixel[3]);throw std::runtime_error("Regular Fill captured signed/HDR/partial-alpha value changed");}}++coverage_texels;}
  }
 }
 std::printf("BTS_GPU_FILL_COVERAGE_VERIFIED cases=%d captured_texels=%llu strict_alpha=1 signed_hdr=1 partial_alpha=1 update_capture=1 max_error=%.12g bound=1e-6 cpu_bound_policy=relative_max1 absolute_cpu_error=%.12g ideal_max_error=%.12g ideal_bound=2e-6\n",coverage_cases,(unsigned long long)coverage_texels,coverage_error,coverage_absolute_error,ideal_error);
}
