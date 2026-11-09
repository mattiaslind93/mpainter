#pragma once
#ifdef __APPLE__
#include "GpuProjectReadbackMetal.hh"
#else
#include "Performance.hh"
#include "TextureMemory.hh"
#include "GPU_texture.hh"
#include "GPU_texture_mip_range.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_storage_buffer_poll.hh"
#include <epoxy/gl.h>
#include <array>
#include <cstring>
#include <memory>
#include <map>
#include <stdexcept>
#include <vector>
#include <utility>
namespace bts {
// OpenGL persistence adapter. Freeze the submitted GPU revision with a GPU
// copy, then stream it through a bounded PBO ring. Allocating a full-image PBO
// zeroed hundreds of MiB synchronously inside NVIDIA, despite asynchronous DMA.
class GpuProjectReadback {
 public:
 struct Image {
  std::uint32_t uid=0;int tile=1001,width=0,height=0,origin_x=0,origin_y=0,next_row=0,completed_rows=0;
  blender::gpu::Texture *retained=nullptr;
  GLuint snapshot=0;blender::gpu::StorageBuf *probe=nullptr;GLsync probe_fence=nullptr;bool uniform=false;std::unique_ptr<float[]> storage;const float *pixels=nullptr;
  std::size_t bytes()const{return std::size_t(width)*height*4*sizeof(float);}
 };
 private:
 static constexpr std::size_t packet_bytes=4ULL*1024*1024;
 struct Packet {GLuint buffer=0;GLsync fence=nullptr;std::size_t image=0;int row=0,rows=0;};
 inline static std::array<GLuint,4> spare_packets_{};
 static GLuint allocate_packet(){performance::Scope timing("save_gpu_packet_allocate");GLuint buffer=0;glCreateBuffers(1,&buffer);glNamedBufferData(buffer,packet_bytes,nullptr,GL_STREAM_READ);return buffer;}
 static GLuint acquire_packet(){for(auto &buffer:spare_packets_)if(buffer)return std::exchange(buffer,0u);return allocate_packet();}
 static void return_packet(GLuint &buffer){for(auto &spare:spare_packets_)if(!spare){spare=std::exchange(buffer,0u);return;}}
 std::vector<Image> images_;std::array<Packet,4> packets_{};inline static GLuint uniform_program_=0,readback_program_=0;
 inline static std::map<blender::gpu::Texture *,std::size_t> pins_;
 static void release_source(Image &image){if(!image.retained)return;auto found=pins_.find(image.retained);if(found!=pins_.end()&&!--found->second)pins_.erase(found);blender::GPU_texture_free(image.retained);image.retained=nullptr;}

 static void ensure_program(){
  if(!uniform_program_){
   const char *source=R"GLSL(#version 430
layout(local_size_x=16,local_size_y=16) in;
layout(binding=0) uniform sampler2D source_img;
uniform ivec2 source_offset,capture_size;
layout(std430,binding=0) coherent buffer Result {uint different;uint pad0,pad1,pad2;vec4 first;};
shared uint skip;shared uvec4 reference;
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(gl_LocalInvocationIndex==0u){reference=floatBitsToUint(texelFetch(source_img,source_offset,0));skip=atomicOr(different,0u);}if(all(equal(p,ivec2(0))))first=uintBitsToFloat(reference);barrier();if(skip!=0u||any(greaterThanEqual(p,capture_size)))return;if(any(notEqual(floatBitsToUint(texelFetch(source_img,p+source_offset,0)),reference)))atomicOr(different,1u);}
)GLSL";
   GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);GLint valid=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&valid);if(!valid){glDeleteShader(shader);throw std::runtime_error("Project uniform probe shader failed");}
   uniform_program_=glCreateProgram();glAttachShader(uniform_program_,shader);glLinkProgram(uniform_program_);glDeleteShader(shader);glGetProgramiv(uniform_program_,GL_LINK_STATUS,&valid);if(!valid)throw std::runtime_error("Project uniform probe program failed");
  }
  if(!readback_program_){
   const char *source=R"GLSL(#version 430
layout(local_size_x=16,local_size_y=16) in;
layout(binding=0) uniform sampler2D source_img;
uniform ivec2 source_offset,capture_size;
layout(std430,binding=0) writeonly buffer Packet {vec4 pixels[];};
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,capture_size)))return;pixels[p.y*capture_size.x+p.x]=texelFetch(source_img,source_offset+p,0);}
)GLSL";
   GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);GLint valid=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&valid);if(!valid){glDeleteShader(shader);throw std::runtime_error("Project packet copy shader failed");}
   readback_program_=glCreateProgram();glAttachShader(readback_program_,shader);glLinkProgram(readback_program_);glDeleteShader(shader);glGetProgramiv(readback_program_,GL_LINK_STATUS,&valid);if(!valid)throw std::runtime_error("Project packet copy program failed");
  }
 }
 // Texture readback entry points can synchronize the NVIDIA driver before
 // queuing DMA. Dispatch the conversion/copy in the GPU stream instead. The
 // packet is mapped only after its fence signals; no GL readback call runs here.
 void queue_packet(const Image &image,const Packet &packet){
  performance::Scope timing("save_gpu_packet_queue");
  GLint active=0,texture=0,sampler=0,program=0,storage=0,generic=0;GLint64 offset=0,size=0;
  glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegeri_v(GL_SAMPLER_BINDING,0,&sampler);glGetIntegerv(GL_CURRENT_PROGRAM,&program);GLint pending_delete=0;if(program)glGetProgramiv(program,GL_DELETE_STATUS,&pending_delete);if(pending_delete)program=0;
  glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&storage);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&generic);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,0,&offset);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,0,&size);
  glUseProgram(readback_program_);glUniform2i(glGetUniformLocation(readback_program_,"source_offset"),image.origin_x,image.origin_y+packet.row);glUniform2i(glGetUniformLocation(readback_program_,"capture_size"),image.width,packet.rows);
  glBindTexture(GL_TEXTURE_2D,image.snapshot);glBindSampler(0,0);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,packet.buffer);glDispatchCompute((image.width+15)/16,(packet.rows+15)/16,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
  if(storage&&size)glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,storage,offset,size);else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,storage);glBindBuffer(GL_SHADER_STORAGE_BUFFER,generic);glUseProgram(program);glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(0,sampler);glActiveTexture(active);
 }
 void classify(Image &image){
  ensure_program();
  GLint active=0,texture=0,sampler=0,program=0,storage=0,generic=0;GLint64 offset=0,size=0;
  glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegeri_v(GL_SAMPLER_BINDING,0,&sampler);glGetIntegerv(GL_CURRENT_PROGRAM,&program);GLint pending_delete=0;if(program)glGetProgramiv(program,GL_DELETE_STATUS,&pending_delete);if(pending_delete)program=0;glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&storage);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&generic);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,0,&offset);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,0,&size);
  const std::array<unsigned,8> empty{};image.probe=blender::GPU_storagebuf_create_ex(sizeof(empty),empty.data(),blender::GPU_USAGE_DYNAMIC,"BTS project uniform probe");
  if(!image.probe)throw std::runtime_error("Project uniform probe buffer allocation failed");
  glUseProgram(uniform_program_);glUniform2i(glGetUniformLocation(uniform_program_,"source_offset"),image.origin_x,image.origin_y);glUniform2i(glGetUniformLocation(uniform_program_,"capture_size"),image.width,image.height);glBindTexture(GL_TEXTURE_2D,image.snapshot);glBindSampler(0,0);blender::GPU_storagebuf_bind(image.probe,0);glDispatchCompute((image.width+15)/16,(image.height+15)/16,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
  blender::GPU_storagebuf_sync_to_host(image.probe);
  if(storage&&size)glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,storage,offset,size);else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,storage);glBindBuffer(GL_SHADER_STORAGE_BUFFER,generic);glUseProgram(program);glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(0,sampler);glActiveTexture(active);
  image.probe_fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!image.probe_fence)throw std::runtime_error("Project uniform probe fence failed");
 }
 public:
 static void warmup(){ensure_program();for(auto &buffer:spare_packets_)if(!buffer)buffer=allocate_packet();}
 static void free_program(){if(readback_program_)glDeleteProgram(readback_program_);readback_program_=0;if(uniform_program_)glDeleteProgram(uniform_program_);uniform_program_=0;for(auto &buffer:spare_packets_)if(buffer){glDeleteBuffers(1,&buffer);buffer=0;}}
 GpuProjectReadback()=default;
 GpuProjectReadback(const GpuProjectReadback &)=delete;
 ~GpuProjectReadback(){clear();}
 // Owner context only. Encoders may read completed CPU arrays on a worker;
 // neither live Blender IDs nor mapped GL storage is shared with that worker.
 void clear(){for(auto &packet:packets_){if(packet.buffer&&!packet.fence)return_packet(packet.buffer);if(packet.fence)glDeleteSync(packet.fence);if(packet.buffer)glDeleteBuffers(1,&packet.buffer);packet={};}for(auto &image:images_){if(image.snapshot)glDeleteTextures(1,&image.snapshot);if(image.probe)blender::GPU_storagebuf_free(image.probe);if(image.probe_fence)glDeleteSync(image.probe_fence);release_source(image);}images_.clear();}
 // Native presentation targets cooperate with this COW boundary. Retaining a
 // GPU allocation costs no full-image copy or PBO allocation during idle save.
 static bool protect_for_write(blender::gpu::Texture **slot){
  using namespace blender;if(!slot||!*slot||!pins_.contains(*slot))return false;auto *before=*slot;
  const auto format=GPU_texture_format(before);const auto usage=GPU_TEXTURE_USAGE_GENERAL;
  auto *after=GPU_texture_is_array(before)?GPU_texture_create_2d_array("BTS live channel COW",GPU_texture_width(before),GPU_texture_height(before),GPU_texture_layer_count(before),GPU_texture_mip_count(before),format,usage,nullptr):GPU_texture_create_2d("BTS live channel COW",GPU_texture_width(before),GPU_texture_height(before),GPU_texture_mip_count(before),format,usage,nullptr);
  if(!after){texture_memory_refresh_needed=true;throw std::bad_alloc();}
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_copy(after,before);GPU_texture_filter_mode(after,true);GPU_texture_mipmap_mode(after,true,true);GPU_texture_anisotropic_filter(after,true);GPU_texture_mipmap_range(after,0,0);*slot=after;GPU_texture_free(before);
  std::printf("BTS_SAVE_GPU_CHANNEL_COW width=%d height=%d layers=%d cpu_copy=0\n",GPU_texture_width(after),GPU_texture_height(after),GPU_texture_layer_count(after));return true;
 }
 void submit_native(std::uint32_t uid,int tile,blender::gpu::Texture *texture,int x,int y,int layer,int width,int height){
  performance::Scope scope("save_gpu_readback_retain");if(!texture||width<1||height<1)throw std::invalid_argument("Invalid native project readback image");
  for(GLenum error=glGetError();error!=GL_NO_ERROR;error=glGetError())std::fprintf(stderr,"BTS_GPU_READBACK_PRIOR_GL_ERROR code=%u\n",unsigned(error));
  Image image;image.uid=uid;image.tile=tile;image.width=width;image.height=height;image.origin_x=x;image.origin_y=y;image.storage=std::make_unique_for_overwrite<float[]>(std::size_t(width)*height*4);image.pixels=image.storage.get();
  GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);blender::GPU_texture_bind(texture,0);blender::GPU_apply_state();glActiveTexture(GL_TEXTURE0);GLint name=0;glGetIntegerv(blender::GPU_texture_is_array(texture)?GL_TEXTURE_BINDING_2D_ARRAY:GL_TEXTURE_BINDING_2D,&name);glActiveTexture(active);
  GLint format=0;glGetTextureLevelParameteriv(name,0,GL_TEXTURE_INTERNAL_FORMAT,&format);glGenTextures(1,&image.snapshot);glTextureView(image.snapshot,GL_TEXTURE_2D,name,format,0,1,layer,1);glTextureParameteri(image.snapshot,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTextureParameteri(image.snapshot,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTextureParameteri(image.snapshot,GL_TEXTURE_MAX_LEVEL,0);
  if(const auto error=glGetError();error!=GL_NO_ERROR){glDeleteTextures(1,&image.snapshot);throw std::runtime_error("Native project snapshot view failed: OpenGL error "+std::to_string(error));}
  blender::GPU_texture_ref(texture);image.retained=texture;++pins_[texture];
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
  try{classify(image);images_.push_back(std::move(image));}catch(...){glDeleteTextures(1,&image.snapshot);if(image.probe)blender::GPU_storagebuf_free(image.probe);if(image.probe_fence)glDeleteSync(image.probe_fence);release_source(image);throw;}glFlush();
 }
 void submit(std::uint32_t uid,int tile,blender::gpu::Texture *texture,int x,int y,int layer,int width,int height){
  performance::Scope scope("save_gpu_readback_submit");
  if(!texture||width<1||height<1)throw std::invalid_argument("Invalid project readback image");

  for(GLenum error=glGetError();error!=GL_NO_ERROR;error=glGetError())std::fprintf(stderr,"BTS_GPU_READBACK_PRIOR_GL_ERROR code=%u\n",unsigned(error));
  Image image;image.uid=uid;image.tile=tile;image.width=width;image.height=height;
  // Uninitialized allocation: page commitment is bounded by packet-sized copies
  // in poll(), not a multi-GiB memset in the snapshot request event.
  image.storage=std::make_unique_for_overwrite<float[]>(std::size_t(width)*height*4);image.pixels=image.storage.get();
  GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
  blender::GPU_texture_bind(texture,0);blender::GPU_apply_state();glActiveTexture(GL_TEXTURE0);
  const bool array=blender::GPU_texture_is_array(texture);GLint name=0;
  glGetIntegerv(array?GL_TEXTURE_BINDING_2D_ARRAY:GL_TEXTURE_BINDING_2D,&name);glActiveTexture(active);
  if(!name)throw std::runtime_error("Project readback requires an OpenGL texture binding");
  GLint format=0;glGetTextureLevelParameteriv(name,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
  glCreateTextures(GL_TEXTURE_2D,1,&image.snapshot);glTextureStorage2D(image.snapshot,1,format,width,height);
  glTextureParameteri(image.snapshot,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTextureParameteri(image.snapshot,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTextureParameteri(image.snapshot,GL_TEXTURE_MAX_LEVEL,0);
  glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
  glCopyImageSubData(name,array?GL_TEXTURE_2D_ARRAY:GL_TEXTURE_2D,0,x,y,layer,image.snapshot,GL_TEXTURE_2D,0,0,0,0,width,height,1);
  if(const auto error=glGetError();error!=GL_NO_ERROR){glDeleteTextures(1,&image.snapshot);texture_memory_refresh_needed=true;throw std::runtime_error("Project GPU snapshot failed: OpenGL error "+std::to_string(error)+" source="+std::to_string(name)+" format="+std::to_string(format)+" tile="+std::to_string(tile)+" size="+std::to_string(width)+"x"+std::to_string(height));}
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
  try{classify(image);images_.push_back(std::move(image));}catch(...){if(image.snapshot)glDeleteTextures(1,&image.snapshot);if(image.probe)blender::GPU_storagebuf_free(image.probe);if(image.probe_fence)glDeleteSync(image.probe_fence);throw;}glFlush();
 }
 bool poll(){
  performance::Scope scope("save_gpu_readback_poll");const auto start=performance::Clock::now();
  for(auto &image:images_)if(image.probe_fence){
   const auto state=glClientWaitSync(image.probe_fence,0,0);if(state==GL_WAIT_FAILED)throw std::runtime_error("Project uniform probe failed");
   if(state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED)continue;
   std::array<unsigned,8> result{};{performance::Scope timing("save_gpu_probe_read");if(!blender::GPU_storagebuf_read_if_ready(image.probe,result.data()))continue;}glDeleteSync(image.probe_fence);image.probe_fence=nullptr;blender::GPU_storagebuf_free(image.probe);image.probe=nullptr;
   if(result[0]==0){image.uniform=true;image.storage=std::make_unique_for_overwrite<float[]>(4);image.pixels=image.storage.get();std::memcpy(image.storage.get(),result.data()+4,16);image.next_row=image.completed_rows=image.height;glDeleteTextures(1,&image.snapshot);image.snapshot=0;release_source(image);}
   if(performance::ms(start)>=3)return false;
  }
  // A signalled fence precedes every mapping. No blocking wait or full-image
  // allocation/mapping occurs here, including the first save.
  for(auto &packet:packets_)if(packet.fence){
   const auto state=glClientWaitSync(packet.fence,0,0);
   if(state==GL_WAIT_FAILED)throw std::runtime_error("Project GPU readback fence failed");
   if(state!=GL_ALREADY_SIGNALED&&state!=GL_CONDITION_SATISFIED)continue;
   auto &image=images_.at(packet.image);const auto bytes=std::size_t(packet.rows)*image.width*16;
   void *mapped=nullptr;{performance::Scope timing("save_gpu_packet_map");mapped=glMapNamedBufferRange(packet.buffer,0,bytes,GL_MAP_READ_BIT);}
   if(!mapped)throw std::runtime_error("Cannot map completed project readback packet");
   {performance::Scope timing("save_gpu_packet_copy");std::memcpy(image.storage.get()+std::size_t(packet.row)*image.width*4,mapped,bytes);}
   bool unmapped=false;{performance::Scope timing("save_gpu_packet_unmap");unmapped=glUnmapNamedBuffer(packet.buffer);}if(!unmapped)throw std::runtime_error("Project readback packet contents became invalid");
   glDeleteSync(packet.fence);packet.fence=nullptr;image.completed_rows+=packet.rows;
   if(image.completed_rows==image.height){glDeleteTextures(1,&image.snapshot);image.snapshot=0;release_source(image);}
   if(performance::ms(start)>=3)return false;
  }
  for(auto &packet:packets_)if(!packet.fence){
   auto found=std::find_if(images_.begin(),images_.end(),[](const auto &image){return !image.probe_fence&&image.next_row<image.height;});if(found==images_.end())break;
   if(!packet.buffer)packet.buffer=acquire_packet();
   const int rows=std::min(found->height-found->next_row,std::max<int>(1,packet_bytes/(std::size_t(found->width)*16)));
   packet.image=found-images_.begin();packet.row=found->next_row;packet.rows=rows;
   queue_packet(*found,packet);
   if(const auto error=glGetError();error!=GL_NO_ERROR)throw std::runtime_error("Project GPU readback packet failed: OpenGL error "+std::to_string(error));
   packet.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!packet.fence)throw std::runtime_error("Project GPU readback fence failed");found->next_row+=rows;glFlush();
   if(performance::ms(start)>=3)return false;
  }
  return std::all_of(images_.begin(),images_.end(),[](const auto &image){return image.completed_rows==image.height;});
 }
 // Worker only, after encoding and before future completion. Large CPU page
 // decommit must not move back into the GUI completion event.
 void release_cpu(){for(auto &image:images_){image.storage.reset();image.pixels=nullptr;}}
 const std::vector<Image> &images()const{return images_;}
};
}

#endif
