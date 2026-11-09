// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#include "EvaluationScope.hh"
#include "PaintPixels.hh"
#include "HistoryPageStorage.hh"
#include "PaintRecipe.hh"
#include "BrushFootprintBounds.hh"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "QuickMask.hh"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#if defined(__linux__)
#include <execinfo.h>
#endif
namespace bts {
namespace {
std::atomic<std::size_t> live_pages=0;
std::atomic<std::uint64_t> hashed_bytes=0;
std::atomic<std::uint64_t> next_page_version=1;
std::atomic<std::uint64_t> materialized_recipes=0;
thread_local std::vector<const std::map<const PaintRecipe *,PaintPixels> *> evaluation_scratch;
constexpr std::size_t page_bytes=PaintPixels::page_side*PaintPixels::page_side*sizeof(PaintPixels::Pixel);
std::uint32_t page_key(int x,int y) {return std::uint32_t(y/PaintPixels::page_side)*65536u+std::uint32_t(x/PaintPixels::page_side);}
std::array<float,3> value(const MaterialValues &v,int channel) {
  switch(channel) {
    case 0:return {v.base_color[0],v.base_color[1],v.base_color[2]};
    case 1:return {v.metallic,0,0};
    case 2:return {v.roughness,0,0};
    case 3:return v.normal;
    default:return {v.bump,0,0};
  }
}
}
struct PaintPixels::Page {
  HistoryPageStorage pixels;
  mutable std::atomic<std::uint64_t> gpu_version{0};
  std::uint64_t version() const {
    auto current=gpu_version.load(std::memory_order_relaxed);if(current)return current;
    const auto fresh=next_page_version.fetch_add(1,std::memory_order_relaxed);std::uint64_t expected=0;
    return gpu_version.compare_exchange_strong(expected,fresh,std::memory_order_relaxed)?fresh:expected;
  }
  Page() {++live_pages;}
  Page(const Page &other):Page() {pixels=other.pixels;}
  ~Page() {--live_pages;}
};
std::uint64_t allocated_history_bytes(){return history_pages::state().live_pages.load()*page_bytes;}
bool request_history_spill(std::uint64_t budget){return history_pages::request_spill(budget);}
bool history_spill_pending(){auto &s=history_pages::state();std::lock_guard lock(s.mutex);return s.job.valid();}
std::optional<HistorySpillResult> poll_history_spill(){return history_pages::collect();}
std::optional<HistorySpillResult> wait_history_spill(){
 auto &s=history_pages::state();std::future<HistorySpillResult> job;
 {std::lock_guard lock(s.mutex);if(s.job.valid())job=std::move(s.job);else return std::exchange(s.completed,std::nullopt);}
 return job.get();
}
PaintRecipe::~PaintRecipe(){
 // Iterative release also keeps a cancelled long stroke off the C++ call stack.
 static thread_local std::vector<std::shared_ptr<const PaintRecipe>> retired;
 static thread_local std::vector<std::shared_ptr<const PaintRasterSource>> retired_sources;
 static thread_local bool draining=false;
 for(auto *input:{&base,&before,&ink})if(input->recipe_)retired.push_back(std::move(input->recipe_));
 if(source)retired_sources.push_back(std::move(source));
 if(draining)return;draining=true;while(!retired.empty()||!retired_sources.empty()){
  if(!retired.empty()){auto next=std::move(retired.back());retired.pop_back();next.reset();}
  else {auto next=std::move(retired_sources.back());retired_sources.pop_back();next.reset();}
 }draining=false;
}
const PaintPixels &PaintRecipe::evaluated()const {
 if(cpu_paint_evaluation_forbidden)throw std::logic_error("GPU persistence requires an authored-pixel readback; CPU paint evaluation is forbidden");
 for(auto i=evaluation_scratch.rbegin();i!=evaluation_scratch.rend();++i)if(auto found=(*i)->find(this);found!=(*i)->end())return found->second;
 if(ready_.load(std::memory_order_acquire))return *pixels_;
#if defined(__linux__)
 if(record_gpu_paint&&std::getenv("BTS_TRACE_GPU_CPU_READ")){
  void *frames[24];const int count=backtrace(frames,24);
  std::fprintf(stderr,"BTS_GPU_AUTHORING_CPU_TRACE\n");
  backtrace_symbols_fd(frames,count,2);
 }
#endif
 // Cache only the requested snapshot, not every dab's intermediate bitmap.
 // Dependencies live in this evaluation's scratch DAG and are released after
 // their last consumer. This keeps a long stroke proportional to its footprint.
 std::call_once(once_,[this]{
  PaintRecordingScope cpu(false);
  std::map<const PaintRecipe *,std::size_t> uses;
  std::vector<const PaintRecipe *> discover{this};uses[this]=1;
  for(std::size_t i=0;i<discover.size();++i){const auto *node=discover[i];
   if(node->ready_.load(std::memory_order_acquire))continue;
   for(const auto *input:node->cpu_inputs())if(input->recipe()){
    const auto *dependency=input->recipe().get();auto [it,fresh]=uses.try_emplace(dependency,0);++it->second;if(fresh)discover.push_back(dependency);
   }
  }
  std::map<const PaintRecipe *,PaintPixels> scratch;
  evaluation_scratch.push_back(&scratch);struct ScratchScope{~ScratchScope(){evaluation_scratch.pop_back();}} scratch_scope;
  std::vector<const PaintRecipe *> pending{this};
  const auto resolved=[&](const PaintPixels &input)->PaintPixels{
   if(!input.recipe())return input;
   const auto *node=input.recipe().get();
   if(node->ready_.load(std::memory_order_acquire))return *node->pixels_;
   return scratch.at(node);
  };
  while(!pending.empty()){
   const auto *node=pending.back();
   if(node->ready_.load(std::memory_order_acquire)||scratch.contains(node)){pending.pop_back();continue;}
   bool waiting=false;for(const auto *input:node->cpu_inputs())if(input->recipe()){
    const auto *dependency=input->recipe().get();if(!dependency->ready_.load(std::memory_order_acquire)&&!scratch.contains(dependency)){pending.push_back(dependency);waiting=true;break;}
   }
   if(waiting)continue;
   auto result=resolved(node->base);
   if(node->kind==Kind::Source){
    result=node->source->evaluate_cpu();
    if(result.recipe()||(result.resolution()!=0&&result.resolution()!=node->resolution))
      throw std::logic_error("Procedural paint source returned a deferred or mismatched raster");
    const auto actual=result.page_versions();
    for(int c=0;c<5;++c)for(const auto &[key,version]:actual[c])
      if(!node->versions[c].contains(key))throw std::logic_error("Procedural paint source exceeded its declared pages");
   }
   else if(node->kind==Kind::Stamp)result.stamp(node->resolution,node->stamp);
   else if(node->kind==Kind::ClearChannel)result.clear_channel(node->cleared_channel);
   else if(node->kind==Kind::Mix)result.mix_stroke(resolved(node->before),resolved(node->ink),node->opacity,node->bounds);
   else if(node->kind==Kind::Difference)result=PaintPixels::difference(resolved(node->before),resolved(node->ink),node->erase);
   else result.over_stroke(resolved(node->before),resolved(node->ink),node->opacity,node->bounds,node->erase,node->replace);
   scratch.emplace(node,std::move(result));materialized_recipes.fetch_add(1,std::memory_order_relaxed);++local_paint_materializations;
   for(const auto *input:node->cpu_inputs())if(input->recipe()&&--uses.at(input->recipe().get())==0)scratch.erase(input->recipe().get());
   pending.pop_back();
  }
  pixels_=std::make_unique<PaintPixels>(std::move(scratch.at(this)));
  ready_.store(true,std::memory_order_release);
 });return *pixels_;
}
// CPU export/reprojection boundary: resolve a whole history DAG once. Calling
// evaluated() separately for each difference raster repeatedly replayed every
// earlier smudge/clone stroke, giving quadratic work and temporary COW traffic.
std::vector<PaintPixels> evaluate_paint_batch(std::span<const PaintPixels> inputs){
 if(cpu_paint_evaluation_forbidden)throw std::logic_error("GPU persistence forbids CPU history evaluation");
 PaintRecordingScope cpu(false);
 std::map<const PaintRecipe *,std::size_t> uses;
 std::vector<const PaintRecipe *> discover,pending;
 for(const auto &input:inputs)if(input.recipe()){
  const auto *node=input.recipe().get();auto [found,fresh]=uses.try_emplace(node,0);++found->second;if(fresh){discover.push_back(node);pending.push_back(node);}
 }
 for(std::size_t i=0;i<discover.size();++i){const auto *node=discover[i];if(node->ready_.load(std::memory_order_acquire))continue;
  for(const auto *input:node->cpu_inputs())if(input->recipe()){const auto *dependency=input->recipe().get();auto [found,fresh]=uses.try_emplace(dependency,0);++found->second;if(fresh)discover.push_back(dependency);}
 }
 std::map<const PaintRecipe *,PaintPixels> scratch;
 evaluation_scratch.push_back(&scratch);struct ScratchScope{~ScratchScope(){evaluation_scratch.pop_back();}} scope;
 const auto resolved=[&](const PaintPixels &input)->PaintPixels{if(!input.recipe())return input;const auto *node=input.recipe().get();return node->ready_.load(std::memory_order_acquire)?*node->pixels_:scratch.at(node);};
 while(!pending.empty()){
  const auto *node=pending.back();if(node->ready_.load(std::memory_order_acquire)||scratch.contains(node)){pending.pop_back();continue;}
  bool waiting=false;for(const auto *input:node->cpu_inputs())if(input->recipe()){const auto *dependency=input->recipe().get();if(!dependency->ready_.load(std::memory_order_acquire)&&!scratch.contains(dependency)){pending.push_back(dependency);waiting=true;break;}}
  if(waiting)continue;
  auto result=resolved(node->base);
  if(node->kind==PaintRecipe::Kind::Source){result=node->source->evaluate_cpu();if(result.recipe()||(result.resolution()!=0&&result.resolution()!=node->resolution))throw std::logic_error("Invalid history raster source");const auto actual=result.page_versions();for(int c=0;c<5;++c)for(const auto &[key,version]:actual[c])if(!node->versions[c].contains(key))throw std::logic_error("History source exceeded declared pages");}
  else if(node->kind==PaintRecipe::Kind::Stamp)result.stamp(node->resolution,node->stamp);
  else if(node->kind==PaintRecipe::Kind::ClearChannel)result.clear_channel(node->cleared_channel);
  else if(node->kind==PaintRecipe::Kind::Mix)result.mix_stroke(resolved(node->before),resolved(node->ink),node->opacity,node->bounds);
  else if(node->kind==PaintRecipe::Kind::Difference)result=PaintPixels::difference(resolved(node->before),resolved(node->ink),node->erase);
  else result.over_stroke(resolved(node->before),resolved(node->ink),node->opacity,node->bounds,node->erase,node->replace);
  scratch.emplace(node,std::move(result));++materialized_recipes;++local_paint_materializations;
  for(const auto *input:node->cpu_inputs())if(input->recipe()&&--uses.at(input->recipe().get())==0)scratch.erase(input->recipe().get());
  pending.pop_back();
 }
 std::vector<PaintPixels> output;output.reserve(inputs.size());for(const auto &input:inputs)output.push_back(resolved(input));return output;
}
PaintPixels PaintPixels::materialized()const{return recipe_?recipe_->evaluated():*this;}
std::uint64_t PaintPixels::recipe_materializations(){return materialized_recipes.load(std::memory_order_relaxed);}
void PaintPixels::clear_channel(int channel)
{
  if(channel<0||channel>=5)throw std::invalid_argument("Invalid paint channel");
  if(recipe_){
    if(recipe_->versions[channel].empty())return;
    auto next=std::make_shared<PaintRecipe>();next->kind=PaintRecipe::Kind::ClearChannel;
    next->base=*this;next->resolution=resolution_;next->cleared_channel=channel;
    next->revision=next_page_version.fetch_add(1,std::memory_order_relaxed);
    next->versions=recipe_->versions;next->versions[channel].clear();
    if(std::all_of(next->versions.begin(),next->versions.end(),[](const auto &pages){return pages.empty();})){*this=PaintPixels{};return;}
    recipe_=std::move(next);
  }
  else{
    channels_[channel].clear();
    if(std::all_of(channels_.begin(),channels_.end(),[](const auto &pages){return pages.empty();})){*this=PaintPixels{};return;}
  }
  hash_cache_[channel].reset();
}
PaintPixels::PageVersions PaintPixels::page_versions()const {
 if(recipe_)return recipe_->versions;PageVersions result;
 for(int c=0;c<5;++c)for(const auto &[key,page]:channels_[c])result[c][key]=page->version();return result;
}
void PaintPixels::visit_page_versions(int channel,const std::function<void(std::uint32_t,std::uint64_t)> &visit)const {
 if(recipe_){for(const auto &[key,version]:recipe_->versions.at(channel))visit(key,version);return;}
 for(const auto &[key,page]:channels_.at(channel))visit(key,page->version());
}
std::uint64_t PaintPixels::page_version(int channel,std::uint32_t key)const {
 if(recipe_){const auto &pages=recipe_->versions.at(channel);const auto found=pages.find(key);return found==pages.end()?0:found->second;}
 const auto &pages=channels_.at(channel);const auto found=pages.find(key);return found==pages.end()?0:found->second->version();
}
std::span<const PaintPixels::Pixel> PaintPixels::page_data(int channel,std::uint32_t key)const {
 if(recipe_)return recipe_->evaluated().page_data(channel,key);
 const auto &pages=channels_.at(channel);const auto found=pages.find(key);return found==pages.end()?std::span<const Pixel>{}:found->second->pixels;
}
std::uint64_t PaintPixels::hash_bytes(){return hashed_bytes.load(std::memory_order_relaxed);}
std::size_t PaintPixels::resident_bytes() {return live_pages.load()*page_bytes;}
std::size_t PaintPixels::page_count(int channel) const {if(recipe_)return paint_metadata_only||edit_storage_identity?recipe_->versions.at(channel).size():recipe_->evaluated().page_count(channel);return channels_.at(channel).size();}
bool PaintPixels::empty() const {if(recipe_){if(!paint_metadata_only&&!edit_storage_identity)return recipe_->evaluated().empty();for(const auto &pages:recipe_->versions)if(!pages.empty())return false;return true;}for (const auto &pages:channels_) if (!pages.empty()) return false;return true;}
PaintPixels::Pixel PaintPixels::sample(int channel,int x,int y) const {
  if(recipe_)return recipe_->evaluated().sample(channel,x,y);
  if (x<0 || y<0 || x>=resolution_ || y>=resolution_) return {};
  const auto &pages=channels_.at(channel);auto it=pages.find(page_key(x,y));
  return it==pages.end() ? Pixel{} : it->second->pixels[(y%page_side)*page_side+x%page_side];
}
PaintPixels::Pixel PaintPixels::sample_resampled(int channel,int x,int y,int width,int height) const {
  if(recipe_)return recipe_->evaluated().sample_resampled(channel,x,y,width,height);
  if(!height)height=width;
  if(width<=0||height<=0||x<0||y<0||x>=width||y>=height||resolution_<=0||channels_.at(channel).empty())return {};
  if(width==resolution_&&height==resolution_)return sample(channel,x,y);
  // Separable area reduction and bilinear enlargement in associated linear values.
  struct Axis {int start,count;double fraction;bool area;};
  const auto axis=[&](int coordinate,int output){if(!fast_preview_sampling&&resolution_>=output&&resolution_%output==0)return Axis{coordinate*(resolution_/output),resolution_/output,0,true};const double source=(coordinate+.5)*resolution_/output-.5;const int first=int(std::floor(source));return Axis{first,2,source-first,false};};
  const auto u=axis(x,width),v=axis(y,height);std::array<double,4> sum{};
  for(int dy=0;dy<v.count;++dy)for(int dx=0;dx<u.count;++dx){const auto pixel=sample(channel,std::clamp(u.start+dx,0,resolution_-1),std::clamp(v.start+dy,0,resolution_-1));const double weight=(u.area?1.0/u.count:dx?u.fraction:1-u.fraction)*(v.area?1.0/v.count:dy?v.fraction:1-v.fraction);for(int c=0;c<4;++c)sum[c]+=pixel[c]*weight;}
  Pixel result;for(int c=0;c<4;++c)result[c]=float(sum[c]);return result;
}
PaintPixels::Pixel &PaintPixels::writable(int channel,int x,int y) {
  hash_cache_.at(channel).reset();
  auto &page=channels_.at(channel)[page_key(x,y)];
  if (!page) page=std::make_shared<Page>();
  else if (!(page.use_count() == 1)) page=std::make_shared<Page>(*page);
  page->gpu_version.store(0,std::memory_order_relaxed);
  return page->pixels[(y%page_side)*page_side+x%page_side];
}
PaintPixels PaintPixels::from_source(int resolution,std::shared_ptr<const PaintRasterSource> source,
                                    const PageVersions &declared_pages)
{
  if(!source||!valid_sparse_texture_extent(resolution))
    throw std::invalid_argument("Invalid procedural paint source or resolution");
  auto recipe=std::make_shared<PaintRecipe>();
  recipe->kind=PaintRecipe::Kind::Source;recipe->source=std::move(source);
  recipe->resolution=resolution;recipe->revision=next_page_version.fetch_add(1,std::memory_order_relaxed);
  for(const auto *input:recipe->source->cpu_inputs())
    if(!input)throw std::invalid_argument("Missing procedural source dependency");
  for(int c=0;c<5;++c)for(const auto &[key,ignored]:declared_pages[c]){
    const int x=int(key%65536)*page_side,y=int(key/65536)*page_side;
    if(x>=resolution||y>=resolution)throw std::invalid_argument("Procedural source page exceeds resolution");
    const Bounds page{x,y,std::min(x+page_side,resolution),std::min(y+page_side,resolution)};
    if(recipe->bounds.empty())recipe->bounds=page;
    else recipe->bounds={std::min(recipe->bounds.x0,page.x0),std::min(recipe->bounds.y0,page.y0),
                         std::max(recipe->bounds.x1,page.x1),std::max(recipe->bounds.y1,page.y1)};
    recipe->versions[c][key]=recipe->revision;
  }
  if(recipe->bounds.empty())return {};
  PaintPixels result;result.resolution_=resolution;result.recipe_=std::move(recipe);return result;
}
PaintPixels::Bounds PaintPixels::stamp(int resolution,const Stamp &stamp) {
  if(stamp.alpha.kind==AlphaKind::DynamicOutline)stamp.alpha.dynamic.validate();
  if(stamp.cloned){if(stamp.smudge||stamp.projected)throw std::invalid_argument("Clone has one sampled source");stamp.cloned->validate();}
  if(stamp.smudge){if(stamp.projected)throw std::invalid_argument("Smudge and material projection are distinct sources");stamp.smudge->validate();}
  if(stamp.stencil)stamp.stencil->stencil.validate();
  if(stamp.projected){stamp.projected->source.validate();if(stamp.projected->scalar_override&&(!std::isfinite(*stamp.projected->scalar_override)||std::abs(*stamp.projected->scalar_override)>65504))throw std::invalid_argument("Invalid projected scalar");}
  if (!valid_sparse_texture_extent(resolution) || (resolution_ && resolution_!=resolution))
    throw std::invalid_argument("Paint resolution mismatch or unrepresentable sparse extent");
  for (const auto &v:{stamp.center,stamp.axis_x,stamp.axis_y}) for (double x:v)
    if (!std::isfinite(x)) throw std::invalid_argument("Nonfinite brush footprint");
  if (!std::isfinite(stamp.flow) || stamp.flow<0 || stamp.flow>1 || !std::isfinite(stamp.hardness) || stamp.hardness<0 || stamp.hardness>1)
    throw std::invalid_argument("Invalid brush flow or hardness");
  for (int channel=0;channel<5;++channel) for (float x:value(stamp.values,channel))
    if (!std::isfinite(x) || std::abs(x)>65504) throw std::invalid_argument("Nonfinite brush channel value");
  if(record_gpu_paint&&gpu_basic_brush_supported(stamp)){
    if(stamp.cloned){bool any=false;for(int c=0;c<5;++c)if(stamp.channels[c])any|=stamp.cloned->has_ink(c);if(!any)return {};}
    if(stamp.coverage_mask&&stamp.coverage_mask->inverted&&stamp.coverage_mask->pixels.empty())return {};
    if(stamp.surface){const auto &surface=*stamp.surface;if(!std::isfinite(surface.radius)||surface.radius<=0)throw std::invalid_argument("Invalid surface footprint");for(double v:surface.center)if(!std::isfinite(v))throw std::invalid_argument("Invalid surface center");for(const auto &tri:surface.triangles){for(const auto &uv:tri.uv)for(double v:uv)if(!std::isfinite(v))throw std::invalid_argument("Invalid surface UV");for(const auto &p:tri.position)for(double v:p)if(!std::isfinite(v))throw std::invalid_argument("Invalid surface vertex");}}
    const auto regions=stamp.surface?surface_brush_regions(resolution,stamp):std::vector<Bounds>{brush_footprint_bounds(resolution,stamp)};
    Bounds bounds{resolution,resolution,0,0};std::set<std::uint32_t> dirty_pages;
    for(const auto &b:regions){if(b.empty())continue;bounds={std::min(bounds.x0,b.x0),std::min(bounds.y0,b.y0),std::max(bounds.x1,b.x1),std::max(bounds.y1,b.y1)};for(int y=b.y0/page_side;y<=(b.y1-1)/page_side;++y)for(int x=b.x0/page_side;x<=(b.x1-1)/page_side;++x)dirty_pages.insert(std::uint32_t(y)*65536u+x);}
    if(bounds.empty()||std::none_of(stamp.channels.begin(),stamp.channels.end(),[](bool on){return on;}))return {};
    auto recipe=std::make_shared<PaintRecipe>();recipe->base=*this;recipe->stamp=stamp;recipe->bounds=bounds;recipe->resolution=resolution;recipe->revision=next_page_version.fetch_add(1,std::memory_order_relaxed);recipe->versions=page_versions();
    for(int c=0;c<5;++c)if(stamp.channels[c])for(auto key:dirty_pages)recipe->versions[c][key]=recipe->revision;
    recipe_=std::move(recipe);channels_={};hash_cache_={};resolution_=resolution;return bounds;
  }
  if(record_gpu_paint)std::fprintf(stderr,"BTS_BRUSH_CPU_FALLBACK reason=unsupported-stamp alpha=%d fractional_mask_offset=%d\n",int(stamp.alpha.kind),int(bool(stamp.coverage_mask)));
  if(recipe_)*this=materialized();
  if(stamp.surface)return stamp_surface(resolution,stamp);
  const double det=stamp.axis_x[0]*stamp.axis_y[1]-stamp.axis_y[0]*stamp.axis_x[1];
  if(!std::isfinite(det))throw std::overflow_error("Brush transform exceeds double arithmetic");
  if (std::abs(det)<1e-16 || stamp.flow==0 || std::none_of(stamp.channels.begin(),stamp.channels.end(),[](bool v){return v;})) return {};
  const bool round=stamp.alpha.kind==AlphaKind::Shape;
  const double rx=round?std::hypot(stamp.axis_x[0],stamp.axis_y[0]):std::abs(stamp.axis_x[0])+std::abs(stamp.axis_y[0]);
  const double ry=round?std::hypot(stamp.axis_x[1],stamp.axis_y[1]):std::abs(stamp.axis_x[1])+std::abs(stamp.axis_y[1]);
  const auto clamp=[resolution](double v){return int(std::clamp(v,0.0,double(resolution)));};
  Bounds box{clamp(std::floor((stamp.center[0]-rx)*resolution)),clamp(std::floor((stamp.center[1]-ry)*resolution)),
             clamp(std::ceil((stamp.center[0]+rx)*resolution)),clamp(std::ceil((stamp.center[1]+ry)*resolution))};
  if (box.empty()) return {};
  // Every accepted stamp writes the caller's working copy. Publish only when its transaction
  // succeeds; callers can discard a partially allocated candidate on allocation failure.
  const auto pages=resource_product((box.x1-1)/page_side-box.x0/page_side+1,(box.y1-1)/page_side-box.y0/page_side+1);

  resolution_=resolution;
  Bounds touched{resolution,resolution,0,0};
  for (int y=box.y0;y<box.y1;++y) for (int x=box.x0;x<box.x1;++x) {
    const double du=(x+.5)/resolution-stamp.center[0],dv=(y+.5)/resolution-stamp.center[1];
    const double u=(du*stamp.axis_y[1]-dv*stamp.axis_y[0])/det;
    const double v=(dv*stamp.axis_x[0]-du*stamp.axis_x[1])/det;
    if(!std::isfinite(u)||!std::isfinite(v))continue;
    const auto projected=stamp.projected?stamp.projected->sample_uv({(x+.5)/resolution+stamp.stencil_uv_offset[0],(y+.5)/resolution+stamp.stencil_uv_offset[1]}):std::optional<MaterialValues>{};
    if(stamp.projected&&!projected)continue;
    const float stencil=stamp.stencil?stamp.stencil->sample_uv({(x+.5)/resolution+stamp.stencil_uv_offset[0],(y+.5)/resolution+stamp.stencil_uv_offset[1]}):1;
    const float alpha=stamp.alpha.sample(u,v,stamp.hardness)*stamp.flow*stencil*(stamp.coverage_mask?stamp.coverage_mask->sample_uv((x+.5)/resolution+stamp.stencil_uv_offset[0],(y+.5)/resolution+stamp.stencil_uv_offset[1],resolution):1);if (alpha<=1e-8f) continue;
    bool wrote=false;
    for (int channel=0;channel<5;++channel) if (stamp.channels[channel]) {
      if(stamp.smudge){const auto before=sample(channel,x,y),source=stamp.smudge->sample(channel,u,v);Pixel after;for(int c=0;c<4;++c)after[c]=std::lerp(before[c],source[c],alpha);if(after[3]<=1e-8f)after={};if(after==before)continue;writable(channel,x,y)=after;}
      else if(stamp.cloned){const auto source=stamp.cloned->sample(channel,u,v);if(source[3]<=1e-8f)continue;auto &pixel=writable(channel,x,y);for(int c=0;c<3;++c)pixel[c]=source[c]*alpha+pixel[c]*(1-source[3]*alpha);pixel[3]=source[3]*alpha+pixel[3]*(1-source[3]*alpha);}
      else {auto &pixel=writable(channel,x,y);const auto source=value(projected?*projected:stamp.values,channel);for(int c=0;c<3;++c)pixel[c]=source[c]*alpha+pixel[c]*(1-alpha);pixel[3]=alpha+pixel[3]*(1-alpha);}
      wrote=true;
    }
    if(!wrote)continue;
    touched.x0=std::min(touched.x0,x);touched.y0=std::min(touched.y0,y);
    touched.x1=std::max(touched.x1,x+1);touched.y1=std::max(touched.y1,y+1);
  }
  if(stamp.smudge)prune_empty(touched);
  return touched.empty() ? Bounds{} : touched;
}
PaintPixels::Bounds PaintPixels::stamp_surface(int resolution,const Stamp &stamp)
{
  const auto &surface=*stamp.surface;
  if(!std::isfinite(surface.radius)||surface.radius<=0)
    throw std::invalid_argument("Invalid surface footprint");
  for(double v:surface.center)if(!std::isfinite(v))throw std::invalid_argument("Invalid surface center");
  if(stamp.flow==0||std::none_of(stamp.channels.begin(),stamp.channels.end(),[](bool v){return v;}))return {};
  constexpr int padding=2;
  const auto box_for=[&](const SurfaceTriangle &tri) {
    double u0=1e100,v0=u0,u1=-u0,v1=-u0;
    for(int i=0;i<3;++i) {
      for(double v:tri.uv[i])if(!std::isfinite(v))throw std::invalid_argument("Invalid surface UV");
      for(double v:tri.position[i])if(!std::isfinite(v))throw std::invalid_argument("Invalid surface vertex");
      u0=std::min(u0,tri.uv[i][0]);u1=std::max(u1,tri.uv[i][0]);v0=std::min(v0,tri.uv[i][1]);v1=std::max(v1,tri.uv[i][1]);
    }
    const auto clip=[&](double x){return int(std::clamp(x,0.,double(resolution)));};
    return Bounds{clip(std::floor(u0*resolution)-padding),clip(std::floor(v0*resolution)-padding),clip(std::ceil(u1*resolution)+padding),clip(std::ceil(v1*resolution)+padding)};
  };
  Bounds bounds{resolution,resolution,0,0};std::uint64_t work=0;
  for(const auto &triangle:surface.triangles) {
    const auto b=box_for(triangle);if(b.empty())continue;
    work=resource_sum(work,resource_product(b.x1-b.x0,b.y1-b.y0));

    bounds.x0=std::min(bounds.x0,b.x0);bounds.y0=std::min(bounds.y0,b.y0);bounds.x1=std::max(bounds.x1,b.x1);bounds.y1=std::max(bounds.y1,b.y1);
  }
  if(bounds.empty())return {};
  const int width=bounds.x1-bounds.x0;
  const auto texels=resource_product(width,bounds.y1-bounds.y0);
  const auto scratch_per_texel=sizeof(float)+((stamp.projected||stamp.smudge||stamp.cloned)?sizeof(std::array<float,2>):0);
  const auto pages=resource_product((bounds.x1-1)/page_side-bounds.x0/page_side+1,(bounds.y1-1)/page_side-bounds.y0/page_side+1);
  const auto page_storage=resource_product(resource_product(pages,std::count(stamp.channels.begin(),stamp.channels.end(),true)),page_bytes);

  std::vector<float> coverage(std::size_t(texels),0);
  std::vector<std::array<float,2>> source_coordinates;if(stamp.projected||stamp.smudge||stamp.cloned)source_coordinates.resize(coverage.size());
  for(const auto &tri:surface.triangles) {
    if(surface.cull&&!accepts_brush_normal(tri.normal,surface.view_direction,surface.cull_angle))continue;
    const auto b=box_for(tri);const auto &a=tri.uv[0],&p=tri.uv[1],&q=tri.uv[2];
    const double ux=p[0]-a[0],uy=p[1]-a[1],vx=q[0]-a[0],vy=q[1]-a[1],det=ux*vy-uy*vx;
    if(!std::isfinite(det))throw std::overflow_error("Surface UV transform exceeds double arithmetic");
    if(std::abs(det)<1e-20)continue;
    for(int y=b.y0;y<b.y1;++y)for(int x=b.x0;x<b.x1;++x) {
      const double u=(x+.5)/resolution,v=(y+.5)/resolution;
      double w1=((u-a[0])*vy-(v-a[1])*vx)/det,w2=(ux*(v-a[1])-uy*(u-a[0]))/det;
      std::array<double,3> weights{1-w1-w2,w1,w2};
      if(*std::min_element(weights.begin(),weights.end()) < -1e-9) {
        double nearest=(double(padding)*padding)/(double(resolution)*resolution);bool found=false;
        for(int edge=0;edge<3;++edge)if(tri.seam_edges[edge]) {
          const int next=(edge+1)%3;const auto &s=tri.uv[edge],&e=tri.uv[next];
          const double dx=e[0]-s[0],dy=e[1]-s[1],length=dx*dx+dy*dy;if(length<1e-20)continue;
          const double t=std::clamp(((u-s[0])*dx+(v-s[1])*dy)/length,0.,1.);
          const double du=u-s[0]-t*dx,dv=v-s[1]-t*dy,distance=du*du+dv*dv;
          if(distance<=nearest){nearest=distance;weights={};weights[edge]=1-t;weights[next]=t;found=true;}
        }
        if(!found)continue;
      }
      std::array<double,3> stencil_position{};
      double projected_u=0,projected_v=0,depth=0;std::array<double,3> normalized_delta{};
      for(int axis=0;axis<3;++axis){double position=0;for(int i=0;i<3;++i)position+=weights[i]*tri.position[i][axis];if(!std::isfinite(position))throw std::overflow_error("Surface position exceeds double arithmetic");stencil_position[axis]=position;const double difference=position-surface.center[axis];const double d=std::isfinite(difference)?difference/surface.radius:position/surface.radius-surface.center[axis]/surface.radius;normalized_delta[axis]=d;projected_u+=d*surface.tangent[axis];projected_v+=d*surface.bitangent[axis];depth+=d*surface.normal[axis];}
      if(surface.unwrapped){projected_u=projected_v=0;for(int i=0;i<3;++i){projected_u+=weights[i]*tri.paint_coordinates[i][0]/surface.radius;projected_v+=weights[i]*tri.paint_coordinates[i][1]/surface.radius;}}
      if(!std::isfinite(projected_u)||!std::isfinite(projected_v)||!std::isfinite(depth))continue;
      if(std::abs(depth)>1&&(!surface.unwrapped||tri.bounded_depth))continue;
      float value=stamp.alpha.sample(projected_u,projected_v,stamp.hardness);
      // Existing caller-provided footprints without a projection keep their radial
      // world-space behavior; native Wrap uses geodesic unfolding instead.
      if(!surface.projected&&stamp.alpha.kind==AlphaKind::Shape)value=stamp.alpha.sample(std::hypot(normalized_delta[0],normalized_delta[1],normalized_delta[2]),0,stamp.hardness);
      if(stamp.stencil)value*=stamp.stencil->sample_world(stencil_position);
      if(surface.planar_fade)value*=float(std::clamp(1-std::abs(depth),0.,1.));
      const auto index=std::size_t(y-bounds.y0)*width+x-bounds.x0;auto &alpha=coverage[index];
      if(stamp.projected&&value>0){const auto screen=stamp.projected->mapping.screen_world(stencil_position);if(!screen||!stamp.projected->source.transform.coordinates(*screen))continue;if(value>=alpha){for(double coordinate:*screen)if(!std::isfinite(coordinate)||std::abs(coordinate)>std::numeric_limits<float>::max())throw std::overflow_error("Projected source coordinate exceeds float storage");source_coordinates[index]={float((*screen)[0]),float((*screen)[1])};}}
      if((stamp.smudge||stamp.cloned)&&value>0&&value>=alpha){if(std::max(std::abs(projected_u),std::abs(projected_v))>std::numeric_limits<float>::max())throw std::overflow_error("Brush source coordinate exceeds float storage");source_coordinates[index]={float(projected_u),float(projected_v)};}
      alpha=std::max(alpha,value); // Shared UV edges receive this stamp only once.
    }
  }
  resolution_=resolution;Bounds touched{resolution,resolution,0,0};
  for(int y=bounds.y0;y<bounds.y1;++y)for(int x=bounds.x0;x<bounds.x1;++x) {
    const auto index=std::size_t(y-bounds.y0)*width+x-bounds.x0;
    const float alpha=coverage[index]*stamp.flow*(stamp.coverage_mask?stamp.coverage_mask->sample_uv((x+.5)/resolution+stamp.stencil_uv_offset[0],(y+.5)/resolution+stamp.stencil_uv_offset[1],resolution):1);if(alpha<=1e-8f)continue;
    auto projected=std::optional<MaterialValues>{};if(stamp.projected){const auto point=source_coordinates[index];projected=stamp.projected->sample({point[0],point[1]});if(!projected)continue;}
    bool wrote=false;
    for(int c=0;c<5;++c)if(stamp.channels[c]) {
      if(stamp.smudge){const auto before=sample(c,x,y);const auto point=source_coordinates[index];const auto source=stamp.smudge->sample(c,point[0],point[1]);Pixel after;for(int component=0;component<4;++component)after[component]=std::lerp(before[component],source[component],alpha);if(after[3]<=1e-8f)after={};if(after==before)continue;writable(c,x,y)=after;}
      else if(stamp.cloned){const auto point=source_coordinates[index];const auto source=stamp.cloned->sample(c,point[0],point[1]);if(source[3]<=1e-8f)continue;auto &pixel=writable(c,x,y);for(int component=0;component<3;++component)pixel[component]=source[component]*alpha+pixel[component]*(1-source[3]*alpha);pixel[3]=source[3]*alpha+pixel[3]*(1-source[3]*alpha);}
      else {auto &pixel=writable(c,x,y);const auto source=value(projected?*projected:stamp.values,c);for(int component=0;component<3;++component)pixel[component]=source[component]*alpha+pixel[component]*(1-alpha);pixel[3]=alpha+pixel[3]*(1-alpha);}
      wrote=true;
    }
    if(!wrote)continue;
    touched.x0=std::min(touched.x0,x);touched.y0=std::min(touched.y0,y);touched.x1=std::max(touched.x1,x+1);touched.y1=std::max(touched.y1,y+1);
  }
  if(stamp.smudge)prune_empty(touched);
  return touched.empty()?Bounds{}:touched;
}

void PaintPixels::prune_empty(Bounds touched){
  if(touched.empty())return;
  for(auto &cached:hash_cache_)cached.reset();
  for(auto &pages:channels_)std::erase_if(pages,[&](const auto &entry){const int px=int(entry.first%65536)*page_side,py=int(entry.first/65536)*page_side;if(px>=touched.x1||px+page_side<=touched.x0||py>=touched.y1||py+page_side<=touched.y0)return false;return std::all_of(entry.second->pixels.begin(),entry.second->pixels.end(),[](const Pixel &pixel){return pixel[3]==0;});});
}
PaintPixels PaintPixels::difference(const PaintPixels &before,const PaintPixels &after,bool erase){
  const int resolution=after.resolution_?after.resolution_:before.resolution_;
  if(before.resolution_&&after.resolution_&&before.resolution_!=after.resolution_)throw std::invalid_argument("Difference resolution mismatch");
  auto recipe=std::make_shared<PaintRecipe>();recipe->kind=PaintRecipe::Kind::Difference;recipe->before=before;recipe->ink=after;recipe->resolution=resolution;recipe->erase=erase;recipe->revision=next_page_version.fetch_add(1,std::memory_order_relaxed);
  const auto a=before.page_versions(),b=after.page_versions();
  for(int c=0;c<5;++c){auto keys=a[c];keys.insert(b[c].begin(),b[c].end());for(const auto &[key,version]:keys)if(before.page_version(c,key)!=after.page_version(c,key))recipe->versions[c][key]=recipe->revision;}
  PaintPixels result;bool any=false;for(const auto &channel:recipe->versions)any|=!channel.empty();if(!any)return result;
  if(record_gpu_paint){result.resolution_=resolution;result.recipe_=std::move(recipe);return result;}
  for(int c=0;c<5;++c)for(const auto &[key,version]:recipe->versions[c]){
    std::array<Pixel,page_side*page_side> pixels{};bool covered=false;
    for(int y=0;y<page_side;++y)for(int x=0;x<page_side;++x){const int px=int(key%65536)*page_side+x,py=int(key/65536)*page_side+y;const auto old=before.sample(c,px,py),value=after.sample(c,px,py);if(old==value)continue;auto &pixel=pixels[y*page_side+x];pixel=erase?Pixel{0,0,0,1}:value;covered|=pixel[3]>0;}
    if(covered)result.assign_page(resolution,c,key,pixels);
  }return result;
}
PaintPixels::Bounds PaintPixels::mix_stroke(const PaintPixels &before,const PaintPixels &transported,float opacity,Bounds touched){
  if(record_gpu_paint){
    if(!std::isfinite(opacity)||opacity<0||opacity>1)throw std::invalid_argument("Invalid transport opacity");
    const int resolution=transported.resolution_?transported.resolution_:before.resolution_;if(!resolution||touched.empty())return {};
    if((before.resolution_&&before.resolution_!=resolution)||(resolution_&&resolution_!=resolution))throw std::invalid_argument("Transport resolution mismatch");
    touched.x0=std::clamp(touched.x0,0,resolution);touched.y0=std::clamp(touched.y0,0,resolution);touched.x1=std::clamp(touched.x1,0,resolution);touched.y1=std::clamp(touched.y1,0,resolution);if(touched.empty())return {};
    auto recipe=std::make_shared<PaintRecipe>();recipe->kind=PaintRecipe::Kind::Mix;recipe->base=*this;recipe->before=before;recipe->ink=transported;recipe->resolution=resolution;recipe->bounds=touched;recipe->opacity=opacity;recipe->complete_stroke=true;recipe->versions=page_versions();recipe->revision=next_page_version.fetch_add(1,std::memory_order_relaxed);
    const auto a=before.page_versions(),b=transported.page_versions();bool changed=false;
    for(int c=0;c<5;++c)for(int y=touched.y0/page_side;y<=(touched.y1-1)/page_side;++y)for(int x=touched.x0/page_side;x<=(touched.x1-1)/page_side;++x){const auto key=std::uint32_t(y)*65536u+x;if(!a[c].contains(key)&&!b[c].contains(key)&&!recipe->versions[c].contains(key))continue;recipe->versions[c][key]=recipe->revision;changed=true;}
    if(!changed)return {};recipe_=std::move(recipe);channels_={};hash_cache_={};resolution_=resolution;return touched;
  }
  if(recipe_||before.recipe_||transported.recipe_){auto target=materialized();const auto source=before.materialized(),ink=transported.materialized();const auto changed=target.mix_stroke(source,ink,opacity,touched);*this=std::move(target);return changed;}
  if(recipe_)*this=materialized();
  if(!std::isfinite(opacity)||opacity<0||opacity>1)throw std::invalid_argument("Invalid transport opacity");
  const int resolution=transported.resolution_?transported.resolution_:before.resolution_;
  if(!resolution||touched.empty())return {};
  if((before.resolution_&&before.resolution_!=resolution)||(resolution_&&resolution_!=resolution))throw std::invalid_argument("Transport resolution mismatch");
  auto candidate=*this;candidate.resolution_=resolution;Bounds changed{resolution,resolution,0,0};
  touched.x0=std::clamp(touched.x0,0,resolution);touched.x1=std::clamp(touched.x1,0,resolution);touched.y0=std::clamp(touched.y0,0,resolution);touched.y1=std::clamp(touched.y1,0,resolution);
  for(int c=0;c<5;++c){if(before.channels_[c].empty()&&transported.channels_[c].empty()&&candidate.channels_[c].empty())continue;for(int y=touched.y0;y<touched.y1;++y)for(int x=touched.x0;x<touched.x1;++x){const auto a=before.sample(c,x,y),b=transported.sample(c,x,y);Pixel after;for(int component=0;component<4;++component)after[component]=std::lerp(a[component],b[component],opacity);if(after[3]<=1e-8f)after={};if(after==sample(c,x,y))continue;candidate.writable(c,x,y)=after;changed.x0=std::min(changed.x0,x);changed.y0=std::min(changed.y0,y);changed.x1=std::max(changed.x1,x+1);changed.y1=std::max(changed.y1,y+1);}}
  candidate.prune_empty(touched);*this=std::move(candidate);return changed.empty()?Bounds{}:changed;
}

PaintPixels::Bounds PaintPixels::over_stroke(const PaintPixels &before,const PaintPixels &stroke,float opacity,Bounds touched,bool erase,bool replace,bool complete_stroke)
{
  if(!std::isfinite(opacity)||opacity<0||opacity>1)throw std::invalid_argument("Invalid whole-stroke opacity");
  if(!record_gpu_paint&&(recipe_||before.recipe_||stroke.recipe_)){auto target=materialized();const auto source=before.materialized(),ink=stroke.materialized();const auto changed=target.over_stroke(source,ink,opacity,touched,erase,replace);*this=std::move(target);return changed;}
  const int resolution=stroke.resolution_?stroke.resolution_:before.resolution_?before.resolution_:resolution_;
  if(!resolution||(!replace&&stroke.empty())||opacity==0||touched.empty())return {};
  if((before.resolution_&&before.resolution_!=resolution)||(resolution_&&resolution_!=resolution))
    throw std::invalid_argument("Stroke composition resolution mismatch");
  touched.x0=std::clamp(touched.x0,0,resolution);touched.y0=std::clamp(touched.y0,0,resolution);
  touched.x1=std::clamp(touched.x1,0,resolution);touched.y1=std::clamp(touched.y1,0,resolution);
  if(record_gpu_paint){
    auto recipe=std::make_shared<PaintRecipe>();recipe->kind=PaintRecipe::Kind::Over;recipe->base=*this;recipe->before=before;recipe->ink=stroke;recipe->bounds=touched;recipe->resolution=resolution;recipe->opacity=opacity;recipe->erase=erase;recipe->replace=replace;recipe->complete_stroke=complete_stroke&&replace;recipe->versions=page_versions();recipe->revision=next_page_version.fetch_add(1,std::memory_order_relaxed);
    const auto source=stroke.page_versions(),basis=before.page_versions();bool changed=false;
    for(int c=0;c<5;++c){if(source[c].empty()&&(!replace||recipe->versions[c]==basis[c]))continue;for(int y=touched.y0/page_side;y<=(touched.y1-1)/page_side;++y)for(int x=touched.x0/page_side;x<=(touched.x1-1)/page_side;++x){const auto key=std::uint32_t(y)*65536u+x;if(!source[c].contains(key)&&!basis[c].contains(key)&&!recipe->versions[c].contains(key))continue;recipe->versions[c][key]=recipe->revision;changed=true;}}
    if(!changed)return {};recipe_=std::move(recipe);channels_={};hash_cache_={};resolution_=resolution;return touched;
  }
  if(recipe_)*this=materialized();
  PaintPixels candidate=*this;candidate.resolution_=resolution;
  Bounds changed{resolution,resolution,0,0};
  for(int c=0;c<5;++c) {
    if(stroke.channels_[c].empty()&&(!replace||channels_[c]==before.channels_[c]))continue;
    for(int y=touched.y0;y<touched.y1;++y)for(int x=touched.x0;x<touched.x1;++x) {
      const auto source=stroke.sample(c,x,y);if(!replace&&source[3]<=0)continue;
      const auto base=before.sample(c,x,y);Pixel combined;
      const float alpha=source[3]*opacity;
      for(int component=0;component<4;++component)combined[component]=(erase?0:source[component]*opacity)+base[component]*(1-alpha);
      if(combined[3]==0)combined={};
      if(combined==sample(c,x,y))continue;
      candidate.writable(c,x,y)=combined;
      changed.x0=std::min(changed.x0,x);changed.y0=std::min(changed.y0,y);
      changed.x1=std::max(changed.x1,x+1);changed.y1=std::max(changed.y1,y+1);
    }
  }
  if(erase||replace)candidate.prune_empty(touched);
  *this=std::move(candidate);return changed.empty()?Bounds{}:changed;
}
void PaintPixels::assign_channel(int resolution,int channel,std::span<const float> rgba) {
  if(recipe_)*this=materialized();
  if (channel<0 || channel>=5 || !valid_sparse_texture_extent(resolution) || (resolution_ && resolution_!=resolution) ||
      rgba.size()!=resource_product(texture_texel_count(resolution,resolution),4)) throw std::invalid_argument("Packed paint channel dimensions are invalid");
  std::uint64_t covered_pages=0;
  for(int py=0;py<resolution;py+=page_side)for(int px=0;px<resolution;px+=page_side){
    bool covered=false;for(int y=py;y<std::min(py+page_side,resolution)&&!covered;++y)for(int x=px;x<std::min(px+page_side,resolution);++x)if(rgba[(std::size_t(y)*resolution+x)*4+3]!=0){covered=true;break;}
    if(covered)covered_pages=resource_sum(covered_pages,1);
  }

  PaintPixels candidate=*this;candidate.channels_[channel].clear();candidate.hash_cache_[channel].reset();candidate.resolution_=resolution;
  for(int y=0;y<resolution;++y)for(int x=0;x<resolution;++x) {
    const auto offset=(std::size_t(y)*resolution+x)*4;
    const float alpha=rgba[offset+3];
    if(!std::isfinite(alpha)||alpha<0||alpha>1)throw std::invalid_argument("Packed paint coverage is outside 0..1");
    for(int c=0;c<3;++c)
      if(!std::isfinite(rgba[offset+c])||std::abs(rgba[offset+c])>65504)throw std::invalid_argument("Packed paint value is invalid");
    if(alpha==0) {
      for(int c=0;c<3;++c)if(rgba[offset+c]!=0)throw std::invalid_argument("Uncovered paint pixel contains associated values");
      continue;
    }
    auto &pixel=candidate.writable(channel,x,y);
    std::copy_n(rgba.data()+offset,4,pixel.begin());
  }
  *this=std::move(candidate);
}
void PaintPixels::visit_versioned_pages(int channel,const std::function<void(std::uint32_t,std::uint64_t,std::span<const Pixel>)> &visit)const {if(recipe_){recipe_->evaluated().visit_versioned_pages(channel,visit);return;}for(const auto &[key,page]:channels_.at(channel))visit(key,page->version(),page->pixels);}
void PaintPixels::visit_pages(const std::function<void(int,std::uint32_t,std::span<const Pixel>)> &visit) const {if(recipe_){recipe_->evaluated().visit_pages(visit);return;}for(int c=0;c<5;++c)for(const auto &[key,page]:channels_[c])visit(c,key,page->pixels);}
void PaintPixels::assign_page(int resolution,int channel,std::uint32_t key,std::span<const Pixel> pixels){
  if(recipe_)*this=materialized();
  const int px=int(key%65536)*page_side,py=int(key/65536)*page_side;if(channel<0||channel>=5||!valid_sparse_texture_extent(resolution,128,true)||(resolution_&&resolution_!=resolution)||px>=resolution||py>=resolution||pixels.size()!=page_side*page_side||channels_[channel].contains(key))throw std::invalid_argument("Invalid sparse stroke page");
  bool covered=false;for(const auto &pixel:pixels){if(!std::isfinite(pixel[3])||pixel[3]<0||pixel[3]>1)throw std::invalid_argument("Invalid sparse stroke coverage");for(int c=0;c<3;++c)if(!std::isfinite(pixel[c])||std::abs(pixel[c])>65504||(pixel[3]==0&&pixel[c]!=0))throw std::invalid_argument("Invalid sparse stroke value");covered|=pixel[3]>0;}
  if(!covered)throw std::invalid_argument("Empty sparse stroke page");auto candidate=*this;candidate.resolution_=resolution;for(int y=0;y<page_side;++y)for(int x=0;x<page_side;++x){const auto &pixel=pixels[y*page_side+x];if(pixel[3]>0)candidate.writable(channel,px+x,py+y)=pixel;}*this=std::move(candidate);
}
std::uint64_t PaintPixels::hash(int channel) const {
  if(recipe_){if(edit_storage_identity){std::uint64_t identity=14695981039346656037ull;for(const auto &[page,version]:recipe_->versions.at(channel)){identity^=page;identity*=1099511628211ull;identity^=version;identity*=1099511628211ull;}return identity;}return recipe_->evaluated().hash(channel);}
  const auto &pages=channels_.at(channel);
  if(edit_storage_identity){std::uint64_t identity=14695981039346656037ull;const auto add=[&](std::uint64_t value){identity^=value;identity*=1099511628211ull;};if(!pages.empty())add(resolution_);for(const auto &[key,page]:pages){add(key);add(reinterpret_cast<std::uintptr_t>(page.get()));}return identity;}
  if(hash_cache_.at(channel))return *hash_cache_[channel];
  hashed_bytes.fetch_add(pages.size()*page_bytes,std::memory_order_relaxed);
  std::uint64_t hash=14695981039346656037ull;
  const auto add=[&](std::uint32_t bits){for (int i=0;i<4;++i) {hash^=(bits>>(8*i))&255;hash*=1099511628211ull;}};
  if (!channels_.at(channel).empty()) add(std::uint32_t(resolution_));
  for (const auto &[key,page]:channels_.at(channel)) {add(key);for (const auto &pixel:page->pixels) for(float value:pixel) add(std::bit_cast<std::uint32_t>(value));}
  hash_cache_[channel]=hash;return hash;
}
}
