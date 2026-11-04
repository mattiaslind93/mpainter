#pragma once
#include "GpuBrushRaster.hh"
#include "GpuPolygonRaster.hh"
#include "GpuSmudgePayload.hh"
#include "GpuFilledPath.hh"
#include "GpuRibbonPath.hh"
#include "PaintRecipe.hh"
#include "TextureMemory.hh"
#include "TextureResidencyPolicy.hh"
#include "GpuPreparedProgramOwner.hh"
#include "GpuProgramWarmup.hh"
#include "GPU_shader_hosted.hh"
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>
#include <list>
namespace bts {
// Immutable 64px GPU authoring pages. Page versions are shared by Undo and
// downstream layer consumers; eviction only removes a reproducible GPU cache.
class GpuPaintPages {
 friend int verify_gpu_authoring_workers(GpuPaintPages &);
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 using Key=std::tuple<std::uint64_t,std::uint32_t,int>;
 struct Entry {Texture image{nullptr,blender::GPU_texture_free};std::list<Key>::iterator position;};
 static constexpr std::uint64_t page_bytes=64*64*16;
 AuthoringCacheBudget memory_budget_;
 std::map<Key,Entry> pages_;
 std::list<Key> lru_;
 std::set<Key> pinned_;
 Texture zero_{nullptr,blender::GPU_texture_free};
 Shader copy_{nullptr,blender::GPU_shader_free};
 GpuBrushRaster raster_;
 GpuPolygonRaster polygons_;
 GpuFilledPath filled_paths_;
 GpuRibbonPath ribbon_paths_;
 GpuSmudgePayload payloads_;
 std::uint64_t computed_=0,uploaded_=0,evicted_=0,allocations_=0,reuses_=0,budget_override_=0;
 blender::gpu::Texture *zero(){
  using namespace blender;
  if(!zero_){zero_=Texture(GPU_texture_create_2d("BTS empty authoring page",64,64,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);if(!zero_)throw std::bad_alloc();const float value[4]={};GPU_texture_clear(zero_.get(),GPU_DATA_FLOAT,value);}
  return zero_.get();
 }
 Entry &allocate(const Key &key){
  using namespace blender;const auto owned=pages_.size()*page_bytes,free=texture_free_vram.load();
  const auto budget=budget_override_?budget_override_:memory_budget_.update(texture_vram_query_generation.load(),owned,free,texture_vram_query_valid.load()||free!=0,texture_vram_query_valid.load()?0:texture_available_ram_bytes());
  Texture reusable{nullptr,GPU_texture_free};
  while((pages_.size()+1)*page_bytes>budget){
   auto victim=lru_.end();for(auto i=lru_.rbegin();i!=lru_.rend();++i)if(!pinned_.contains(*i)){victim=std::prev(i.base());break;}
   if(victim==lru_.end())break;
   if(!reusable)reusable=std::move(pages_.at(*victim).image);
   pages_.erase(*victim);lru_.erase(victim);++evicted_;
  }
  auto &entry=pages_[key];lru_.push_front(key);entry.position=lru_.begin();if(reusable){GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS|GPU_BARRIER_TEXTURE_UPDATE);entry.image=std::move(reusable);++reuses_;}else {entry.image=Texture(GPU_texture_create_2d("BTS immutable authoring page",64,64,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);++allocations_;}
  if(!entry.image){texture_memory_refresh_needed=true;lru_.erase(entry.position);pages_.erase(key);throw std::bad_alloc();}return entry;
 }
  struct CoverageEntry {Texture image{nullptr,blender::GPU_texture_free};std::set<std::uint32_t> copied;std::uint64_t used=0,bytes=0;};
 std::map<std::string,CoverageEntry> coverage_;std::set<std::string> coverage_pinned_;std::uint64_t coverage_clock_=0,coverage_bytes_=0,coverage_allocations_=0;
 blender::gpu::Texture *coverage(const PaintPixels::Stamp &stamp,performance::Clock::time_point deadline){
  using namespace blender;if(!stamp.coverage_mask)return zero();const int u=int(stamp.stencil_uv_offset[0]),v=int(stamp.stencil_uv_offset[1]);if(u<0||u>=10||v<0||v>=100)return zero();
  const auto &pixels=stamp.coverage_mask->pixels.tile(1001+u+v*10);const auto versions=pixels.page_versions()[2];if(versions.empty())return zero();
  std::string key;const auto append=[&](const auto &value){key.append(reinterpret_cast<const char *>(&value),sizeof(value));};const int size=pixels.resolution();append(size);for(const auto &[number,version]:versions){append(number);append(version);}
  if(!coverage_pinned_.insert(key).second)throw std::logic_error("Cyclic Quick Mask authoring dependency");
  struct Unpin{std::set<std::string> &keys;const std::string &key;~Unpin(){keys.erase(key);}} unpin{coverage_pinned_,key};
  auto found=coverage_.find(key);if(found==coverage_.end()){
   if(performance::Clock::now()>=deadline)return nullptr;
   const auto bytes=std::uint64_t(size)*size*16,free=texture_free_vram.load(),budget=resource_sum(coverage_bytes_,(texture_vram_query_valid.load()||free!=0)?free:texture_available_ram_bytes());
   while(coverage_bytes_+bytes>budget&&!coverage_.empty()){auto victim=coverage_.end();for(auto i=coverage_.begin();i!=coverage_.end();++i)if(!coverage_pinned_.contains(i->first)&&(victim==coverage_.end()||i->second.used<victim->second.used))victim=i;if(victim==coverage_.end())break;coverage_bytes_-=victim->second.bytes;coverage_.erase(victim);}

   Texture image(GPU_texture_create_2d("BTS shared Quick Mask brush source",size,size,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);if(!image)throw std::bad_alloc();const float empty[4]={};GPU_texture_clear(image.get(),GPU_DATA_FLOAT,empty);
   found=coverage_.try_emplace(key).first;found->second.image=std::move(image);found->second.bytes=bytes;coverage_bytes_+=bytes;++coverage_allocations_;
  }
  auto &entry=found->second;entry.used=++coverage_clock_;
  for(const auto &[number,version]:versions)if(!entry.copied.contains(number)){auto *source=page(pixels,2,number,deadline);if(!source)return nullptr;copy(entry.image.get(),source,number);entry.copied.insert(number);}
  return entry.image.get();
 }

 blender::gpu::Texture *page(const PaintPixels &pixels,int channel,std::uint32_t number,performance::Clock::time_point deadline){
  using namespace blender;
  const auto wanted=pixels.page_version(channel,number);if(!wanted)return zero();
  std::vector<const PaintPixels *> pending{&pixels};
  const auto lookup=[&](const PaintPixels &value){const auto version=value.page_version(channel,number);return version?pages_.at(Key{version,number,channel}).image.get():zero();};
  while(!pending.empty()){
   const auto &current=*pending.back();const auto version=current.page_version(channel,number);
   if(!version){pending.pop_back();continue;}const Key key{version,number,channel};
   if(auto found=pages_.find(key);found!=pages_.end()){lru_.splice(lru_.begin(),lru_,found->second.position);pinned_.insert(key);pending.pop_back();continue;}
   const auto &recipe=current.recipe();
   if(!recipe){
    if(performance::Clock::now()>=deadline)return nullptr;
    auto &entry=allocate(key);pinned_.insert(key);const auto data=current.page_data(channel,number);
    if(data.size()!=64*64)throw std::logic_error("Missing source authoring page");
    GPU_texture_update(entry.image.get(),GPU_DATA_FLOAT,data.data());uploaded_+=page_bytes;pending.pop_back();continue;
   }
   if(version!=recipe->revision){pending.push_back(&recipe->base);continue;}
   if(recipe->kind==PaintRecipe::Kind::Source){
    if(performance::Clock::now()>=deadline)return nullptr;
    const auto *source=dynamic_cast<const FilledPathSource *>(recipe->source.get());
    const auto *ribbon=dynamic_cast<const RibbonPathSource *>(recipe->source.get());
    if(!source&&!ribbon)throw std::logic_error("GPU renderer for this procedural paint source is not installed");
    PaintPixels::Stamp mask;mask.coverage_mask=source?source->grid->geometry->mask:ribbon->grid->geometry->mask;
    const int tile=source?source->grid->tile:ribbon->grid->tile;
    mask.stencil_uv_offset={double((tile-1001)%10),double((tile-1001)/10)};
    auto *coverage_image=coverage(mask,deadline);if(!coverage_image)return nullptr;
    auto &entry=allocate(key);pinned_.insert(key);
    try{if(source)filled_paths_.render(entry.image.get(),*source,channel,number,coverage_image);else ribbon_paths_.render(entry.image.get(),*ribbon,channel,number,coverage_image);}
    catch(...){lru_.erase(entry.position);pages_.erase(key);throw;}
    ++computed_;pending.pop_back();continue;
   }
   if(recipe->kind==PaintRecipe::Kind::Stamp){
    std::vector<const PaintPixels::Stamp *> commands;const PaintPixels *base=&current;std::size_t triangles=0;
    const auto bitmap=[](const PaintPixels::Stamp &stamp){return stamp.alpha.kind==AlphaKind::Bitmap?stamp.alpha.bitmap.get():nullptr;};
    const auto *alpha=bitmap(recipe->stamp);const auto stencil_bitmap=[](const PaintPixels::Stamp &s){return s.stencil&&s.stencil->stencil.resource&&s.stencil->stencil.resource->kind==AlphaKind::Bitmap?s.stencil->stencil.resource->bitmap.get():nullptr;};const auto *stencil_alpha=stencil_bitmap(recipe->stamp);
    while(base->recipe()&&commands.size()<64){
     const auto v=base->page_version(channel,number);if(!v||pages_.contains(Key{v,number,channel}))break;
     const auto &next=*base->recipe();if(v!=next.revision){base=&next.base;continue;}
     if(next.kind!=PaintRecipe::Kind::Stamp||bitmap(next.stamp)!=alpha||stencil_bitmap(next.stamp)!=stencil_alpha||next.stamp.coverage_mask!=recipe->stamp.coverage_mask||next.stamp.stencil_uv_offset!=recipe->stamp.stencil_uv_offset||next.stamp.smudge!=recipe->stamp.smudge||next.stamp.cloned!=recipe->stamp.cloned)break;
     const auto count=(next.stamp.surface?next.stamp.surface->triangles.size():0)+(next.stamp.stencil?next.stamp.stencil->uv_world.size():0)+(next.stamp.projected?next.stamp.projected->mapping.uv_world.size():0);
     if(!commands.empty()&&triangles+count>4096)break;
     commands.push_back(&next.stamp);triangles+=count;base=&next.base;
    }
    const auto base_version=base->page_version(channel,number);const Key dependency{base_version,number,channel};
    if(base_version&&!pages_.contains(dependency)){pending.push_back(base);continue;}
    if(performance::Clock::now()>=deadline)return nullptr;
    if(base_version){pinned_.insert(dependency);lru_.splice(lru_.begin(),lru_,pages_.at(dependency).position);}
    auto *coverage_image=coverage(recipe->stamp,deadline);if(!coverage_image)return nullptr;
    std::shared_ptr<GpuSmudgePayload::Entry> payload;
    if(recipe->stamp.smudge||recipe->stamp.cloned){payload=payloads_.acquire(recipe->stamp.smudge?recipe->stamp.smudge:recipe->stamp.cloned,channel,[&](const PaintPixels &p,int c,std::uint32_t n,auto until){return page(p,c,n,until);},deadline);if(!payload)return nullptr;}
    auto &entry=allocate(key);pinned_.insert(key);GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_copy(entry.image.get(),lookup(*base));GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);
    std::reverse(commands.begin(),commands.end());const std::array<int,2> origin{int(number%65536)*64,int(number/65536)*64};
    if(!raster_.stamps(entry.image.get(),channel,commands,recipe->resolution,origin,coverage_image,payload?payload->channels[channel].get():nullptr)){lru_.erase(entry.position);pages_.erase(key);throw std::runtime_error("Unsupported GPU brush command batch");}
    ++computed_;pending.pop_back();continue;
   }
   bool waiting=false;
   for(const auto *input:{recipe->complete_stroke?&recipe->before:&recipe->base,&recipe->before,&recipe->ink}){
    const auto dependency=input->page_version(channel,number);if(dependency){const Key source{dependency,number,channel};const auto found=pages_.find(source);if(found==pages_.end()){pending.push_back(input);waiting=true;break;}lru_.splice(lru_.begin(),lru_,found->second.position);pinned_.insert(source);}
   }
   if(waiting)continue;
   if(performance::Clock::now()>=deadline)return nullptr;
   auto *base=lookup(recipe->complete_stroke?recipe->before:recipe->base);auto &entry=allocate(key);pinned_.insert(key);
   GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_copy(entry.image.get(),base);GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);
   const std::array<int,2> origin{int(number%65536)*64,int(number/65536)*64};bool okay=false;
   if(recipe->kind==PaintRecipe::Kind::Stamp)okay=raster_.stamp(entry.image.get(),channel,recipe->stamp,recipe->resolution,origin);
   else {auto bounds=((recipe->complete_stroke&&recipe->kind!=PaintRecipe::Kind::Mix)||recipe->kind==PaintRecipe::Kind::Difference)?PaintPixels::Bounds{origin[0],origin[1],origin[0]+64,origin[1]+64}:recipe->bounds;bounds={bounds.x0-origin[0],bounds.y0-origin[1],bounds.x1-origin[0],bounds.y1-origin[1]};okay=raster_.over(entry.image.get(),lookup(recipe->before),lookup(recipe->ink),recipe->opacity,bounds,recipe->erase,recipe->replace,recipe->kind==PaintRecipe::Kind::Mix?1:recipe->kind==PaintRecipe::Kind::Difference?2:0);}
   if(!okay){lru_.erase(entry.position);pages_.erase(key);throw std::runtime_error("Unsupported GPU authoring command");}
   ++computed_;pending.pop_back();
  }
  return pages_.at(Key{wanted,number,channel}).image.get();
 }
 static blender::gpu::shader::ShaderCreateInfo copy_info(){using namespace blender;using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).sampler(0,ImageType::Float2D,"source_img").image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"result_img").push_constant(Type::int2_t,"destination");info.compute_source_generated="void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p+destination,imageSize(result_img))))return;imageStore(result_img,p+destination,texelFetch(source_img,p,0));}";return info;
 }
 void copy(blender::gpu::Texture *target,blender::gpu::Texture *source,std::uint32_t number){
  using namespace blender;
  if(!copy_){trace_authoring_compilation();auto info=copy_info();gpu_derived::stabilize_hosted_resource_owners(info);copy_=Shader(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free);}
  if(!copy_)throw std::runtime_error("GPU authoring page copy shader failed");
  const int origin[2]={int(number%65536)*64,int(number/65536)*64};GPU_shader_bind(copy_.get());GPU_shader_uniform_2iv(copy_.get(),"destination",origin);GPU_texture_bind(source,0);GPU_texture_image_bind(target,0);
  GPU_compute_dispatch(copy_.get(),4,4,1);trace_authoring_dispatch(copy_.get());GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_image_unbind(target);GPU_shader_unbind();
 }
#include "GpuAuthoringPrograms.inc"
 public:
 GpuPolygonRaster &polygon_raster(){return polygons_;}
 void adjustment_resolver(GpuSmudgePayload::AdjustmentResolver resolver){payloads_.adjustment_resolver=std::move(resolver);}
 void evict(){if(!pinned_.empty())throw std::logic_error("Cannot evict active authoring pages");pages_.clear();lru_.clear();coverage_.clear();coverage_bytes_=0;payloads_.evict();raster_.evict();polygons_.evict();filled_paths_.evict();ribbon_paths_.evict();}
 void retire_payloads(){payloads_.retire_unused();}
 explicit GpuPaintPages(std::uint64_t maximum_cache_bytes=0):budget_override_(maximum_cache_bytes){}
 void prepare(){
  using namespace blender;
  {performance::Scope part("gpu_authoring_warmup_filled_path");filled_paths_.warm_up();}
  {performance::Scope part("gpu_authoring_warmup_ribbon_path");ribbon_paths_.warm_up();}
  {performance::Scope part("gpu_authoring_warmup_polygon_owner");if(!polygons_.prepare())throw std::runtime_error("GPU polygon shader preparation failed");}
  {performance::Scope part("gpu_authoring_warmup_brush");if(!raster_.prepare())throw std::runtime_error("GPU brush shader preparation failed");}
  {performance::Scope part("gpu_authoring_warmup_smudge");if(!payloads_.prepare())throw std::runtime_error("GPU brush shader preparation failed");}
  performance::Scope copy_timing("gpu_authoring_warmup_copy_owner");
  Texture target(GPU_texture_create_2d("BTS authoring warmup",64,64,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
  if(!target)throw std::bad_alloc();copy(target.get(),zero(),0);
 }
 bool write(blender::gpu::Texture *target,const PaintPixels &pixels,int channel,std::uint32_t number,performance::Clock::time_point deadline=performance::Clock::time_point::max()){
  performance::Scope timing("gpu_authoring_page");auto saved_pins=pinned_;struct Restore{std::set<Key> &pins;std::set<Key> saved;~Restore(){pins=std::move(saved);}} restore{pinned_,std::move(saved_pins)};
  try{auto *source=page(pixels,channel,number,deadline);if(!source){pinned_.clear();return false;}copy(target,source,number);pinned_.clear();return true;}
  catch(...){pinned_.clear();throw;}
 }
 std::shared_ptr<GpuSmudgePayload::Entry> capture(const std::shared_ptr<const SmudgePayload> &source,int channel,performance::Clock::time_point deadline=performance::Clock::time_point::max()){
  auto saved_pins=pinned_;struct Restore{std::set<Key> &pins;std::set<Key> saved;~Restore(){pins=std::move(saved);}} restore{pinned_,std::move(saved_pins)};try{auto result=payloads_.acquire(source,channel,[&](const PaintPixels &p,int c,std::uint32_t n,auto until){return page(p,c,n,until);},deadline);pinned_.clear();return result;}catch(...){pinned_.clear();throw;}
 }
 std::uint64_t capture_staged_pages()const{return payloads_.staged_pages();}
 std::uint64_t ribbon_image_allocations()const{return ribbon_paths_.image_allocations();}
 std::uint64_t coverage_allocations()const{return coverage_allocations_;}
 std::uint64_t evicted_pages()const{return evicted_;}
 std::uint64_t image_allocations()const{return allocations_;}
 std::uint64_t image_reuses()const{return reuses_;}
 std::uint64_t computed_pages()const{return computed_;}
 std::uint64_t source_upload_bytes()const{return uploaded_;}
 std::size_t resident_pages()const{return pages_.size();}
  std::size_t retire_unused(int plane,const std::set<std::tuple<std::uint64_t,std::uint32_t,int>> &keep){
   std::size_t retired=0;
   for(auto it=pages_.begin();it!=pages_.end();){if(std::get<2>(it->first)!=plane||keep.contains(it->first)||pinned_.contains(it->first)){++it;continue;}lru_.erase(it->second.position);it=pages_.erase(it);++retired;}
   return retired;
  }
};
}
