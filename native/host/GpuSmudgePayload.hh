#pragma once
#include "GpuBrushRaster.hh"
#include "SmudgeCapture.hh"
#include "SmudgeFootprintBounds.hh"
#include "TextureMemory.hh"
#include <functional>
#include <set>
namespace bts {
class GpuPaintPages;
// Small brush-space captures backed by the same immutable GPU pages as Undo.
// An atlas is temporary GPU-to-GPU staging; no source pixels cross the CPU.
class GpuSmudgePayload {
 friend class GpuPaintPages;
 friend int verify_gpu_authoring_workers(GpuPaintPages &);
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 struct Page {int tile;std::uint32_t key;const PaintPixels *pixels;};
 struct Gather {Texture atlas{nullptr,blender::GPU_texture_free},table{nullptr,blender::GPU_texture_free};std::vector<Page> pages;std::size_t copied=0;int columns=1;};
 public:
 struct Entry {
  std::shared_ptr<const SmudgePayload> source;
  Texture contact{nullptr,blender::GPU_texture_free};
  std::array<Texture,5> channels{{{nullptr,blender::GPU_texture_free},{nullptr,blender::GPU_texture_free},{nullptr,blender::GPU_texture_free},{nullptr,blender::GPU_texture_free},{nullptr,blender::GPU_texture_free}}};
  std::array<std::unique_ptr<Gather>,5> gathering;
  std::uint64_t used=0;
 };
 using AdjustmentResolver=std::function<blender::gpu::Texture *(const SmudgeCapture &,int,int,std::uint32_t,blender::gpu::Texture *,performance::Clock::time_point)>;
 AdjustmentResolver adjustment_resolver;
 using Resolver=std::function<blender::gpu::Texture *(const PaintPixels &,int,std::uint32_t,performance::Clock::time_point)>;
 private:
 std::map<const SmudgePayload *,std::shared_ptr<Entry>> entries_;
 GpuBrushRaster raster_;
 Shader gather_shader_{nullptr,blender::GPU_shader_free},sample_shader_{nullptr,blender::GPU_shader_free},carry_shader_{nullptr,blender::GPU_shader_free};
 std::uint64_t clock_=0,captures_=0,carried_=0,staged_pages_=0;
 static std::uint64_t bytes(const Texture &image){return image?std::uint64_t(blender::GPU_texture_width(image.get()))*blender::GPU_texture_height(image.get())*16:0;}
 std::uint64_t owned()const{std::uint64_t result=0;for(const auto &[key,entry]:entries_){result+=bytes(entry->contact);for(const auto &image:entry->channels)result+=bytes(image);for(const auto &g:entry->gathering)if(g)result+=bytes(g->atlas)+bytes(g->table);}return result;}
 void reserve(std::uint64_t requested){
  const auto free=texture_free_vram.load(),budget=resource_sum(owned(),(texture_vram_query_valid.load()||free!=0)?free:texture_available_ram_bytes());
  while(owned()+requested>budget){auto oldest=entries_.end();for(auto i=entries_.begin();i!=entries_.end();++i)if(i->second.use_count()==1&&(oldest==entries_.end()||i->second->used<oldest->second->used))oldest=i;if(oldest==entries_.end())break;entries_.erase(oldest);}
 }
 Texture image(const char *label,int width,int height,const float *data=nullptr){using namespace blender;reserve(std::uint64_t(width)*height*16);Texture result(GPU_texture_create_2d(label,width,height,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,data),GPU_texture_free);if(!result)throw std::bad_alloc();return result;}
 static blender::gpu::shader::ShaderCreateInfo program_info(int kind){using namespace blender;using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"result_img");
  if(kind==0){info.sampler(0,ImageType::Float2D,"source_img").push_constant(Type::int2_t,"destination");info.compute_source_generated=R"GLSL(
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);imageStore(result_img,p+destination,texelFetch(source_img,p,0));}
)GLSL";}
  else if(kind==1){info.sampler(0,ImageType::Float2D,"contact_img").sampler(1,ImageType::Float2D,"atlas_img").sampler(2,ImageType::Float2D,"table_img").push_constant(Type::int_t,"paint_size").push_constant(Type::int_t,"page_count").push_constant(Type::int_t,"atlas_columns");info.compute_source_generated=R"GLSL(
ivec3 page_entry(int index){ivec2 size=textureSize(table_img,0);return ivec3(texelFetch(table_img,ivec2(index%size.x,index/size.x),0).xyz);}
vec4 source_pixel(ivec2 p){
 ivec2 tile=ivec2(floor(vec2(p)/float(paint_size)));if(tile.x<0||tile.x>=10||tile.y<0||tile.y>=100)return vec4(0);
 ivec2 local=p-tile*paint_size,number=local/64;int tile_id=1001+tile.x+10*tile.y,key=number.y*65536+number.x;
 int lo=0,hi=page_count;while(lo<hi){int mid=(lo+hi)/2;ivec3 entry=page_entry(mid);if(entry.x<tile_id||(entry.x==tile_id&&entry.y<key))lo=mid+1;else hi=mid;}
 if(lo>=page_count)return vec4(0);ivec3 entry=page_entry(lo);if(entry.x!=tile_id||entry.y!=key)return vec4(0);
 return texelFetch(atlas_img,ivec2(entry.z%atlas_columns,entry.z/atlas_columns)*64+local%64,0);
}
void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(result_img))))return;
 vec4 contact=texelFetch(contact_img,p,0);vec4 result_value=vec4(0);
 if(contact.a!=0){vec2 point=contact.xy*float(paint_size)-.5,f=fract(point);ivec2 at=ivec2(floor(point));result_value=mix(mix(source_pixel(at),source_pixel(at+ivec2(1,0)),f.x),mix(source_pixel(at+ivec2(0,1)),source_pixel(at+ivec2(1)),f.x),f.y);}
 imageStore(result_img,p,result_value);
}
)GLSL";}
  else {info.sampler(0,ImageType::Float2D,"current_img").sampler(1,ImageType::Float2D,"previous_img").sampler(2,ImageType::Float2D,"contact_img").push_constant(Type::float_t,"pickup");info.compute_source_generated=R"GLSL(
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(result_img);if(any(greaterThanEqual(p,size)))return;
 ivec2 previous_size=textureSize(previous_img,0);vec2 location=(vec2(p)+.5)/vec2(size)*vec2(previous_size)-.5,f=fract(location);ivec2 at=ivec2(floor(location));
 vec4 a=texelFetch(previous_img,clamp(at,ivec2(0),previous_size-1),0),b=texelFetch(previous_img,clamp(at+ivec2(1,0),ivec2(0),previous_size-1),0),c=texelFetch(previous_img,clamp(at+ivec2(0,1),ivec2(0),previous_size-1),0),d=texelFetch(previous_img,clamp(at+ivec2(1),ivec2(0),previous_size-1),0);
 vec4 old=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);float weight=pickup*texelFetch(contact_img,p,0).z;
 imageStore(result_img,p,mix(old,texelFetch(current_img,p,0),weight));}
)GLSL";}
  return info;
 }
 static Shader compile(int kind){trace_authoring_compilation();auto info=program_info(kind);gpu_derived::stabilize_hosted_resource_owners(info);return Shader(blender::GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),blender::GPU_shader_free);}
 static void finish(blender::gpu::Shader *shader,blender::gpu::Texture *target,int width,int height){using namespace blender;GPU_texture_image_bind(target,0);GPU_compute_dispatch(shader,(width+15)/16,(height+15)/16,1);trace_authoring_dispatch(shader);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS|GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_image_unbind(target);GPU_shader_unbind();}
 void prepare_gather(Entry &entry,int channel,const SmudgeCapture &capture){
  auto pending=std::make_unique<Gather>();const auto bounds=smudge_source_bounds(capture.footprint);const int resolution=capture.resolution;
  std::set<int> source_tiles;for(const auto &[tile,pixels]:capture.source.tiles())source_tiles.insert(tile);if(capture.adjustment.enabled(channel))source_tiles.insert(capture.adjustment.snapshot->tiles.begin(),capture.adjustment.snapshot->tiles.end());
  if(capture.footprint.channels[channel])for(int tile:source_tiles){const auto &pixels=capture.source.tile(tile);
   const int u=(tile-1001)%10,v=(tile-1001)/10;const auto versions=pixels.page_versions();std::set<std::uint32_t> wanted;
   for(const auto &b:bounds){
    int x0=int(std::floor((b[0]-u)*resolution-.5)),y0=int(std::floor((b[1]-v)*resolution-.5));
    int x1=int(std::floor((b[2]-u)*resolution-.5))+1,y1=int(std::floor((b[3]-v)*resolution-.5))+1;
    if(x1<0||y1<0||x0>=resolution||y0>=resolution)continue;
    x0=std::max(0,x0)/64;y0=std::max(0,y0)/64;x1=std::min(resolution-1,x1)/64;y1=std::min(resolution-1,y1)/64;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){const auto key=std::uint32_t(y)*65536u+x;if(capture.adjustment.enabled(channel)||versions[channel].contains(key))wanted.insert(key);}
   }
   for(auto key:wanted)pending->pages.push_back({tile,key,&pixels});
  }
  const int count=int(pending->pages.size()),maximum=blender::GPU_max_texture_size();pending->columns=std::max(1,int(std::ceil(std::sqrt(double(count)))));const int rows=std::max(1,(count+pending->columns-1)/pending->columns);
  if(pending->columns*64>maximum||rows*64>maximum)throw std::length_error("Clone/Smudge footprint exceeds GPU atlas dimensions");
  entry.gathering[channel]=std::move(pending);auto &g=*entry.gathering[channel];g.atlas=image("BTS temporary source pages",g.columns*64,rows*64);
  const int width=std::min(maximum,std::max(1,count)),height=std::max(1,(count+width-1)/width);std::vector<std::array<float,4>> table(std::size_t(width)*height);
  for(int i=0;i<count;++i)table[i]={float(g.pages[i].tile),float(g.pages[i].key),float(i),0};g.table=image("BTS source page coordinates",width,height,table.front().data());
 }
 public:
 bool prepare(){if(!gather_shader_)gather_shader_=compile(0);if(!sample_shader_)sample_shader_=compile(1);if(!carry_shader_)carry_shader_=compile(2);return gather_shader_&&sample_shader_&&carry_shader_&&raster_.prepare();}
 std::shared_ptr<Entry> acquire(const std::shared_ptr<const SmudgePayload> &source,int channel,const Resolver &resolve,performance::Clock::time_point deadline){
  performance::Scope timing("gpu_source_capture");
  using namespace blender;if(!source)return {};if(!prepare())throw std::runtime_error("GPU Clone/Smudge capture shader failed");
  auto &slot=entries_[source.get()];if(!slot){slot=std::make_shared<Entry>();slot->source=source;}auto entry=slot;entry->used=++clock_;if(entry->channels[channel])return entry;
  if(performance::Clock::now()>=deadline)return {};
  const int side=source->side;
  if(!source->recipe){if(!entry->contact){std::vector<std::array<float,4>> contact(side*side);for(std::size_t i=0;i<source->pickup.size();++i)contact.at(i)={0,0,source->pickup[i],1};entry->contact=image("BTS explicit CPU pickup",side,side,contact.front().data());}const auto &pixels=source->channels[channel];std::vector<std::array<float,4>> empty;if(pixels.empty())empty.resize(side*side);entry->channels[channel]=image("BTS explicit CPU source capture",side,side,pixels.empty()?empty.front().data():pixels.front().data());return entry;}
  if(source->recipe->kind==SmudgePayloadRecipe::Kind::Capture){
   const auto &capture=static_cast<const SmudgeCapture &>(*source->recipe);
   if(!entry->contact){entry->contact=image("BTS GPU brush contact",side,side);if(!raster_.contact(entry->contact.get(),capture.footprint,capture.resolution))throw std::runtime_error("GPU Clone/Smudge contact failed");}
   if(!entry->gathering[channel])prepare_gather(*entry,channel,capture);auto &g=*entry->gathering[channel];
   while(g.copied<g.pages.size()){
    if(performance::Clock::now()>=deadline)return {};const auto &page=g.pages[g.copied];auto *pixels=resolve(*page.pixels,channel,page.key,deadline);if(!pixels)return {};
    if(capture.adjustment.enabled(channel)){if(!adjustment_resolver)throw std::logic_error("Adjustment GPU source resolver missing");pixels=adjustment_resolver(capture,channel,page.tile,page.key,pixels,deadline);if(!pixels)return {};}
    const int origin[2]={int(g.copied%g.columns)*64,int(g.copied/g.columns)*64};GPU_shader_bind(gather_shader_.get());GPU_shader_uniform_2iv(gather_shader_.get(),"destination",origin);GPU_texture_bind(pixels,0);finish(gather_shader_.get(),g.atlas.get(),64,64);++g.copied;++staged_pages_;
   }
   entry->channels[channel]=image("BTS GPU source capture",side,side);GPU_shader_bind(sample_shader_.get());GPU_texture_bind(entry->contact.get(),0);GPU_texture_bind(g.atlas.get(),1);GPU_texture_bind(g.table.get(),2);GPU_shader_uniform_1i(sample_shader_.get(),"paint_size",capture.resolution);GPU_shader_uniform_1i(sample_shader_.get(),"page_count",int(g.pages.size()));GPU_shader_uniform_1i(sample_shader_.get(),"atlas_columns",g.columns);finish(sample_shader_.get(),entry->channels[channel].get(),side,side);entry->gathering[channel].reset();++captures_;
  }else{
   const auto &carry=static_cast<const SmudgeCarry &>(*source->recipe);auto current=acquire(carry.current,channel,resolve,deadline);if(!current)return {};auto previous=acquire(carry.previous,channel,resolve,deadline);if(!previous)return {};
   // Carry retains the current capture's pickup map for later transport.
   if(!entry->contact){entry->contact=image("BTS carried contact",side,side);GPU_texture_copy(entry->contact.get(),current->contact.get());GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_TEXTURE_UPDATE);}
   entry->channels[channel]=image("BTS GPU carried source",side,side);GPU_shader_bind(carry_shader_.get());GPU_texture_bind(current->channels[channel].get(),0);GPU_texture_bind(previous->channels[channel].get(),1);GPU_texture_bind(current->contact.get(),2);GPU_shader_uniform_1f(carry_shader_.get(),"pickup",carry.pickup);finish(carry_shader_.get(),entry->channels[channel].get(),side,side);++carried_;
  }
  return entry;
 }
 void retire_unused(){std::erase_if(entries_,[](const auto &p){return p.second.use_count()==1&&p.second->source.use_count()==1;});}
 std::uint64_t staged_pages()const{return staged_pages_;}
 std::uint64_t captures()const{return captures_;}
 std::uint64_t carried()const{return carried_;}
};
}
