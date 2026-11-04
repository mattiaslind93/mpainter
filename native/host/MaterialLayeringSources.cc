#include "MaterialLayeringSources.hh"
#include "GpuLayerPreview.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include "NativeGpuImages.hh"
#include "BKE_image_hosted.hh"
#include "BKE_main.hh"
#include "BLI_implicit_sharing_ptr.hh"
#include "IMB_imbuf.hh"
#include "GPU_context.hh"
#include <QCryptographicHash>
#include <QThreadPool>
#include <QScopeGuard>
#include <map>
#include <set>
#include <thread>
namespace bts {
namespace {
using namespace blender;
using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
using PixelOwner=ImplicitSharedValue<std::shared_ptr<const std::vector<FillTexture::Pixel>>>;
std::map<std::string,std::shared_ptr<const FillTexture>> required(std::span<const MaterialLayering *const> models){
 std::map<std::string,std::shared_ptr<const FillTexture>> result;
 for(const auto *model:models)if(model&&model->enabled)for(const auto &input:model->materials)for(const auto &source:input.textures)if(source){
  source->validate();
  const auto add=[&](const auto &member){if(!member->pixels)throw std::invalid_argument("Reusable material map member is not hydrated");result.try_emplace(gpu_texture_resource_key(*member),member);};
  if(source->tiles){if(!source->raw)material_layer_color_source_transforms(*source);for(const auto &[tile,member]:*source->tiles)add(member);}else add(source);
 }
 return result;
}
}
ImBuf *material_layer_source_pixel_view(const FillTexture &source){
 source.validate();if(source.tiles||!source.pixels)throw std::invalid_argument("Reusable material map pixels are not hydrated");

 Buffer buffer(IMB_allocImBuf(source.width,source.height,ImBufFlags(0)),IMB_freeImBuf);if(!buffer)throw std::bad_alloc();buffer->channels=4;
 buffer->assign_float_data(source.pixels->front().data(),ImplicitSharingPtr<>(new PixelOwner(source.pixels)));
 IMB_colormanagement_assign_float_colorspace(buffer.get(),IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA));
 return buffer.release();
}
struct MaterialLayeringSources::Impl {
 struct Entry {
  Image *image=nullptr;
  std::shared_ptr<const FillTexture> source;
  // An additional implicit-sharing user makes any accidental ImBuf write copy
  // rather than mutate the immutable decoded vector while the adapter is live.
  ImplicitSharingPtr<> pixels;
  std::optional<GpuTextureResource> gpu;
 };
 std::map<std::string,Entry> entries;
 Main *main=nullptr;void *context=nullptr;
 std::thread::id owner;
 void require_owner(){
  if((context&&owner!=std::this_thread::get_id())||!GPU_context_active_get())throw std::logic_error("Material source bindings require their GPU owner context");
  if(context&&context!=GPU_context_active_get())throw std::logic_error("Material source GPU context changed");
  context=GPU_context_active_get();owner=std::this_thread::get_id();
 }
 void detach(Entry &entry)noexcept{
  // Also retire provisional cache state after a failed preparation. A GPU
  // lease is installed before the first cache mutation, never afterward.
  if(!entry.gpu&&!entry.source&&!entry.pixels)return;
  // Do not delete IDs: old Cycles Image loaders can outlive graph replacement.
  // Release the Image's alias first; then the pool can evict its own allocation.
  if(entry.image)BKE_image_free_gputextures(entry.image);
  entry.gpu.reset();
  // Patch 38 makes each live immutable Cycles loader own a shared CPU header.
  // Save snapshots own their own header too. Cache retirement cannot invalidate
  // those readers; only the final genuine reader releases decoded storage.
  Buffer keeper(BKE_image_detach_hosted_source(entry.image,1001),IMB_freeImBuf);
  entry.pixels={};entry.source.reset();
  if(keeper){try{QThreadPool::globalInstance()->start([keeper=std::move(keeper)]()mutable{keeper.reset();});}catch(...){/* Allocation failure: CPU-only fallback retirement on this owner. */}}
 }
};
MaterialLayeringSources::MaterialLayeringSources():impl_(std::make_unique<Impl>()){}
MaterialLayeringSources::~MaterialLayeringSources(){reset();}
bool MaterialLayeringSources::prepare(Main &main,GpuLayerPreview &pool,std::span<const MaterialLayering *const> models){
 auto &s=*impl_;const auto wanted=required(models);if(wanted.empty())return true;s.require_owner();
 if(s.main&&s.main!=&main)throw std::logic_error("Material source bindings cannot cross Main ownership");s.main=&main;
 std::vector<const FillTexture *> sources;for(const auto &[key,source]:wanted)sources.push_back(source.get());
 const auto status=pool.prepare_texture_resources(sources);
 if(status==GpuPreviewUpdate::Unsupported)throw std::length_error("Insufficient GPU memory for reusable material maps at their original dimensions");
 if(status!=GpuPreviewUpdate::Updated)return false;
 for(const auto &[key,source]:wanted){
  auto lease=pool.retain_texture_resource(*source);if(!lease)throw std::logic_error("Prepared material source lost GPU residency");
  auto [at,inserted]=s.entries.try_emplace(key);auto &entry=at->second;
  if(entry.gpu&&entry.gpu->texture==lease->texture&&entry.image->runtime->gputexture[TEXTARGET_2D][0]==lease->texture&&BKE_image_has_loaded_ibuf(entry.image))continue;
  if(!entry.image){
   const auto digest=QCryptographicHash::hash(QByteArray::fromStdString(key),QCryptographicHash::Sha256).toHex().left(32);
   const std::string name="BTS Layer Source "+digest.toStdString();const float empty[4]={0,0,0,1};
   auto *image=native_generated_image(&main,source->width,source->height,name.c_str(),32,1,IMA_GENTYPE_BLANK,empty,false,true,false);
   if(!image)throw std::bad_alloc();id_us_min(&image->id);image->flag|=IMA_HIGH_BITDEPTH;image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;
   image->runtime->gpuflag|=IMA_HOSTED_IMMUTABLE_SOURCE;
   entry.image=image;
  }
  if(entry.gpu)s.detach(entry);
  entry.gpu=std::move(lease);
  bool attached=false;const auto rollback=qScopeGuard([&]()noexcept{if(!attached)s.detach(entry);});
  auto *image=entry.image;Buffer buffer(material_layer_source_pixel_view(*source),IMB_freeImBuf);
  if(!image->runtime->partial_update_user)image->runtime->partial_update_user=BKE_image_partial_update_create(image);
  if(image->runtime->gputexture[TEXTARGET_2D][0])BKE_image_free_gputextures(image);
  if(!BKE_image_assign_hosted_immutable_source(image,1001,buffer.get()))throw std::runtime_error("Cannot assign immutable material source pixels");
  entry.source=source;entry.pixels=buffer->float_buffer.sharing_info;
  // Install only the existing allocation. Do not call prepare_native_gpu_image
  // or a decode/upload getter, which would allocate another texture.
  GPU_texture_ref(entry.gpu->texture);image->runtime->gputexture[TEXTARGET_2D][0]=entry.gpu->texture;
  image->runtime->gpu_pass=image->runtime->gpu_layer=image->runtime->gpu_view=0;
  BKE_image_gpu_override_updated(image);
  attached=true;
 }
 return true;
}
std::optional<MaterialLayerImages> MaterialLayeringSources::images(const MaterialLayering &model)const{
 MaterialLayerImages result{};if(!model.enabled)return result;
 for(int i=0;i<4;++i)for(int c=0;c<6;++c)if(const auto &source=model.materials[i].textures[c]){
  const auto get=[&](const FillTexture &member)->Image *{const auto at=impl_->entries.find(gpu_texture_resource_key(member));if(at==impl_->entries.end()||!at->second.gpu||at->second.image->runtime->gputexture[TEXTARGET_2D][0]!=at->second.gpu->texture||!BKE_image_has_loaded_ibuf(at->second.image))return nullptr;return at->second.image;};
  if(source->tiles){for(const auto &[tile,member]:*source->tiles){auto *image=get(*member);if(!image)return std::nullopt;result.members[i][c].emplace(tile,image);}}
  else{result[i][c]=get(*source);if(!result[i][c])return std::nullopt;}
 }
 return result;
}
void MaterialLayeringSources::retain_only(std::span<const MaterialLayering *const> models){
 const auto wanted=required(models);auto &s=*impl_;
 if(std::any_of(s.entries.begin(),s.entries.end(),[&](const auto &pair){return pair.second.gpu&&!wanted.contains(pair.first);})){s.require_owner();for(auto &[key,entry]:s.entries)if(!wanted.contains(key))s.detach(entry);}
}
void MaterialLayeringSources::reset(){
 auto &s=*impl_;if(std::any_of(s.entries.begin(),s.entries.end(),[](const auto &pair){return bool(pair.second.gpu)||bool(pair.second.source)||bool(pair.second.pixels);})){s.require_owner();for(auto &[key,entry]:s.entries)s.detach(entry);}
 s.entries.clear();s.main=nullptr;s.context=nullptr;
}
}
