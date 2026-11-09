#include "GpuElapsedQuery.hh"
#include "NativeMaterialPreview.hh"
#include "BlenderHost.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialImageUpload.hh"
#include "GpuThumbnail.hh"
#include "IMB_colormanagement.hh"
#include "Performance.hh"
#include "BKE_main.hh"
#include "BKE_image.hh"
#include "BLI_listbase.h"
#include "DNA_image_types.h"
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QTimer>
#include <algorithm>
#include <stdexcept>
namespace bts {
namespace {
QImage display_preview(const ThumbnailMetadata &packet){
 QImage image(ThumbnailMetadata::side,ThumbnailMetadata::side,QImage::Format_RGB32);
 auto display=packet.pixels;
 auto processor=blender::ColormanageProcessor::colorspace_processor_new("ACEScg","sRGB");
 processor.apply(display.front().data(),ThumbnailMetadata::side,ThumbnailMetadata::side,4,false);
 for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto &p=packet.pixels[y*image.width()+x];const auto &rgb=display[y*image.width()+x];
  // Canonical GPU channel output is ACEScg, including before New. Never rely
  // on the live project's working space or interpolate display-space colors.
  const float alpha=std::clamp(p[3],0.f,1.f),checker=((x/12+y/12)%2)?1.f/3:.6f;int color[3];
  for(int c=0;c<3;++c)color[c]=qRound(255*(std::clamp(rgb[c],0.f,1.f)*alpha+checker*(1-alpha)));image.setPixel(x,y,qRgb(color[0],color[1],color[2]));
 }return image;
}
std::exception_ptr cancelled(){return std::make_exception_ptr(std::runtime_error("Base Color preview cancelled"));}
}
struct NativeMaterialPreview::Impl {
 struct Request {QString file,name;std::shared_ptr<std::atomic_bool> cancel;std::promise<NativeMaterialPreviewResult> promise;};
 struct Gpu {
  NativeMaterialGpuLibrary library;std::unique_ptr<NativeMaterialImageUpload> upload;
  std::shared_ptr<NativeMaterialGraph> graph;std::unique_ptr<GpuThumbnail> thumbnail;
  NativeMaterialImageUpload::Progress images;QJsonArray formats;
  blender::gpu::Texture *target=nullptr;int stage=0;GpuElapsedQuery query;double compile_ms=0,compile_cpu_ms=0;
  ~Gpu(){thumbnail.reset();if(target)blender::GPU_texture_free(target);library.clear();upload.reset();graph.reset();}
 };
 BlenderHost &host;Worker worker;QTimer timer;
 std::unique_ptr<Request> wanted,active;std::shared_future<std::shared_ptr<NativeMaterialGraph>> future;
 std::unique_ptr<Gpu> gpu;double maximum_owner_ms=0,maximum_owner_cpu_ms=0;
 Impl(BlenderHost &h,Worker w):host(h),worker(std::move(w)){timer.setInterval(16);QObject::connect(&timer,&QTimer::timeout,&timer,[this]{poll();});}
 void fail(std::unique_ptr<Request> &r,std::exception_ptr error){if(r){r->promise.set_exception(error);r.reset();}}
 void cancel(){fail(wanted,cancelled());if(active)active->cancel->store(true);}
 void clear(){
  // Also consume a completed worker here so its detached Main and images are
  // released on the owner, with GPU context if uploads have already begun.
  if(future.valid()){try{auto retired=future.get();future={};}catch(...){future={};}}
  gpu.reset();active.reset();
 }
 void poll(){
  if(wanted&&wanted->cancel->load())fail(wanted,cancelled());
  if(!active&&wanted){active=std::move(wanted);try{future=worker(active->file,active->name,active->cancel);}catch(...){fail(active,std::current_exception());}}
  if(!active){timer.stop();return;}
  if(future.valid()&&future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
  try{
   if(active->cancel->load()){auto retired=std::move(active);retired->promise.set_exception(cancelled());host.with_gpu([&]{clear();});return;}
   std::optional<NativeMaterialPreviewResult> completed;
   host.with_gpu([&]{
    QElapsedTimer owner;owner.start();const auto cpu=performance::thread_ms();const auto record=qScopeGuard([&]{maximum_owner_ms=std::max(maximum_owner_ms,owner.nsecsElapsed()/1e6);maximum_owner_cpu_ms=std::max(maximum_owner_cpu_ms,performance::thread_ms()-cpu);});
    if(!gpu){auto graph=future.get();future={};const auto found=std::find_if(graph->channels.begin(),graph->channels.end(),[](const auto &c){return c.channel==0;});
     if(found==graph->channels.end()||!found->tree)throw std::runtime_error("Native Base Color channel is unavailable");
     const auto dependencies=native_material_dependencies(*found->tree,graph->description.parameters.size());
     if(dependencies.surface||dependencies.object||dependencies.view||dependencies.ao)throw std::runtime_error("Base Color preview unavailable: source requires object, surface, view or baked AO context");
     gpu=std::make_unique<Gpu>();gpu->graph=std::move(graph);gpu->upload=std::make_unique<NativeMaterialImageUpload>(gpu->graph);return;
    }
    if(gpu->upload){if(gpu->upload->step()){gpu->images=gpu->upload->progress();for(const auto &image:gpu->graph->main->images)if(image.runtime)if(auto *texture=image.runtime->gputexture[blender::TEXTARGET_2D][0]){const auto format=blender::GPU_texture_format(texture);const auto usage=blender::GPU_texture_usage(texture);gpu->formats.append(QJsonObject{{"format",QString::fromUtf8(blender::GPU_texture_format_name(format))},{"shader_write",bool(usage&blender::GPU_TEXTURE_USAGE_SHADER_WRITE)},{"mipmap_route",(usage&blender::GPU_TEXTURE_USAGE_SHADER_WRITE)&&(format==blender::gpu::TextureFormat::SFLOAT_16_16_16_16||format==blender::gpu::TextureFormat::SFLOAT_32_32_32_32)?"builtin-compute":"unclassified"}});}gpu->upload.reset();}return;}
    if(gpu->stage==0){gpu->library.install("preview","preview",gpu->graph);gpu->stage=1;return;}
    if(gpu->stage==1){QElapsedTimer compile;compile.start();const auto cpu=performance::thread_ms();gpu->library.prepare("preview",0);gpu->compile_ms=compile.nsecsElapsed()/1e6;gpu->compile_cpu_ms=performance::thread_ms()-cpu;gpu->stage=2;return;}
    if(gpu->stage==2){using namespace blender;constexpr int side=ThumbnailMetadata::side;gpu->target=GPU_texture_create_2d("Native Base Color preview",side,side,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr);if(!gpu->target)throw std::bad_alloc();gpu->stage=3;return;}
    if(gpu->stage==3){gpu->query.begin();{const auto end=qScopeGuard([&]{gpu->query.end();});gpu->library.render("preview",0,gpu->graph->description,gpu->target,gpu->library.default_context("preview"),{0,0,ThumbnailMetadata::side,ThumbnailMetadata::side});}gpu->thumbnail=std::make_unique<GpuThumbnail>();gpu->thumbnail->update({},{{gpu->target,ThumbnailMetadata::side,ThumbnailMetadata::side}},1);gpu->stage=4;return;}
    gpu->thumbnail->poll();if(auto result=gpu->thumbnail->get()){const double elapsed=gpu->query.milliseconds();QJsonObject timing{{"image_formats",gpu->formats},{"compile_wall_ms",gpu->compile_ms},{"compile_cpu_ms",gpu->compile_cpu_ms},{"maximum_owner_step_ms",maximum_owner_ms},{"maximum_owner_step_cpu_ms",maximum_owner_cpu_ms},{"native_dispatch_gpu_ms",elapsed},{"compiled_channels",int(gpu->library.statistics().programs)},{"uploaded_images",int(gpu->images.total)},{"transfer_budget_upper_bound_bytes",qint64(gpu->images.transfer_budget_bytes)}};completed.emplace(NativeMaterialPreviewResult{display_preview(*result),timing,*result});gpu.reset();}
   });
   if(completed){completed->timings["maximum_owner_step_ms"]=maximum_owner_ms;completed->timings["maximum_owner_step_cpu_ms"]=maximum_owner_cpu_ms;active->promise.set_value(std::move(*completed));active.reset();}
  }catch(...){const auto error=std::current_exception();try{host.with_gpu([&]{gpu.reset();if(future.valid()){try{auto retired=future.get();future={};}catch(...){future={};}}});}catch(...){/* No GPU work was admitted before initialization. */}fail(active,error);}
 }
};
NativeMaterialPreview::NativeMaterialPreview(BlenderHost &host,Worker worker):impl_(std::make_unique<Impl>(host,std::move(worker))){}
NativeMaterialPreview::~NativeMaterialPreview(){shutdown();}
std::shared_future<NativeMaterialPreviewResult> NativeMaterialPreview::request(const QString &file,const QString &name,std::shared_ptr<std::atomic_bool> cancel){
 if(!cancel)throw std::invalid_argument("Missing preview cancellation");auto &s=*impl_;s.cancel();s.maximum_owner_ms=s.maximum_owner_cpu_ms=0;s.wanted=std::make_unique<Impl::Request>();s.wanted->file=file;s.wanted->name=name;s.wanted->cancel=std::move(cancel);auto future=s.wanted->promise.get_future().share();s.timer.start();return future;
}
void NativeMaterialPreview::cancel(){impl_->cancel();}
bool NativeMaterialPreview::pending()const{return bool(impl_->active)||bool(impl_->wanted);}
void NativeMaterialPreview::shutdown(){auto &s=*impl_;s.timer.stop();s.cancel();if(s.future.valid())s.future.wait();if(s.active){s.active->promise.set_exception(cancelled());try{s.host.with_gpu([&]{s.clear();});}catch(...){s.clear();}}}
}
