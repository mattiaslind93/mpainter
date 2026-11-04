#include "Theme.hh"
#include <epoxy/gl.h>
#include "SmartMaskThumbnailJobs.hh"
#include "SmartMaskThumbnailPacket.hh"
#include "BlenderHost.hh"
#include "GpuLayerPreview.hh"
#include "GpuThumbnail.hh"
#include "MaterialSpherePreview.hh"
#include "Performance.hh"
#include "gpu_shader_create_info.hh"
#include <QTimer>
#include <deque>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace bts {namespace {
constexpr int side=64;
struct Profile {std::shared_ptr<const FillSurfaceAtlas> geometry;MeshMaps maps;};
Profile sphere_profile(){
 auto uv=detail::shaderball_uv_surface(side);Profile profile;
 profile.maps=std::move(uv.maps);profile.geometry=std::make_shared<const FillSurfaceAtlas>(std::move(uv.atlas));return profile;
}

TextureSet sphere_set(const StudioAsset &asset,const Profile &profile){
 if(!asset.smart_mask)throw std::invalid_argument("Smart-mask thumbnail source is not hydrated");
 TextureSet set;set.id=QUuid("{07b36d85-5cdb-41e6-b1bf-bd7a4b30d8ec}");set.name="Own smart-mask sphere";set.texture_resolution=side;set.padding_3d=false;
 set.channels={true,false,false,false,false};set.precision[0]=ChannelPrecision::RGB32F;
 set.base_values.base_color={0,0,0,1};set.mesh_maps=profile.maps;set.projection_geometry=profile.geometry;
 auto &layer=set.layers.front();layer.kind=LayerKind::Fill;layer.name=asset.name.toStdString();
 layer.channels={true,false,false,false,false};layer.values.base_color={1,1,1,1};layer.mask=*asset.smart_mask;
 return set;
}
QImage display_packet(const ThumbnailMetadata &packet){
 constexpr int out_side=84;
 constexpr int pkt_side=ThumbnailMetadata::side;
 QImage image(out_side,out_side,QImage::Format_RGB32);
 // Fixed reference raster background, part of the thumbnail pixels, not UI chrome.
 image.fill(qRgb(41,41,41));
 if(!packet.valid||packet.pixels.size()<std::size_t(pkt_side*pkt_side))return image;
 const auto &gb=detail::shaderball_jl01_gbuffer();
 const auto encode=[](double v){return v<=.0031308?12.92*v:1.055*std::pow(v,1.0/2.4)-.055;};
 const auto sample_mask=[&](double u,double v)->double{
  const double fx=std::clamp(u*double(pkt_side)-0.5,0.0,double(pkt_side-1));
  const double fy=std::clamp(v*double(pkt_side)-0.5,0.0,double(pkt_side-1));
  const int x_base=int(std::floor(fx)),y_base=int(std::floor(fy));
  const int x_next=std::min(x_base+1,pkt_side-1),y_next=std::min(y_base+1,pkt_side-1);
  const double tx=fx-double(x_base),ty=fy-double(y_base);
  double wsum=0.0,msum=0.0;
  const int xs[2]={x_base,x_next},ys[2]={y_base,y_next};
  const double wx[2]={1.0-tx,tx},wy[2]={1.0-ty,ty};
  for(int iy=0;iy<2;++iy)for(int ix=0;ix<2;++ix){
   const auto &px=packet.pixels[std::size_t(ys[iy]*pkt_side+xs[ix])];
   if(px[3]<0.0f)continue;
   const double w=wx[ix]*wy[iy];
   wsum+=w;msum+=w*std::clamp(double(px[3]),0.0,1.0);
  }
  if(wsum>1e-6)return msum/wsum;
  const int cx=std::clamp(int(u*pkt_side),0,pkt_side-1);
  const int cy=std::clamp(int(v*pkt_side),0,pkt_side-1);
  for(int r=1;r<=3;++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){
   const int nx=std::clamp(cx+dx,0,pkt_side-1),ny=std::clamp(cy+dy,0,pkt_side-1);
   const auto &px=packet.pixels[std::size_t(ny*pkt_side+nx)];
   if(px[3]>=0.0f)return std::clamp(double(px[3]),0.0,1.0);
  }
  return 0.0;
 };
 for(int y=0;y<out_side;++y)for(int x=0;x<out_side;++x){
  const double u=(double(x)+0.5)/double(out_side),v=(double(y)+0.5)/double(out_side);
  const auto sp=gb.sample(u,v);
  if(sp.cov<=.005)continue;
  const double m=sample_mask(u,v);
  const double shading=.76+.24*std::max(0.0,sp.nx*.3+sp.ny*.6+sp.nz*.74162);
  const double linear=(.003+.897*m)*shading;
  const int c=qRound(255.0*std::clamp(encode(linear)*sp.cov+encode(.022)*(1.0-sp.cov),0.0,1.0));
  image.setPixel(x,y,qRgb(c,c,c));
 }
 return image;
}
}
struct SmartMaskThumbnailJobs::Impl {
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 struct Request {StudioAsset asset;std::vector<std::function<void(QImage)>> complete;performance::Clock::time_point queued=performance::Clock::now();};
 static bool same_source(const StudioAsset &a,const StudioAsset &b){
  // Immutable recipe/byte-owner identity avoids a GUI-thread archive digest.
  // Catalog revisions are authoritative; the owners also distinguish explicit
  // revision-zero requests without evaluating either mask.
  return a.id==b.id&&a.revision==b.revision&&a.smart_mask==b.smart_mask&&a.bytes.constData()==b.bytes.constData()&&a.bytes.size()==b.bytes.size();
 }
 struct Gpu {
  GpuLayerPreview renderer;GpuThumbnail download;
  Texture mask{nullptr,blender::GPU_texture_free},shaded{nullptr,blender::GPU_texture_free},gbuffer{nullptr,blender::GPU_texture_free};
  Shader shader{nullptr,blender::GPU_shader_free};
  void initialize(){using namespace blender;using namespace gpu::shader;
   if(mask)return;
   for(auto *image:{&mask,&shaded})image->reset(GPU_texture_create_2d("Own smart-mask sphere",side,side,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr));
   const auto &gb=detail::shaderball_jl01_gbuffer();
   std::vector<float> gb_data(std::size_t(side)*side*4,0.f);
   for(int y=0;y<side;++y)for(int x=0;x<side;++x){
    const auto sp=gb.sample((x+.5)/double(side),(y+.5)/double(side));const std::size_t idx=(std::size_t(y)*side+x)*4;
    if(sp.cov>1e-3){gb_data[idx]=float(sp.u);gb_data[idx+1]=float(sp.v);gb_data[idx+2]=float(.76+.24*std::max(0.0,sp.nx*.3+sp.ny*.6+sp.nz*.74162));gb_data[idx+3]=float(sp.cov);}
    else{gb_data[idx]=0;gb_data[idx+1]=0;gb_data[idx+2]=1;gb_data[idx+3]=-1.f;}
   }
   gbuffer.reset(GPU_texture_create_2d("Own smart-mask shaderball",side,side,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,gb_data.data()));
   if(!mask||!shaded||!gbuffer)throw std::bad_alloc();
   ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(8,8).sampler(0,ImageType::Float2D,"mask_image").sampler(1,ImageType::Float2D,"gbuffer_image")
    .image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"sphere_image");
   info.compute_source_generated=R"GLSL(
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(sphere_image);if(any(greaterThanEqual(p,size)))return;
 vec4 gb=texelFetch(gbuffer_image,p,0);float value=.022,coverage=-1;
 if(gb.w>=0.0){
  ivec2 uv_pixel=clamp(ivec2(gb.xy*vec2(textureSize(mask_image,0))),ivec2(0),textureSize(mask_image,0)-1);
  coverage=clamp(texelFetch(mask_image,uv_pixel,0).r,0.0,1.0);
  value=mix(.003,.90,coverage)*gb.z;}
 imageStore(sphere_image,ivec2(p.x,size.y-1-p.y),vec4(vec3(value),coverage));}
)GLSL";
   shader.reset(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
   if(!shader)throw std::runtime_error("Smart-mask sphere shader compilation failed");
  }
 };
 BlenderHost &host;std::function<bool()> ready;std::function<void(QString)> error;QTimer timer;
 std::deque<Request> queue;std::unique_ptr<Request> active;std::optional<TextureSet> set;
 std::unique_ptr<Profile> profile;std::unique_ptr<Gpu> gpu;bool downloading=false,stopped=false;std::uint64_t next_ticket=0,active_ticket=0;
 Impl(BlenderHost &h,std::function<bool()> r,std::function<void(QString)> e):host(h),ready(std::move(r)),error(std::move(e)){
  timer.setInterval(16);QObject::connect(&timer,&QTimer::timeout,&timer,[this]{poll();});
 }
 void prepare_startup(){
  performance::Scope phase("smart_mask_thumbnail_startup");
  if(!profile)profile=std::make_unique<Profile>(sphere_profile());
  host.with_gpu([&]{if(!gpu){gpu=std::make_unique<Gpu>();gpu->initialize();}gpu->renderer.begin_frame();gpu->renderer.poll_programs();});
 }
 void clear_active(){active.reset();set.reset();downloading=false;active_ticket=0;}
 void poll(){
  if(stopped||!ready())return;
  performance::Scope tick("smart_mask_thumbnail_tick");
  std::optional<QImage> result;
  try{
   if(!active){if(queue.empty()){timer.stop();return;}active=std::make_unique<Request>(std::move(queue.front()));queue.pop_front();active_ticket=++next_ticket;
    if(!profile){performance::Scope phase("smart_mask_thumbnail_profile");profile=std::make_unique<Profile>(sphere_profile());}set=sphere_set(active->asset,*profile);}
   host.with_gpu([&]{
   if(!gpu){performance::Scope phase("smart_mask_thumbnail_gpu_init");gpu=std::make_unique<Gpu>();gpu->initialize();}else gpu->initialize();
   if(downloading){gpu->download.poll();const bool pending=gpu->download.pending();const auto packet=pending?std::optional<ThumbnailMetadata>{}:gpu->download.get();if(smart_mask_thumbnail_packet_ready(pending,packet?&*packet:nullptr,active_ticket)){
    if(performance::enabled()){float low=1,high=0,display_low=1,display_high=0;double sum=0;int samples=0;
     for(const auto &p:packet->pixels)if(p[3]>=0){low=std::min(low,p[3]);high=std::max(high,p[3]);sum+=p[3];display_low=std::min(display_low,p[0]);display_high=std::max(display_high,p[0]);++samples;}
     std::printf("BTS_SMART_MASK_THUMBNAIL_VALUES asset=%s samples=%d mask_min=%.9g mask_max=%.9g mask_mean=%.9g display_min=%.9g display_max=%.9g\n",qPrintable(active->asset.name),samples,low,high,samples?sum/samples:0,display_low,display_high);
    }result=display_packet(*packet);
   }return;}
   gpu->renderer.begin_frame();gpu->renderer.poll_programs();const auto update=gpu->renderer.update(*set,0,gpu->mask.get());
   if(update==GpuPreviewUpdate::Pending)return;if(update==GpuPreviewUpdate::Unsupported)throw std::runtime_error("Smart-mask sphere has unsupported native effects");
   using namespace blender;GPU_shader_bind(gpu->shader.get());GPU_texture_bind(gpu->mask.get(),0);GPU_texture_bind(gpu->gbuffer.get(),1);GPU_texture_image_bind(gpu->shaded.get(),0);
   GPU_compute_dispatch(gpu->shader.get(),side/8,side/8,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);
   GPU_texture_image_unbind(gpu->shaded.get());GPU_texture_unbind(gpu->gbuffer.get());GPU_texture_unbind(gpu->mask.get());GPU_shader_unbind();
   gpu->download.update({},{{gpu->shaded.get(),side,side}},active_ticket);downloading=true;
  });
  }catch(const std::exception &failure){if(error)error(QString::fromUtf8(failure.what()));result=QImage{};}
  if(result){if(performance::enabled())std::printf("BTS_SMART_MASK_THUMBNAIL_READY asset=%s request_to_ready_ms=%.3f valid=%d\n",qPrintable(active->asset.name),performance::ms(active->queued),!result->isNull());auto callbacks=std::move(active->complete);clear_active();for(auto &callback:callbacks)callback(*result);}
  if(!active&&queue.empty())timer.stop();
 }
 void shutdown(){if(stopped)return;stopped=true;timer.stop();queue.clear();clear_active();
  if(gpu)host.with_gpu([&]{gpu.reset();GpuLayerPreview::collect_retired(true);});profile.reset();}
};
SmartMaskThumbnailJobs::SmartMaskThumbnailJobs(BlenderHost &host,std::function<bool()> ready,std::function<void(QString)> error):impl_(std::make_unique<Impl>(host,std::move(ready),std::move(error))){}
SmartMaskThumbnailJobs::~SmartMaskThumbnailJobs(){shutdown();}
void SmartMaskThumbnailJobs::prepare_startup(){impl_->prepare_startup();}
void SmartMaskThumbnailJobs::shutdown(){impl_->shutdown();}
bool SmartMaskThumbnailJobs::pending()const{return impl_->active||!impl_->queue.empty();}
void SmartMaskThumbnailJobs::request(const StudioAsset &asset,std::function<void(QImage)> complete){
 if(impl_->stopped)return;
 if(impl_->active&&Impl::same_source(impl_->active->asset,asset)){impl_->active->complete.push_back(std::move(complete));return;}
 for(auto &request:impl_->queue)if(Impl::same_source(request.asset,asset)){request.complete.push_back(std::move(complete));return;}
 impl_->queue.push_back({asset,{std::move(complete)}});impl_->timer.start();
}
}
