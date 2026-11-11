// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Performance.hh"
#include "TextureMemory.hh"
#include "GpuReadbackFence.hh"
#include "GPU_texture.hh"
#include "GPU_texture_mip_range.hh"
#include "GPU_shader.hh"
#include "GPU_compute.hh"
#include "GPU_state.hh"
#include "gpu_shader_create_info.hh"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
namespace bts {
// Metal persistence adapter: freeze a revision entirely on the GPU and stream
// RGBA float data through four bounded SSBOs. Owner-context use only. The CPU
// encoder sees only completed arrays, never textures or mapped GPU allocations.
class GpuProjectReadback {
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Buffer=std::unique_ptr<blender::gpu::StorageBuf,decltype(&blender::GPU_storagebuf_free)>;
 public:
 struct Image {
  std::uint32_t uid=0;int tile=1001,width=0,height=0,origin_x=0,origin_y=0,next_row=0,completed_rows=0;
  blender::gpu::Texture *retained=nullptr;
  Texture snapshot{nullptr,blender::GPU_texture_free};
  bool uniform=false,uniform_candidate=true,uniform_first=false;std::array<float,4> uniform_pixel{};std::unique_ptr<float[]> storage;const float *pixels=nullptr;
  std::size_t bytes()const{return std::size_t(width)*height*4*sizeof(float);}
 };
 private:
 static constexpr std::size_t packet_bytes=4ULL*1024*1024;
 struct Packet {
  Buffer buffer{nullptr,blender::GPU_storagebuf_free};GpuReadbackFence fence;
  std::size_t image=0;int row=0,rows=0;
 };
 std::vector<Image> images_;std::array<Packet,4> packets_{};
 std::unique_ptr<float[]> packet_cpu_;
 inline static std::array<blender::gpu::Shader *,2> copy_programs_{};
 inline static blender::gpu::Shader *readback_program_=nullptr;
 inline static std::map<blender::gpu::Texture *,std::size_t> pins_;
 static void release_source(Image &image){
  if(!image.retained)return;
  auto found=pins_.find(image.retained);if(found!=pins_.end()&&!--found->second)pins_.erase(found);
  blender::GPU_texture_free(image.retained);image.retained=nullptr;
 }
 static blender::gpu::Shader *program(bool snapshot,bool array=false){
  using namespace blender;using namespace gpu::shader;
  auto &shader=snapshot?copy_programs_[array]:readback_program_;
  if(shader)return shader;
  ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16);
  info.sampler(0,array?ImageType::Float2DArray:ImageType::Float2D,"source_img")
      .push_constant(Type::int3_t,"source_offset").push_constant(Type::int2_t,"capture_size");
  if(snapshot)info.image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"result_img");
  else info.storage_buf(0,Qualifier::write,"vec4","pixels[]");
  info.compute_source_generated=std::string("void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,capture_size)))return;vec4 v=texelFetch(source_img,")+
   (array?"ivec3(p+source_offset.xy,source_offset.z)":"p+source_offset.xy")+",0);"+
   (snapshot?"imageStore(result_img,p,v);}":"pixels[p.y*capture_size.x+p.x]=v;}");
  shader=GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
  if(!shader)throw std::runtime_error("Metal project readback shader failed");return shader;
 }
 static void dispatch(blender::gpu::Shader *shader,blender::gpu::Texture *source,int x,int y,int layer,int width,int height){
  using namespace blender;GPU_shader_bind(shader);GPU_texture_bind(source,0);
  const int offset[3]={x,y,layer},size[2]={width,height};
  GPU_shader_uniform_3iv(shader,"source_offset",offset);GPU_shader_uniform_2iv(shader,"capture_size",size);
  GPU_compute_dispatch(shader,(width+15)/16,(height+15)/16,1);
  GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE|GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();
 }
 static void validate(blender::gpu::Texture *texture,int x,int y,int layer,int width,int height){
  using namespace blender;
  if(!texture||width<1||height<1||x<0||y<0||layer<0||
     width>GPU_texture_width(texture)||height>GPU_texture_height(texture)||
     x>GPU_texture_width(texture)-width||y>GPU_texture_height(texture)-height||
     layer>=std::max(1,GPU_texture_layer_count(texture))||std::size_t(width)*16>packet_bytes)
   throw std::invalid_argument("Invalid Metal project readback region");
 }
 static Texture level_zero(blender::gpu::Texture *texture){
  using namespace blender;
  Texture view(GPU_texture_create_view("Metal project level zero",texture,GPU_texture_format(texture),0,1,0,std::max(1,GPU_texture_layer_count(texture)),false,false),GPU_texture_free);
  if(!view)throw std::runtime_error("Metal project texture view failed");return view;
 }
 void submit_impl(std::uint32_t uid,int tile,blender::gpu::Texture *texture,int x,int y,int layer,int width,int height,bool native){
  using namespace blender;validate(texture,x,y,layer,width,height);
  Image image;image.uid=uid;image.tile=tile;image.width=width;image.height=height;
  image.storage=std::make_unique_for_overwrite<float[]>(std::size_t(width)*height*4);image.pixels=image.storage.get();
  // Snapshot even for native input on Metal: a single 2D staging allocation
  // avoids array-view dimensionality assumptions and mutable atlas aliases.
  // Pin native sources so callers still observe the established COW contract.
  auto view=level_zero(texture);
  image.snapshot=Texture(GPU_texture_create_2d("Metal immutable project revision",width,height,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
  if(!image.snapshot)throw std::bad_alloc();
  auto *shader=program(true,GPU_texture_is_array(texture));GPU_texture_image_bind(image.snapshot.get(),0);
  dispatch(shader,view.get(),x,y,layer,width,height);
  if(native){++pins_[texture];GPU_texture_ref(texture);image.retained=texture;}
  try{images_.push_back(std::move(image));}catch(...){release_source(image);throw;}
 }
 public:
 GpuProjectReadback()=default;
 GpuProjectReadback(const GpuProjectReadback &)=delete;
 ~GpuProjectReadback(){clear();}
 static void warmup(){program(true);program(true,true);program(false);}
 static void free_program(){
  for(auto &p:copy_programs_){if(p)blender::GPU_shader_free(p);p=nullptr;}
  if(readback_program_)blender::GPU_shader_free(readback_program_);readback_program_=nullptr;
 }
 void clear(){
  for(auto &packet:packets_){packet.fence.reset();packet.buffer.reset();}
  for(auto &image:images_){image.snapshot.reset();release_source(image);}images_.clear();packet_cpu_.reset();
 }
 static bool protect_for_write(blender::gpu::Texture **slot){
  using namespace blender;if(!slot||!*slot||!pins_.contains(*slot))return false;auto *before=*slot;
  const auto format=GPU_texture_format(before);const auto usage=GPU_TEXTURE_USAGE_GENERAL;
  auto *after=GPU_texture_is_array(before)?GPU_texture_create_2d_array("BTS live channel COW",GPU_texture_width(before),GPU_texture_height(before),GPU_texture_layer_count(before),GPU_texture_mip_count(before),format,usage,nullptr):GPU_texture_create_2d("BTS live channel COW",GPU_texture_width(before),GPU_texture_height(before),GPU_texture_mip_count(before),format,usage,nullptr);
  if(!after){texture_memory_refresh_needed=true;throw std::bad_alloc();}
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_copy(after,before);GPU_texture_filter_mode(after,true);GPU_texture_mipmap_mode(after,true,true);GPU_texture_anisotropic_filter(after,true);GPU_texture_mipmap_range(after,0,0);*slot=after;GPU_texture_free(before);
  std::printf("BTS_SAVE_GPU_CHANNEL_COW width=%d height=%d layers=%d cpu_copy=0\n",GPU_texture_width(after),GPU_texture_height(after),GPU_texture_layer_count(after));return true;
 }
 void submit(std::uint32_t uid,int tile,blender::gpu::Texture *texture,int x,int y,int layer,int width,int height){submit_impl(uid,tile,texture,x,y,layer,width,height,false);}
 void submit_native(std::uint32_t uid,int tile,blender::gpu::Texture *texture,int x,int y,int layer,int width,int height){submit_impl(uid,tile,texture,x,y,layer,width,height,true);}
 bool poll(){
  using namespace blender;const auto start=performance::Clock::now();
  for(auto &packet:packets_)if(packet.fence){
   if(!packet_cpu_)packet_cpu_=std::make_unique_for_overwrite<float[]>(packet_bytes/sizeof(float));
   if(!packet.fence.read(packet.buffer.get(),packet_cpu_.get()))continue;
   auto &image=images_.at(packet.image);const auto bytes=std::size_t(packet.rows)*image.width*16;
   std::memcpy(image.storage.get()+std::size_t(packet.row)*image.width*4,packet_cpu_.get(),bytes);
   image.completed_rows+=packet.rows;
   // Preserve the GL adapter's uniform-image contract. Project IO uses it
   // for disabled 1x1 native placeholders whose logical image remains larger.
   // Compare completed packet bytes exactly (including signed zero/NaN bits),
   // never revisit the mutable live texture or change its frozen revision.
   if(image.uniform_candidate){
    if(!image.uniform_first){std::memcpy(image.uniform_pixel.data(),packet_cpu_.get(),16);image.uniform_first=true;}
    for(std::size_t i=0;i<bytes/16;++i)if(std::memcmp(packet_cpu_.get()+i*4,image.uniform_pixel.data(),16)!=0){image.uniform_candidate=false;break;}
   }
   if(image.completed_rows==image.height){
    image.snapshot.reset();release_source(image);
    if(image.uniform_candidate){
     auto compact=std::make_unique_for_overwrite<float[]>(4);
     std::memcpy(compact.get(),image.uniform_pixel.data(),16);
     image.storage=std::move(compact);image.pixels=image.storage.get();image.uniform=true;
    }
   }
   if(performance::ms(start)>=3)return false;
  }
  for(auto &packet:packets_)if(!packet.fence){
   auto found=std::find_if(images_.begin(),images_.end(),[](const auto &i){return i.next_row<i.height;});if(found==images_.end())break;
   if(!packet.buffer)packet.buffer=Buffer(GPU_storagebuf_create_ex(packet_bytes,nullptr,GPU_USAGE_DYNAMIC,"Metal project packet"),GPU_storagebuf_free);
   if(!packet.buffer)throw std::bad_alloc();
   packet.image=found-images_.begin();packet.row=found->next_row;
   packet.rows=std::min(found->height-found->next_row,int(packet_bytes/(std::size_t(found->width)*16)));
   auto *shader=program(false);GPU_storagebuf_bind(packet.buffer.get(),0);
   dispatch(shader,found->snapshot.get(),0,packet.row,0,found->width,packet.rows);
   GPU_storagebuf_unbind(packet.buffer.get());packet.fence.submit(packet.buffer.get());found->next_row+=packet.rows;
   if(performance::ms(start)>=3)return false;
  }
  return std::all_of(images_.begin(),images_.end(),[](const auto &i){return i.completed_rows==i.height;});
 }
 void release_cpu(){for(auto &image:images_){image.storage.reset();image.pixels=nullptr;}}
 const std::vector<Image> &images()const{return images_;}
};
}
