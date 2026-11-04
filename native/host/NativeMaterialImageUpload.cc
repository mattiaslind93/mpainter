#include "NativeMaterialImageUpload.hh"
#include "NativeMaterialImageResource.hh"
#include "NativeMaterialTiledStorage.hh"
#include "NativeFragmentImageReadiness.hh"
#include "ResourceBudget.hh"
#include "GPU_capabilities.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialSharedGpuImages.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "BKE_image_residency.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_node_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "GPU_context.hh"
#include "GPU_texture.hh"
#include "Performance.hh"
#include <QElapsedTimer>
#include <QScopeGuard>
#include <algorithm>
#include <set>
#include <map>
#include <thread>
#include <utility>

namespace bts {
using namespace blender;
struct NativeMaterialImageUpload::Impl {
  std::shared_ptr<NativeMaterialGraph> graph;
  std::vector<NativeImageUploadRequest> images;
  struct PendingTiled {gpu::Texture *primary=nullptr,*mapping=nullptr;~PendingTiled(){if(primary)GPU_texture_free(primary);if(mapping)GPU_texture_free(mapping);}};
  std::unique_ptr<NativeTiledUploadState<std::unique_ptr<PendingTiled>>> tiled_upload;
  std::shared_ptr<const NativeTiledStoragePlan> tiled_plan;
  std::shared_ptr<const NativeMaterialImageTileSet> tiled_source;
  std::thread::id owner=std::this_thread::get_id();
  std::size_t next=0,allocations=0,shared_images=0;
  Image *image=nullptr;
  ImBuf *buffer=nullptr;
  gpu::Texture *texture=nullptr;
  ImageUser user{};
  bool high=false,premultiplied=false;
  int row=0;
  std::uint64_t bytes=0;
  double maximum_ms=0;
  ~Impl(){release();}
  void release(){
    if(texture)GPU_texture_free(std::exchange(texture,nullptr));
    if(buffer)BKE_image_release_ibuf(image,std::exchange(buffer,nullptr),nullptr);
    image=nullptr;
  }
  void require_owner() const {
    if(owner!=std::this_thread::get_id()||!GPU_context_active_get())
      throw std::logic_error("Native source image upload requires its GPU owner context");
  }
};
NativeMaterialImageUpload::NativeMaterialImageUpload(std::shared_ptr<NativeMaterialGraph> graph,std::span<blender::bNodeTree *const> extra_roots,bool ordinary_channels)
    :impl_(std::make_unique<Impl>())
{
  auto &s=*impl_;s.require_owner();if(!graph)throw std::invalid_argument("Native source graph is missing");s.graph=std::move(graph);
  s.images=native_material_image_upload_requests(*s.graph,extra_roots,ordinary_channels);
}
NativeMaterialImageUpload::~NativeMaterialImageUpload()=default;
bool NativeMaterialImageUpload::step(std::size_t packet_bytes)
{
  auto &s=*impl_;s.require_owner();if(packet_bytes<16)throw std::invalid_argument("Native image upload packet is empty");
  QElapsedTimer clock;clock.start();const auto timing=qScopeGuard([&]{s.maximum_ms=std::max(s.maximum_ms,clock.nsecsElapsed()/1e6);});
  if(s.next==s.images.size())return true;
  const auto request=s.images[s.next];
  if(request.image->source==IMA_SRC_TILED){
    if(!s.tiled_upload){
      for(const auto &entry:s.graph->tiled_image_resources)if(entry.first==request.image)s.tiled_source=entry.second;
      if(!s.tiled_source)throw std::logic_error("Native TILED images must retain a complete worker-decoded source");
      int limit=GPU_texture_size_with_limit(GPU_max_texture_size());if(const int residency=BKE_image_gpu_residency_limit(request.image);residency>0)limit=std::min(limit,residency);
      s.tiled_plan=std::make_shared<const NativeTiledStoragePlan>(native_tiled_storage_plan(*request.image,*s.tiled_source,request.representation,{limit,GPU_max_texture_layers(),GPU_max_texture_size()}));
      try{const auto &ready=retained_native_tiled_gpu_plan(s.graph,request.image,request.representation);if(ready.key==s.tiled_plan->key){++s.next;s.tiled_plan.reset();s.tiled_source.reset();return s.next==s.images.size();}}catch(const NativeFragmentImagePending &){}
      if(reuse_native_tiled_gpu_image(s.graph,request.image,s.tiled_plan)){++s.next;++s.shared_images;s.tiled_plan.reset();s.tiled_source.reset();return s.next==s.images.size();}
      ResourceBudget{}.require(s.tiled_plan->mapping_bytes,resource_sum(s.tiled_plan->texture_bytes,s.tiled_plan->mapping_bytes),"Native retained TILED storage");
      s.tiled_upload=std::make_unique<NativeTiledUploadState<std::unique_ptr<Impl::PendingTiled>>>(*s.tiled_plan);s.image=request.image;
    }
    const auto done=s.tiled_upload->step(packet_bytes,[&](const NativeTiledUploadAction &action,std::unique_ptr<Impl::PendingTiled> &pending){
      const auto &plan=*s.tiled_plan;
      switch(action.phase){
       case NativeTiledPhase::Allocate:{pending=std::make_unique<Impl::PendingTiled>();const auto *buffer=s.tiled_source->tiles().at(plan.first_tile)->buffer();pending->primary=IMB_touch_gpu_texture(s.image->id.name+2,const_cast<ImBuf *>(buffer),plan.width,plan.height,plan.representation==NativeTiledRepresentation::ArrayMapping?plan.layers:0,bool(s.image->flag&IMA_HIGH_BITDEPTH),true);if(!pending->primary)throw std::runtime_error("Native TILED texture allocation failed");++s.allocations;break;}
       case NativeTiledPhase::Pixels:{const auto packet=native_tiled_packet_view(*s.tiled_source,action);GPU_texture_update_sub(pending->primary,GPU_DATA_FLOAT,packet.pixels,action.target_x,action.target_y,action.layer,packet.width,packet.height,1,packet.source_row_length);s.bytes+=action.transfer_bytes;break;}
       case NativeTiledPhase::Mips:GPU_texture_update_mipmap_chain(pending->primary);GPU_texture_mipmap_mode(pending->primary,true,true);GPU_texture_extend_mode(pending->primary,GPU_SAMPLER_EXTEND_MODE_REPEAT);GPU_texture_original_size_set(pending->primary,plan.width,plan.height);break;
       case NativeTiledPhase::AllocateMapping:pending->mapping=GPU_texture_create_1d_array(s.image->id.name+2,plan.map_width,2,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_SHADER_READ,nullptr);if(!pending->mapping)throw std::runtime_error("Native TILED mapping allocation failed");GPU_texture_mipmap_mode(pending->mapping,false,false);++s.allocations;break;
       case NativeTiledPhase::Mapping:GPU_texture_update_sub(pending->mapping,GPU_DATA_FLOAT,plan.mapping[std::size_t(action.source_y)*plan.map_width+action.source_x].data(),action.target_x,action.target_y,0,action.width,1,1);s.bytes+=action.transfer_bytes;break;
       case NativeTiledPhase::Publish:publish_native_tiled_gpu_image(s.graph,s.image,s.tiled_plan,pending->primary,pending->mapping);break;
       default:throw std::logic_error("Unknown native TILED upload action");
      }
    });
    if(done){s.tiled_upload.reset();s.tiled_plan.reset();s.tiled_source.reset();s.release();++s.next;}return s.next==s.images.size();
  }
  if(!s.image){
    performance::Scope phase("native_image_allocate");
    auto *image=request.image;
    if(image->source!=IMA_SRC_FILE&&image->source!=IMA_SRC_GENERATED)
      throw std::invalid_argument(std::string(image->id.name+2)+": bounded native upload currently requires a single image, not a sequence/UDIM");
    if(!BKE_image_has_loaded_ibuf(image))throw std::logic_error("Native source images must be decoded on a worker before GPU upload");
    BKE_imageuser_default(&s.user);
    // Consume the first partial-update generation before installing our cache.
    const auto cached=BKE_image_get_gpu_material_texture_try(image,&s.user,false);
    if(cached.texture&&*cached.texture){if(!(image->runtime->gpuflag&IMA_GPU_MIPMAP_COMPLETE)&&!BKE_image_gpu_mipmaps_suspended(image)){GPU_texture_update_mipmap_chain(*cached.texture);GPU_texture_mipmap_mode(*cached.texture,true,true);image->runtime->gpuflag|=IMA_GPU_MIPMAP_COMPLETE;return false;}++s.next;return s.next==s.images.size();}
    if(reuse_native_gpu_image(s.graph,image)){++s.next;++s.shared_images;return s.next==s.images.size();}
    s.image=image;s.buffer=BKE_image_acquire_ibuf(image,&s.user,nullptr);
    if(!s.buffer||s.buffer->x<=0||s.buffer->y<=0)throw std::runtime_error("Native decoded source image is unavailable");
    s.high=bool(image->flag&IMA_HIGH_BITDEPTH);s.premultiplied=BKE_image_has_gpu_texture_premultiplied_alpha(image,s.buffer);
    s.texture=IMB_touch_gpu_texture(image->id.name+2,s.buffer,s.buffer->x,s.buffer->y,0,s.high,true);
    if(!s.texture)throw std::runtime_error(std::string(image->id.name+2)+": GPU source allocation failed");
    ++s.allocations;s.row=0;return false;
  }
  if(s.row<s.buffer->y){
    performance::Scope phase("native_image_upload_packet");
    const auto &source=*s.buffer;
    const std::size_t row_bytes=std::size_t(source.x)*4*sizeof(float);
    const int rows=int(std::min<std::size_t>(source.y-s.row,std::max<std::size_t>(1,packet_bytes/row_bytes)));
    ImBuf patch;patch.x=source.x;patch.y=rows;patch.channels=source.channels;patch.color_mode=source.color_mode;patch.flags=source.flags;patch.foptions=source.foptions;
    patch.byte_buffer.colorspace=source.byte_buffer.colorspace;patch.float_buffer.colorspace=source.float_buffer.colorspace;
    if(source.float_data())patch.assign_float_data(source.float_data()+std::size_t(s.row)*source.x*(source.channels?source.channels:4),source.float_buffer.sharing_info);
    else if(source.byte_data())patch.assign_byte_data(source.byte_data()+std::size_t(s.row)*source.x*4,source.byte_buffer.sharing_info);
    else throw std::runtime_error("Native source has no CPU pixel buffer");
    // The small header has the packet's dimensions, avoiding ImBuf's full-image
    // resize path. Shared pixel ownership keeps the original buffer alive.
    IMB_update_gpu_texture_sub(s.texture,&patch,0,s.row,0,source.x,rows,s.high,true,s.premultiplied);
    s.row+=rows;s.bytes+=std::uint64_t(row_bytes)*rows;return false;
  }
  {
    performance::Scope phase("native_image_publish");
    GPU_texture_update_mipmap_chain(s.texture);GPU_texture_mipmap_mode(s.texture,true,true);
    GPU_texture_extend_mode(s.texture,GPU_SAMPLER_EXTEND_MODE_REPEAT);
    GPU_texture_original_size_set(s.texture,s.buffer->x,s.buffer->y);
    auto &runtime=*s.image->runtime;
    if(runtime.gputexture[TEXTARGET_2D][0])throw std::runtime_error("Native source GPU cache changed during upload");
    runtime.gputexture[TEXTARGET_2D][0]=std::exchange(s.texture,nullptr);
    runtime.gpu_pass=0;runtime.gpu_layer=0;runtime.gpu_view=0;runtime.gpuflag|=IMA_GPU_MIPMAP_COMPLETE;
    BKE_image_tag_time(s.image);remember_native_gpu_image(s.graph,s.image);
  }
  s.release();++s.next;return s.next==s.images.size();
}
NativeMaterialImageUpload::Progress NativeMaterialImageUpload::progress() const
{
  const auto &s=*impl_;Progress result;result.completed=s.next;result.total=s.images.size();result.allocations=s.allocations;result.shared_images=s.shared_images;result.transfer_budget_bytes=s.bytes;result.maximum_step_ms=s.maximum_ms;
  if(s.image){result.image=QString::fromUtf8(s.image->id.name+2);result.uploaded_rows=s.row;result.rows=s.buffer?s.buffer->y:0;}
  return result;
}
}
